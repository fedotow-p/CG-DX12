//
// Created by elm on 27.03.2026.
//

#ifndef CG_DX12_THROWIFFAILED_H
#define CG_DX12_THROWIFFAILED_H

#include <stdexcept>
#include <string>
#include <sstream>

#include "Window.h"

inline void ThrowIfFailed(HRESULT hr, const char* message = "DirectX operation failed!") {
    if (FAILED(hr)) {
        std :: stringstream ss;
        ss << message << " | HRESULT: 0x" << std::hex << hr;
        throw std::runtime_error(ss.str());
    }
}

#endif //CG_DX12_THROWIFFAILED_H
