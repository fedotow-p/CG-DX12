#include "../h/ShadowMap.h"
#include "../h/d3dx12.h"
#include "../h/ThrowIfFailed.h"
#include <algorithm>
#include <cmath>
#include <cfloat>

ShadowMap::ShadowMap(ID3D12Device* device, UINT size, UINT cascadeCount)
    : mSize(size), mCascadeCount((std::min)(cascadeCount, CASCADE_COUNT))
{
    BuildResource(device);
}

void ShadowMap::BuildResource(ID3D12Device* device)
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = mSize;
    texDesc.Height = mSize;
    texDesc.DepthOrArraySize = static_cast<UINT16>(mCascadeCount);
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&mShadowMap)));

    mViewport.TopLeftX = 0.0f;
    mViewport.TopLeftY = 0.0f;
    mViewport.Width = static_cast<float>(mSize);
    mViewport.Height = static_cast<float>(mSize);
    mViewport.MinDepth = 0.0f;
    mViewport.MaxDepth = 1.0f;

    mScissorRect = { 0, 0, static_cast<LONG>(mSize), static_cast<LONG>(mSize) };
}

void ShadowMap::CreateDsvs(ID3D12Device* device, ID3D12DescriptorHeap* dsvHeap, UINT dsvDescriptorSize, UINT heapOffset)
{
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Texture2DArray.ArraySize = 1;
    dsvDesc.Texture2DArray.MipSlice = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(dsvHeap->GetCPUDescriptorHandleForHeapStart());
    handle.Offset(heapOffset * dsvDescriptorSize);

    for (UINT cascade = 0; cascade < mCascadeCount; ++cascade)
    {
        dsvDesc.Texture2DArray.FirstArraySlice = cascade;
        mDsvHandles[cascade] = handle;
        device->CreateDepthStencilView(mShadowMap.Get(), &dsvDesc, handle);
        handle.Offset(dsvDescriptorSize);
    }
}

void ShadowMap::CreateSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = mCascadeCount;

    device->CreateShaderResourceView(mShadowMap.Get(), &srvDesc, srvHandle);
}

// Practical split scheme (Zhang et al.): blends a logarithmic distribution,
// which keeps near cascades tight for high shadow resolution close to the
// camera, with a uniform distribution that keeps far cascades from becoming
// impractically thin. lambda == 1 -> pure log, lambda == 0 -> pure uniform.
void ShadowMap::CalculateCascadeSplits(float nearPlane, float farPlane, float lambda)
{
    for (UINT i = 0; i < mCascadeCount; ++i)
    {
        const float idm = static_cast<float>(i + 1) / static_cast<float>(mCascadeCount);
        const float logSplit = nearPlane * std::pow(farPlane / nearPlane, idm);
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * idm;
        mCascadeSplits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }
    mCascadeSplits[mCascadeCount - 1] = farPlane;
}

void ShadowMap::UpdateLightMatrices(const XMFLOAT3& lightDir, const XMMATRIX& viewMatrix,
                                    float nearPlane, float aspectRatio, float fovY)
{
    for (UINT cascade = 0; cascade < mCascadeCount; ++cascade)
    {
        const float cascadeNear = (cascade == 0) ? nearPlane : mCascadeSplits[cascade - 1];
        const float cascadeFar = mCascadeSplits[cascade];

        XMVECTOR frustumCorners[8];
        CalculateFrustumCorners(frustumCorners, viewMatrix, cascadeNear, cascadeFar, aspectRatio, fovY);

        XMVECTOR center = XMVectorZero();
        for (int i = 0; i < 8; ++i)
            center = XMVectorAdd(center, frustumCorners[i]);
        center = XMVectorScale(center, 1.0f / 8.0f);

        // Bounding sphere radius keeps the ortho box stable (no shimmering)
        // as the camera rotates, since the box size no longer depends on view angle.
        float radius = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            const float dist = XMVectorGetX(XMVector3Length(XMVectorSubtract(frustumCorners[i], center)));
            radius = (std::max)(radius, dist);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        XMVECTOR lightDirVec = XMVector3Normalize(XMLoadFloat3(&lightDir));
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        if (std::abs(XMVectorGetY(lightDirVec)) > 0.99f)
            up = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);

        const XMVECTOR eye = XMVectorSubtract(center, XMVectorScale(lightDirVec, radius * 2.0f));
        XMMATRIX lightView = XMMatrixLookAtLH(eye, center, up);

        // Snap the origin to shadow-map texel size to eliminate swimming
        // as the light-space bounding box translates with the camera.
        const float texelSize = (radius * 2.0f) / static_cast<float>(mSize);
        XMVECTOR originLS = XMVector3TransformCoord(center, lightView);
        originLS = XMVectorScale(XMVectorRound(XMVectorScale(originLS, 1.0f / texelSize)), texelSize);
        XMFLOAT3 snappedOriginLS;
        XMStoreFloat3(&snappedOriginLS, originLS);

        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(
            snappedOriginLS.x - radius, snappedOriginLS.x + radius,
            snappedOriginLS.y - radius, snappedOriginLS.y + radius,
            0.0f, radius * 4.0f);

        mLightViewProj[cascade] = XMMatrixMultiply(lightView, lightProj);
    }
}

void ShadowMap::CalculateFrustumCorners(XMVECTOR* corners, const XMMATRIX& viewMatrix,
                                        float nearPlane, float farPlane, float aspectRatio, float fovY)
{
    const float tanHalfFov = std::tan(fovY * 0.5f);
    const float nearHeight = 2.0f * tanHalfFov * nearPlane;
    const float nearWidth = nearHeight * aspectRatio;
    const float farHeight = 2.0f * tanHalfFov * farPlane;
    const float farWidth = farHeight * aspectRatio;

    const XMMATRIX invView = XMMatrixInverse(nullptr, viewMatrix);
    const XMVECTOR forward = XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), invView);
    const XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), invView);
    const XMVECTOR up = XMVector3TransformNormal(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), invView);
    const XMVECTOR camPos = invView.r[3];

    const XMVECTOR nearCenter = XMVectorAdd(camPos, XMVectorScale(forward, nearPlane));
    const XMVECTOR farCenter = XMVectorAdd(camPos, XMVectorScale(forward, farPlane));

    corners[0] = XMVectorAdd(XMVectorAdd(nearCenter, XMVectorScale(right, -nearWidth * 0.5f)), XMVectorScale(up, -nearHeight * 0.5f));
    corners[1] = XMVectorAdd(XMVectorAdd(nearCenter, XMVectorScale(right, nearWidth * 0.5f)), XMVectorScale(up, -nearHeight * 0.5f));
    corners[2] = XMVectorAdd(XMVectorAdd(nearCenter, XMVectorScale(right, nearWidth * 0.5f)), XMVectorScale(up, nearHeight * 0.5f));
    corners[3] = XMVectorAdd(XMVectorAdd(nearCenter, XMVectorScale(right, -nearWidth * 0.5f)), XMVectorScale(up, nearHeight * 0.5f));

    corners[4] = XMVectorAdd(XMVectorAdd(farCenter, XMVectorScale(right, -farWidth * 0.5f)), XMVectorScale(up, -farHeight * 0.5f));
    corners[5] = XMVectorAdd(XMVectorAdd(farCenter, XMVectorScale(right, farWidth * 0.5f)), XMVectorScale(up, -farHeight * 0.5f));
    corners[6] = XMVectorAdd(XMVectorAdd(farCenter, XMVectorScale(right, farWidth * 0.5f)), XMVectorScale(up, farHeight * 0.5f));
    corners[7] = XMVectorAdd(XMVectorAdd(farCenter, XMVectorScale(right, -farWidth * 0.5f)), XMVectorScale(up, farHeight * 0.5f));
}

ShadowConstants ShadowMap::GetShadowConstants(const XMFLOAT3& lightDir) const
{
    ShadowConstants constants = {};

    for (UINT i = 0; i < CASCADE_COUNT; ++i)
    {
        const XMMATRIX mat = (i < mCascadeCount) ? mLightViewProj[i] : XMMatrixIdentity();
        XMStoreFloat4x4(&constants.mLightViewProj[i], XMMatrixTranspose(mat));
    }

    constants.mCascadeSplits = XMFLOAT4(
        mCascadeSplits[0],
        mCascadeCount > 1 ? mCascadeSplits[1] : mCascadeSplits[0],
        mCascadeCount > 2 ? mCascadeSplits[2] : mCascadeSplits[0],
        mCascadeCount > 3 ? mCascadeSplits[3] : mCascadeSplits[0]);

    constants.mShadowMapSize = XMFLOAT4(
        static_cast<float>(mSize),
        1.0f / static_cast<float>(mSize),
        0.0f,
        0.0f);

    constants.mLightDir = XMFLOAT4(lightDir.x, lightDir.y, lightDir.z, 0.0f);

    return constants;
}

XMMATRIX ShadowMap::GetLightViewProj(UINT cascade) const
{
    return (cascade < mCascadeCount) ? mLightViewProj[cascade] : XMMatrixIdentity();
}
