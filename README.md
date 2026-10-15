# ClassicImeSwitcher · 经典输入法切换

独立的 Windows 个人工具，使用 **QML + C++、Qt 6.12.0 动态库、VS2026 MSVC v145、CMake 和 Ninja**。项目与原 LYTools 分开，不引用或修改原工程。

## 使用

构建并准备可直接运行的目录：

```powershell
pwsh -File .\tools\Build.ps1 -Deploy
```

程序入口：`out/app/bin/ClassicImeSwitcher.exe`。保留整个 `out/app` 目录，Qt 的 DLL、QML 插件和 VS2026 运行库已经放在其中。

主窗口使用 QML 自绘标题栏，无系统标题栏和原生边框。拖动标题栏移动窗口，双击最大化或还原，四边和四角可调整大小；右上角提供最小化、最大化/还原和关闭按钮，关闭仍遵循托盘设置。

首次运行时，经典切换和开机启动均默认关闭：

1. 在“经典切换”页检查美式键盘；尚未添加时，点击“添加美式键盘”。
2. 首版要求只启用一个中文输入法。存在多个时，先在输入法管理页禁用其他中文输入法，再刷新列表并开启经典切换。
3. 默认使用 `Ctrl + Space` 在所选中文输入法和独立美式键盘之间切换。常规设置中也可选择 `Ctrl + Shift + Space` 或 `Alt + Space`。

输入法管理页支持查看已启用和未启用的输入法、启用/禁用、设为 Windows 默认输入法，以及打开 Windows 语言设置。这里的禁用不卸载输入法软件，可以在未启用列表中重新启用。不会禁用最后一个可用输入法；经典切换运行期间，需先暂停才能禁用其切换目标。

关闭窗口默认留在托盘，从托盘菜单可以打开设置、暂停切换或退出。开机启动只写当前用户的启动项；移动程序目录后，应重新开关一次开机启动。设置窗口打开期间也可以在首页的输入框测试切换。

原 LYTools 的输入法热键逻辑按本次要求保持原状。使用新 App 时，应避免旧切换程序同时处理同一快捷键。

## 构建与工程

- Qt：`C:/Qt/6.12.0/msvc2022_64`。
- VS2026：`C:/Program Files/Microsoft Visual Studio/18/Insiders`。
- Preset：`vs2026-release`，Ninja、Release、x64。
- `tools/Build.ps1` 初始化 VS2026 环境，再配置、构建并运行 CTest；`-Deploy` 额外部署动态依赖，`-Fresh` 重新生成 CMake 配置。
- 在 VS2026 中直接打开本项目目录或 `CMakeLists.txt`，选择对应的 CMake Preset，不需要 `.vcxproj`。
- C++ 文件为 UTF-8 BOM，编译使用 `/utf-8`；QML 和 CMake 文件为 UTF-8。

`Build.ps1 -VsRoot ... -QtRoot ...` 可覆盖本机安装路径。构建前退出正在运行的本 App，避免 Windows 占用可执行文件。

当前 Qt 安装的可选 QML Asset Downloader 插件缺少 `Qt6TaskTree`，配置时会产生一条依赖警告。本应用没有导入该插件；所用 Quick/Controls 模块的构建和运行验证通过。

## 实现

| 文件 | 职责 |
| --- | --- |
| `src/WindowsInput.*` | 当前用户的输入法枚举、启用/禁用、添加美式键盘、设置默认 |
| `src/SwitchEngine.*` | 独立输入线程、低级键盘 Hook、一次按下触发一次、系统切换与状态确认 |
| `src/AppController.*` | QML 绑定、设置持久化、数据快照、日志和启动项 |
| `qml/Main.qml` | 经典切换、输入法管理、常规设置三个页面 |
| `src/SmokeTest.*` | 显式命令行触发的页面、热键和管理验证 |
| `tests/tst_imecore.cpp` | Profile ID 解析与快捷键状态回归 |
| `tests/NativeInputTarget.cpp` | 独立 Win32 测试进程，验证跨进程真实英文输入 |

输入法目录结合系统安装目录、实际用户启用列表与 TSF Provider GUID，避免把全部系统键盘布局误报为已启用，也能识别安装目录接口遗漏的第三方输入法。目录覆盖当前加载的语言以及简体中文、美式英语。

添加和启用使用 `input.dll!InstallLayoutOrTip`，设置默认使用 `SetDefaultLayoutOrTip`。DLL 限定从系统目录加载。切到美式键盘时，通过带专用标记的 `SendInput` 触发 Windows 自身的 `Win + Space` 通路；切回中文时，使用 TSF `ActivateProfile` 的会话激活并请求前台窗口接受中文布局。本应用同时同步自身 UI 线程的 TSF 语言。中文激活会作用于当前会话；切换过程中可能短暂显示系统输入切换浮窗。

Hook 回调只判断按键状态、投递请求；不在回调中操作界面或写文件。正常运行忽略模拟按键，保留普通键输入，抑制触发键的长按重复。松开空格后完成切换，同一时段的快速连按会合并为最终目标。系统切换前临时释放修饰键，确认变化后只恢复用户仍按住的按键。只有观察到上一次布局变化后才允许继续循环，不在系统未应答时重复发送。单次切换请求最多等待 2 秒、发送 16 次循环；前台窗口或输入焦点改变后停止。

多个中文输入法可能共用同一 HKL，跨进程无法据此可靠区分具体 Provider。首版会阻止在多个中文输入法同时启用时开启经典切换，管理页仍支持启用和禁用这些输入法。尚未提供多中文输入法轮换。

普通用户即可运行。配置、日志和每次管理操作前的快照保存在 `%LOCALAPPDATA%/LYLab/ClassicImeSwitcher`。快照用于诊断，不会在退出时整体覆盖 Windows 设置；主动修改的输入法状态会保留。

当前版本未提供系统输入法顺序调整或传统桌面语言栏开关。

## 验证

本机构建与回归：

```powershell
pwsh -File .\tools\Build.ps1 -Deploy
```

显式页面检查，不安装热键 Hook、不修改输入法，使用临时配置目录，保存三页截图和 JSON：

```powershell
.\out\app\bin\ClassicImeSwitcher.exe --smoke-test C:\Temp\ClassicIme-Smoke
```

虚拟机验证：

```powershell
pwsh -File .\tools\Test-Vm.ps1
```

该脚本使用已有的 DPAPI WinRM 凭据，先核对 `DESKTOP-KUOPJ45\codexvm` 管理身份，再以桌面用户的 Interactive Token、普通权限执行 App。默认地址为当前已核实的 `10.0.0.6`，可通过 `-ComputerName` 更改。测试结束后注销临时计划任务，截图和报告复制到 `out/checks/vm-时间/`。

- Smoke：加载三个 QML 页面、检查运行警告并截图。
- Management：美式键盘禁用/添加往返，设置默认与恢复原默认，以及多个中文输入法的启用保护；结束时恢复测试前的输入法配置。
- Switch：Qt 专用测试窗口验证热键、持续 2.3 秒的长按、松开空格后仍按住 Ctrl、快速连续切换，以及真实输出 `你好`、`abc123`。中文文本断言限微软拼音，其他输入法会在报告中明确标记跳过该项。
- External：独立 Win32 进程执行相同的切换和真实英文输入检查。测试结束前通过系统切换通路恢复测试窗口的初始输入布局。

接受模拟快捷键仅限显式测试模式，并限制在本次创建的测试窗口；正常运行忽略模拟按键。测试使用临时应用配置，不开启正式配置中的切换或启动项。

测试不会自动注销或重启 Windows。普通权限应用的自动化测试不等于管理员窗口、游戏、其他第三方输入法和物理键盘的人工验收。

2026-10-14 验证记录：

- VS2026 MSVC 19.51、Release x64 构建通过。单元测试详细记录写入 `out/build/release/imecore-tests.txt`。
- 三个 QML 页面的 `qmllint` 检查无警告；虚拟机运行无 QML 警告，三页截图已检查。
- Windows 10 Pro 19045 虚拟机的页面和管理测试通过，见 `out/checks/vm-20261014-160114/`。
- 追加长按及修饰键恢复测试后，Qt 和独立 Win32 进程均通过，见 `out/checks/vm-20261014-160312/Switch/report.json` 和 `out/checks/vm-20261014-160332/External/report.json`。
- 管理测试恢复了原输入法启用状态及默认输入法；输入测试恢复了测试窗口的初始布局。未测试物理键盘、搜狗等第三方输入法的实际切换、管理员窗口、游戏及重新登录后的启动项行为。
