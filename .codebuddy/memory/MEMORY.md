# 长期记忆

## 本机开发环境
- Windows（win32），Shell 为 PowerShell。
- 已安装 MinGW-w64：`C:\mingw64\bin\g++.exe` / `gcc.exe`（已在 PATH）。
- 系统 PATH 中没有 MSVC `cl.exe`（未检测到 VS 命令行环境），Windows C++ 项目优先用 MinGW 编译验证。
- 注意：PowerShell 里 `where` 是 `Where-Object` 别名，查找可执行文件要用 `Get-Command xxx -ErrorAction SilentlyContinue`。

## 项目：d:\nodejs\look —— 鼠标锁定工具（Win32 + C++）
- 主程序单文件 `mouse_lock.cpp`：运行后锁定鼠标移动；解锁通道有 Ctrl+O / Ctrl+I 切换、托盘双击/右键菜单、连点解锁（锁定态 5 秒内连点鼠标 10 下）、系统锁屏/休眠自动解锁；Ctrl+Shift+O 直接锁定；托盘图标右键菜单操作。
- 构建：`build_mingw.bat`（推荐，本机可用，产物 mouse_lock.exe）/ `build.bat`（MSVC）/ `CMakeLists.txt`。
- 技术要点：`WH_MOUSE_LL` 钩子吞 `WM_MOUSEMOVE`/`WM_NCMOUSEMOVE`；`ClipCursor` 作强力模式兜底；隐藏 `HWND_MESSAGE` 窗口收 `WM_HOTKEY` 与托盘回调；托盘图标 GDI 自绘无资源文件；`MOD_NOREPEAT` 需自行 fallback 定义（mingw 下）。

## 迭代 2 需求（规划阶段，尚未编码）
1. 定时锁鼠标（默认空闲 5 分钟，可配置，可切换「仅鼠标 / 鼠标+键盘」判定）
2. 开机自启（HKCU Run，自启只驻留不锁定）
3. 锁定时全屏截图并置顶显示（鼠标所在显示器、画上光标、纯截图不穿透）
- 附带：直接锁定热键定为 **`Ctrl+Shift+O`**（原 Shift+O，中间曾议 Alt+O 因菜单栏冲突放弃；Ctrl+O 仍为切换）。
- 关键约束：
  1. 开机自启后**不得**立刻锁死鼠标（注册表值写成 `"<exe>" --startup`，自启只驻留）。
  2. 锁定 + 全屏覆盖后**解锁通道以 Ctrl+O 为主**（用户明确不要单击/Esc 等"易误触"的备用解锁）；后为手机远程无键盘场景，新增「锁定态 5 秒内连点鼠标 10 下」解锁（高阈值 + 时间窗天然防误触，用户认可）。
  3. 因此任务管理器必须与截图层共存且**位于更上层**——实现上只能"我方主动让位"（Z-band 限制，普通进程无法压过 Taskmgr）。
  4. 设置窗口在锁定态无法用鼠标操作（用户明确不做临时解锁）→ 需灰掉菜单项或提示先解锁。
- 全部决策表与待确认清单（E/F 系列）、规格定稿、**踩坑记录与测试结论**见 `2026-08-31.md`。
- 状态：迭代 2（定时锁定 / 开机自启 / 全屏截图置顶 / 任务管理器让位 / 设置界面）**已编码完成并通过全部测试**。
- 构建：`g++ -std=c++17 -O2 -municode -mwindows -o mouse_lock.exe mouse_lock.cpp -luser32 -lgdi32 -lshell32 -lgdiplus -lwtsapi32 -static`（MinGW 下 gdiplus.h 前须 include `<objidl.h>`）。
- 关键维护点：
  1. 前台完整性级别变化会清除 `ClipCursor` → 靠 500ms 周期 `ReassertClip()` 自愈；`--startup` 用 `GetCommandLineW()` 解析。
  2. 时间戳相减必须判方向：`GetTickCount()` 与 `GetLastInputInfo()` 存在 last>now 的竞态窗口，无符号相减会绕成巨大值导致空闲误判（曾造成"每几秒误锁一次"）。
- 调试模式：配置 `Debug=1`（托盘菜单可切），日志在 `%APPDATA%\MouseLock\debug.log`，LOCK/UNLOCK 均带 reason，可用于定位误锁来源。
- 快捷键：`Ctrl+O` 与 **`Ctrl+I`** 均为切换锁定（后者用于绕开把 Ctrl+O 当"打开"的程序），`Ctrl+Shift+O` 为直接锁定。
- **连点解锁**：锁定态下 5 秒内累计鼠标按键（任意按钮，含远控注入点击）≥10 次即解锁；专为手机远程、键盘不便设计。配置 `TapUnlock=0` 可关闭。覆盖层右下角有半透明提示。关键：钩子里**不过滤 `LLMHF_INJECTED`**（与空闲判定相反，因为远控点击正是解锁手段）；解锁请求经 `g_tapUnlockPending` 交由 500ms 巡检在主线程执行 `UnlockNow`（避免钩子回调操作窗口）。
- 托盘可隐藏（`HideTray=1` / 菜单项），隐藏后靠命令行恢复：`mouse_lock.exe --tray`（显示托盘）、`--quit`（优雅退出）。查找已运行实例用 `FindWindowEx(HWND_MESSAGE, ...)` + 类名 `MouseLockHiddenWindowClass`。
- 用户配置路径：`C:\Users\Administrator\AppData\Roaming\MouseLock\config.ini`（UTF-16 编码）。
- **用户机器环境（重要）**：装有 AweSun / RustDesk / ToDesk 三个远控软件，会定时注入合成输入防休眠 → `GetLastInputInfo` 空闲判定会失效。因此空闲判定必须用 `IdleMode=2`（只认真实硬件输入，靠 `LLMHF_INJECTED`/`LLKHF_INJECTED` 过滤）。
- 任务管理器**不会**暂停空闲计时（只让截图层隐藏）；旧版曾因它一直开着导致自动锁定彻底失效。
- **锁定方式 `LockMode`**：`1`=冻结屏幕（默认，截图置顶、点击被吸收、**鼠标仍可移动**）；`0`=锁鼠标（钩子+ClipCursor）。冻结模式下截图层盖不住时（让位/截图失败/关截图/多显示器）会**自动临时接管为锁鼠标**。
- 冻结模式是默认方案：绕开 UIPI（提权窗口前台会清除 ClipCursor），也避免光标被周期性拽回锚点。
- 截图是否画鼠标光标**按模式区分**：锁鼠标模式（光标被钉住、位置真实）→ 画；冻结模式（鼠标会动、会变成"假光标"）→ **不画**。

## 协作偏好
- 用户会明确指示「某阶段只做规划不改代码」——此时只输出方案与待确认项，不做文件编辑（记忆文件除外）。
- 需求确认后倾向于给出可直接编译运行的完整实现。
