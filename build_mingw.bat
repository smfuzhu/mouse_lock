@echo off
setlocal
rem ============================================================
rem  MinGW-w64 编译脚本（需要 g++ 在系统 PATH 中）
rem  用法：在本目录执行 build_mingw.bat
rem ============================================================

where g++ >nul 2>nul
if errorlevel 1 (
    echo [x] 未找到 g++，请先安装 MinGW-w64 / MSYS2 并把 bin 目录加入 PATH
    exit /b 1
)

echo [i] 正在编译 mouse_lock.cpp ...
g++ -std=c++17 -O2 -Wall -municode -mwindows -o mouse_lock.exe mouse_lock.cpp -luser32 -lgdi32 -lshell32 -lgdiplus -lwtsapi32 -static
if errorlevel 1 (
    echo [x] 编译失败
    exit /b 1
)

echo [+] 编译成功: mouse_lock.exe
echo     运行后鼠标移动即被锁定，按 Ctrl+O 解除；托盘图标右键可退出。
