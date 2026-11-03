// zwin.cpp —— 一次性诊断：列出所有顶层窗口的类名/进程/矩形/扩展样式/分层透明度
// 用途：判定"外来全屏置顶窗口"到底是谁、是不是真的看得见（不透明）。
#include <windows.h>
#include <cstdio>
#include <cstring>

static void Utf8(const wchar_t *w, char *out, int cap) {
    if (cap <= 0) return;
    out[0] = 0;
    if (!w || !*w) return;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, nullptr, nullptr);
}

struct Ctx {
    const wchar_t *filter;  // 进程名过滤（大小写不敏感）；nullptr = 全部
    int shown;
};

static BOOL CALLBACK Proc(HWND hwnd, LPARAM lp) {
    auto *ctx = reinterpret_cast<Ctx *>(lp);
    if (!IsWindowVisible(hwnd)) return TRUE;

    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    const LONG w = r.right - r.left, hh = r.bottom - r.top;
    if (w <= 0 || hh <= 0) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    wchar_t exe[MAX_PATH * 2] = L"?";
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hp) {
        DWORD n = MAX_PATH * 2;
        QueryFullProcessImageNameW(hp, 0, exe, &n);
        CloseHandle(hp);
    }
    const wchar_t *base = wcsrchr(exe, L'\\');
    base = base ? base + 1 : exe;

    if (ctx->filter && _wcsicmp(base, ctx->filter) != 0) return TRUE;

    wchar_t cls[256] = L"", title[512] = L"";
    GetClassNameW(hwnd, cls, 256);
    GetWindowTextW(hwnd, title, 512);

    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    BYTE alpha = 255;
    DWORD flags = 0;
    COLORREF key = 0;
    bool hasAlpha = false;
    if (ex & WS_EX_LAYERED) {
        if (GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags)) hasAlpha = true;
    }

    char clsU[256], titleU[512], exeU[300];
    Utf8(cls, clsU, sizeof(clsU));
    Utf8(title, titleU, sizeof(titleU));
    Utf8(base, exeU, sizeof(exeU));

    printf("0x%08llX %-22s %-24s pid=%-6lu rect=(%ld,%ld)-(%ld,%ld) %ldx%ld\n",
           static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(hwnd)), clsU, exeU,
           static_cast<unsigned long>(pid), r.left, r.top, r.right, r.bottom, w, hh);
    printf("          title=\"%s\"\n", titleU);
    printf("          topmost=%d layered=%d transparent=%d noactivate=%d toolwindow=%d "
           "alpha=%u%s(%s)\n",
           (ex & WS_EX_TOPMOST) ? 1 : 0, (ex & WS_EX_LAYERED) ? 1 : 0,
           (ex & WS_EX_TRANSPARENT) ? 1 : 0, (ex & WS_EX_NOACTIVATE) ? 1 : 0,
           (ex & WS_EX_TOOLWINDOW) ? 1 : 0, static_cast<unsigned>(alpha),
           hasAlpha ? "" : "(unknown)",
           hasAlpha ? (flags & LWA_ALPHA ? "LWA_ALPHA" : "LWA_COLORKEY") : "");
    ++ctx->shown;
    return TRUE;
}

int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    Ctx ctx{};
    ctx.filter = argc >= 2 ? argv[1] : nullptr;
    if (ctx.filter) {
        char f[300];
        Utf8(ctx.filter, f, sizeof(f));
        printf("=== 顶层可见窗口（进程过滤: %s）===\n", f);
    } else {
        printf("=== 顶层可见窗口（全部）===\n");
    }
    EnumWindows(Proc, reinterpret_cast<LPARAM>(&ctx));
    printf("共 %d 个\n", ctx.shown);
    return 0;
}
