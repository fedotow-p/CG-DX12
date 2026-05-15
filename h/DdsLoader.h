#pragma once

#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>

struct DdsImage
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t MipCount = 1;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    uint32_t RowPitch = 0;
    uint32_t SlicePitch = 0;
    uint32_t RowCount = 0;
    std::vector<uint8_t> Data;
};

bool LoadDDS(const std::string& filename, DdsImage& outImage);
