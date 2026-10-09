# 鼠标锁定工具（Mouse Lock）

Windows 平台、纯 C++ / Win32 API 实现的鼠标锁定工具。运行后鼠标移动立即锁定，
按 **Ctrl + O** / **Ctrl + I** 解锁（手机远程键盘不便时也可**连点鼠标 10 下**解锁）。鼠标按键与滚轮不受影响，仍可正常使用。

## 功能

| 功能 | 说明 |
| --- | --- |
| 启动即锁定 | 双击 `mouse_lock.exe` 后鼠标光标立刻无法移动（可在设置中关闭，见 `LockOnStart`） |
| 空闲自动锁定 | 默认 **5 分钟**无操作自动锁定，阈值 1–1440 分钟可配置 |
| `Ctrl + O` | 切换 锁定 / 解锁（任何程序获得焦点时都生效） |
| `Ctrl + I` | 与 `Ctrl + O` 完全等价——部分程序把 `Ctrl+O` 占用为「打开」导致解锁失败时改用这个 |
| `Ctrl + Shift + O` | 直接锁定（不管当前状态），解锁后随手一按继续锁 |
| 连点解锁 | **锁定态下 5 秒内连点鼠标 10 下**即解锁（任意按钮，含远控注入点击），专为手机远程、键盘不便设计；可在设置中关闭 |
| 隐藏托盘图标 | 托盘右键 → 隐藏托盘图标；隐藏后热键照常工作，用 `--tray` / `--quit` 恢复或退出 |
| 冻结屏幕（默认） | 锁定时截取鼠标所在显示器的全屏画面置顶显示：画面被冻结、点击与滚轮被吸收，**鼠标仍可自由移动**（截图不画光标，见下）；解锁即销毁 |
| 锁鼠标（可选） | 经典模式：用低层钩子拦截鼠标移动，`ClipCursor` 兜底；鼠标按键与滚轮仍可正常使用 |
| 开机自启 | 基于 `HKCU Run`，开机只驻留托盘、**不自动锁定**；exe 移动后自动校正注册路径 |
| 任务管理器让位 | 任务管理器运行时截图层自动隐藏并暂停计时，方便用键盘结束进程 |
| 锁屏 / 休眠保护 | 系统锁屏或休眠时自动解锁并暂停计时，回来不会面对冻结画面 |
| 空闲判定可选 | 「鼠标+键盘都不动」（系统级 `GetLastInputInfo`）或「仅鼠标不动」 |
| 强力模式 | 默认开启，`ClipCursor` 把光标钉在原地，可拦截绕过消息钩子的程序 |
| 截图保存（可选） | PNG 保存到 `%APPDATA%\MouseLock\shots`，自动保留最近 100 张 |
| 单实例 / 安全兜底 | 重复运行不叠加；退出、注销、关机都会释放光标并卸载钩子 |

## 配置文件位置

```
%APPDATA%\MouseLock\config.ini
```

本机实际路径：`C:\Users\Administrator\AppData\Roaming\MouseLock\config.ini`

同目录下的其它文件：

| 路径 | 说明 |
| --- | --- |
| `%APPDATA%\MouseLock\config.ini` | 配置文件（UTF-16 编码，可用记事本直接改，改完重启程序生效） |
| `%APPDATA%\MouseLock\debug.log` | 调试日志（仅调试模式开启时产生，超过 2MB 自动重写） |
| `%APPDATA%\MouseLock\shots\` | 保存的 PNG 截图（需开启 `SaveShot`） |

## 配置

配置项保存在 `%APPDATA%\MouseLock\config.ini`（首次运行自动生成，UTF-16 编码）：

```ini
[General]
IdleEnabled=1      ; 1=启用空闲自动锁定
IdleMinutes=5      ; 空闲阈值，1 – 1440 分钟
IdleMode=2         ; 空闲判定方式，见下表（默认环境有远控软件时建议 2）
Strict=1           ; 强力模式（ClipCursor）
ShotEnabled=1      ; 锁定时截图并置顶显示
SaveShot=0         ; 1=同时保存 PNG 截图
LockOnStart=1      ; 1=双击/无参启动时立即锁定  0=启动后只驻留托盘
Debug=0            ; 1=调试模式（写 debug.log，托盘显示空闲倒计时）
HideTray=0         ; 1=隐藏托盘图标（用 mouse_lock.exe --tray 恢复）
LockMode=1         ; 0=锁鼠标  1=冻结屏幕（默认）
TapUnlock=1        ; 1=开启连点解锁（锁定态下 5 秒连点 10 下鼠标解锁，无需键盘）
```

### 锁定方式（`LockMode`）

| 值 | 行为 | 说明 |
| --- | --- | --- |
| `1` 冻结屏幕（默认） | 截图置顶覆盖，画面冻结、点击与滚轮被吸收，**不拦截鼠标移动** | 视觉上等同于锁屏；鼠标仍可移动，解锁后光标停在原位，无缝继续工作；**截图不绘制鼠标光标** |
| `0` 锁鼠标 | 钩子拦截 `WM_MOUSEMOVE` + `ClipCursor` 兜底 | 不依赖截图，鼠标完全动不了 |

**为什么默认改成冻结屏幕**：
截图层已经盖住画面并吸收了点击与滚轮，再锁鼠标收益有限；而不锁鼠标可以顺带绕开
`ClipCursor` 被提权窗口前台清除的 UIPI 问题，也消除了每 500ms 重新裁剪把光标拽回锚点的突兀感。

**自动兜底（重要）**：冻结屏幕模式下，一旦截图层盖不住屏幕，会自动临时切换为锁鼠标：
- 任务管理器让位（截图层隐藏）时
- 截图失败时
- 关闭截图功能（`ShotEnabled=0`）时
- **多显示器**时（当前只截取鼠标所在显示器，其它屏幕没盖住）

> `LockOnStart=0` 时程序启动后只驻留托盘，不会锁定也不会弹截图层；
> 开机自启（`--startup`）**永远不锁定**，与本项无关。

开机自启状态**不写入 ini**，以注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\MouseLock`
为唯一真值（菜单勾选状态实时回读注册表）。

### 空闲判定方式（`IdleMode`）

| 值 | 含义 | 适用环境 |
| --- | --- | --- |
| `0` | 仅鼠标移动算活动（**忽略合成输入**） | 远控软件注入的是键盘时 |
| `1` | 系统判定 `GetLastInputInfo()`，鼠标+键盘都算（**包含合成输入**） | 干净的机器，无远控/防休眠工具 |
| `2` | 鼠标+键盘都算，但**只认真实硬件输入，忽略合成输入** | **推荐**：装了向日葵 / RustDesk / ToDesk / 防休眠工具的机器 |

合成输入通过 `LLMHF_INJECTED` / `LLKHF_INJECTED` 标记识别（程序用 `SendInput`、`mouse_event`、
`keybd_event` 产生的输入）。鼠标移动还要求累计位移 ≥ 3 像素才计入，可挡住"1 像素抖动"类保活工具。

> ⚠️ 如果远控软件通过**虚拟 HID 驱动**注入（不走 `SendInput`），系统无法区分它与真实输入，
> 此时任何基于输入的空闲判定都会失效。可通过调试日志的 `moves(r/i)` / `keys(r/i)`
> 判断：离开电脑后若 `real` 仍在增长，说明是驱动级输入。

## 使用

1. 运行 `mouse_lock.exe` —— 立即锁定（默认冻结屏幕画面），任务栏出现红色锁形托盘图标。
2. `Ctrl + O` 或 `Ctrl + I` 解锁（图标变绿）；`Ctrl + Shift + O` 直接锁定；手机远程键盘不便时，**5 秒内连点鼠标 10 下**也能解锁。
3. 右键托盘图标 → **设置…**（锁定态下会置灰，需先解锁）可调整阈值、判定方式、截图、开机自启等。
4. 右键托盘图标 → **退出** 关闭程序；菜单展开期间会自动临时解锁，方便点选。

### 命令行参数

| 命令 | 作用 |
| --- | --- |
| `mouse_lock.exe` | 正常启动（是否立即锁定取决于 `LockOnStart`） |
| `mouse_lock.exe --startup` | 开机自启专用：**只驻留托盘，不锁定**（注册表里写的就是这条） |
| `mouse_lock.exe --tray` | 已有实例在运行时：让**它**重新显示托盘图标（用于 `HideTray=1` 后恢复） |
| `mouse_lock.exe --quit` | 已有实例在运行时：让**它**优雅退出（会释放光标、卸载钩子） |

> 隐藏托盘后就没有 UI 入口了，**必须**靠上面两条命令恢复或退出。

> 忘记快捷键时：`Ctrl + Shift + Esc` 打开任务管理器——程序检测到任务管理器会自动隐藏截图层，
> 用键盘即可结束 `mouse_lock.exe` 进程。

## 编译

源码为**单文件** `mouse_lock.cpp`，无第三方依赖（用到系统库：user32、gdi32、shell32、gdiplus、wtsapi32）。

### 方式一：MSVC（Visual Studio / Build Tools）

```bat
build.bat
```

脚本会用 `vswhere` 自动定位 VS 并初始化环境（x64）。手工编译：

```bat
cl /std:c++17 /O2 /EHsc /utf-8 /Fe:mouse_lock.exe mouse_lock.cpp ^
   user32.lib gdi32.lib shell32.lib gdiplus.lib wtsapi32.lib
```

> `/utf-8` 是必须的（源码含中文界面文案），需要 VS2015 Update 2 及以上。

### 方式二：MinGW-w64（本项目实际验证环境）

```bat
build_mingw.bat
```

```bat
g++ -std=c++17 -O2 -municode -mwindows -o mouse_lock.exe mouse_lock.cpp ^
    -luser32 -lgdi32 -lshell32 -lgdiplus -lwtsapi32 -static
```

### 方式三：CMake

```bat
cmake -B build
cmake --build build --config Release
```

## 实现要点

- **锁定**：`SetWindowsHookEx(WH_MOUSE_LL, ...)` 低层钩子对 `WM_MOUSEMOVE` / `WM_NCMOUSEMOVE`
  返回 `1` 吞掉事件；低层钩子不需要注入 DLL，也不需要管理员权限。
- **强力模式**：`ClipCursor` 把光标限制在 1×1 矩形内。
  ⚠️ Windows 会在**前台窗口完整性级别变化**时（例如打开任务管理器这类提权窗口）自动清除
  `ClipCursor`，因此程序在 500ms 巡检中周期性重新裁剪，让锁定具备自愈能力。
- **空闲检测**：`SetTimer` 500ms 巡检；`GetLastInputInfo()` 取系统级最后输入时间（鼠标+键盘），
  「仅鼠标」模式则取钩子记录的最后一次 `WM_MOUSEMOVE`。以 `g_ignoreBefore` 作为下界，
  保证启动、解锁、休眠唤醒后重新计时；用 `DWORD` 差值运算天然免疫 `GetTickCount` 49.7 天回绕。
- **截图**：`MonitorFromPoint` + `GetMonitorInfo` 定位鼠标所在显示器，`BitBlt(SRCCOPY|CAPTUREBLT)`
  抓屏；`BitBlt` 不含光标，需 `GetCursorInfo` + `DrawIconEx` 补画。
  **是否补画按锁定方式区分**：锁鼠标模式下光标被钉在原地，截图上的位置真实且恒定，补画；
  冻结屏幕模式下鼠标仍会移动，截图上那个静止光标会变成"假光标"（真实光标早已挪走），
  反而比不画更误导，因此**不画**。
- **截图层**：`WS_POPUP` 无边框窗口 + `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，
  不抢焦点、不进 Alt-Tab 与任务栏；解锁时 `DestroyWindow` 并释放位图。
- **DPI**：`SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`（失败回退 `SetProcessDPIAware`），
  在创建任何窗口之前调用，避免多屏不同缩放时截图被拉伸或裁切。
- **开机自启**：注册表值写成 `"<exe路径>" --startup`；`wWinMain` 用 `GetCommandLineW()` 解析该参数
  （不同 CRT 对 `lpCmdLine` 的处理有差异），自启时只驻留托盘、不锁定。
- **会话 / 电源**：`WTSRegisterSessionNotification` + `WM_WTSSESSION_CHANGE`、`WM_POWERBROADCAST`，
  锁屏或挂起时解锁并暂停计时，恢复后重置计时。
- **PNG**：GDI+ 编码保存。MinGW 下 `<gdiplus.h>` 需要在其之前包含 `<objidl.h>`，否则 `PROPID` 未定义。
- **托盘图标**：运行时用 GDI 自绘锁形图标（含 1px 抗锯齿），无需任何资源文件。
- **连点解锁**：低层鼠标钩子统计 `WM_LBUTTONDOWN/WM_RBUTTONDOWN/WM_MBUTTONDOWN/WM_XBUTTONDOWN`
  时间戳（**不过滤 `LLMHF_INJECTED`，因为远控软件的点击正是解锁手段**），滑动窗口 5 秒内累计 ≥10 次
  即置位 `g_tapUnlockPending`，由 500ms 巡检在主线程安全执行 `UnlockNow`，避免在钩子回调里操作窗口。
  解锁动作与覆盖层是否显示无关（任务管理器让位等情况下仍可连点解锁）。

## 调试模式

托盘右键 → **调试模式** 可开关（配置 `Debug`），开启后：

- 日志写入 `%APPDATA%\MouseLock\debug.log`（超过 2MB 自动重写）：
  - `=== start ===`：启动参数与当前配置快照
  - `TICK`：每秒一条，`idle=已空闲/阈值 ms`、锁定态、任务管理器、会话、计时是否在跑、
    `glitch=` 竞态计数、`moves(r/i)=真实移动/被忽略移动`、`keys(r/i)=真实按键/被忽略按键`
  - `INPUT detected`：检测到鼠标/键盘活动、计时被重置（用来验证输入是否真的刷新了计时）
  - `HOTKEY` / `LOCK` / `UNLOCK` / `IDLE-TRIGGER`：每一次锁定与解锁都带**原因**和时间戳
- 托盘提示实时显示空闲倒计时：`调试 | 空闲 12 s / 300 s（计时中）`

判读要点：`LOCK reason=idle-timeout` 才是空闲超时触发；如果是 `hotkey-*` / `menu-*`，
说明是人为或热键触发。看到 `IDLE-TRIGGER` 时若 `elapsed` 远小于阈值，属于异常（见下）。

## 已修复：空闲计时竞态导致误锁

**现象**：正在打字时会莫名其妙每隔几秒自动锁定一次。

**根因**：`IdleElapsedMs()` 先取 `now = GetTickCount()`，再调 `GetLastInputInfo()` 取最后输入时间。
若按键恰好在这两次调用之间到达，`last` 会**新于** `now`，`DWORD` 无符号相减绕成一个约 49 天的巨大值，
立即超过阈值 → 误判为「已空闲 5 分钟」并锁定。实测高频打字时 30 秒内会发生 3–11 次。

**修复**：
1. `IdleElapsedMs()` 发现 `now < last` 时钳制为 0（而不是让它绕回巨大值）；
2. `GetLastInputInfo()` 失败时不再回退到「最后一次鼠标移动时间戳」（只打字不动鼠标时它可能已是几分钟前，
   同样会瞬间放大空闲时长），改为视为「此刻有活动」；
3. 超时需**连续两次巡检**（约 1 秒）都成立才锁定，抵御单次采样抖动。

## 已知限制

- **锁鼠标模式下，提权窗口前台期间锁定会失效**：这是 Windows 的 UIPI 安全边界——低完整性进程
  既无法挂钩、也无法裁剪高完整性进程（任务管理器等）的输入；前台切回普通窗口后 0.5 秒内自动恢复
  （500ms 巡检会重新裁剪）。**默认的冻结屏幕模式不受此影响**（不依赖钩子与 ClipCursor）。
- 独占全屏（DXGI Exclusive）的游戏画面 `BitBlt` 抓不到，会截到桌面。
- 「仅鼠标」判定模式下，使用 raw input 的全屏程序不产生 `WM_MOUSEMOVE`，可能被误判为静止。

## 测试情况

- 单元自测（配置读写、注册表自启、截图、PNG、空闲计时含回绕、截图层窗口、设置界面）：**50 项全通过**。
- 实机行为测试：**全通过** —— `--startup` 只驻留不锁定、`Ctrl+Shift+O` 锁定、`Ctrl+O` 解锁、
  启动即锁定、空闲 1 分钟自动锁定、任务管理器让位与恢复、光标裁剪自愈、连点 10 下鼠标解锁。

## 文件说明

```
mouse_lock.cpp   主程序（单文件，含托盘图标自绘与设置界面）
build.bat        MSVC 一键编译脚本
build_mingw.bat  MinGW-w64 一键编译脚本
CMakeLists.txt   CMake 构建配置（可选）
```
