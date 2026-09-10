#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

constexpr UINT CASCADE_COUNT = 4;
constexpr UINT SHADOW_MAP_SIZE = 2048;

// Layout must match the ShadowConstants cbuffer consumed by lighting.hlsl.
struct ShadowConstants
{
    XMFLOAT4X4 mLightViewProj[CASCADE_COUNT];
    XMFLOAT4 mCascadeSplits;   // view-space far distance of each cascade
    XMFLOAT4 mShadowMapSize;   // x = size, y = 1/size
    XMFLOAT4 mLightDir;        // xyz = direction (world space), w = unused
};

// Single Texture2DArray depth-only shadow map with one slice per cascade.
class ShadowMap
{
public:
    ShadowMap(ID3D12Device* device, UINT size, UINT cascadeCount);
    ~ShadowMap() = default;

    // Computes non-uniform (practical split scheme) cascade far-distances.
    // lambda blends between a logarithmic and a uniform split: lambda = 1
    // is fully logarithmic (tighter near cascades), lambda = 0 is uniform.
    void CalculateCascadeSplits(float nearPlane, float farPlane, float lambda);

    // Fits each cascade's light view/projection matrix around the portion
    // of the camera frustum it covers.
    void UpdateLightMatrices(const XMFLOAT3& lightDir, const XMMATRIX& viewMatrix,
                             float nearPlane, float aspectRatio, float fovY);

    ID3D12Resource* GetResource() const { return mShadowMap.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDsv(UINT cascade) const { return mDsvHandles[cascade]; }

    D3D12_VIEWPORT GetViewport() const { return mViewport; }
    D3D12_RECT GetScissorRect() const { return mScissorRect; }

    UINT GetSize() const { return mSize; }
    UINT GetCascadeCount() const { return mCascadeCount; }
    float GetCascadeSplit(UINT index) const { return (index < CASCADE_COUNT) ? mCascadeSplits[index] : 0.0f; }
    XMMATRIX GetLightViewProj(UINT cascade) const;

    ShadowConstants GetShadowConstants(const XMFLOAT3& lightDir) const;

    void CreateDsvs(ID3D12Device* device, ID3D12DescriptorHeap* dsvHeap, UINT dsvDescriptorSize, UINT heapOffset);
    void CreateSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);

private:
    void BuildResource(ID3D12Device* device);
    static void CalculateFrustumCorners(XMVECTOR* corners, const XMMATRIX& viewMatrix,
                                         float nearPlane, float farPlane, float aspectRatio, float fovY);

    ComPtr<ID3D12Resource> mShadowMap;
    D3D12_CPU_DESCRIPTOR_HANDLE mDsvHandles[CASCADE_COUNT] = {};

    D3D12_VIEWPORT mViewport = {};
    D3D12_RECT mScissorRect = {};

    XMMATRIX mLightViewProj[CASCADE_COUNT] = {};
    float mCascadeSplits[CASCADE_COUNT] = {};

    UINT mSize = SHADOW_MAP_SIZE;
    UINT mCascadeCount = CASCADE_COUNT;
};
