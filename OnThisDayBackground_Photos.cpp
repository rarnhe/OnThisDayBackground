#include "OnThisDayBackground.h"

#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <limits>

namespace on_this_day {
using Microsoft::WRL::ComPtr;

std::optional<MonthDay> ReadPhotoDate(const std::filesystem::path& photo) {
    ComPtr<IShellItem2> item;
    if (FAILED(SHCreateItemFromParsingName(photo.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        return std::nullopt;
    }
    FILETIME recorded{};
    SYSTEMTIME date{};
    // EXIF DateTimeOriginal expresses the camera's local calendar date.
    if (FAILED(item->GetFileTime(PKEY_Photo_DateTaken, &recorded)) ||
        !FileTimeToSystemTime(&recorded, &date)) {
        return std::nullopt;
    }
    return MonthDay{date.wMonth, date.wDay};
}

HBITMAP LoadPhoto(const std::filesystem::path& photo) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(photo.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory->CreateFormatConverter(&converter))) {
        return nullptr;
    }

    ComPtr<IWICBitmapSource> source;
    if (FAILED(frame.As(&source))) return nullptr;
    ComPtr<IWICMetadataQueryReader> metadata;
    if (SUCCEEDED(frame->GetMetadataQueryReader(&metadata))) {
        PROPVARIANT orientation{};
        const auto jpeg = metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &orientation);
        if (FAILED(jpeg)) {
            PropVariantClear(&orientation);
            metadata->GetMetadataByName(L"/ifd/{ushort=274}", &orientation);
        }
        constexpr WICBitmapTransformOptions transforms[] = {
            WICBitmapTransformRotate0, WICBitmapTransformRotate0, WICBitmapTransformFlipHorizontal,
            WICBitmapTransformRotate180, WICBitmapTransformFlipVertical,
            static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal),
            WICBitmapTransformRotate90,
            static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal),
            WICBitmapTransformRotate270
        };
        if (orientation.vt == VT_UI2 && orientation.uiVal > 1 && orientation.uiVal < 9) {
            ComPtr<IWICBitmapFlipRotator> rotated;
            if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rotated)) &&
                SUCCEEDED(rotated->Initialize(frame.Get(), transforms[orientation.uiVal]))) {
                rotated.As(&source);
            }
        }
        PropVariantClear(&orientation);
    }

    UINT width = 0, height = 0;
    if (FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGR,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 ||
        width > std::numeric_limits<UINT>::max() / 4 ||
        height > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
        height > std::numeric_limits<UINT>::max() / (width * 4)) {
        return nullptr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap && FAILED(converter->CopyPixels(nullptr, width * 4, width * 4 * height,
            static_cast<BYTE*>(pixels)))) {
        DeleteObject(bitmap);
        return nullptr;
    }
    return bitmap;
}
} // namespace on_this_day
