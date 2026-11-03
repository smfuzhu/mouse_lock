@echo off
setlocal
rem ============================================================
rem  MinGW-w64 编译脚本（需要 g++ 在系统 PATH 中）
rem  用法：双击运行，或在任意目录用完整路径调用都行
rem        （脚本会自己切到所在目录，不依赖当前工作目录）
rem ============================================================

rem 必须自己切到脚本所在目录：下面用的是相对路径 mouse_lock.cpp，
rem 从别处调用时的报错是：
rem   cc1plus.exe: fatal error: mouse_lock.cpp: No such file or directory
rem %~dp0 自带结尾反斜杠，cd /d 能安全处理带引号的结尾反斜杠。
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [x] 未找到 g++，请先安装 MinGW-w64 / MSYS2 并把 bin 目录加入 PATH
    exit /b 1
)

echo [i] 正在编译 mouse_lock.cpp ...
g++ -std=c++17 -O2 -Wall -municode -mwindows -o _chk_build.exe mouse_lock.cpp -luser32 -lgdi32 -lshell32 -lgdiplus -lwtsapi32 -static
if errorlevel 1 (
    echo [x] 编译失败
    exit /b 1
)

echo [+] 编译成功: mouse_lock.exe
echo     运行后鼠标移动即被锁定，按 Ctrl+O 解除；托盘图标右键可退出。
