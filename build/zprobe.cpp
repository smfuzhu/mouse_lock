// zprobe.cpp —— 只读 Z 序探针（一次性诊断工具，不属于 MouseLock 程序本体）
//
// 目的：判定"屏幕中心这一点上，最上层的可见窗口是谁"，以固定频率采样，
//       从而看清两个全屏置顶窗口（MouseLock 的 MouseLockCoverClass 与
//       ResProbe 的 ResProbePrivacyOverlay）谁压着谁、多长时间翻一次。
//
// 为什么用 EnumWindows 而不是 WindowFromPoint：
//   ResProbe 的遮罩带 WS_EX_TRANSPARENT（点击穿透），WindowFromPoint 会直接
//   跳过它、返回它下面的窗口 —— 那样就永远看不到遮罩，探针等于瞎了。
//   EnumWindows 给出的顺序是真实 Z 序（最上层在前），再按"矩形是否包含采样点"
//   过滤，得到的第一个就是视觉上盖住该点的窗口，与点击穿透无关。
//
// 侵入性：零。只做 IsWindowVisible / GetWindowRect / GetClassName /
//         GetWindowThreadProcessId / QueryFullProcessImageName 这类查询，
//         不注入、不挂钩子、不调用任何 Set* 接口、不改变任何窗口状态。
//
// 用法：
//   zprobe.exe [秒数] [输出csv] [x y]
//   例：zprobe.exe 120 build\zprobe-run.csv
//       zprobe.exe 30 out.csv 960 540
//
// 输出：
//   - 控制台：采样点信息 + 每次"最上层窗口变化"时打印一行（含上一状态驻留时长）
//   - CSV：每次采样一行（t_ms, top_hwnd, top_class, top_exe, top_pid, topmost,
//          transparent, rect, matches, second_class, second_exe）
//   - 结束摘要：采样数、翻转次数、各窗口累计时长、翻转间隔中位数与分布
//
// 编译：
//   g++ -std=c++17 -O2 -Wall -o build\zprobe.exe build\zprobe.cpp -luser32 -ldwmapi -static

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static const int kMaxMatches = 4;

struct WinInfo {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring cls;
    std::wstring exe;
    bool topmost = false;
    bool transparent = false;
    RECT rc{};
};

static std::map<DWORD, std::wstring> g_exeCache;

static std::string U8(const std::wstring &w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr,
                        nullptr);
    return s;
}

static std::wstring ExeNameForPid(DWORD pid) {
    auto it = g_exeCache.find(pid);
    if (it != g_exeCache.end()) return it->second;

    std::wstring name = L"?";
    if (pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) {
            wchar_t buf[MAX_PATH * 2]{};
            DWORD n = MAX_PATH * 2;
            if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
                std::wstring full(buf, n);
                const size_t s = full.find_last_of(L"\\/");
                name = (s == std::wstring::npos) ? full : full.substr(s + 1);
            }
            CloseHandle(h);
        }
    }
    g_exeCache[pid] = name;
    return name;
}

// DWMWA_CLOAKED = 14：UWP/被 DWM 隐藏的窗口虽然 IsWindowVisible 为真，但看不见。
static bool IsCloaked(HWND hwnd) {
    typedef HRESULT(WINAPI * PFN_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
    static PFN_DwmGetWindowAttribute fn = []() -> PFN_DwmGetWindowAttribute {
        HMODULE m = LoadLibraryW(L"dwmapi.dll");
        return m ? reinterpret_cast<PFN_DwmGetWindowAttribute>(
                       GetProcAddress(m, "DwmGetWindowAttribute"))
                 : nullptr;
    }();
    if (!fn) return false;
    DWORD cloaked = 0;
    if (fn(hwnd, 14, &cloaked, sizeof(cloaked)) != S_OK) return false;
    return cloaked != 0;
}

struct SampleCtx {
    POINT pt{};
    int n = 0;
    WinInfo items[kMaxMatches];
};

// EnumWindows 的回调顺序即 Z 序（最上层在前），拿到第一个匹配就够，多留几层便于观察。
static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    auto *c = reinterpret_cast<SampleCtx *>(lp);

    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return TRUE;
    if (rc.right <= rc.left || rc.bottom <= rc.top) return TRUE;
    if (!PtInRect(&rc, c->pt)) return TRUE;
    if (IsCloaked(hwnd)) return TRUE;

    WinInfo wi;
    wi.hwnd = hwnd;
    wi.rc = rc;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    wi.topmost = (ex & WS_EX_TOPMOST) != 0;
    wi.transparent = (ex & WS_EX_TRANSPARENT) != 0;

    GetWindowThreadProcessId(hwnd, &wi.pid);
    wchar_t cls[256]{};
    GetClassNameW(hwnd, cls, 256);
    wi.cls = cls;
    wi.exe = ExeNameForPid(wi.pid);

    c->items[c->n++] = wi;
    return c->n < kMaxMatches ? TRUE : FALSE;
}

static void Sample(const POINT &pt, SampleCtx &c) {
    c.pt = pt;
    c.n = 0;
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&c));
}

struct Span {
    std::wstring cls;
    std::wstring exe;
    HWND hwnd = nullptr;
    long long ms = 0;
    int count = 0;
};

static volatile LONG g_stop = 0;

static BOOL WINAPI CtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        InterlockedExchange(&g_stop, 1);
        return TRUE;
    }
    return FALSE;
}

int wmain(int argc, wchar_t **argv) {
    int seconds = 60;
    std::wstring csvPath = L"zprobe.csv";
    // 默认采样点故意偏离画面中心：ResProbe 开关隐私屏会弹居中的确认框，
    // 正中点会被那个对话框挡住，测出来的是对话框而不是遮罩层。
    POINT pt{GetSystemMetrics(SM_CXSCREEN) / 4, GetSystemMetrics(SM_CYSCREEN) / 4};
    bool pointGiven = false;

    if (argc >= 2) seconds = _wtoi(argv[1]);
    if (argc >= 3 && argv[2][0]) csvPath = argv[2];
    if (argc >= 5) {
        pt.x = _wtoi(argv[3]);
        pt.y = _wtoi(argv[4]);
        pointGiven = true;
    }
    if (seconds <= 0) seconds = 60;

    // 与 MouseLock 一致的 DPI 感知，保证采样点坐标是物理像素。
    // 走 GetProcAddress：老版本 MinGW 头文件里可能没有这个符号。
    {
        typedef BOOL(WINAPI * PFN_SetCtx)(void *);
        const FARPROC p = GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                         "SetProcessDpiAwarenessContext");
        if (p) {
            reinterpret_cast<PFN_SetCtx>(reinterpret_cast<void *>(p))(
                reinterpret_cast<void *>(static_cast<INT_PTR>(-4)));  // PER_MONITOR_AWARE_V2
        } else {
            SetProcessDPIAware();
        }
    }

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    timeBeginPeriod(1);

    FILE *csv = nullptr;
    if (_wfopen_s(&csv, csvPath.c_str(), L"wb") == 0 && csv) {
        const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
        fwrite(bom, 1, 3, csv);
        fputs("t_ms,top_hwnd,top_class,top_exe,top_pid,topmost,transparent,rect,matches,"
              "second_class,second_exe\n",
              csv);
    } else {
        csv = nullptr;
        printf("[!] 无法写入 %s，本次只输出到控制台\n", U8(csvPath).c_str());
    }

    printf("=== zprobe ===\n");
    printf("采样点     : (%ld, %ld)%s\n", pt.x, pt.y,
           pointGiven ? " (命令行指定)" : " 主屏 1/4 处（避开居中弹窗）");
    printf("时长       : %d s，目标 60 Hz\n", seconds);
    printf("CSV        : %s\n", csv ? U8(csvPath).c_str() : "(未启用)");
    printf("判据       : MouseLockCoverClass / ResProbePrivacyOverlay 交替出现即为争夺\n");
    printf("---------------------------------------------------------------\n");

    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t0{};
    QueryPerformanceCounter(&t0);

    const long long periodUs = 16667;  // 60 Hz
    long long nextUs = 0;
    long long samples = 0;

    SampleCtx cur{};

    bool havePrev = false;
    HWND prevHwnd = nullptr;
    std::wstring prevCls;
    long long prevChangeMs = 0;
    long long changes = 0;

    std::vector<Span> spans;
    std::vector<long long> dwells;  // 每次翻转前一个状态的驻留时长

    for (;;) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const long long elapsedUs = (now.QuadPart - t0.QuadPart) * 1000000LL / freq.QuadPart;
        if (InterlockedCompareExchange(&g_stop, 0, 0) != 0) break;
        if (elapsedUs >= static_cast<long long>(seconds) * 1000000LL) break;

        Sample(pt, cur);
        ++samples;
        const long long ms = elapsedUs / 1000;

        const HWND topHwnd = cur.n > 0 ? cur.items[0].hwnd : nullptr;
        const std::wstring topCls = cur.n > 0 ? cur.items[0].cls : std::wstring();

        if (!havePrev || topHwnd != prevHwnd) {
            const long long dwell = havePrev ? (ms - prevChangeMs) : 0;

            if (havePrev) {
                ++changes;
                dwells.push_back(dwell);
                if (!spans.empty()) spans.back().ms += dwell;
            }

            Span s;
            s.cls = topCls;
            s.exe = cur.n > 0 ? cur.items[0].exe : std::wstring();
            s.hwnd = topHwnd;
            s.count = 1;
            spans.push_back(s);

            printf("[+%7lld ms] %-26s -> %-26s | 上一状态驻留 %6lld ms\n", ms,
                   havePrev ? U8(prevCls).c_str() : "", U8(topCls).c_str(), dwell);

            prevHwnd = topHwnd;
            prevCls = topCls;
            prevChangeMs = ms;
            havePrev = true;
        } else {
            if (!spans.empty()) spans.back().count++;
        }

        if (csv) {
            char rect[64] = "-";
            if (cur.n > 0) {
                const RECT &r = cur.items[0].rc;
                snprintf(rect, sizeof(rect), "%ld,%ld,%ld,%ld", r.left, r.top, r.right, r.bottom);
            }
            const std::string cls0 = cur.n > 0 ? U8(cur.items[0].cls) : std::string();
            const std::string exe0 = cur.n > 0 ? U8(cur.items[0].exe) : std::string();
            const std::string cls1 = cur.n > 1 ? U8(cur.items[1].cls) : std::string();
            const std::string exe1 = cur.n > 1 ? U8(cur.items[1].exe) : std::string();
            fprintf(csv, "%lld,0x%llX,%s,%s,%lu,%d,%d,%s,%d,%s,%s\n", ms,
                    static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(topHwnd)),
                    cls0.c_str(), exe0.c_str(),
                    cur.n > 0 ? static_cast<unsigned long>(cur.items[0].pid) : 0UL,
                    cur.n > 0 && cur.items[0].topmost ? 1 : 0,
                    cur.n > 0 && cur.items[0].transparent ? 1 : 0, rect, cur.n, cls1.c_str(),
                    exe1.c_str());
        }

        nextUs += periodUs;
        const long long remainUs = nextUs - elapsedUs;
        if (remainUs > 1000) {
            Sleep(static_cast<DWORD>((remainUs - 500) / 1000));
        } else if (remainUs < -periodUs) {
            nextUs = elapsedUs + periodUs;  // 落后太多就重新对齐，不补采样
        }
    }

    // 收尾：把最后一段的时长补上
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const long long ms = (now.QuadPart - t0.QuadPart) * 1000000LL / freq.QuadPart / 1000;
        if (!spans.empty()) spans.back().ms += (ms - prevChangeMs);
    }

    if (csv) fclose(csv);
    timeEndPeriod(1);

    printf("---------------------------------------------------------------\n");
    printf("采样 %lld 次，翻转 %lld 次\n", samples, changes);
    if (!spans.empty()) {
        std::vector<Span> sorted = spans;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Span &a, const Span &b) { return a.ms > b.ms; });
        printf("各状态累计（按时长降序）：\n");
        const int show = static_cast<int>(sorted.size()) < 8 ? static_cast<int>(sorted.size()) : 8;
        for (int i = 0; i < show; ++i) {
            printf("  %-26s %-18s 累计 %7lld ms  %4d 次采样\n", U8(sorted[i].cls).c_str(),
                   U8(sorted[i].exe).c_str(), sorted[i].ms, sorted[i].count);
        }
    }
    if (!dwells.empty()) {
        std::vector<long long> d = dwells;
        std::sort(d.begin(), d.end());
        printf("翻转间隔：中位数 %lld ms，最小 %lld ms，最大 %lld ms\n", d[d.size() / 2], d.front(),
               d.back());
        int band100 = 0, band500 = 0, other = 0;
        for (long long v : d) {
            if (v >= 50 && v <= 150) ++band100;
            else if (v >= 400 && v <= 600) ++band500;
            else ++other;
        }
        printf("  50~150ms: %d 次 | 400~600ms: %d 次 | 其它: %d 次\n", band100, band500, other);
        printf("  读法：100ms 一档对应 ResProbe 的置顶定时器，500ms 一档对应 MouseLock 的巡检；\n"
               "        两档交替出现 = 双置顶互抢；只有一档 = 单方在抢。\n");
    } else {
        printf("全程没有翻转（最上层窗口始终未变）。\n");
    }
    return 0;
}
