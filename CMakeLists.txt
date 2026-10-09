cmake_minimum_required(VERSION 3.15)
project(MouseLock CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mouse_lock WIN32 mouse_lock.cpp)

target_link_libraries(mouse_lock PRIVATE user32 gdi32 shell32 gdiplus wtsapi32)

if(MSVC)
    # 源码为 UTF-8（含中文界面文案）
    target_compile_options(mouse_lock PRIVATE /utf-8 /W3 /O2)
else()
    target_compile_options(mouse_lock PRIVATE -municode -O2)
endif()
