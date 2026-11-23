# OnThisDayBackground

首次启动扫描具有拍摄日期的全部照片，并在后台缓存路径和月日。仅在桌面右键菜单选择「刷新」时，通知同一个后台实例换下一张 Windows 原生壁纸。

## 使用

双击 `OnThisDayBackground.exe` 会扫描 Windows 的「图片」文件夹。也可以传入指定目录。

```powershell
.\OnThisDayBackground.exe "D:\Photos\FolderA"
```

- 程序递归读取 `System.Photo.DateTaken`，将所有具有拍摄日期的图片按完整路径排序，建立内存索引 A（路径 + 月日）。缺少拍摄日期的照片会跳过。
- 首次启动时，后台递归扫描全部具有拍摄日期的照片，建立完整的索引 A。CMD/PowerShell 仅打印**当前本地月日**匹配的照片路径，打印后启动器退出。终端展示是独立输出，不参与 A/B 缓存或壁纸轮换。
- 后台已经运行时，再次指定目录会**独立扫描该目录并只打印当天月日匹配路径**，不会修改后台 A/B 缓存，也不会触发换图；不带参数再次运行也不会触发换图。
- **只有在 Explorer 桌面空白处右键选择「刷新」时才轮换壁纸。** 后台在那一刻读取本地月日，从完整 A 索引创建或复用对应 B（当天图片路径 + 独立轮换位置）。跨天无需重启，B 不存在时仅从 A 过滤，不重复读取磁盘元数据；当天没有匹配项则保持等待。
- 启动器在打印完路径后退出，后台实例独立于 CMD 或 PowerShell，只有后台实例退出后才重新建立 A。

- 使用一个虚拟桌面时，通过 `IDesktopWallpaper` 指定主显示器。扩展显示模式下，副屏继续使用其原有壁纸。
- 使用多个虚拟桌面时，核对 Explorer 当前桌面的 GUID，然后用该 GUID 定位并更新原生壁纸。程序会报告目标 GUID，并读回目标和其他桌面的壁纸路径，检查 Explorer 是否只更新了目标。当前虚拟桌面的多个显示器由 Windows 决定如何显示同一壁纸。
- 单独设置当前虚拟桌面使用 Explorer 的内部接口。Windows 11 构建 22000、22621 和 22631 使用各自的接口布局。构建 26100 起（包括 26300 和 28000）尝试使用现代布局，并先验证 Explorer 是否提供相应接口、能否枚举桌面并读取当前桌面的 GUID。如果 Explorer 拒绝调用，错误会列出 Windows 构建号、失败步骤和 HRESULT。接口参考 [VirtualDesktop](https://github.com/MScholtes/VirtualDesktop) 和 [pyvda](https://github.com/mirober/pyvda)。

已删除 `--list` 参数。直接执行上面的带目录命令，只会打印**今天月日匹配**的图片路径；后台仍按 A/B 结构独立运行。

## 构建与验证

应用源码集中在 `OnThisDayBackground.cpp` 和 `OnThisDayBackground.h`。在 Visual Studio 中通过「打开文件夹」打开包含 `CMakeLists.txt` 的项目根目录，选择 `x64-release` 后构建。该预设使用 Ninja 和 MSVC 的 `cl.exe`，由 Visual Studio 设置 x64 编译环境。Visual Studio Installer 中需要安装「使用 C++ 的桌面开发」、MSVC x64/x86 生成工具和 Windows SDK。

如果之前配置过本项目，请在 Visual Studio 中删除 CMake 缓存并重新配置。

在命令行构建时，从 Visual Studio 的 x64 Developer Command Prompt 或 Developer PowerShell 执行以下命令。

```powershell
cmake --preset x64-release
cmake --build --preset x64-release --parallel
ctest --preset x64-release
```

x64 Release 的可执行文件在 `out/build/x64-release/OnThisDayBackground.exe`。

GitHub Actions 验证 x64 和 Win32，并生成 `test_report.md`。实际壁纸效果可先启动后台实例，再在单个或多个虚拟桌面下右键桌面空白处选择「刷新」验证。使用扩展显示器时，可在单虚拟桌面下检查副屏壁纸。切换虚拟桌面后再次点击「刷新」，可检查程序是否只更新当前桌面。
