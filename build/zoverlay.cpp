// zoverlay.cpp —— 一次性测试工装（不属于 MouseLock 本体）
//
// 造一个"外来全屏置顶遮罩"窗口（独立进程，模拟 ResProbe 隐私屏的形态：
// WS_EX_TOPMOST + 全屏 + 另一个进程 PID），并可定时注入 Ctrl+Shift+O 让
// MouseLock 进入锁定，最后注入连点把它解开 —— 全程无需人工操作。
//
// 用法： zoverlay.exe [总秒数=20] [第几秒锁定=3] [末尾注入点击次数=12]
//
// 用途：给"检测到外来全屏遮罩就让位"这条逻辑做可重复的 A/B 验证，
//       不必依赖 ResProbe 在场，也不动 MouseLock 的一行代码。

#include <windows.h>
#include <cstdio>
#include <cstdlib>

static const wchar_t kClass[] = L"ZTestOverlayClass";
static HBRUSH g_brush = nullptr;

static LRESULT CALLBACK WP(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(h, &ps);
            RECT rc{};
            GetClientRect(h, &rc);
            FillRect(dc, &rc, g_brush);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_TIMER:
            // 与 ResProbe 隐私屏一致：100ms 一次把自己抬回置顶带最上层。
            // 少了这一句，本工装就只是"一个静态置顶窗口"，构不成互抢。
            SetWindowPos(h, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            return 0;
        default:
            return DefWindowProcW(h, m, w, l);
    }
}

static void SendKeyCombo(WORD vk, bool ctrl, bool shift) {
    INPUT in[6]{};
    int n = 0;
    auto key = [&](WORD v, DWORD flags) {
        in[n].type = INPUT_KEYBOARD;
        in[n].ki.wVk = v;
        in[n].ki.dwFlags = flags;
        ++n;
    };
    if (ctrl) key(VK_CONTROL, 0);
    if (shift) key(VK_SHIFT, 0);
    key(vk, 0);
    key(vk, KEYEVENTF_KEYUP);
    if (shift) key(VK_SHIFT, KEYEVENTF_KEYUP);
    if (ctrl) key(VK_CONTROL, KEYEVENTF_KEYUP);
    SendInput(static_cast<UINT>(n), in, sizeof(INPUT));
}

static void SendClick() {
    INPUT in[2]{};
    in[0].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
}

int wmain(int argc, wchar_t **argv) {
    const int totalSec = argc >= 2 ? _wtoi(argv[1]) : 20;
    const int lockAtSec = argc >= 3 ? _wtoi(argv[2]) : 3;
    const int clicks = argc >= 4 ? _wtoi(argv[3]) : 12;
    // 遮罩在第几秒出现（默认 0=一开始就在）。用来测"先锁定、后出现外来遮罩"的反向场景。
    const int overlayAtSec = argc >= 5 ? _wtoi(argv[4]) : 0;
    // 遮罩高度（默认 0=铺满虚拟屏）。给 1040 可以复现"在任务栏之上最大化"的类 360 窗口，
    // 用来验证覆盖率门槛（要求 >=99% 且盖住任务栏那条边）。
    const int heightArg = argc >= 6 ? _wtoi(argv[5]) : 0;

    SetConsoleOutputCP(CP_UTF8);

    // totalSec <= 0：不造遮罩，只做按键注入。用于验证"任务管理器让位"这类
    // 不需要外来遮罩的场景（zoverlay 0 1 12 = 1 秒后锁定，8 秒后用连点解锁）。
    if (totalSec <= 0) {
        if (lockAtSec >= 0) {
            Sleep(static_cast<DWORD>(lockAtSec) * 1000);
            SendKeyCombo('O', true, true);
            printf("[zoverlay] keyonly: 已注入 Ctrl+Shift+O\n");
        }
        Sleep(8000);
        printf("[zoverlay] keyonly: 注入 %d 次点击解锁\n", clicks);
        for (int i = 0; i < clicks; ++i) {
            SendClick();
            Sleep(120);
        }
        printf("[zoverlay] keyonly 结束\n");
        return 0;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WP;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    g_brush = CreateSolidBrush(RGB(0, 0, 0));

    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    const int useH = (heightArg > 0 && heightArg <= vh) ? heightArg : vh;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass,
                                L"ZTestOverlay", WS_POPUP, vx, vy, vw, useH, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (!hwnd) {
        printf("[zoverlay] CreateWindow 失败 %lu\n", GetLastError());
        return 1;
    }
    bool overlayShown = false;
    if (overlayAtSec <= 0) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        SetTimer(hwnd, 1, 100, nullptr);  // 100ms 周期重新置顶，模拟 ResProbe
        overlayShown = true;
        printf("[zoverlay] 遮罩 %dx%d @(%d,%d) 已显示，pid=%lu（100ms 周期重抢）\n", vw, vh, vx, vy,
               GetCurrentProcessId());
    } else {
        printf("[zoverlay] 遮罩将在 +%d s 显示（先锁定、后出现）\n", overlayAtSec);
    }

    const DWORD t0 = GetTickCount();
    bool locked = false;
    MSG msg{};
    for (;;) {
        const DWORD elapsed = GetTickCount() - t0;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!overlayShown && elapsed >= static_cast<DWORD>(overlayAtSec) * 1000) {
            overlayShown = true;
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            SetTimer(hwnd, 1, 100, nullptr);
            printf("[zoverlay] +%lu ms 遮罩已显示\n", static_cast<unsigned long>(elapsed));
        }
        if (!locked && elapsed >= static_cast<DWORD>(lockAtSec) * 1000) {
            locked = true;
            SendKeyCombo('O', true, true);  // Ctrl+Shift+O：MouseLock 直接锁定
            printf("[zoverlay] +%lu ms 已注入 Ctrl+Shift+O\n",
                   static_cast<unsigned long>(elapsed));
        }
        if (elapsed >= static_cast<DWORD>(totalSec) * 1000) break;
        Sleep(10);
    }

    DestroyWindow(hwnd);
    printf("[zoverlay] +%lu ms 遮罩已销毁，等待 3 秒让 MouseLock 恢复截图层\n",
           static_cast<unsigned long>(GetTickCount() - t0));
    Sleep(3000);

    printf("[zoverlay] 注入 %d 次点击，尝试用连点解锁\n", clicks);
    for (int i = 0; i < clicks; ++i) {
        SendClick();
        Sleep(120);
    }
    Sleep(300);
    printf("[zoverlay] 结束\n");
    DeleteObject(g_brush);
    return 0;
}
