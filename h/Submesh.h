#pragma once
#include <string>
#include <cstdint>

struct Submesh
{
    uint32_t IndexStart = 0;
    uint32_t IndexCount = 0;
    std::string MaterialName;
};