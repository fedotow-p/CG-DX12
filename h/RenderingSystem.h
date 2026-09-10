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
#include "Octree.h"
#include "Material.h"
#include "GBuffer.h"
#include "CameraConstants.h"
#include "ObjectConstants.h"

using Microsoft::WRL::ComPtr;

struct GeometryPassStats
{
    UINT TotalSubmeshes = 0;
    UINT BoundedSubmeshes = 0;
    UINT UnboundedSubmeshes = 0;
    UINT CandidateSubmeshes = 0;
    UINT CulledSubmeshes = 0;
    UINT NodesTested = 0;
    UINT NodesRejected = 0;
    UINT DrawnSubmeshes = 0;
    UINT MissingMaterials = 0;
    UINT64 CulledIndices = 0;
    UINT64 SubmittedIndices = 0;
};

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
        const std::vector<uint32_t>& visibleSubmeshIndices,
        const OctreeTraversalStats& traversalStats,
        ID3D12Resource* vertexBuffer,
        ID3D12Resource* indexBuffer,
        const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
        const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
        ID3D12Resource* depthStencilBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissorRect);

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

    void ShadowPass(
        const std::vector<Submesh>& submeshes,
        ID3D12Resource* vertexBuffer,
        ID3D12Resource* indexBuffer,
        const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
        const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
        const CameraConstants& cameraConstants);

    void Shutdown();
    void FlushCommandQueue();

    GBuffer* GetGBuffer() { return mGBuffer.get(); }
    UploadBuffer<LightConstants>* GetLightingCB() { return mLightingCB.get(); }
    ID3D12PipelineState* GetLightingPSO() { return mLightingPSO.Get(); }
    ID3D12RootSignature* GetLightingRootSignature() { return mLightingRootSignature.Get(); }
    const GeometryPassStats& GetGeometryPassStats() const { return mGeometryPassStats; }

private:
    std::vector<Light> mLights;
    GeometryPassStats mGeometryPassStats;
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
    std::unique_ptr<UploadBuffer<ObjectConstants>> mShadowCB;
    ComPtr<ID3D12Resource> mShadowMap;
    ComPtr<ID3D12DescriptorHeap> mShadowDsvHeap;
    ComPtr<ID3D12DescriptorHeap> mLightingSrvHeap;
    ComPtr<ID3D12PipelineState> mShadowPSO;
    ComPtr<ID3D12RootSignature> mShadowRootSignature;

    // Размеры дескрипторов
    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    UINT mWidth = 0;
    UINT mHeight = 0;
};
