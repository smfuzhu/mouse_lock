@echo off
setlocal
rem ============================================================
rem  MSVC 编译脚本（Visual Studio / Build Tools）
rem  用法：双击运行，或在任意目录用完整路径调用都行
rem        （脚本会自己切到所在目录，不依赖当前工作目录）
rem ============================================================

rem 必须自己切到脚本所在目录：下面用的是相对路径 mouse_lock.cpp，
rem 从别处调用会直接 cc1plus/cl 找不到源文件。
rem %~dp0 自带结尾反斜杠，cd /d 能安全处理带引号的结尾反斜杠。
cd /d "%~dp0"

set "SRC=mouse_lock.cpp"
set "OUT=_chk_build.exe"

where cl >nul 2>nul
if not errorlevel 1 goto :do_build

rem ---- 自动定位 Visual Studio 并初始化编译环境 ----------------------------
rem 这里刻意全部用 goto 而不是 (...) 块，原因是一次真实的踩坑：
rem   %VSHOME% 形如 "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
rem   一旦把它放进块里（哪怕只是 echo %VSHOME%），cmd 会先把整块解析完，
rem   再逐行展开变量 —— 展开结果里的 ")" 会被当成块的结束符，
rem   剩下半截被当命令执行，报：\Microsoft was unexpected at this time.
rem   顶层语句没有这个问题（展开值不会被重新当作块结构解析），所以用 goto。
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :try_build

for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSHOME=%%i"
if not defined VSHOME goto :try_build
if not exist "%VSHOME%\VC\Auxiliary\Build\vcvarsall.bat" goto :try_build

echo [i] 初始化 MSVC 环境: %VSHOME%
call "%VSHOME%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 goto :try_build

:try_build
where cl >nul 2>nul
if errorlevel 1 (
    echo [x] 未找到 MSVC 编译器 cl.exe
    echo     请安装 "Visual Studio 生成工具"，或改用 MinGW-w64: build_mingw.bat
    exit /b 1
)

:do_build
echo [i] 正在编译 %SRC% ...
rem advapi32.lib 不能漏：开机自启用到 RegOpenKeyExW / RegQueryValueExW / RegSetValueExW /
rem RegDeleteValueW / RegCloseKey，MSVC 下缺它就全是 LNK2019（MinGW 默认链 advapi32，
rem 所以只有 MSVC 这条路会暴露）。
rem _CRT_SECURE_NO_WARNINGS：源码用了 _wfopen（C4996 只是"建议用 _wfopen_s"的告警）。
cl /nologo /std:c++17 /O2 /W3 /EHsc /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fe:%OUT% %SRC% user32.lib gdi32.lib shell32.lib gdiplus.lib wtsapi32.lib advapi32.lib
if errorlevel 1 (
    echo [x] 编译失败
    exit /b 1
)

if exist _chk_build.obj del /q _chk_build.obj
echo [+] 编译成功: %OUT%
echo     运行后鼠标移动即被锁定，按 Ctrl+O 解除；托盘图标右键可退出。
