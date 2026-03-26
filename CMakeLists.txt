cmake_minimum_required(VERSION 3.31)
project(CG_DX12)

set(CMAKE_CXX_STANDARD 20)

include_directories(.)
include_directories(h)

add_definitions(-DUNICODE -D_UNICODE)

add_executable(CG_DX12
        h/Window.h
        h/Window.h
        src/Window.cpp
        src/main.cpp)
