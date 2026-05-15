#pragma once

#include <d3d12.h>
#include <string>
#include <wrl/client.h>

struct Material
{
    std::string Name;
    std::string DiffuseMap;
    std::string NormalMap;
    std::string HeightMap;
    UINT DiffuseSrvHeapIndex = 0;
    UINT NormalSrvHeapIndex = 0;
    UINT HeightSrvHeapIndex = 0;
    bool EnableTessellation = false;
    DXGI_FORMAT TextureFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    DXGI_FORMAT NormalFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    DXGI_FORMAT HeightFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    Microsoft::WRL::ComPtr<ID3D12Resource> DiffuseTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> NormalTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> HeightTexture;
};
