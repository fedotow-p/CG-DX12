#include "../h/RenderingSystem.h"
#include "../h/d3dx12.h"
#include "../h/d3dUtil.h"
#include "../h/ThrowIfFailed.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cctype>

namespace
{
bool IsAnimatedFlagMaterial(const Material& material)
{
    std::string name = material.Name;
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    return name == "fabric"
        || name.rfind("fabric_", 0) == 0;
}
}

RenderingSystem::RenderingSystem(
    ID3D12Device* device,
    ID3D12CommandQueue* commandQueue,
    ID3D12GraphicsCommandList* commandList,
    ID3D12CommandAllocator* commandAllocator,
    ID3D12Fence* fence,
    UINT swapChainBufferCount,
    DXGI_FORMAT backBufferFormat)
    : mDevice(device)
    , mCommandQueue(commandQueue)
    , mCommandList(commandList)
    , mCommandAllocator(commandAllocator)
    , mFence(fence)
    , mSwapChainBufferCount(swapChainBufferCount)
    , mBackBufferFormat(backBufferFormat)
{
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

RenderingSystem::~RenderingSystem()
{
    Shutdown();
}

bool RenderingSystem::Initialize(UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;

    if (!CreateGBuffer(width, height))
        return false;

    if (!CreateShadowResources())
        return false;

    if (!CreateLightingResources())
        return false;

    return true;
}

bool RenderingSystem::CreateShadowResources()
{
    mShadowMap = std::make_unique<ShadowMap>(mDevice, SHADOW_MAP_SIZE, CASCADE_COUNT);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = CASCADE_COUNT;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)));

    mShadowMap->CreateDsvs(mDevice, mShadowDsvHeap.Get(), mDsvDescriptorSize, 0);

    // ============= SHADOW ROOT SIGNATURE =============
    D3D12_ROOT_PARAMETER shadowRootParams[1] = {};
    shadowRootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadowRootParams[0].Descriptor.ShaderRegister = 0;
    shadowRootParams[0].Descriptor.RegisterSpace = 0;
    shadowRootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC shadowRootSigDesc = {};
    shadowRootSigDesc.NumParameters = 1;
    shadowRootSigDesc.pParameters = shadowRootParams;
    shadowRootSigDesc.NumStaticSamplers = 0;
    shadowRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedShadowRootSig;
    ComPtr<ID3DBlob> shadowErrorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&shadowRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                              &serializedShadowRootSig, &shadowErrorBlob);
    if (FAILED(hr))
    {
        if (shadowErrorBlob)
            OutputDebugStringA((char*)shadowErrorBlob->GetBufferPointer());
        OutputDebugStringA("Failed to serialize shadow root signature\n");
        return false;
    }

    hr = mDevice->CreateRootSignature(0, serializedShadowRootSig->GetBufferPointer(),
                                      serializedShadowRootSig->GetBufferSize(),
                                      IID_PPV_ARGS(&mShadowRootSignature));
    if (FAILED(hr))
    {
        OutputDebugStringA("Failed to create shadow root signature\n");
        return false;
    }

    // ============= SHADOW PSO =============
    auto vsShadow = d3dUtil::CompileShader(
        L"../src/shadowDepth.hlsl",
        nullptr,
        "VS",
        "vs_5_0");

    if (!vsShadow)
    {
        OutputDebugStringA("Failed to compile shadow depth VS\n");
        return false;
    }

    std::vector<D3D12_INPUT_ELEMENT_DESC> shadowInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = {};
    shadowPsoDesc.VS = { vsShadow->GetBufferPointer(), vsShadow->GetBufferSize() };
    shadowPsoDesc.pRootSignature = mShadowRootSignature.Get();
    shadowPsoDesc.InputLayout = { shadowInputLayout.data(), (UINT)shadowInputLayout.size() };
    shadowPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowPsoDesc.NumRenderTargets = 0;
    shadowPsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowPsoDesc.SampleDesc.Count = 1;
    shadowPsoDesc.SampleDesc.Quality = 0;
    shadowPsoDesc.SampleMask = UINT_MAX;

    shadowPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    shadowPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    shadowPsoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    // Slope-scaled depth bias reduces shadow acne without introducing
    // significant peter-panning across steeply-angled surfaces.
    shadowPsoDesc.RasterizerState.DepthBias = 50000;
    shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.01f;
    shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 2.0f;
    shadowPsoDesc.RasterizerState.DepthClipEnable = TRUE;

    shadowPsoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    shadowPsoDesc.BlendState.IndependentBlendEnable = FALSE;

    shadowPsoDesc.DepthStencilState.DepthEnable = TRUE;
    shadowPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadowPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadowPsoDesc.DepthStencilState.StencilEnable = FALSE;

    hr = mDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mShadowPSO));
    if (FAILED(hr))
    {
        OutputDebugStringA("Failed to create shadow PSO\n");
        return false;
    }

    mShadowPassCB = std::make_unique<UploadBuffer<ShadowPassCB>>(mDevice, CASCADE_COUNT, true);
    mShadowConstantsCB = std::make_unique<UploadBuffer<ShadowConstants>>(mDevice, 1, true);

    return true;
}

void RenderingSystem::UpdateShadowCascades(
    const XMFLOAT3& lightDir,
    const XMMATRIX& cameraView,
    float nearPlane,
    float farPlane,
    float aspectRatio,
    float fovY)
{
    // lambda = 0.6 favors tighter, higher-resolution near cascades while
    // still keeping the far cascade wide enough to cover the whole scene.
    mShadowMap->CalculateCascadeSplits(nearPlane, farPlane, 0.6f);
    mShadowMap->UpdateLightMatrices(lightDir, cameraView, nearPlane, aspectRatio, fovY);
}

void RenderingSystem::ShadowPass(
    const std::vector<Submesh>& submeshes,
    const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView,
    const D3D12_INDEX_BUFFER_VIEW& indexBufferView)
{
    if (!mShadowMap) return;

    mCommandAllocator->Reset();
    mCommandList->Reset(mCommandAllocator, mShadowPSO.Get());

    D3D12_RESOURCE_BARRIER toWrite = CD3DX12_RESOURCE_BARRIER::Transition(
        mShadowMap->GetResource(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    mCommandList->ResourceBarrier(1, &toWrite);

    D3D12_VIEWPORT viewport = mShadowMap->GetViewport();
    D3D12_RECT scissorRect = mShadowMap->GetScissorRect();
    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);
    mCommandList->SetGraphicsRootSignature(mShadowRootSignature.Get());

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    mCommandList->IASetIndexBuffer(&indexBufferView);

    UINT elementSize = mShadowPassCB->GetElementSize();
    D3D12_GPU_VIRTUAL_ADDRESS baseAddr = mShadowPassCB->Resource()->GetGPUVirtualAddress();

    for (UINT cascade = 0; cascade < mShadowMap->GetCascadeCount(); ++cascade)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = mShadowMap->GetDsv(cascade);
        mCommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        mCommandList->OMSetRenderTargets(0, nullptr, false, &dsv);

        ShadowPassCB cb;
        XMStoreFloat4x4(&cb.mLightViewProj, XMMatrixTranspose(mShadowMap->GetLightViewProj(cascade)));
        mShadowPassCB->CopyData(cascade, cb);
        mCommandList->SetGraphicsRootConstantBufferView(0, baseAddr + cascade * elementSize);

        for (const Submesh& sm : submeshes)
        {
            mCommandList->DrawIndexedInstanced(sm.IndexCount, 1, sm.IndexStart, 0, 0);
        }
    }

    D3D12_RESOURCE_BARRIER toRead = CD3DX12_RESOURCE_BARRIER::Transition(
        mShadowMap->GetResource(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCommandList->ResourceBarrier(1, &toRead);

    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
}

bool RenderingSystem::CreateGBuffer(UINT width, UINT height)
{
    mGBuffer = std::make_unique<GBuffer>();
    return mGBuffer->Initialize(mDevice, width, height);
}

bool RenderingSystem::CreateLightingResources()
{

    // Загружаем шейдеры
    auto vsLighting = d3dUtil::CompileShader(
        L"../src/lighting.hlsl",
        nullptr,
        "VS",
        "vs_5_0");

    if (!vsLighting)
    {
        OutputDebugStringA("Failed to compile lighting VS\n");
        return false;
    }

    auto psLighting = d3dUtil::CompileShader(
        L"../src/lighting.hlsl",
        nullptr,
        "PS",
        "ps_5_0");

    if (!psLighting)
    {
        OutputDebugStringA("Failed to compile lighting PS\n");
        return false;
    }



    // ============= ROOT SIGNATURE =============
    // Single contiguous SRV table: t0 Albedo, t1 Normal, t2 Depth, t3 Shadow cascade array.
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 4;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[4] = {};

    // Root Parameter 0: Descriptor table для G-buffer SRVs + shadow map
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Root Parameter 1: CBV для параметров света (b0)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[1].Descriptor.ShaderRegister = 0;
    rootParams[1].Descriptor.RegisterSpace = 0;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Root Parameter 2: CBV для параметров камеры (b1)
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].Descriptor.ShaderRegister = 1;
    rootParams[2].Descriptor.RegisterSpace = 0;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Root Parameter 3: CBV для параметров каскадных теней (b2)
    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[3].Descriptor.ShaderRegister = 2;
    rootParams[3].Descriptor.RegisterSpace = 0;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};

    // s0: обычный линейный sampler для G-buffer
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MipLODBias = 0;
    samplers[0].MaxAnisotropy = 1;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    samplers[0].MinLOD = 0;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: comparison sampler used for hardware Percentage-Closer Filtering
    // against the shadow map (SampleCmp does the depth comparison per-tap).
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].MipLODBias = 0;
    samplers[1].MaxAnisotropy = 1;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE; // outside cascade -> unshadowed
    samplers[1].MinLOD = 0;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 4;
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 2;
    rootSigDesc.pStaticSamplers = samplers;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &serializedRootSig, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        }
        OutputDebugStringA("Failed to serialize lighting root signature\n");
        return false;
    }

    hr = mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
                                     serializedRootSig->GetBufferSize(),
                                     IID_PPV_ARGS(&mLightingRootSignature));
    if (FAILED(hr))
    {
        OutputDebugStringA("Failed to create lighting root signature\n");
        return false;
    }

    // ============= PSO =============
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

    psoDesc.VS = { vsLighting->GetBufferPointer(), vsLighting->GetBufferSize() };
    psoDesc.PS = { psLighting->GetBufferPointer(), psLighting->GetBufferSize() };
    psoDesc.pRootSignature = mLightingRootSignature.Get();
    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.SampleMask = UINT_MAX;

    // Rasterizer state
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;  // Не отсекаем ничего для полноэкранного треугольника
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = 0;
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 0.0f;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Blend state - аддитивное смешивание для накопления света
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Depth-stencil state - отключаем depth testing
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    hr = mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mLightingPSO));
    if (FAILED(hr))
    {
        char msg[256];
        sprintf_s(msg, "Lighting PSO create failed: 0x%08X\n", hr);
        OutputDebugStringA(msg);

        // Дополнительная диагностика
        switch (hr)
        {
            case E_INVALIDARG:
                OutputDebugStringA("  Reason: E_INVALIDARG - One or more parameters are invalid\n");
                break;
            case E_OUTOFMEMORY:
                OutputDebugStringA("  Reason: E_OUTOFMEMORY - Out of memory\n");
                break;
            case D3D12_ERROR_DRIVER_VERSION_MISMATCH:
                OutputDebugStringA("  Reason: D3D12_ERROR_DRIVER_VERSION_MISMATCH - Driver version mismatch\n");
                break;
            case D3D12_ERROR_ADAPTER_NOT_FOUND:
                OutputDebugStringA("  Reason: D3D12_ERROR_ADAPTER_NOT_FOUND - Adapter not found\n");
                break;
        }

        return false;
    }

    OutputDebugStringA("Lighting PSO created successfully\n");

    mLightingCB = std::make_unique<UploadBuffer<LightConstants>>(
        mDevice,
        10,  // Максимум источников
        true);

    // Combined shader-visible SRV heap: G-buffer textures (t0-t2) followed
    // by the shadow cascade array (t3), matching the root signature table.
    D3D12_DESCRIPTOR_HEAP_DESC lightingSrvHeapDesc = {};
    lightingSrvHeapDesc.NumDescriptors = GBuffer::GBUFFER_COUNT + 1;
    lightingSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    lightingSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&lightingSrvHeapDesc, IID_PPV_ARGS(&mLightingSrvHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE lightingSrvHandle(mLightingSrvHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < GBuffer::GBUFFER_COUNT; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = (i == GBuffer::GBUFFER_ALBEDO) ? DXGI_FORMAT_R8G8B8A8_UNORM :
                          (i == GBuffer::GBUFFER_NORMAL) ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R32_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        mDevice->CreateShaderResourceView(mGBuffer->GetTexture((GBuffer::GBUFFER_TEXTURE_TYPE)i), &srvDesc, lightingSrvHandle);
        lightingSrvHandle.Offset(mCbvSrvUavDescriptorSize);
    }

    mShadowMap->CreateSrv(mDevice, lightingSrvHandle);

    return true;
}

void RenderingSystem::GeometryPass(
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
    const D3D12_RECT& scissorRect)
{
    if (!mGBuffer) return;

    mCommandAllocator->Reset();
    mCommandList->Reset(mCommandAllocator, pso);

    // Переводим G-буфер текстуры в состояние RENDER_TARGET
    D3D12_RESOURCE_BARRIER barriers[GBuffer::GBUFFER_COUNT];
    for (int i = 0; i < GBuffer::GBUFFER_COUNT; ++i)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            mGBuffer->GetTexture((GBuffer::GBUFFER_TEXTURE_TYPE)i),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    mCommandList->ResourceBarrier(GBuffer::GBUFFER_COUNT, barriers);

    // Переводим depth buffer в состояние DEPTH_WRITE
    D3D12_RESOURCE_BARRIER depthBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthStencilBuffer,
        D3D12_RESOURCE_STATE_DEPTH_READ,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    mCommandList->ResourceBarrier(1, &depthBarrier);

    // Очищаем G-буфер (только Albedo и Normal)
    mGBuffer->ClearRenderTargets(mCommandList);   // очистит Albedo, Normal, Depth
    mCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[3] = {
        mGBuffer->GetRTV(GBuffer::GBUFFER_ALBEDO),
        mGBuffer->GetRTV(GBuffer::GBUFFER_NORMAL),
        mGBuffer->GetRTV(GBuffer::GBUFFER_DEPTH)
    };
    mCommandList->OMSetRenderTargets(3, rtvHandles, false, &dsvHandle);

    // Viewport, scissor, root signature, descriptor heap
    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);
    mCommandList->SetGraphicsRootSignature(rootSignature);
    ID3D12DescriptorHeap* heaps[] = { cbvSrvHeap };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, cbvSrvHeap->GetGPUDescriptorHandleForHeapStart());

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    mCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    mCommandList->IASetIndexBuffer(&indexBufferView);

    mGeometryPassStats = {};
    mGeometryPassStats.TotalSubmeshes = traversalStats.TotalSubmeshes;
    mGeometryPassStats.BoundedSubmeshes = traversalStats.BoundedSubmeshes;
    mGeometryPassStats.UnboundedSubmeshes = traversalStats.UnboundedSubmeshes;
    mGeometryPassStats.CandidateSubmeshes = traversalStats.CandidateSubmeshes;
    mGeometryPassStats.CulledSubmeshes = traversalStats.CulledSubmeshes;
    mGeometryPassStats.NodesTested = traversalStats.NodesTested;
    mGeometryPassStats.NodesRejected = traversalStats.NodesRejected;
    for (const uint32_t submeshIndex : visibleSubmeshIndices)
    {
        if (submeshIndex >= submeshes.size())
            continue;

        const Submesh& sm = submeshes[submeshIndex];

        const Material* mat = nullptr;
        for (auto& m : materials)
        {
            if (m.Name == sm.MaterialName)
            {
                mat = &m;
                break;
            }
        }

        if (!mat)
        {
            mGeometryPassStats.MissingMaterials++;
            continue;
        }

        // Root Parameter 1: SRV for main texture
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle1 =
            cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle1.ptr += (1 + mat->DiffuseSrvHeapIndex) * cbvSrvDescriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(1, srvHandle1);

        // Root Parameter 2: SRV for secondary texture
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle2 =
            cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle2.ptr += (1 + mat->NormalSrvHeapIndex) * cbvSrvDescriptorSize;

        // Проверяем, нужно ли использовать secondary texture
        mCommandList->SetGraphicsRootDescriptorTable(2, srvHandle2);

        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle3 =
            cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle3.ptr += (1 + mat->HeightSrvHeapIndex) * cbvSrvDescriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(3, srvHandle3);

        const float isFlag = IsAnimatedFlagMaterial(*mat) ? 1.0f : 0.0f;
        mCommandList->SetGraphicsRoot32BitConstant(4, *reinterpret_cast<const UINT*>(&isFlag), 0);

        mCommandList->DrawIndexedInstanced(sm.IndexCount, 1, sm.IndexStart, 0, 0);
        mGeometryPassStats.DrawnSubmeshes++;
        mGeometryPassStats.SubmittedIndices += sm.IndexCount;
    }

    // Переводим G-буфер текстуры обратно в PIXEL_SHADER_RESOURCE
    for (int i = 0; i < GBuffer::GBUFFER_COUNT; ++i)
    {
        barriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            mGBuffer->GetTexture((GBuffer::GBUFFER_TEXTURE_TYPE)i),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    mCommandList->ResourceBarrier(GBuffer::GBUFFER_COUNT, barriers);

    // Переводим depth buffer обратно в DEPTH_READ
    depthBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthStencilBuffer,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_DEPTH_READ);
    mCommandList->ResourceBarrier(1, &depthBarrier);

    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
}

void RenderingSystem::LightingPass(
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
    GBuffer* gBuffer)
{
    mCommandAllocator->Reset();
    mCommandList->Reset(mCommandAllocator, lightingPSO);

    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);

    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &rtvHandle, true, nullptr);

    mCommandList->SetGraphicsRootSignature(lightingRootSignature);
    ID3D12DescriptorHeap* heaps[] = { mLightingSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, mLightingSrvHeap->GetGPUDescriptorHandleForHeapStart());

    if (cameraCB)
    {
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddr = cameraCB->Resource()->GetGPUVirtualAddress();
        mCommandList->SetGraphicsRootConstantBufferView(2, cameraAddr);
    }

    // Directional light drives the cascaded shadow map; find it to build ShadowConstants.
    XMFLOAT3 shadowLightDir(0.0f, -1.0f, 0.0f);
    for (const Light& light : lights)
    {
        if (light.Type == LIGHT_DIRECTIONAL)
        {
            shadowLightDir = light.Direction;
            break;
        }
    }

    ShadowConstants shadowConstants = mShadowMap->GetShadowConstants(shadowLightDir);
    mShadowConstantsCB->CopyData(0, shadowConstants);
    mCommandList->SetGraphicsRootConstantBufferView(3, mShadowConstantsCB->Resource()->GetGPUVirtualAddress());

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 0, nullptr);  // Нет вершинных буферов
    mCommandList->IASetIndexBuffer(nullptr);

    if (lightingCB)
    {
        D3D12_GPU_VIRTUAL_ADDRESS baseAddr = lightingCB->Resource()->GetGPUVirtualAddress();
        UINT elementSize = lightingCB->GetElementSize();

        for (size_t i = 0; i < lights.size(); ++i)
        {
            LightConstants lightConstants;
            lightConstants.SetFromLight(lights[i], cameraPos);
            lightingCB->CopyData((UINT)i, lightConstants);

            // Root Parameter 1: CBV для текущего света (b0)
            D3D12_GPU_VIRTUAL_ADDRESS cbAddr = baseAddr + i * elementSize;
            mCommandList->SetGraphicsRootConstantBufferView(1, cbAddr);

            // Рисуем полноэкранный треугольник (3 вершины)
            mCommandList->DrawInstanced(3, 1, 0, 0);
        }
    }

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    swapChain->Present(0, 0);
    currBackBufferIndex = (currBackBufferIndex + 1) % mSwapChainBufferCount;
}

void RenderingSystem::Shutdown()
{
    FlushCommandQueue();

    if (mGBuffer)
    {
        mGBuffer->Shutdown();
        mGBuffer.reset();
    }

    mLightingPSO.Reset();
    mLightingRootSignature.Reset();
    mLightingCB.reset();
    mLightingSrvHeap.Reset();

    mShadowPSO.Reset();
    mShadowRootSignature.Reset();
    mShadowDsvHeap.Reset();
    mShadowPassCB.reset();
    mShadowConstantsCB.reset();
    mShadowMap.reset();
}

void RenderingSystem::FlushCommandQueue()
{
    static UINT64 fenceValue = 1;

    mCommandQueue->Signal(mFence, fenceValue);

    if (mFence->GetCompletedValue() < fenceValue)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        mFence->SetEventOnCompletion(fenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    fenceValue++;
}
