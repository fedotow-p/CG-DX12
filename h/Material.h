#pragma once

#include <d3d12.h>
#include <string>
#include <wrl/client.h>

struct Material
{
    std::string Name;
    std::string DiffuseMap;
    UINT SrvHeapIndex = 0;
    DXGI_FORMAT TextureFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    Microsoft::WRL::ComPtr<ID3D12Resource> DiffuseTexture;
};