// zidle.cpp —— 一次性测试工装：按固定间隔注入"合成鼠标移动"，用来验证空闲计时口径
//
// 用途：区分 IdleMode=1（系统 GetLastInputInfo，注入输入也算活动）
//       与 IdleMode=2（只认 LL 钩子收到的真实硬件输入，注入输入不算）。
//
// 用法： zidle.exe [秒数=20] [间隔毫秒=2000]

#include <windows.h>
#include <cstdio>
#include <cstdlib>

int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    const int totalSec = argc >= 2 ? _wtoi(argv[1]) : 20;
    const int intervalMs = argc >= 3 ? _wtoi(argv[2]) : 2000;

    printf("[zidle] 开始：每 %d ms 注入一次 +1/-1 像素的相对移动，共 %d 秒\n", intervalMs,
           totalSec);
    const DWORD t0 = GetTickCount();
    int n = 0;
    int sign = 1;
    while (GetTickCount() - t0 < static_cast<DWORD>(totalSec) * 1000) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = sign;
        in.mi.dy = -sign;
        in.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, &in, sizeof(INPUT));
        sign = -sign;
        ++n;
        printf("[zidle] +%lu ms 第 %d 次注入\n",
               static_cast<unsigned long>(GetTickCount() - t0), n);
        Sleep(static_cast<DWORD>(intervalMs));
    }
    printf("[zidle] 结束，共注入 %d 次\n", n);
    return 0;
}
