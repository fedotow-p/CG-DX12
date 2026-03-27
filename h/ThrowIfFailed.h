#pragma once
#include <stdexcept>
#include <string>
#include <sstream>

template<typename T>
inline void ThrowIfFailed(T hr, const char* message = "DirectX operation failed")
{
    if (FAILED(hr))
    {
        std::stringstream ss;
        ss << message << " | HRESULT: 0x" << std::hex << hr;
        throw std::runtime_error(ss.str());
    }
}