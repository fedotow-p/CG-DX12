#pragma once

#include <string>
#include <wrl/client.h>
#include <d3d12.h>

struct Material
{
    std::string Name;

    std::string DiffuseMap;      // ��� .tga �����
    UINT SrvHeapIndex = 0;       // ������ SRV � ����
    bool isFlag = false;


    Microsoft::WRL::ComPtr<ID3D12Resource> DiffuseTexture;
};