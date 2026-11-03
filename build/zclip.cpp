// zclip.cpp —— 一次性诊断：读出当前 ClipCursor 状态，判断鼠标是否真的被锁定
#include <windows.h>
#include <cstdio>

int wmain() {
    SetConsoleOutputCP(CP_UTF8);
    RECT rc{};
    if (!GetClipCursor(&rc)) {
        printf("GetClipCursor 失败，错误码 %lu\n", GetLastError());
        return 1;
    }
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    POINT pt{};
    GetCursorPos(&pt);

    printf("裁剪矩形 : (%ld,%ld)-(%ld,%ld)  宽高 %ldx%ld\n", rc.left, rc.top, rc.right, rc.bottom,
           rc.right - rc.left, rc.bottom - rc.top);
    printf("虚拟桌面 : (%d,%d)-(%d,%d)\n", vx, vy, vx + vw, vy + vh);
    printf("光标位置 : (%ld,%ld)\n", pt.x, pt.y);

    const bool coversDesktop = rc.left <= vx && rc.top <= vy && rc.right >= vx + vw &&
                               rc.bottom >= vy + vh;
    if (coversDesktop) {
        printf("判定     : 未裁剪（可自由移动）\n");
    } else {
        printf("判定     : 正在被裁剪（鼠标被钉住）\n");
    }
    return 0;
}
