cmake_minimum_required(VERSION 3.20)
project(CG_DX12)

set(CMAKE_CXX_STANDARD 20)

if(MSVC)
    add_compile_options(/W4 /EHsc /MTd)
    # Определяем _CRT_SECURE_NO_WARNINGS для старых функций
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()


include_directories(.)
include_directories(h)

add_definitions(-DUNICODE -D_UNICODE)

file(GLOB SOURCES "src/*.cpp")

add_executable(${PROJECT_NAME} WIN32 ${SOURCES}
        h/Vertex.h
)

if(MSVC)
    target_link_libraries(${PROJECT_NAME}
            d3d12.lib
            dxgi.lib
            d3dcompiler.lib
            dxguid.lib
            # Windows базовые библиотеки
            user32.lib
            gdi32.lib
    )
endif()

