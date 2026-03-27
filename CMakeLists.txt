cmake_minimum_required(VERSION 3.31)
project(CG_DX12)

set(CMAKE_CXX_STANDARD 20)

include_directories(.)
include_directories(h)

add_definitions(-DUNICODE -D_UNICODE)

file(GLOB SOURCES "src/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
        d3d12
        dxgi
        d3dcompiler
        dxguid
        uuid
        gdi32
        user32
)

