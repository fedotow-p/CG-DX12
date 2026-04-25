#include "../h/GBuffer.h"
#include "../h/ThrowIfFailed.h"
#include <DirectXMath.h>

bool GBuffer::Initialize(ID3D12Device* device, UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;

    // Получаем размеры дескрипторов
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mCbvSrvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Создаем текстуры
    if (!CreateTextures(device))
        return false;

    // Создаем RTV кучу и представления
    if (!CreateRTVs(device))
        return false;

    // Создаем SRV кучу и представления
    if (!CreateSRVs(device))
        return false;

    return true;
}

bool GBuffer::CreateTextures(ID3D12Device* device)
{
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = mWidth;
    texDesc.Height = mHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    // Albedo texture
    texDesc.Format = mAlbedoFormat;
    D3D12_CLEAR_VALUE clearValueAlbedo = {};
    clearValueAlbedo.Format = mAlbedoFormat;
    clearValueAlbedo.Color[0] = 0.0f;
    clearValueAlbedo.Color[1] = 0.0f;
    clearValueAlbedo.Color[2] = 0.0f;
    clearValueAlbedo.Color[3] = 1.0f;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,  // <- ИСПРАВЛЕНО
        &clearValueAlbedo,
        IID_PPV_ARGS(&mTextures[GBUFFER_ALBEDO])
));

    // Normal texture
    texDesc.Format = mNormalFormat;
    D3D12_CLEAR_VALUE clearValueNormal = {};
    clearValueNormal.Format = mNormalFormat;
    clearValueNormal.Color[0] = 0.0f;
    clearValueNormal.Color[1] = 0.0f;
    clearValueNormal.Color[2] = 0.0f;
    clearValueNormal.Color[3] = 0.0f;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, // Изменено с RENDER_TARGET
        &clearValueNormal,
        IID_PPV_ARGS(&mTextures[GBUFFER_NORMAL])
    ));

    // Depth texture (просто R32_FLOAT)
    texDesc.Format = mDepthFormat;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValueDepth = {};
    clearValueDepth.Format = mDepthFormat;
    clearValueDepth.Color[0] = 1.0f;  // Бесконечность

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValueDepth,
        IID_PPV_ARGS(&mTextures[GBUFFER_DEPTH])
    ));

    return true;
}

bool GBuffer::CreateRTVs(ID3D12Device* device)
{
    // Создаем RTV кучу
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = GBUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(device->CreateDescriptorHeap(
        &rtvHeapDesc,
        IID_PPV_ARGS(&mRtvHeap)
    ));

    // Создаем RTV для каждой текстуры
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = (i == GBUFFER_ALBEDO) ? mAlbedoFormat :
                         (i == GBUFFER_NORMAL) ? mNormalFormat :mDepthFormat;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        device->CreateRenderTargetView(mTextures[i].Get(), &rtvDesc, rtvHandle);

        rtvHandle.ptr += mRtvDescriptorSize;
    }

    return true;
}

bool GBuffer::CreateSRVs(ID3D12Device* device)
{
    // Создаем SRV кучу
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = GBUFFER_COUNT;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(device->CreateDescriptorHeap(
        &srvHeapDesc,
        IID_PPV_ARGS(&mSrvHeap)
    ));

    // Создаем SRV для каждой текстуры
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = mSrvHeap->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = (i == GBUFFER_ALBEDO) ? mAlbedoFormat :
                         (i == GBUFFER_NORMAL) ? mNormalFormat : mDepthFormat;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        device->CreateShaderResourceView(mTextures[i].Get(), &srvDesc, srvHandle);

        srvHandle.ptr += mCbvSrvDescriptorSize;
    }

    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetRTV(GBUFFER_TEXTURE_TYPE type) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += type * mRtvDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE GBuffer::GetSRV(GBUFFER_TEXTURE_TYPE type) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mSrvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += type * mCbvSrvDescriptorSize;
    return handle;
}

void GBuffer::ClearRenderTargets(ID3D12GraphicsCommandList* cmdList,
                                 const float* clearColorAlbedo,
                                 const float* clearColorNormal,
                                 const float* clearColorDepth) // переименовать
{
    float defaultClearAlbedo[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float defaultClearNormal[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float defaultClearDepth[] = { 1.0f, 0.0f, 0.0f, 0.0f };   // глубина = 1.0

    cmdList->ClearRenderTargetView(GetRTV(GBUFFER_ALBEDO), clearColorAlbedo ? clearColorAlbedo : defaultClearAlbedo, 0, nullptr);
    cmdList->ClearRenderTargetView(GetRTV(GBUFFER_NORMAL), clearColorNormal ? clearColorNormal : defaultClearNormal, 0, nullptr);
    cmdList->ClearRenderTargetView(GetRTV(GBUFFER_DEPTH),   clearColorDepth ? clearColorDepth : defaultClearDepth, 0, nullptr);
}

void GBuffer::SetRenderTargets(ID3D12GraphicsCommandList* cmdList,
                               ID3D12DescriptorHeap* rtvHeap,
                               D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[GBUFFER_COUNT] = {
        GetRTV(GBUFFER_ALBEDO),
        GetRTV(GBUFFER_NORMAL),
        GetRTV(GBUFFER_DEPTH)
    };

    cmdList->OMSetRenderTargets(GBUFFER_COUNT, rtvHandles, false, &dsvHandle);
}

void GBuffer::Shutdown()
{
    for (int i = 0; i < GBUFFER_COUNT; ++i)
    {
        mTextures[i].Reset();
    }
    mRtvHeap.Reset();
    mSrvHeap.Reset();
}