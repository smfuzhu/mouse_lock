/*
 * mouse_lock.cpp —— Windows 鼠标锁定工具
 *
 * 功能总览
 *   1. 锁定鼠标移动（鼠标按键与滚轮照常使用）
 *   2. Ctrl + O        切换 锁定 / 解锁
 *      Ctrl + Shift + O  直接锁定
 *      连点解锁          锁定态下 5 秒内连点 10 下鼠标即解锁（手机远程无键盘时可用）
 *   3. 空闲自动锁定：默认 5 分钟无操作自动锁定，阈值 1–1440 分钟可配置
 *   4. 开机自启：HKCU Run，自启只驻留托盘、不锁定（--startup 参数）
 *   5. 锁定时截取鼠标所在显示器的全屏画面并置顶显示（解锁即销毁）
 *   6. 任务管理器存在时截图层主动让位，方便用键盘结束进程
 *   7. 锁屏 / 休眠时暂停计时并自动解锁，回来不会面对冻结画面
 *
 * 编译：见 build.bat（MSVC）或 build_mingw.bat（MinGW-w64）
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WINVER
#define WINVER 0x0600
#endif

#include <windows.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <objidl.h>   // PROPID：MinGW 下必须早于 gdiplus.h 引入
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <deque>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "wtsapi32.lib")
#endif

// 一些老 SDK / MinGW 下可能缺失的定义
#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#ifndef WS_EX_NOACTIVATE
#define WS_EX_NOACTIVATE 0x08000000
#endif
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif
#ifndef PBT_APMRESUMEAUTOMATIC
#define PBT_APMRESUMEAUTOMATIC 0x0012
#endif
#ifndef LLMHF_INJECTED
#define LLMHF_INJECTED 0x00000001
#endif
#ifndef LLMHF_LOWER_IL_INJECTED
#define LLMHF_LOWER_IL_INJECTED 0x00000002
#endif
#ifndef LLKHF_INJECTED
#define LLKHF_INJECTED 0x00000010
#endif
#ifndef LLKHF_LOWER_IL_INJECTED
#define LLKHF_LOWER_IL_INJECTED 0x00000002
#endif

// ---------------------------------------------------------------- 小工具

template <size_t N>
static void CopyW(wchar_t (&dst)[N], const wchar_t *src) {
    size_t i = 0;
    for (; i + 1 < N && src[i]; ++i) dst[i] = src[i];
    dst[i] = L'\0';
}

static float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// 带 1 像素抗锯齿的圆角矩形覆盖率
static float RoundRectCoverage(float x, float y, float x0, float y0, float x1, float y1, float r) {
    float cx = std::clamp(x, x0 + r, x1 - r);
    float cy = std::clamp(y, y0 + r, y1 - r);
    float dx = x - cx, dy = y - cy;
    return Clamp01(0.5f - (std::sqrt(dx * dx + dy * dy) - r));
}

// 带 1 像素抗锯齿的圆环覆盖率
static float RingCoverage(float x, float y, float cx, float cy, float radius, float half) {
    float dx = x - cx, dy = y - cy;
    return Clamp01(0.5f - (std::fabs(std::sqrt(dx * dx + dy * dy) - radius) - half));
}

// ------------------------------------------------------- 运行时绘制托盘图标

struct Pix {
    float r = 0.f, g = 0.f, b = 0.f, a = 0.f;
};

static HICON CreateLockIcon(bool locked) {
    constexpr int S = 32;
    const float accR = locked ? 0.94f : 0.22f;
    const float accG = locked ? 0.36f : 0.78f;
    const float accB = locked ? 0.36f : 0.38f;

    std::vector<Pix> buf(static_cast<size_t>(S) * S);

    auto blend = [&](int x, int y, float r, float g, float b, float a) {
        if (x < 0 || y < 0 || x >= S || y >= S) return;
        if (a <= 0.f) return;
        if (a > 1.f) a = 1.f;
        Pix &p = buf[static_cast<size_t>(y) * S + x];
        const float ia = 1.f - a;
        p.r = p.r * ia + r * a;
        p.g = p.g * ia + g * a;
        p.b = p.b * ia + b * a;
        p.a = p.a * ia + a;
    };

    // 锁梁（解锁时向右上方弹开）
    const float cx = locked ? 16.f : 20.5f;
    const float cy = locked ? 15.f : 13.f;
    const float radius = 6.2f;
    const float half = 1.7f;

    auto shackle = [&](float x, float y, float extra) {
        return RingCoverage(x, y, cx, cy, radius + extra * 0.5f, half + extra) *
               Clamp01(0.5f - (y - cy + extra * 0.5f));
    };
    auto body = [&](float x, float y, float extra) {
        return RoundRectCoverage(x, y, 7.f - extra, 14.f - extra, 25.f + extra, 28.5f + extra,
                                 2.5f + extra);
    };

    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            float outline = std::max(body(px, py, 1.0f), shackle(px, py, 1.0f));
            blend(x, y, 0.07f, 0.08f, 0.10f, outline * 0.85f);
            float shape = std::max(body(px, py, 0.f), shackle(px, py, 0.f));
            blend(x, y, accR, accG, accB, shape);
            float hole = RoundRectCoverage(px, py, 14.6f, 18.4f, 17.4f, 24.2f, 1.0f);
            blend(x, y, 0.09f, 0.10f, 0.13f, hole * shape);
        }
    }

    std::vector<BYTE> dib(static_cast<size_t>(S) * S * 4);
    for (size_t i = 0; i < static_cast<size_t>(S) * S; ++i) {
        const Pix &p = buf[i];
        const float a = Clamp01(p.a);
        dib[i * 4 + 0] = static_cast<BYTE>(p.b * a * 255.f + 0.5f);
        dib[i * 4 + 1] = static_cast<BYTE>(p.g * a * 255.f + 0.5f);
        dib[i * 4 + 2] = static_cast<BYTE>(p.r * a * 255.f + 0.5f);
        dib[i * 4 + 3] = static_cast<BYTE>(a * 255.f + 0.5f);
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = S;
    bmi.bmiHeader.biHeight = -S;  // 自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) return nullptr;
    std::memcpy(bits, dib.data(), dib.size());

    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);  // 全 0：交给 alpha 通道
    if (!mask) {
        DeleteObject(color);
        return nullptr;
    }

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

// ---------------------------------------------------------------- 配置

enum LockMode : int {
    kLockMouse = 0,     // 锁鼠标：钩子 + ClipCursor（可带或不带截图）
    kFreezeScreen = 1,  // 冻结屏幕：截图置顶 + 不拦鼠标移动（点击/滚轮仍被吸收）
};

enum IdleMode : int {
    kIdleMouseOnly = 0,      // 仅鼠标移动（忽略合成输入）
    kIdleMouseKeyboard = 1,  // 系统判定 GetLastInputInfo（含合成输入）
    kIdleRealInputOnly = 2,  // 鼠标+键盘，但只认真实硬件输入（远控环境推荐）
};

struct Config {
    bool idleEnabled = true;
    int idleMinutes = 5;
    int idleMode = kIdleMouseKeyboard;
    bool strict = true;
    bool shotEnabled = true;
    bool saveShot = false;
    bool lockOnStart = true;  // 双击/无参启动时立即锁定（开机自启不受影响）
    bool debug = false;       // 调试模式：写 debug.log + 托盘显示空闲倒计时
    bool hideTray = false;    // 隐藏托盘图标（通过 mouse_lock.exe --tray 恢复）
    int lockMode = kFreezeScreen;  // 锁定方式：0=锁鼠标 1=冻结屏幕（默认）
    bool tapUnlock = true;         // 连点解锁：锁定态下 5 秒内连点 10 下鼠标即解锁（无需键盘）
};

static Config g_cfg;

static std::wstring GetAppDataDir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = n > 0 && n < MAX_PATH ? buf : L".";
    dir += L"\\MouseLock";
    return dir;
}

static std::wstring GetConfigPath() { return GetAppDataDir() + L"\\config.ini"; }
static std::wstring GetShotDir() { return GetAppDataDir() + L"\\shots"; }

static void EnsureDirExists(const std::wstring &dir) {
    CreateDirectoryW(dir.c_str(), nullptr);
}

// 先建一个带 UTF-16LE BOM 的空文件，后续 WritePrivateProfileStringW 才会按 Unicode 写入
static void EnsureConfigFile(const std::wstring &path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const wchar_t bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(h, &bom, sizeof(bom), &written, nullptr);
    CloseHandle(h);
}

static void LoadConfig() {
    const std::wstring dir = GetAppDataDir();
    EnsureDirExists(dir);
    const std::wstring path = GetConfigPath();
    EnsureConfigFile(path);
    const wchar_t *sec = L"General";
    g_cfg.idleEnabled = GetPrivateProfileIntW(sec, L"IdleEnabled", 1, path.c_str()) != 0;
    g_cfg.idleMinutes = GetPrivateProfileIntW(sec, L"IdleMinutes", 5, path.c_str());
    g_cfg.idleMode = GetPrivateProfileIntW(sec, L"IdleMode", 1, path.c_str());
    g_cfg.strict = GetPrivateProfileIntW(sec, L"Strict", 1, path.c_str()) != 0;
    g_cfg.shotEnabled = GetPrivateProfileIntW(sec, L"ShotEnabled", 1, path.c_str()) != 0;
    g_cfg.saveShot = GetPrivateProfileIntW(sec, L"SaveShot", 0, path.c_str()) != 0;
    g_cfg.lockOnStart = GetPrivateProfileIntW(sec, L"LockOnStart", 1, path.c_str()) != 0;
    g_cfg.debug = GetPrivateProfileIntW(sec, L"Debug", 0, path.c_str()) != 0;
    g_cfg.hideTray = GetPrivateProfileIntW(sec, L"HideTray", 0, path.c_str()) != 0;
    g_cfg.lockMode = GetPrivateProfileIntW(sec, L"LockMode", kFreezeScreen, path.c_str());
    if (g_cfg.lockMode != kLockMouse && g_cfg.lockMode != kFreezeScreen)
        g_cfg.lockMode = kFreezeScreen;
    g_cfg.tapUnlock = GetPrivateProfileIntW(sec, L"TapUnlock", 1, path.c_str()) != 0;

    if (g_cfg.idleMinutes < 1 || g_cfg.idleMinutes > 1440) g_cfg.idleMinutes = 5;
    if (g_cfg.idleMode < kIdleMouseOnly || g_cfg.idleMode > kIdleRealInputOnly)
        g_cfg.idleMode = kIdleMouseKeyboard;
}

static void WriteIntSetting(const wchar_t *key, int value) {
    const std::wstring path = GetConfigPath();
    wchar_t buf[32];
    swprintf(buf, 32, L"%d", value);
    WritePrivateProfileStringW(L"General", key, buf, path.c_str());
}

static void SaveConfig() {
    WriteIntSetting(L"IdleEnabled", g_cfg.idleEnabled ? 1 : 0);
    WriteIntSetting(L"IdleMinutes", g_cfg.idleMinutes);
    WriteIntSetting(L"IdleMode", g_cfg.idleMode);
    WriteIntSetting(L"Strict", g_cfg.strict ? 1 : 0);
    WriteIntSetting(L"ShotEnabled", g_cfg.shotEnabled ? 1 : 0);
    WriteIntSetting(L"SaveShot", g_cfg.saveShot ? 1 : 0);
    WriteIntSetting(L"LockOnStart", g_cfg.lockOnStart ? 1 : 0);
    WriteIntSetting(L"Debug", g_cfg.debug ? 1 : 0);
    WriteIntSetting(L"HideTray", g_cfg.hideTray ? 1 : 0);
    WriteIntSetting(L"LockMode", g_cfg.lockMode);
    WriteIntSetting(L"TapUnlock", g_cfg.tapUnlock ? 1 : 0);
}

// ---------------------------------------------------------------- 调试日志

static void DbgLog(const char *fmt, ...) {
    if (!g_cfg.debug) return;
    const std::wstring path = GetAppDataDir() + L"\\debug.log";

    // 超过 2MB 就重新开始，避免无限增长
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    bool tooBig = false;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        tooBig = fad.nFileSizeHigh > 0 || fad.nFileSizeLow > 2 * 1024 * 1024;

    FILE *f = _wfopen(path.c_str(), tooBig ? L"w" : L"a");
    if (!f) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "%02u:%02u:%02u.%03u | tick=%10lu | ", st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, static_cast<unsigned long>(GetTickCount()));
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

// ---------------------------------------------------------------- 开机自启

static constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr wchar_t kRunValue[] = L"MouseLock";

static std::wstring GetOwnExePath() {
    wchar_t buf[MAX_PATH * 2]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return n ? std::wstring(buf, n) : std::wstring();
}

static std::wstring BuildAutoStartCommand() {
    return L"\"" + GetOwnExePath() + L"\" --startup";
}

static bool QueryAutoStartValue(std::wstring &out) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
        return false;
    wchar_t buf[1024]{};
    DWORD size = sizeof(buf);
    LONG r = RegQueryValueExW(hk, kRunValue, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf),
                              &size);
    RegCloseKey(hk);
    if (r != ERROR_SUCCESS || buf[0] == L'\0') return false;
    out = buf;
    return true;
}

static bool IsAutoStartEnabled() {
    std::wstring v;
    return QueryAutoStartValue(v);
}

static bool SetAutoStart(bool enable) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hk) !=
        ERROR_SUCCESS)
        return false;
    bool ok = false;
    if (enable) {
        const std::wstring val = BuildAutoStartCommand();
        ok = RegSetValueExW(hk, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE *>(val.c_str()),
                            static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t))) ==
             ERROR_SUCCESS;
    } else {
        ok = RegDeleteValueW(hk, kRunValue) == ERROR_SUCCESS || GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(hk);
    return ok;
}

// B4：exe 被移动后，启动时校正注册表里的路径
static void ValidateAutoStartPath() {
    std::wstring stored;
    if (!QueryAutoStartValue(stored)) return;
    const std::wstring want = BuildAutoStartCommand();
    if (_wcsicmp(stored.c_str(), want.c_str()) != 0) SetAutoStart(true);
}

// ---------------------------------------------------------------- 全局状态

static constexpr wchar_t kMainClass[] = L"MouseLockHiddenWindowClass";

static constexpr UINT WM_TRAYICON = WM_APP + 1;
static constexpr UINT WM_APP_SHOWTRAY = WM_APP + 2;  // 让已运行实例重新显示托盘图标
static constexpr int HOTKEY_ID_TOGGLE = 1;   // Ctrl + O：切换
static constexpr int HOTKEY_ID_LOCK = 2;     // Ctrl + Shift + O：直接锁定
static constexpr int HOTKEY_ID_TOGGLE2 = 3;  // Ctrl + I：与 Ctrl+O 等价（绕开程序占用 Ctrl+O）
static constexpr UINT_PTR IDT_MAIN = 10;     // 500ms 巡检定时器
static constexpr int kMaxShots = 100;        // 截图保留数量
static constexpr int kMoveThresholdPx = 3;   // 计入活动的鼠标最小位移（像素，曼哈顿距离）
static constexpr DWORD kTapWindowMs = 5000;  // 连点解锁：判定窗口（毫秒）
static constexpr size_t kTapCount = 10;      // 连点解锁：窗口内需累计的按键次数

enum : UINT {
    IDM_TOGGLE = 1001,
    IDM_LOCK = 1005,
    IDM_STRICT = 1002,
    IDM_SETTINGS = 1006,
    IDM_AUTOSTART = 1007,
    IDM_DEBUG = 1008,
    IDM_HIDETRAY = 1009,
    IDM_ABOUT = 1003,
    IDM_EXIT = 1004
};

enum : UINT {
    IDC_CHK_IDLE = 2001,
    IDC_EDT_MIN,
    IDC_CB_IDLEMODE,
    IDC_CHK_STRICT,
    IDC_CHK_SHOT,
    IDC_CHK_SAVE,
    IDC_CHK_AUTO,
    IDC_CHK_LOCKONSTART,
    IDC_CB_LOCKMODE,
    IDC_CHK_TAP,
    IDC_BTN_OK,
    IDC_BTN_CANCEL,
    IDC_BTN_APPLY
};

static HINSTANCE g_hInst = nullptr;
static HWND g_hwnd = nullptr;          // 隐藏主窗口（热键 + 托盘回调）
static HWND g_cover = nullptr;         // 截图层（置顶全屏）
static HWND g_settings = nullptr;      // 非模态设置窗口
static HHOOK g_hook = nullptr;         // 低层鼠标钩子
static HHOOK g_kbdHook = nullptr;      // 低层键盘钩子（仅用于空闲判定）
static HICON g_iconLocked = nullptr;
static HICON g_iconUnlocked = nullptr;
static HFONT g_uiFont = nullptr;
static NOTIFYICONDATAW g_nid{};
static bool g_trayAdded = false;
static ULONG_PTR g_gdiplusToken = 0;
static bool g_gdiplusReady = false;

static std::atomic<bool> g_locked{true};
static std::atomic<bool> g_menuOpen{false};
static POINT g_anchor{0, 0};

// 空闲计时
static DWORD g_lastMouseTick = 0;      // 仅鼠标模式下，最近一次鼠标移动
static DWORD g_ignoreBefore = 0;       // 该时间点之前的空闲不算数（启动/解锁/唤醒时重置）
static bool g_sessionLocked = false;   // 锁屏 / 挂起中

// 让位（任务管理器 / 外来全屏遮罩）
static bool g_taskmgrPresent = false;
static bool g_taskmgrHintShown = false;
static bool g_foreignOverlay = false;   // 外来全屏置顶遮罩在位（如 ResProbe 隐私屏）
static bool g_overlayHintShown = false;
static int g_overlayStreak = 0;         // 外来遮罩判定的连续确认次数（去抖）
static bool g_yielding = false;         // 当前已让位（截图层隐藏中）
static POINT g_lastMousePos{0, 0};  // 最近一次「计入活动」的鼠标位置

// 调试
static DWORD g_dbgLastActivity = 0;
static DWORD g_dbgLastHeartbeat = 0;
static bool g_dbgInputSinceLog = false;
static bool g_dbgLastInputOk = true;
static int g_idleOverTicks = 0;  // 连续判定超时的巡检次数
static DWORD g_dbgGlitch = 0;    // now < last 的竞态次数（本应被钳制）
static DWORD g_dbgMovesReal = 0;     // 计入活动的真实鼠标移动
static std::deque<DWORD> g_tapTimes;            // 连点解锁：最近按键时间戳（单线程访问）
static std::atomic<bool> g_tapUnlockPending{false};  // 钩子请求主线程执行解锁
static DWORD g_dbgMovesIgnored = 0;  // 被忽略的移动（合成输入或位移过小）
static DWORD g_dbgKeysReal = 0;      // 计入活动的真实按键
static DWORD g_dbgKeysIgnored = 0;   // 被忽略的合成按键
static DWORD g_lastKeyTick = 0;      // 最近一次真实按键时间

// 截图
struct Shot {
    HBITMAP bmp = nullptr;
    RECT rect{0, 0, 0, 0};
};
static Shot g_shot;
static bool g_coverShowing = false;  // 覆盖层当前是否真的显示在屏幕上
static int g_monitorCount = 1;       // 显示器数量（启动时缓存）

// ---------------------------------------------------------------- 截图

static void FreeShot() {
    if (g_shot.bmp) {
        DeleteObject(g_shot.bmp);
        g_shot.bmp = nullptr;
    }
    g_shot.rect = RECT{0, 0, 0, 0};
}

static bool CaptureShot() {
    FreeShot();

    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return false;
    const RECT r = mi.rcMonitor;
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return false;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP bmp = CreateCompatibleBitmap(hdcScreen, w, h);
    if (!hdcMem || !bmp) {
        if (hdcMem) DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }
    HGDIOBJ old = SelectObject(hdcMem, bmp);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, r.left, r.top, SRCCOPY | CAPTUREBLT);

    // BitBlt 抓不到光标，需要手动补画。
    // 冻结屏幕模式下鼠标仍可自由移动，截图上那个静止的光标会变成一个"假光标"
    // （真实光标早已挪走），反而比不画更误导，因此只在锁鼠标模式下补画——
    // 那时光标被钉在原地，截图上的位置是真实且恒定的。
    const bool drawCursor = (g_cfg.lockMode == kLockMouse);
    DbgLog("shot %dx%d @(%d,%d) cursorDrawn=%d lockMode=%d", w, h, r.left, r.top,
           drawCursor ? 1 : 0, g_cfg.lockMode);

    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (drawCursor && GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
        ICONINFO ii{};
        if (GetIconInfo(ci.hCursor, &ii)) {
            const int x = ci.ptScreenPos.x - r.left - ii.xHotspot;
            const int y = ci.ptScreenPos.y - r.top - ii.yHotspot;
            DrawIconEx(hdcMem, x, y, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL | DI_COMPAT);
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
        }
    }

    SelectObject(hdcMem, old);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    g_shot.bmp = bmp;
    g_shot.rect = r;
    return true;
}

static int GetEncoderClsid(const wchar_t *mime, CLSID &clsid) {
    UINT num = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return -1;
    auto *info = static_cast<Gdiplus::ImageCodecInfo *>(std::malloc(size));
    if (!info) return -1;
    Gdiplus::GetImageEncoders(num, size, info);
    int idx = -1;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, mime) == 0) {
            clsid = info[i].Clsid;
            idx = static_cast<int>(i);
            break;
        }
    }
    std::free(info);
    return idx;
}

static void PruneShots(const std::wstring &dir, int keep) {
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW fd{};
    const std::wstring pattern = dir + L"\\shot_*.png";
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.push_back(fd.cFileName);
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    if (static_cast<int>(files.size()) <= keep) return;
    std::sort(files.begin(), files.end());  // 文件名含时间戳，字典序即时间序
    for (size_t i = 0; i + keep < files.size(); ++i)
        DeleteFileW((dir + L"\\" + files[i]).c_str());
}

static bool SaveShotPng() {
    if (!g_shot.bmp || !g_gdiplusReady) return false;
    const std::wstring dir = GetShotDir();
    EnsureDirExists(dir);
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[64];
    swprintf(name, 64, L"shot_%04u%02u%02u_%02u%02u%02u.png", st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    const std::wstring path = dir + L"\\" + name;

    CLSID clsid{};
    if (GetEncoderClsid(L"image/png", clsid) < 0) return false;
    Gdiplus::Bitmap bmp(g_shot.bmp, nullptr);
    const bool ok = bmp.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
    if (ok) PruneShots(dir, kMaxShots);
    return ok;
}

// ---------------------------------------------------------------- 截图层窗口

static constexpr wchar_t kCoverClass[] = L"MouseLockCoverClass";

static LRESULT CALLBACK CoverWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (g_shot.bmp) {
                HDC mem = CreateCompatibleDC(hdc);
                HGDIOBJ old = SelectObject(mem, g_shot.bmp);
                const int w = g_shot.rect.right - g_shot.rect.left;
                const int h = g_shot.rect.bottom - g_shot.rect.top;
                BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            if (g_cover == hwnd) g_cover = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static void ShowCover() {
    if (!g_shot.bmp) return;
    const RECT &r = g_shot.rect;
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    if (!g_cover) {
        g_cover = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCoverClass,
                                  L"", WS_POPUP, r.left, r.top, w, h, nullptr, nullptr, g_hInst,
                                  nullptr);
        if (!g_cover) return;
    }
    SetWindowPos(g_cover, HWND_TOPMOST, r.left, r.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_cover, nullptr, TRUE);
    g_coverShowing = true;
    DbgLog("cover shown");
}

static void HideCover() {
    if (!g_cover) return;
    ShowWindow(g_cover, SW_HIDE);
    g_coverShowing = false;
    DbgLog("cover hidden (yielded)");
}

static void DestroyCover() {
    if (!g_cover) return;
    DestroyWindow(g_cover);
    g_cover = nullptr;
    g_coverShowing = false;
}

// ---------------------------------------------------------------- 任务管理器检测

static BOOL CALLBACK FindTaskmgrProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) return TRUE;

    wchar_t cls[256]{};
    if (GetClassNameW(hwnd, cls, 256) && _wcsicmp(cls, L"TaskManagerWindow") == 0) {
        *reinterpret_cast<bool *>(lParam) = true;
        return FALSE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return TRUE;
    wchar_t path[MAX_PATH * 2]{};
    DWORD size = MAX_PATH * 2;
    BOOL got = QueryFullProcessImageNameW(h, 0, path, &size);
    CloseHandle(h);
    if (!got) return TRUE;
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    if (_wcsicmp(name, L"Taskmgr.exe") == 0) {
        *reinterpret_cast<bool *>(lParam) = true;
        return FALSE;
    }
    return TRUE;
}

static bool DetectTaskmgr() {
    bool found = false;
    EnumWindows(FindTaskmgrProc, reinterpret_cast<LPARAM>(&found));
    return found;
}

// ---------------------------------------------------------------- 外来全屏遮罩检测
//
// 场景：另一款"全屏置顶遮罩"工具（典型是 ResProbe 的隐私屏）与我方截图层同在置顶带里，
// 双方都周期性把自己抬到最上层。实测（60Hz 采样，两次独立复现，结论一致）：
//   - 我方 500ms 一次的重新置顶 100% 抢赢，占空 11~13%；
//   - 对方 100ms 一次的 HWND_TOP 总能在 120ms 内抢回；
//   - 净效果 = 每 500ms 露一次冻结画面（中位 65ms），远控端每秒能看两次。
// 对方在位时截图层本来就被完全遮住、功能被抵消，所以应由我方让位：
// 隐藏截图层、回落锁鼠标兜底，把"谁在最上面"彻底让出去。
//
// 判定用"独立扫描"而不是"看我方窗口的上邻"：让位期间我方窗口是隐藏的，
// 以自己的 Z 序位置当参照会在边界上抖动。
struct OverlayScanCtx {
    RECT target;
    DWORD selfPid;
    bool found;
};

static BOOL CALLBACK FindForeignOverlayProc(HWND hwnd, LPARAM lParam) {
    auto *ctx = reinterpret_cast<OverlayScanCtx *>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;
    // 非置顶窗口不可能压在置顶遮罩之上，顺手滤掉绝大多数普通窗口
    if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == ctx->selfPid) return TRUE;  // 自己的窗口不算

    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    const LONG iw = (std::min)(r.right, ctx->target.right) - (std::max)(r.left, ctx->target.left);
    const LONG ih = (std::min)(r.bottom, ctx->target.bottom) - (std::max)(r.top, ctx->target.top);
    if (iw <= 0 || ih <= 0) return TRUE;

    const long long inter = static_cast<long long>(iw) * ih;
    const long long area = static_cast<long long>(ctx->target.right - ctx->target.left) *
                           static_cast<long long>(ctx->target.bottom - ctx->target.top);
    if (area <= 0) return TRUE;
    // ≥90% 覆盖：任务栏、开始菜单、输入法候选框、普通弹窗都远达不到这个比例
    if (inter * 100 < area * 90) return TRUE;

    ctx->found = true;
    return FALSE;
}

static bool DetectForeignFullscreenOverlay() {
    // 与我方截图层对齐：截的是鼠标所在显示器，判据也用同一个矩形
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return false;

    OverlayScanCtx ctx{};
    ctx.target = mi.rcMonitor;
    ctx.selfPid = GetCurrentProcessId();
    ctx.found = false;
    EnumWindows(FindForeignOverlayProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

// ---------------------------------------------------------------- 空闲计时

static DWORD LastActivityTick() {
    if (g_cfg.idleMode == kIdleMouseKeyboard) {
        LASTINPUTINFO lii{};
        lii.cbSize = sizeof(lii);
        if (GetLastInputInfo(&lii)) {
            g_dbgLastInputOk = true;
            return lii.dwTime;
        }
        // 获取失败时绝不能回退到 g_lastMouseTick：只打字不动鼠标时它可能是几分钟前的
        // 时间戳，会把空闲时长瞬间放大成"已超时"，导致刚解锁就被误锁。
        g_dbgLastInputOk = false;
        return GetTickCount();  // 视为"此刻有活动"
    }
    if (g_cfg.idleMode == kIdleRealInputOnly)
        return std::max(g_lastMouseTick, g_lastKeyTick);
    return g_lastMouseTick;
}

// 以 g_ignoreBefore 为下界计算空闲时长（天然免疫 GetTickCount 49.7 天回绕）
static DWORD IdleElapsedMs(DWORD now) {
    DWORD last = LastActivityTick();
    if (static_cast<int>(last - g_ignoreBefore) < 0) last = g_ignoreBefore;
    // 防御：last 跑到 now 之后时，unsigned 相减会绕成一个巨大值并立即判定为超时。
    // 这个竞态真实存在：取完 now 之后，用户按键在下一瞬间到达，GetLastInputInfo
    // 返回的时间戳反而比 now 还新（正在打字时几乎每秒都会发生）。
    if (static_cast<int>(now - last) < 0) {
        ++g_dbgGlitch;
        return 0;
    }
    return now - last;
}

// 注意：这里不再因"任务管理器存在"而暂停计时。
// 用户操作任务管理器时本身就会产生真实输入，空闲计时自然会被重置；
// 反过来，若任务管理器一直开着就永久暂停，自动锁定会彻底失效。
// 任务管理器在场时只做"截图层让位"（HideCover），不再干扰计时。
static bool IdleTimerActive() {
    return g_cfg.idleEnabled && !g_locked.load() && !g_sessionLocked;
}

// ---------------------------------------------------------------- 核心逻辑

// 是否真的需要"锁鼠标移动"。
// 冻结屏幕模式下，截图层已经盖住画面并吸收点击/滚轮，鼠标动不动已无意义，
// 因此不必再拦；但截图层没盖住时（任务管理器让位、截图失败、关闭截图、多显示器）
// 必须锁鼠标兜底，否则屏幕就是裸奔的。
// 注意：本函数会在鼠标钩子回调里高频调用，只能读缓存值，不能调窗口 API。
static bool NeedMouseLock() {
    if (g_cfg.lockMode == kLockMouse) return true;
    if (!g_cfg.shotEnabled) return true;  // 不截图，只能靠锁鼠标
    if (g_monitorCount > 1) return true;  // 多屏：截图只盖一个屏，其它屏必须锁
    return !g_coverShowing;               // 让位或截图失败时兜底
}

static void ApplyLock() {
    if (g_locked.load() && !g_menuOpen.load() && NeedMouseLock() && g_cfg.strict) {
        GetCursorPos(&g_anchor);
        RECT r{g_anchor.x, g_anchor.y, g_anchor.x + 1, g_anchor.y + 1};
        ClipCursor(&r);
    } else {
        ClipCursor(nullptr);
    }
}

// Windows 会在前台窗口切换（尤其是跨完整性级别，如任务管理器这类提权窗口）时
// 自动清除 ClipCursor，导致锁定被悄悄解除。这里周期性重新裁剪，让锁定自愈。
static void ReassertClip() {
    if (!g_locked.load() || g_menuOpen.load() || !g_cfg.strict) return;
    if (!NeedMouseLock()) return;  // 冻结屏幕模式且覆盖层在位时不需要裁剪
    RECT r{g_anchor.x, g_anchor.y, g_anchor.x + 1, g_anchor.y + 1};
    ClipCursor(&r);
}

static void ShowBalloon(const wchar_t *title, const wchar_t *text, DWORD flags) {
    if (!g_trayAdded) return;
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = flags;
    CopyW(g_nid.szInfoTitle, title);
    CopyW(g_nid.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void UpdateTray(bool balloon) {
    if (!g_trayAdded) return;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.hIcon = g_locked.load() ? g_iconLocked : g_iconUnlocked;
    CopyW(g_nid.szTip, g_locked.load()
                           ? (g_cfg.lockMode == kFreezeScreen ? L"屏幕已冻结（Ctrl+O / Ctrl+I / 连点10下解锁）"
                                                              : L"鼠标已锁定（Ctrl+O / Ctrl+I / 连点10下解锁）")
                           : L"未锁定（Ctrl+Shift+O 锁定）");
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (balloon) {
        ShowBalloon(L"鼠标锁定工具",
                    g_locked.load() ? L"已锁定，按 Ctrl+O / Ctrl+I 解除。"
                                    : L"已解除锁定，按 Ctrl+Shift+O 重新锁定。",
                    NIIF_INFO);
    }
}

static void ShowTrayIcon() {
    if (g_trayAdded) return;
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.hIcon = g_locked.load() ? g_iconLocked : g_iconUnlocked;
    CopyW(g_nid.szTip, L"鼠标锁定工具");
    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) return;
    g_trayAdded = true;
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
    UpdateTray(false);
    DbgLog("tray icon shown");
}

static void HideTrayIcon() {
    if (!g_trayAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayAdded = false;
    DbgLog("tray icon hidden");
}

// 调试模式下，托盘提示实时显示空闲倒计时，方便观察计时是否被输入重置
static void UpdateDebugTip(DWORD now) {
    if (!g_trayAdded || g_locked.load()) return;
    const char *state = !g_cfg.idleEnabled ? "已禁用"
                        : IdleTimerActive() ? "计时中"
                                            : "已暂停";
    wchar_t tip[128];
    swprintf(tip, 128, L"调试 | 空闲 %lu s / %lu s（%hs）",
             static_cast<unsigned long>(IdleElapsedMs(now) / 1000),
             static_cast<unsigned long>(g_cfg.idleMinutes) * 60UL, state);
    CopyW(g_nid.szTip, tip);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// 锁定：截图 → 显示截图层 → 生效锁定
static void LockNow(bool balloon, const char *reason = "manual") {
    DbgLog("LOCK   reason=%s idle=%lu/%lu ms mode=%d", reason,
           static_cast<unsigned long>(IdleElapsedMs(GetTickCount())),
           static_cast<unsigned long>(g_cfg.idleMinutes) * 60000UL, g_cfg.idleMode);
    // 让位判定必须放在截图之前：外来全屏遮罩在位时（对方带捕获排除），
    // BitBlt 抓到的既不是真实桌面也不是有效画面，抓了只会白闪一下、
    // 还可能把画面落盘。这里同时刷新缓存值，避免第一轮巡检再翻一次。
    g_taskmgrPresent = DetectTaskmgr();
    g_foreignOverlay = DetectForeignFullscreenOverlay();
    g_overlayStreak = 0;
    const bool yieldNow = g_taskmgrPresent || g_foreignOverlay;
    g_yielding = yieldNow;

    if (g_cfg.shotEnabled && !yieldNow) {
        if (CaptureShot()) {
            ShowCover();
            if (g_cfg.saveShot) SaveShotPng();
        }
    } else if (g_coverShowing) {
        HideCover();  // 锁定态重复调用时别把上一轮的截图层留在最上层
    }
    if (yieldNow) {
        DbgLog("       yield at lock: taskmgr=%d ovl=%d (skip capture, mouse-lock fallback)",
               g_taskmgrPresent ? 1 : 0, g_foreignOverlay ? 1 : 0);
    }
    g_locked = true;
    ApplyLock();
    UpdateTray(balloon);
}

// 解锁：销毁截图层 → 释放内存 → 解除锁定 → 重置空闲计时
static void UnlockNow(bool balloon, const char *reason = "manual") {
    DbgLog("UNLOCK reason=%s", reason);
    g_locked = false;
    DestroyCover();
    FreeShot();
    g_yielding = false;    // 截图层已销毁，让位状态随之归零（下次锁定会重新判定）
    g_overlayStreak = 0;
    ApplyLock();
    g_ignoreBefore = GetTickCount();
    DbgLog("       idle timer reset (g_ignoreBefore=%lu)",
           static_cast<unsigned long>(g_ignoreBefore));
    UpdateTray(balloon);
}

static void SetLock(bool locked, bool balloon, const char *reason = "manual") {
    if (locked)
        LockNow(balloon, reason);
    else
        UnlockNow(balloon, reason);
}

static void ToggleLock(bool balloon, const char *reason = "manual") {
    SetLock(!g_locked.load(), balloon, reason);
}

// 低层鼠标钩子：拦截移动事件，按键与滚轮照常放行
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // 连点解锁：锁定态下 5 秒内累计 10 次鼠标按键（任意按钮，含远控注入事件）即解锁。
    // 此处刻意不过滤 LLMHF_INJECTED —— 手机远控传入的点击正是解锁手段（与空闲判定相反）。
    if (nCode >= 0 && g_locked.load() && g_cfg.tapUnlock &&
        (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN ||
         wParam == WM_XBUTTONDOWN)) {
        const DWORD now = GetTickCount();
        g_tapTimes.push_back(now);
        while (!g_tapTimes.empty() && (now - g_tapTimes.front()) > kTapWindowMs)
            g_tapTimes.pop_front();
        if (g_tapTimes.size() >= kTapCount) {
            g_tapTimes.clear();
            g_tapUnlockPending.store(true);
            DbgLog("TAP-UNLOCK triggered (count>=%zu)", kTapCount);
        } else {
            DbgLog("TAP count=%zu window=%lu", g_tapTimes.size(),
                   g_tapTimes.empty() ? 0UL : (now - g_tapTimes.front()));
        }
    }

    if (nCode >= 0 && (wParam == WM_MOUSEMOVE || wParam == WM_NCMOUSEMOVE)) {
        // 空闲判定只认「真实硬件的、有位移的」鼠标操作。
        // 远控软件（向日葵/RustDesk/ToDesk 等）常定时注入输入来防休眠，
        // 这类输入会打上 LLMHF_INJECTED 标记；照单全收的话空闲时长永远攒不满。
        const auto *hs = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
        if (hs) {
            const bool injected = (hs->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) != 0;
            const int dist = std::abs(hs->pt.x - g_lastMousePos.x) + std::abs(hs->pt.y - g_lastMousePos.y);
            if (!injected && dist >= kMoveThresholdPx) {
                g_lastMouseTick = GetTickCount();
                g_lastMousePos = hs->pt;
                ++g_dbgMovesReal;
            } else {
                ++g_dbgMovesIgnored;
            }
        }
        if (g_locked.load() && !g_menuOpen.load() && NeedMouseLock())
            return 1;  // 吞掉：光标不再移动
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

// 低层键盘钩子：只用于空闲判定，区分真实按键与合成输入（不拦截任何按键）
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN || wParam == WM_KEYUP ||
                       wParam == WM_SYSKEYUP)) {
        const auto *ks = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        if (ks) {
            const bool injected = (ks->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) != 0;
            if (injected)
                ++g_dbgKeysIgnored;
            else {
                g_lastKeyTick = GetTickCount();
                ++g_dbgKeysReal;
            }
        }
    }
    return CallNextHookEx(g_kbdHook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------- 设置窗口

static constexpr wchar_t kSettingsClass[] = L"MouseLockSettingsClass";

static void SetCheck(UINT id, bool checked) {
    CheckDlgButton(g_settings, id, checked ? BST_CHECKED : BST_UNCHECKED);
}
static bool IsChecked(UINT id) { return IsDlgButtonChecked(g_settings, id) == BST_CHECKED; }

static HWND AddControl(const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w,
                       int height, UINT id) {
    HWND ctrl = CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, x, y, w, height,
                                g_settings, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
                                g_hInst, nullptr);
    if (ctrl && g_uiFont) SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
    return ctrl;
}

static void MakeSettingsFont() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
        g_uiFont = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!g_uiFont) g_uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

static void FillSettingsFromConfig() {
    SetCheck(IDC_CHK_IDLE, g_cfg.idleEnabled);
    wchar_t buf[32];
    swprintf(buf, 32, L"%d", g_cfg.idleMinutes);
    SetWindowTextW(GetDlgItem(g_settings, IDC_EDT_MIN), buf);
    SendMessageW(GetDlgItem(g_settings, IDC_CB_IDLEMODE), CB_SETCURSEL,
                 static_cast<WPARAM>(g_cfg.idleMode), 0);
    SetCheck(IDC_CHK_STRICT, g_cfg.strict);
    SetCheck(IDC_CHK_SHOT, g_cfg.shotEnabled);
    SetCheck(IDC_CHK_SAVE, g_cfg.saveShot);
    SetCheck(IDC_CHK_AUTO, IsAutoStartEnabled());
    SetCheck(IDC_CHK_LOCKONSTART, g_cfg.lockOnStart);
    SendMessageW(GetDlgItem(g_settings, IDC_CB_LOCKMODE), CB_SETCURSEL,
                 static_cast<WPARAM>(g_cfg.lockMode), 0);
    SetCheck(IDC_CHK_TAP, g_cfg.tapUnlock);
}

static bool ApplySettings() {
    g_cfg.idleEnabled = IsChecked(IDC_CHK_IDLE);

    wchar_t buf[32]{};
    GetWindowTextW(GetDlgItem(g_settings, IDC_EDT_MIN), buf, 32);
    const int minutes = _wtoi(buf);
    if (minutes >= 1 && minutes <= 1440) {
        g_cfg.idleMinutes = minutes;
    } else {
        swprintf(buf, 32, L"%d", g_cfg.idleMinutes);
        SetWindowTextW(GetDlgItem(g_settings, IDC_EDT_MIN), buf);
        MessageBoxW(g_settings, L"空闲阈值需要是 1 – 1440 之间的整数，已恢复为原值。", L"设置",
                    MB_ICONWARNING | MB_OK);
        return false;
    }

    g_cfg.idleMode = static_cast<int>(
        SendMessageW(GetDlgItem(g_settings, IDC_CB_IDLEMODE), CB_GETCURSEL, 0, 0));
    if (g_cfg.idleMode < 0 || g_cfg.idleMode > kIdleRealInputOnly) g_cfg.idleMode = kIdleMouseKeyboard;
    g_cfg.strict = IsChecked(IDC_CHK_STRICT);
    g_cfg.shotEnabled = IsChecked(IDC_CHK_SHOT);
    g_cfg.saveShot = IsChecked(IDC_CHK_SAVE);
    g_cfg.lockOnStart = IsChecked(IDC_CHK_LOCKONSTART);
    g_cfg.lockMode =
        static_cast<int>(SendMessageW(GetDlgItem(g_settings, IDC_CB_LOCKMODE), CB_GETCURSEL, 0, 0));
    if (g_cfg.lockMode != kLockMouse && g_cfg.lockMode != kFreezeScreen)
        g_cfg.lockMode = kFreezeScreen;
    g_cfg.tapUnlock = IsChecked(IDC_CHK_TAP);

    SaveConfig();
    SetAutoStart(IsChecked(IDC_CHK_AUTO));
    ApplyLock();

    // 关闭截图功能时，若正处于锁定态则撤掉截图层
    if (!g_cfg.shotEnabled && g_locked.load()) {
        DestroyCover();
        FreeShot();
    }
    g_ignoreBefore = GetTickCount();
    return true;
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_OK:
                    if (ApplySettings()) DestroyWindow(hwnd);
                    return 0;
                case IDC_BTN_CANCEL:
                    DestroyWindow(hwnd);
                    return 0;
                case IDC_BTN_APPLY:
                    ApplySettings();
                    return 0;
                default:
                    break;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (g_settings == hwnd) g_settings = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void OpenSettings() {
    if (g_settings) {
        SetForegroundWindow(g_settings);
        return;
    }
    g_settings = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsClass, L"鼠标锁定工具 - 设置",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                 CW_USEDEFAULT, 470, 380, nullptr, nullptr, g_hInst, nullptr);
    if (!g_settings) return;

    AddControl(L"button", L"启用空闲自动锁定", BS_AUTOCHECKBOX, 20, 16, 300, 22, IDC_CHK_IDLE);
    AddControl(L"static", L"空闲阈值：", 0, 20, 48, 80, 22, 0);
    AddControl(L"edit", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 100, 46, 60, 24, IDC_EDT_MIN);
    AddControl(L"static", L"分钟（1 – 1440）", 0, 170, 48, 160, 22, 0);

    AddControl(L"static", L"空闲判定方式：", 0, 20, 82, 100, 22, 0);
    HWND cb = AddControl(L"combobox", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 125, 79,
                         325, 140, IDC_CB_IDLEMODE);
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"鼠标+键盘不动（系统判定，含合成输入）"));
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"仅鼠标移动（忽略合成输入）"));
    SendMessageW(cb, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"鼠标+键盘真实输入（忽略合成输入，推荐）"));

    AddControl(L"button", L"强力模式（ClipCursor，可拦截绕过钩子的程序）", BS_AUTOCHECKBOX, 20, 112,
               420, 22, IDC_CHK_STRICT);
    AddControl(L"button", L"锁定时截取全屏并置顶显示", BS_AUTOCHECKBOX, 20, 142, 420, 22,
               IDC_CHK_SHOT);
    AddControl(L"button", L"同时保存截图（PNG）到 AppData\\MouseLock\\shots", BS_AUTOCHECKBOX, 20,
               172, 420, 22, IDC_CHK_SAVE);
    AddControl(L"button", L"开机自启（开机后只驻留托盘，不自动锁定）", BS_AUTOCHECKBOX, 20, 202, 420,
               22, IDC_CHK_AUTO);
    AddControl(L"button", L"程序启动时立即锁定（开机自启不受此项影响）", BS_AUTOCHECKBOX, 20, 232,
               420, 22, IDC_CHK_LOCKONSTART);

    AddControl(L"static", L"锁定方式：", 0, 20, 264, 100, 22, 0);
    HWND cb2 = AddControl(L"combobox", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 125, 261,
                          325, 120, IDC_CB_LOCKMODE);
    SendMessageW(cb2, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"锁鼠标：拦截移动（可带截图）"));
    SendMessageW(cb2, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"冻结屏幕：截图置顶，鼠标仍可移动"));

    AddControl(L"button", L"连点解锁（锁定后 5 秒内连点 10 下鼠标，无需键盘）", BS_AUTOCHECKBOX, 20,
               296, 430, 22, IDC_CHK_TAP);

    AddControl(L"button", L"确定", BS_DEFPUSHBUTTON, 230, 330, 70, 26, IDC_BTN_OK);
    AddControl(L"button", L"取消", BS_PUSHBUTTON, 310, 330, 70, 26, IDC_BTN_CANCEL);
    AddControl(L"button", L"应用", BS_PUSHBUTTON, 390, 330, 70, 26, IDC_BTN_APPLY);

    FillSettingsFromConfig();
    ShowWindow(g_settings, SW_SHOW);
    SetForegroundWindow(g_settings);
}

// ---------------------------------------------------------------- 托盘菜单

static void ShowTrayMenu(HWND hwnd) {
    POINT pt{};
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE,
                g_locked.load() ? L"解除锁定\tCtrl+O / Ctrl+I" : L"重新锁定\tCtrl+O / Ctrl+I");
    AppendMenuW(menu, MF_STRING | (g_locked.load() ? (MF_GRAYED | MF_DISABLED) : 0), IDM_LOCK,
                L"立即锁定\tCtrl+Shift+O");
    AppendMenuW(menu, MF_STRING | (g_cfg.strict ? MF_CHECKED : 0), IDM_STRICT,
                L"强力模式（ClipCursor）");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu,
                MF_STRING | (g_locked.load() ? (MF_GRAYED | MF_DISABLED) : 0), IDM_SETTINGS,
                L"设置…");
    AppendMenuW(menu, MF_STRING | (IsAutoStartEnabled() ? MF_CHECKED : 0), IDM_AUTOSTART,
                L"开机自启");
    AppendMenuW(menu, MF_STRING | (g_cfg.debug ? MF_CHECKED : 0), IDM_DEBUG,
                L"调试模式（写 debug.log + 托盘倒计时）");
    AppendMenuW(menu, MF_STRING, IDM_HIDETRAY, L"隐藏托盘图标");
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"使用说明");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");

    // 菜单展开期间必须放开鼠标，否则光标被钉住无法选择菜单项
    g_menuOpen = true;
    ClipCursor(nullptr);

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd,
                   nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    g_menuOpen = false;
    ApplyLock();
    DestroyMenu(menu);
}

static void ShowAbout() {
    MessageBoxW(
        nullptr,
        L"鼠标锁定工具\r\n\r\n"
        L"快捷键\r\n"
        L"  Ctrl + O            切换 锁定 / 解锁\r\n"
        L"  Ctrl + I            同上（某些程序占用 Ctrl+O 时改用这个）\r\n"
        L"  Ctrl + Shift + O    直接锁定\r\n"
        L"  连点解锁            锁定后 5 秒内连点 10 下鼠标（任意键）即解锁，\r\n"
        L"                      适合手机远程、键盘不便的场景\r\n\r\n"
        L"托盘图标\r\n"
        L"  双击                切换 锁定 / 解锁\r\n"
        L"  右键                打开菜单（可隐藏托盘图标）\r\n"
        L"  隐藏后恢复          运行 mouse_lock.exe --tray\r\n"
        L"  隐藏后退出          运行 mouse_lock.exe --quit\r\n\r\n"
        L"锁定方式（设置中可切换）\r\n"
        L"  - 冻结屏幕（默认）：截取当前屏幕并置顶显示，画面被冻结、\r\n"
        L"    点击与滚轮被吸收，鼠标仍可自由移动。\r\n"
        L"  - 锁鼠标：拦截鼠标移动，鼠标按键与滚轮仍可正常使用。\r\n\r\n"
        L"说明\r\n"
        L"  - 冻结屏幕模式下，若截图层被隐藏（如任务管理器让位）或截图失败，\r\n"
        L"    会自动临时切换为锁鼠标兜底。\r\n"
        L"  - 多显示器时只截取鼠标所在显示器，此时会自动改用锁鼠标以保证安全。\r\n"
        L"  - 空闲自动锁定：默认 5 分钟无操作触发，阈值可在「设置」中调整。\r\n"
        L"  - 锁定时会截取鼠标所在显示器的画面并置顶显示；解锁后自动销毁。\r\n"
        L"  - 任务管理器运行时，截图层会自动让位，可用键盘结束本进程。\r\n"
        L"  - 锁屏或系统休眠时会自动解锁并暂停计时，回来不会看到冻结画面。\r\n"
        L"  - 强力模式额外调用 ClipCursor 把光标钉在原地。",
        L"使用说明", MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
}

// ---------------------------------------------------------------- 定时器巡检

static void OnMainTimer() {
    const DWORD now = GetTickCount();

    // 0) 连点解锁请求（由鼠标钩子置位，在此主线程安全地执行，避免在钩子回调里操作窗口）
    if (g_tapUnlockPending.exchange(false) && g_locked.load()) {
        UnlockNow(true, "tap-unlock");
    }

    // 1) 让位判定：任务管理器 / 外来全屏置顶遮罩（跨 Z 序带，只能我方主动让位）
    g_taskmgrPresent = DetectTaskmgr();

    // 去抖故意做成不对称的：
    //   发现 -> 立刻让位（漏判一个 tick 就是多闪一次，宁可早让）；
    //   消失 -> 连续 2 次确认才恢复（误恢复一次就又要 ShowCover 抢回顶部，等于再闪一次）。
    const bool rawOverlay = DetectForeignFullscreenOverlay();
    if (rawOverlay) {
        g_overlayStreak = 0;
        g_foreignOverlay = true;
    } else if (g_foreignOverlay) {
        if (++g_overlayStreak >= 2) {
            g_overlayStreak = 0;
            g_foreignOverlay = false;
        }
    } else {
        g_overlayStreak = 0;
    }

    const bool yield = g_taskmgrPresent || g_foreignOverlay;
    if (yield != g_yielding) {
        g_yielding = yield;
        // 未锁定时只同步标志：没有截图层可让位，也不该弹"已让位"的气球
        if (!g_locked.load()) {
            DbgLog("yield=%d (unlocked, flags only)", yield ? 1 : 0);
        } else if (yield) {
            HideCover();
            ApplyLock();  // 覆盖层让位 → 自动接管为"锁鼠标"兜底
            if (g_taskmgrPresent) {
                if (!g_taskmgrHintShown) {
                    g_taskmgrHintShown = true;
                    ShowBalloon(L"鼠标锁定工具", L"检测到任务管理器，截图层已临时让位。",
                                NIIF_INFO);
                }
            } else if (!g_overlayHintShown) {
                g_overlayHintShown = true;
                ShowBalloon(L"鼠标锁定工具",
                            L"检测到其它全屏遮罩窗口，截图层已临时让位（改用锁鼠标）。", NIIF_INFO);
            }
            DbgLog("yield=1 taskmgr=%d ovl=%d coverShowing=%d needMouseLock=%d",
                   g_taskmgrPresent ? 1 : 0, g_foreignOverlay ? 1 : 0, g_coverShowing ? 1 : 0,
                   NeedMouseLock() ? 1 : 0);
        } else {
            g_ignoreBefore = now;  // 让位结束，重新计时
            // 让位期间可能压根没截过图（锁定那一刻就已让位），这里补一张，
            // 否则 ShowCover() 会因为 g_shot.bmp 为空直接返回，永远只剩锁鼠标。
            if (!g_shot.bmp && g_cfg.shotEnabled) CaptureShot();
            ShowCover();
            ApplyLock();  // 覆盖层恢复 → 交还给"冻结屏幕"
            DbgLog("yield=0 taskmgr=%d ovl=%d coverShowing=%d needMouseLock=%d",
                   g_taskmgrPresent ? 1 : 0, g_foreignOverlay ? 1 : 0, g_coverShowing ? 1 : 0,
                   NeedMouseLock() ? 1 : 0);
        }
    }

    // 2) 截图层保持置顶（让位期间不抢，注意这行带 SWP_SHOWWINDOW，
    //    漏判会把刚隐藏的窗口又显示出来），并重新断言光标裁剪
    if (g_locked.load()) {
        ReassertClip();
        if (!g_yielding && g_cover) {
            SetWindowPos(g_cover, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }
    }

    // 3) 空闲自动锁定：需连续两次巡检（约 1 秒）都判定超时才锁定，
    //    避免单次采样抖动造成误锁
    const DWORD threshold = static_cast<DWORD>(g_cfg.idleMinutes) * 60000UL;
    if (IdleTimerActive() && IdleElapsedMs(now) >= threshold) {
        ++g_idleOverTicks;
        if (g_idleOverTicks >= 2) {
            g_idleOverTicks = 0;
            DbgLog("IDLE-TRIGGER elapsed=%lu/%lu minutes=%d last=%lu ignore=%lu gli=%d",
                   static_cast<unsigned long>(IdleElapsedMs(now)),
                   static_cast<unsigned long>(threshold), g_cfg.idleMinutes,
                   static_cast<unsigned long>(LastActivityTick()),
                   static_cast<unsigned long>(g_ignoreBefore), g_dbgLastInputOk ? 1 : 0);
            LockNow(true, "idle-timeout");
        }
    } else {
        g_idleOverTicks = 0;
    }

    // 4) 调试：每秒一条心跳 + 检测到输入（计时被重置）时单独记录
    if (g_cfg.debug) {
        const DWORD lastTick = LastActivityTick();
        if (lastTick != g_dbgLastActivity) {
            g_dbgLastActivity = lastTick;
            g_dbgInputSinceLog = true;
        }
        if (now - g_dbgLastHeartbeat >= 1000) {
            g_dbgLastHeartbeat = now;
            if (g_dbgInputSinceLog) {
                g_dbgInputSinceLog = false;
                DbgLog("INPUT  detected: lastInput advanced, idle now=%lu ms (mode=%d)",
                       static_cast<unsigned long>(IdleElapsedMs(now)), g_cfg.idleMode);
            }
            DbgLog("TICK   idle=%lu/%lu ms | locked=%d mode=%d taskmgr=%d ovl=%d yield=%d session=%d menu=%d active=%d cover=%d mouselock=%d gli=%d glitch=%lu moves(r/i)=%lu/%lu keys(r/i)=%lu/%lu",
                   static_cast<unsigned long>(IdleElapsedMs(now)),
                   static_cast<unsigned long>(threshold), g_locked.load() ? 1 : 0, g_cfg.idleMode,
                   g_taskmgrPresent ? 1 : 0, g_foreignOverlay ? 1 : 0, g_yielding ? 1 : 0,
                   g_sessionLocked ? 1 : 0, g_menuOpen.load() ? 1 : 0,
                   IdleTimerActive() ? 1 : 0, g_coverShowing ? 1 : 0, NeedMouseLock() ? 1 : 0,
                   g_dbgLastInputOk ? 1 : 0,
                   static_cast<unsigned long>(g_dbgGlitch),
                   static_cast<unsigned long>(g_dbgMovesReal),
                   static_cast<unsigned long>(g_dbgMovesIgnored),
                   static_cast<unsigned long>(g_dbgKeysReal),
                   static_cast<unsigned long>(g_dbgKeysIgnored));
            UpdateDebugTip(now);
        }
    }
}

// ---------------------------------------------------------------- 窗口过程

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HOTKEY:
            // lParam: 低字=修饰键, 高字=虚拟键码
            DbgLog("HOTKEY id=%llu mods=0x%04X vk=0x%02X locked=%d",
                   static_cast<unsigned long long>(wParam), static_cast<unsigned>(LOWORD(lParam)),
                   static_cast<unsigned>(HIWORD(lParam)), g_locked.load() ? 1 : 0);
            if (wParam == HOTKEY_ID_TOGGLE) {
                ToggleLock(true, "hotkey-ctrl-o");
            } else if (wParam == HOTKEY_ID_TOGGLE2) {
                ToggleLock(true, "hotkey-ctrl-i");
            } else if (wParam == HOTKEY_ID_LOCK) {
                SetLock(true, true, "hotkey-ctrl-shift-o");
            }
            return 0;

        case WM_APP_SHOWTRAY:
            g_cfg.hideTray = false;
            SaveConfig();
            ShowTrayIcon();
            return 0;

        case WM_TIMER:
            if (wParam == IDT_MAIN) OnMainTimer();
            return 0;

        case WM_TRAYICON:
            switch (LOWORD(lParam)) {
                case WM_LBUTTONDBLCLK:
                case NIN_SELECT:
                    ToggleLock(true, "tray-double-click");
                    break;
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU:
                    ShowTrayMenu(hwnd);
                    break;
                default:
                    break;
            }
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_TOGGLE:
                    ToggleLock(true, "menu-toggle");
                    break;
                case IDM_LOCK:
                    SetLock(true, true, "menu-lock");
                    break;
                case IDM_DEBUG: {
                    g_cfg.debug = !g_cfg.debug;
                    SaveConfig();
                    DbgLog("=== debug %s ===", g_cfg.debug ? "enabled" : "disabled");
                    ShowBalloon(L"鼠标锁定工具",
                                g_cfg.debug ? L"调试模式已开启：日志写入 AppData\\MouseLock\\debug.log"
                                            : L"调试模式已关闭。",
                                NIIF_INFO);
                    break;
                }
                case IDM_STRICT:
                    g_cfg.strict = !g_cfg.strict;
                    SaveConfig();
                    ApplyLock();
                    break;
                case IDM_SETTINGS:
                    if (!g_locked.load()) OpenSettings();
                    break;
                case IDM_AUTOSTART: {
                    const bool enable = !IsAutoStartEnabled();
                    if (SetAutoStart(enable)) {
                        ShowBalloon(L"鼠标锁定工具",
                                    enable ? L"已开启开机自启（开机后只驻留托盘，不自动锁定）。"
                                           : L"已关闭开机自启。",
                                    NIIF_INFO);
                    } else {
                        MessageBoxW(nullptr, L"修改开机自启失败，请检查注册表权限。",
                                    L"鼠标锁定工具", MB_ICONWARNING | MB_TOPMOST);
                    }
                    break;
                }
                case IDM_HIDETRAY:
                    g_cfg.hideTray = true;
                    SaveConfig();
                    HideTrayIcon();
                    MessageBoxW(nullptr,
                                L"托盘图标已隐藏，热键仍然有效。\r\n\r\n"
                                L"恢复图标：mouse_lock.exe --tray\r\n"
                                L"正常退出：mouse_lock.exe --quit",
                                L"鼠标锁定工具", MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
                    break;
                case IDM_ABOUT:
                    ShowAbout();
                    break;
                case IDM_EXIT:
                    DestroyWindow(hwnd);
                    break;
                default:
                    break;
            }
            return 0;

        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_LOCK) {
                g_sessionLocked = true;
                if (g_locked.load()) SetLock(false, false, "session-lock");
                g_ignoreBefore = GetTickCount();
            } else if (wParam == WTS_SESSION_UNLOCK) {
                g_sessionLocked = false;
                g_ignoreBefore = GetTickCount();
            }
            return 0;

        case WM_POWERBROADCAST:
            if (wParam == PBT_APMSUSPEND) {
                g_sessionLocked = true;
                if (g_locked.load()) SetLock(false, false, "system-suspend");
            } else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
                g_sessionLocked = false;
                g_ignoreBefore = GetTickCount();
            }
            return TRUE;

        case WM_QUERYENDSESSION:
            if (g_locked.load()) SetLock(false, false, "session-end");
            return TRUE;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ---------------------------------------------------------------- 入口与清理

// 直接读系统命令行，避免不同 CRT（MinGW -municode / MSVC）对 lpCmdLine 的差异
static bool HasArg(const wchar_t *flag) {
    const wchar_t *cmd = GetCommandLineW();
    return cmd != nullptr && wcsstr(cmd, flag) != nullptr;
}

static bool HasStartupArg() { return HasArg(L"--startup"); }

// 找到已运行实例的隐藏窗口（message-only 窗口要用 FindWindowEx(HWND_MESSAGE, ...)）
static HWND FindRunningInstance() {
    return FindWindowExW(HWND_MESSAGE, nullptr, kMainClass, nullptr);
}

static void EnablePerMonitorDpiAwareness() {
    using FnSetDpiAwarenessContext = BOOL(WINAPI *)(void *);
    const FARPROC proc = GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                        "SetProcessDpiAwarenessContext");
    const auto fn = reinterpret_cast<FnSetDpiAwarenessContext>(reinterpret_cast<void *>(proc));
    if (fn) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == -4
        fn(reinterpret_cast<void *>(static_cast<INT_PTR>(-4)));
        return;
    }
    SetProcessDPIAware();
}

static void Cleanup() {
    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    if (g_kbdHook) {
        UnhookWindowsHookEx(g_kbdHook);
        g_kbdHook = nullptr;
    }
    ClipCursor(nullptr);
    KillTimer(g_hwnd, IDT_MAIN);
    if (g_hwnd) {
        UnregisterHotKey(g_hwnd, HOTKEY_ID_TOGGLE);
        UnregisterHotKey(g_hwnd, HOTKEY_ID_TOGGLE2);
        UnregisterHotKey(g_hwnd, HOTKEY_ID_LOCK);
        WTSUnRegisterSessionNotification(g_hwnd);
    }
    if (g_cover) DestroyWindow(g_cover);
    if (g_settings) DestroyWindow(g_settings);
    FreeShot();
    if (g_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayAdded = false;
    }
    if (g_iconLocked && g_iconLocked != g_iconUnlocked) DestroyIcon(g_iconLocked);
    if (g_iconUnlocked) DestroyIcon(g_iconUnlocked);
    g_iconLocked = g_iconUnlocked = nullptr;
    if (g_uiFont) {
        DeleteObject(g_uiFont);
        g_uiFont = nullptr;
    }
    if (g_gdiplusReady) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusReady = false;
    }
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    g_hInst = hInst;
    EnablePerMonitorDpiAwareness();

    // 托盘图标被隐藏后没有 UI 入口，用命令行把指令转交给已运行的实例
    HWND existing = FindRunningInstance();
    if (existing) {
        if (HasArg(L"--quit")) {
            PostMessageW(existing, WM_CLOSE, 0, 0);  // 优雅退出，会执行 Cleanup
        } else if (HasArg(L"--tray")) {
            PostMessageW(existing, WM_APP_SHOWTRAY, 0, 0);
        } else {
            MessageBoxW(nullptr,
                        L"鼠标锁定工具已在运行中。\r\n\r\n"
                        L"显示托盘图标：mouse_lock.exe --tray\r\n"
                        L"正常退出：mouse_lock.exe --quit",
                        L"鼠标锁定工具", MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
        }
        return 0;
    }

    // 单实例保护
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\MouseLockTool_SingleInstance_v1");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        MessageBoxW(nullptr, L"鼠标锁定工具已在运行中。", L"鼠标锁定工具",
                    MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
        return 0;
    }

    const bool startupMode = HasStartupArg();
    const bool forceTray = HasArg(L"--tray");

    LoadConfig();

    // GDI+（PNG 保存）
    Gdiplus::GdiplusStartupInput gpsi{};
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gpsi, nullptr) == Gdiplus::Ok)
        g_gdiplusReady = true;

    // 图标
    g_iconLocked = CreateLockIcon(true);
    g_iconUnlocked = CreateLockIcon(false);
    if (!g_iconLocked || !g_iconUnlocked) {
        if (g_iconLocked) DestroyIcon(g_iconLocked);
        if (g_iconUnlocked) DestroyIcon(g_iconUnlocked);
        g_iconLocked = static_cast<HICON>(
            LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE));
        g_iconUnlocked = g_iconLocked;
    }
    MakeSettingsFont();

    // 窗口类
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"注册主窗口类失败。", L"鼠标锁定工具", MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW cw{};
    cw.cbSize = sizeof(cw);
    cw.lpfnWndProc = CoverWndProc;
    cw.hInstance = hInst;
    cw.lpszClassName = kCoverClass;
    cw.hbrBackground = nullptr;
    RegisterClassExW(&cw);

    WNDCLASSEXW sw{};
    sw.cbSize = sizeof(sw);
    sw.lpfnWndProc = SettingsWndProc;
    sw.hInstance = hInst;
    sw.lpszClassName = kSettingsClass;
    sw.hCursor = LoadCursor(nullptr, IDC_ARROW);
    sw.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassExW(&sw);

    g_hwnd = CreateWindowExW(0, kMainClass, L"MouseLock", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                             hInst, nullptr);
    if (!g_hwnd) {
        MessageBoxW(nullptr, L"创建窗口失败。", L"鼠标锁定工具", MB_ICONERROR);
        return 1;
    }

    WTSRegisterSessionNotification(g_hwnd, NOTIFY_FOR_THIS_SESSION);

    // 全局热键：Ctrl + O 切换，Ctrl + Shift + O 直接锁定
    if (!RegisterHotKey(g_hwnd, HOTKEY_ID_TOGGLE, MOD_CONTROL | MOD_NOREPEAT, 'O')) {
        MessageBoxW(nullptr,
                    L"注册快捷键 Ctrl+O 失败，可能已被其它程序占用。\r\n程序仍会运行，可通过托盘图标操作。",
                    L"鼠标锁定工具", MB_ICONWARNING | MB_TOPMOST);
    }
    if (!RegisterHotKey(g_hwnd, HOTKEY_ID_LOCK, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'O')) {
        MessageBoxW(nullptr,
                    L"注册快捷键 Ctrl+Shift+O 失败，可能已被其它程序占用。\r\nCtrl+O 与托盘图标仍可正常使用。",
                    L"鼠标锁定工具", MB_ICONWARNING | MB_TOPMOST);
    }
    // Ctrl+O 常被程序占用为「打开」，这里再提供一个等价的 Ctrl+I
    if (!RegisterHotKey(g_hwnd, HOTKEY_ID_TOGGLE2, MOD_CONTROL | MOD_NOREPEAT, 'I')) {
        MessageBoxW(nullptr,
                    L"注册快捷键 Ctrl+I 失败，可能已被其它程序占用。\r\nCtrl+O / Ctrl+Shift+O 不受影响。",
                    L"鼠标锁定工具", MB_ICONWARNING | MB_TOPMOST);
    }

    g_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    g_kbdHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    if (!g_hook) {
        g_cfg.strict = true;
        MessageBoxW(nullptr, L"安装鼠标钩子失败，已启用强力模式（ClipCursor）作为兜底。",
                    L"鼠标锁定工具", MB_ICONWARNING | MB_TOPMOST);
    }

    ValidateAutoStartPath();

    // 托盘图标：隐藏模式下不添加；带上 --tray 启动时强制恢复
    if (g_cfg.hideTray && forceTray) {
        g_cfg.hideTray = false;
        SaveConfig();
    }
    if (!g_cfg.hideTray) ShowTrayIcon();

    // 初始状态：自启模式或关闭了「启动时锁定」时只驻留托盘，否则立即锁定
    g_lastMouseTick = GetTickCount();
    g_ignoreBefore = g_lastMouseTick;
    GetCursorPos(&g_lastMousePos);
    g_monitorCount = GetSystemMetrics(SM_CMONITORS) > 0 ? GetSystemMetrics(SM_CMONITORS) : 1;
    if (startupMode || !g_cfg.lockOnStart) {
        g_locked = false;
        ApplyLock();
        UpdateTray(false);
    } else {
        SetLock(true, true, "startup");
    }

    DbgLog("=== start === cmd=[%ls] startupMode=%d lockOnStart=%d idle=%d/%dmin mode=%d strict=%d shot=%d",
           GetCommandLineW(), startupMode ? 1 : 0, g_cfg.lockOnStart ? 1 : 0,
           g_cfg.idleEnabled ? 1 : 0, g_cfg.idleMinutes, g_cfg.idleMode, g_cfg.strict ? 1 : 0,
           g_cfg.shotEnabled ? 1 : 0);
    DbgLog("       lockMode=%d (%s) monitors=%d", g_cfg.lockMode,
           g_cfg.lockMode == kFreezeScreen ? "freeze-screen" : "lock-mouse", g_monitorCount);
    g_dbgLastActivity = LastActivityTick();
    g_dbgLastHeartbeat = GetTickCount();

    SetTimer(g_hwnd, IDT_MAIN, 500, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_settings && IsDialogMessageW(g_settings, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Cleanup();
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return static_cast<int>(msg.wParam);
}
