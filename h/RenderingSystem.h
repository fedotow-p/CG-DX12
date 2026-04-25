#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <string>
#include "UploadBuffer.h"
#include "Light.h"
#include "Submesh.h"
#include "Material.h"
#include "GBuffer.h"
#include "CameraConstants.h"

using Microsoft::WRL::ComPtr;

class RenderingSystem
{
public:
    RenderingSystem(
        ID3D12Device* device,
        ID3D12CommandQueue* commandQueue,
        ID3D12GraphicsCommandList* commandList,
        ID3D12CommandAllocator* commandAllocator,
        ID3D12Fence* fence,
        UINT swapChainBufferCount,
        DXGI_FORMAT backBufferFormat);

    ~RenderingSystem();

    bool Initialize(UINT width, UINT height);

    void GeometryPass(
        ID3D12PipelineState* pso,
        ID3D12RootSignature* rootSignature,
        ID3D12DescriptorHeap* cbvSrvHeap,
        UINT cbvSrvDescriptorSize,
        const std::vector<Submesh>& submeshes,
        const std::vector<Material>& materials,
        ID3D12Resource* vertexBuffer,
        ID3D12Resource* indexBuffer,
        const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
        const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
        ID3D12Resource* depthStencilBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissorRect,
        UINT materialCount,
        ID3D12Resource* secondaryTexture);

    void LightingPass(
        ID3D12Resource* backBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
        const std::vector<Light>& lights,
        const DirectX::XMFLOAT3& cameraPos,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissorRect,
        int& currBackBufferIndex,
        IDXGISwapChain* swapChain,
        ID3D12PipelineState* lightingPSO,
        ID3D12RootSignature* lightingRootSignature,
        UploadBuffer<LightConstants>* lightingCB,
        UploadBuffer<CameraConstants>* cameraCB,
        GBuffer* gBuffer);

    void Shutdown();
    void FlushCommandQueue();

    GBuffer* GetGBuffer() { return mGBuffer.get(); }
    UploadBuffer<LightConstants>* GetLightingCB() { return mLightingCB.get(); }
    ID3D12PipelineState* GetLightingPSO() { return mLightingPSO.Get(); }
    ID3D12RootSignature* GetLightingRootSignature() { return mLightingRootSignature.Get(); }

private:
    std::vector<Light> mLights;
    bool CreateGBuffer(UINT width, UINT height);
    bool CreateLightingResources();

    // Устройство и очередь
    ID3D12Device* mDevice;
    ID3D12CommandQueue* mCommandQueue;
    ID3D12GraphicsCommandList* mCommandList;
    ID3D12CommandAllocator* mCommandAllocator;
    ID3D12Fence* mFence;
    UINT mSwapChainBufferCount;
    DXGI_FORMAT mBackBufferFormat;

    // G-буфер
    std::unique_ptr<GBuffer> mGBuffer;

    // Ресурсы освещения
    ComPtr<ID3D12PipelineState> mLightingPSO;
    ComPtr<ID3D12RootSignature> mLightingRootSignature;
    std::unique_ptr<UploadBuffer<LightConstants>> mLightingCB;

    // Размеры дескрипторов
    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    UINT mWidth = 0;
    UINT mHeight = 0;
};