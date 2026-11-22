# OnThisDayBackground

在 Windows 桌面刷新时，把往年同一天拍摄的照片轮流显示在当前虚拟桌面的主显示器。

## 使用

```powershell
.\OnThisDayBackground.exe "D:\Photos\FolderA"
```

- 程序通过 `wWinMain` 启动并在后台扫描，支持含空格的路径。重复启动会打开已有进程的菜单。
- 扫描包含子目录。日期优先读 Windows 图片属性中的拍摄日期，缺失时读取文件名中的 `MM-DD`、`MM.DD` 或 `MM_DD`。例如 `2018-11-22.JPG` 可在每年 11 月 22 日匹配。程序按完整路径排序，跨本地日期时重扫并重置轮换位置。
- 点击 Windows 桌面的空白处或图标后按 **F5**，或者在程序的托盘菜单选择 **Refresh wallpaper**。长按 F5 只轮换一次。Explorer 自带的右键「刷新」菜单不在监听范围内。跳过不能解码的候选图片，没有可用图片时保留当前背景。
- 每个虚拟桌面保留自己上次选中的照片，全部刷新共用同一轮换顺序。使用扩展显示器时，照片只绘制在 Windows 标记为主显示器的范围内。照片等比居中填满屏幕。
- 照片在程序运行期间绘制到 Explorer 的图标后方。Windows 的壁纸设置保留原值。选择托盘菜单的 **Exit** 后，主屏重新显示 Windows 自己的背景。此功能依赖 Explorer 的 `WorkerW` 桌面窗口结构。无法识别虚拟桌面或安全定位背景层时，程序保留系统原有背景。

## 架构

| 文件 | 职责 |
| --- | --- |
| `OnThisDayBackground.cpp` | GUI 入口、单实例、消息循环、后台扫描和刷新调度 |
| `OnThisDayBackground_Core.cpp` | 月日匹配、递归扫描、排序和轮换 |
| `OnThisDayBackground_Photos.cpp` | Windows 图片属性和 WIC 解码 |
| `OnThisDayBackground_Desktop.cpp` | 虚拟桌面识别、主显示器定位和背景绘制 |
| `tests/` | 核心逻辑、Windows 解码和后台单实例启动测试 |

## 构建与验证

在 Windows 上使用 Visual Studio C++ 工具链。

```powershell
cmake -S . -B out/test -A x64 -DBUILD_TESTING=ON
cmake --build out/test --config Release --parallel
ctest --test-dir out/test -C Release --output-on-failure
```

GitHub Actions 验证 x64 和 Win32，成功或失败时都尝试生成并上传 `test_report.md`。主分支的 x64 运行同时将报告写回仓库。

多显示器和虚拟桌面的合成效果需要在交互式 Windows 桌面验证。可在两个虚拟桌面和扩展显示器上分别按 F5，切换桌面检查各自保留的图片，确认副屏保持 Windows 的背景。退出进程后检查原背景是否恢复。
