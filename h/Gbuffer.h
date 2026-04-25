#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

class GBuffer
{
public:
    enum GBUFFER_TEXTURE_TYPE
    {
        GBUFFER_ALBEDO = 0,  // RGB - Albedo, A - unused
        GBUFFER_NORMAL,      // RGB - Normal, A - unused
        GBUFFER_DEPTH,
        GBUFFER_COUNT
    };

    GBuffer() = default;
    ~GBuffer() = default;

    bool Initialize(ID3D12Device* device, UINT width, UINT height);
    void Shutdown();

    // Геттеры для ресурсов
    ID3D12Resource* GetTexture(GBUFFER_TEXTURE_TYPE type) const { return mTextures[type].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(GBUFFER_TEXTURE_TYPE type) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(GBUFFER_TEXTURE_TYPE type) const;

    // Получить размеры
    UINT GetWidth() const { return mWidth; }
    UINT GetHeight() const { return mHeight; }

    // Очистка всех текстур G-буфера
    void ClearRenderTargets(ID3D12GraphicsCommandList* cmdList, 
                            const float* clearColorAlbedo = nullptr,
                            const float* clearColorNormal = nullptr,
                            const float* clearColorPosition = nullptr);

    // Установка в качестве render targets
    void SetRenderTargets(ID3D12GraphicsCommandList* cmdList, 
                          ID3D12DescriptorHeap* rtvHeap,
                          D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);

    // Дескрипторные кучи
    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mSrvHeap;

private:
    bool CreateTextures(ID3D12Device* device);
    bool CreateRTVs(ID3D12Device* device);
    bool CreateSRVs(ID3D12Device* device);

    ComPtr<ID3D12Resource> mTextures[GBUFFER_COUNT];

    UINT mRtvDescriptorSize = 0;
    UINT mCbvSrvDescriptorSize = 0;
    UINT mWidth = 0;
    UINT mHeight = 0;

    // Форматы текстур
    static constexpr DXGI_FORMAT mAlbedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT mNormalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;  // Для точности нормалей
    static constexpr DXGI_FORMAT mDepthFormat = DXGI_FORMAT_R32_FLOAT;
};