cmake_minimum_required(VERSION 3.31)
project(CG_DX12)

set(CMAKE_CXX_STANDARD 20)

add_definitions(-DUNICODE -D_UNICODE)

add_executable(CG_DX12
        src/main.cpp
        src/d3dUtil.cpp
        h/d3dUtil.h
        src/DirectXApp.cpp
        h/DirectXApp.h
        src/InputDevice.cpp
        h/InputDevice.h
        h/Material.h
        h/MathHelper.h
        h/ObjectConstants.h
        src/Parser.cpp
        h/Parser.h
        h/Submesh.h
        src/TgaLoader.cpp
        h/TgaLoader.h
        h/ThrowIfFailed.h
        src/Timer.cpp
        h/Timer.h
        h/UploadBuffer.h
        h/Vertex.h
        src/Window.cpp
        h/Window.h
)
