# OnThisDayBackground

每次启动，使用往年同一天拍摄的照片更换一次 Windows 原生壁纸，然后退出。

## 使用

双击 `OnThisDayBackground.exe` 会扫描 Windows 的「图片」文件夹。也可以传入指定目录。

```powershell
.\OnThisDayBackground.exe "D:\Photos\FolderA"
```

- 程序递归读取 `System.Photo.DateTaken`，按本地时间比较月日，再按完整路径排序。缺少拍摄日期的照片会跳过。
- 成功换图后记录当天最后使用的照片。当天再次启动时选下一张，到末尾后回到第一张。没有匹配项时直接退出。
- 使用一个虚拟桌面时，通过 `IDesktopWallpaper` 指定主显示器。扩展显示模式下，副屏继续使用其原有壁纸。
- 使用多个虚拟桌面时，优先更换当前虚拟桌面的原生壁纸。Windows 决定这张壁纸在当前虚拟桌面的多个显示器上如何应用。程序不会调用「为所有虚拟桌面设置壁纸」。
- 单独设置当前虚拟桌面使用 Explorer 的内部接口，针对 Windows 11 的 22621、22631、26100 和 26200 构建。如果无法获取此接口且检测到多个虚拟桌面，程序报错退出，避免改到其他桌面。接口参考 [VirtualDesktop](https://github.com/MScholtes/VirtualDesktop) 和 [pyvda](https://github.com/mirober/pyvda)。

从终端启动会输出候选数量、路径和实际更新目标。使用 `--list` 可以只打印候选列表。

```powershell
Start-Process -FilePath .\OnThisDayBackground.exe -ArgumentList '--list "D:\Photos\FolderA"' -NoNewWindow -Wait
```

## 构建与验证

应用源码集中在 `OnThisDayBackground.cpp` 和 `OnThisDayBackground.h`。在 Windows 上使用 Visual Studio C++ 工具链。

```powershell
cmake -S . -B out/test -A x64 -DBUILD_TESTING=ON
cmake --build out/test --config Release --parallel
ctest --test-dir out/test -C Release --output-on-failure
```

GitHub Actions 验证 x64 和 Win32，并生成 `test_report.md`。实际壁纸效果可在一个虚拟桌面和多个虚拟桌面下分别启动程序验证。使用扩展显示器时，可在单虚拟桌面下检查副屏壁纸。切换虚拟桌面后再次运行，可检查程序是否只更新当前桌面。
