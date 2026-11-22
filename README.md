# OnThisDayBackground

首次启动扫描往年同一天拍摄的照片，并在后台缓存路径。再次启动或选择桌面右键菜单的「刷新」时，通知同一个后台实例换下一张 Windows 原生壁纸。

## 使用

双击 `OnThisDayBackground.exe` 会扫描 Windows 的「图片」文件夹。也可以传入指定目录。

```powershell
.\OnThisDayBackground.exe "D:\Photos\FolderA"
```

- 程序递归读取 `System.Photo.DateTaken`，按本地时间比较月日，再按完整路径排序。缺少拍摄日期的照片会跳过。
- 首次启动只准备候选列表，并在 CMD 或 PowerShell 打印数量和完整路径。随后每次再次运行，或在桌面空白处右键选择「刷新」，都会轮换一张，到末尾后回到第一张。没有匹配项时后台继续等待。
- 启动器打印完路径后退出，后台实例独立于 CMD 或 PowerShell。后续触发沿用首次扫描的路径。
- 使用一个虚拟桌面时，通过 `IDesktopWallpaper` 指定主显示器。扩展显示模式下，副屏继续使用其原有壁纸。
- 使用多个虚拟桌面时，核对 Explorer 当前桌面的 GUID，然后用该 GUID 定位并更新原生壁纸。程序会报告目标 GUID，并读回目标和其他桌面的壁纸路径，检查 Explorer 是否只更新了目标。当前虚拟桌面的多个显示器由 Windows 决定如何显示同一壁纸。
- 单独设置当前虚拟桌面使用 Explorer 的内部接口。Windows 11 构建 22000、22621 和 22631 使用各自的接口布局。构建 26100 起（包括 26300 和 28000）尝试使用现代布局，并先验证 Explorer 是否提供相应接口、能否枚举桌面并读取当前桌面的 GUID。如果 Explorer 拒绝调用，错误会列出 Windows 构建号、失败步骤和 HRESULT。接口参考 [VirtualDesktop](https://github.com/MScholtes/VirtualDesktop) 和 [pyvda](https://github.com/mirober/pyvda)。

使用 `--list` 可以独立扫描并打印候选数量和路径，供检查所指定的目录。

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

GitHub Actions 验证 x64 和 Win32，并生成 `test_report.md`。实际壁纸效果可先启动后台实例，再在单个或多个虚拟桌面下再次运行 EXE，或右键桌面空白处选择「刷新」验证。使用扩展显示器时，可在单虚拟桌面下检查副屏壁纸。切换虚拟桌面后再次运行，可检查程序是否只更新当前桌面。
