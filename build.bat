@echo off
setlocal
rem ============================================================
rem  MSVC 编译脚本（Visual Studio / Build Tools）
rem  用法：双击运行 ，或在本目录打开命令行执行 build.bat
rem ============================================================

set "SRC=mouse_lock.cpp"
set "OUT=mouse_lock.exe"

where cl >nul 2>nul
if not errorlevel 1 goto :do_build

rem 尝试自动定位 Visual Studio 并初始化编译环境
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSHOME=%%i"
)
if defined VSHOME if exist "%VSHOME%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [i] 初始化 MSVC 环境: %VSHOME%
    call "%VSHOME%\VC\Auxiliary\Build\vcvarsall.bat" x64
)

where cl >nul 2>nul
if errorlevel 1 (
    echo [x] 未找到 MSVC 编译器 cl.exe
    echo     请安装 "Visual Studio 生成工具"，或改用 MinGW-w64: build_mingw.bat
    exit /b 1
)

:do_build
echo [i] 正在编译 %SRC% ...
cl /nologo /std:c++17 /O2 /W3 /EHsc /utf-8 /DUNICODE /D_UNICODE /Fe:%OUT% %SRC% user32.lib gdi32.lib shell32.lib gdiplus.lib wtsapi32.lib
if errorlevel 1 (
    echo [x] 编译失败
    exit /b 1
)

if exist mouse_lock.obj del /q mouse_lock.obj
echo [+] 编译成功: %OUT%
echo     运行后鼠标移动即被锁定，按 Ctrl+O 解除；托盘图标右键可退出。
