#pragma once

#include <string>
#include <wrl/client.h>
#include <d3d12.h>

struct Material
{
    std::string Name;

    std::string DiffuseMap;      // имя .tga файла
    UINT SrvHeapIndex = 0;       // индекс SRV в куче

    Microsoft::WRL::ComPtr<ID3D12Resource> DiffuseTexture;
};