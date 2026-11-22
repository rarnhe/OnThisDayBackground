# OnThisDayBackground

在 Windows 桌面刷新时，把往年同一天拍摄的照片轮流显示在当前虚拟桌面的主显示器。

## 使用

```powershell
.\OnThisDayBackground.exe "D:\Photos\FolderA"
```

- 双击 `OnThisDayBackground.exe` 会在后台扫描 Windows 的「图片」文件夹。传入目录参数可指定其他文件夹，支持带空格的路径。程序使用 Windows GUI 子系统启动，无控制台窗口。重复启动会打开已有进程的菜单。
- 扫描包含子目录。通过 Windows Shell 属性存储读取 `System.Photo.DateTaken`（`PKEY_Photo_DateTaken`），按本地时间比较月日。跳过缺少该属性的图片。程序按完整路径排序，跨本地日期时重扫并重置轮换位置。
- 点击 Windows 桌面的空白处或图标后按 **F5**，或者在程序的托盘菜单选择 **Refresh wallpaper**。长按 F5 只轮换一次。Explorer 自带的右键「刷新」菜单不在监听范围内。跳过不能解码的候选图片，没有可用图片时保留当前背景。
- 每个虚拟桌面保留自己上次选中的照片，全部刷新共用同一轮换顺序。使用扩展显示器时，照片只绘制在 Windows 标记为主显示器的范围内。照片等比居中填满屏幕。
- 照片在程序运行期间绘制到 Explorer 的图标后方。Windows 的壁纸设置保留原值。选择托盘菜单的 **Exit** 后，主屏重新显示 Windows 自己的背景。此功能依赖 Explorer 的 `WorkerW` 桌面窗口结构。无法识别虚拟桌面或安全定位背景层时，程序保留系统原有背景。

从终端启动时，每次扫描完成都会输出匹配数量和完整照片路径，输出顺序与轮换列表相同。只查看结果时，在 PowerShell 中运行以下命令。它等待扫描完成后退出，也可以在程序已经常驻时运行：

```powershell
Start-Process -FilePath .\OnThisDayBackground.exe -ArgumentList '--list "D:\Photos\FolderA"' -NoNewWindow -Wait
```

省略目录时使用 Windows「图片」文件夹。重定向标准输出时使用 UTF-8。

## 分开验证壁纸和桌面刷新

先从托盘退出已运行的实例，再用 PowerShell 启动验证模式。PowerShell 等待程序退出，把终端键盘输入留给验证模式。

```powershell
Start-Process -FilePath .\OnThisDayBackground.exe -ArgumentList '--verify "D:\Photos\FolderA"' -NoNewWindow -Wait
```

1. 看到 `Verification ready` 和扫描列表后，在终端按 **b**。出现 `[Terminal b] Refresh received.` 表示终端键已触发壁纸流程，无需先聚焦桌面。
2. 点击 Windows 桌面，再按 **F5**。出现 `[Desktop F5] Refresh received.` 表示桌面刷新已触发相同流程。托盘刷新以 `[Tray]` 标记。
3. 检查下一行结果和实际主屏。日志会标明照片解码失败、找不到当前虚拟桌面、无法放置 Explorer 背景层，或已更新背景层及照片路径。

## 架构

| 文件 | 职责 |
| --- | --- |
| `OnThisDayBackground.cpp` | GUI 入口、单实例、后台扫描、拍摄日期读取、轮换与主屏背景绘制 |
| `OnThisDayBackground.h` | 应用声明 |
| `tests/` | 扫描与轮换、图片解码、真实拍摄日期、命令行列表、GUI 子系统和无参数启动测试 |

## 构建与验证

在 Windows 上使用 Visual Studio C++ 工具链。

```powershell
cmake -S . -B out/test -A x64 -DBUILD_TESTING=ON
cmake --build out/test --config Release --parallel
ctest --test-dir out/test -C Release --output-on-failure
```

GitHub Actions 验证 x64 和 Win32，成功或失败时都尝试生成并上传 `test_report.md`。主分支的 x64 运行同时将报告写回仓库。

多显示器和虚拟桌面的合成效果需要在交互式 Windows 桌面验证。可在两个虚拟桌面和扩展显示器上分别按 F5，切换桌面检查各自保留的图片，确认副屏保持 Windows 的背景。退出进程后检查原背景是否恢复。
