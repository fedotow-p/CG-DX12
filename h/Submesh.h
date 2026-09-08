#pragma once
#include <DirectXMath.h>
#include <string>
#include <cstdint>

struct Submesh
{
    uint32_t IndexStart = 0;
    uint32_t IndexCount = 0;
    std::string MaterialName;
    DirectX::XMFLOAT3 BoundsMin = {};
    DirectX::XMFLOAT3 BoundsMax = {};
    bool HasBounds = false;
};