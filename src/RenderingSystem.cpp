#include "../h/RenderingSystem.h"
#include "../h/d3dx12.h"
#include "../h/d3dUtil.h"
#include "../h/ThrowIfFailed.h"
#include <DirectXMath.h>
#include <array>
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
    mNextFenceValue = mFence->GetCompletedValue() + 1;
}

RenderingSystem::~RenderingSystem()
{
    Shutdown();
}

bool RenderingSystem::Initialize(UINT width, UINT height)
{
    mWidth = width;
    mHeight = height;

    ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mShadowCommandAllocator)));
    ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mGeometryCommandAllocator)));
    ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mLightingCommandAllocator)));

    if (!CreateGBuffer(width, height))
        return false;

    if (!CreatePostProcessResources())
        return false;

    if (!CreateLightingResources())
        return false;

    return true;
}

void RenderingSystem::PrepareCommandAllocator(ID3D12CommandAllocator* allocator, UINT64 completedFenceValue)
{
    if (completedFenceValue != 0 && mFence->GetCompletedValue() < completedFenceValue)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(completedFenceValue, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
    ThrowIfFailed(allocator->Reset());
}

void RenderingSystem::SubmitCommandList(UINT64& fenceValue)
{
    ID3D12CommandList* commandLists[] = { mCommandList };
    mCommandQueue->ExecuteCommandLists(1, commandLists);
    fenceValue = mNextFenceValue++;
    ThrowIfFailed(mCommandQueue->Signal(mFence, fenceValue));
}

bool RenderingSystem::CreateGBuffer(UINT width, UINT height)
{
    mGBuffer = std::make_unique<GBuffer>();
    return mGBuffer->Initialize(mDevice, width, height);
}

bool RenderingSystem::CreatePostProcessResources()
{
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC sceneDesc = {};
    sceneDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    sceneDesc.Width = mWidth;
    sceneDesc.Height = mHeight;
    sceneDesc.DepthOrArraySize = 1;
    sceneDesc.MipLevels = 1;
    sceneDesc.Format = mBackBufferFormat;
    sceneDesc.SampleDesc.Count = 1;
    sceneDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    sceneDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = mBackBufferFormat;
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &sceneDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&mSceneColor)));

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mPostProcessRtvHeap)));
    mDevice->CreateRenderTargetView(mSceneColor.Get(), nullptr,
        mPostProcessRtvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.NumDescriptors = 2;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mPostProcessSrvHeap)));

    auto srv = mPostProcessSrvHeap->GetCPUDescriptorHandleForHeapStart();
    mDevice->CreateShaderResourceView(mSceneColor.Get(), nullptr, srv);
    srv.ptr += mCbvSrvUavDescriptorSize;
    mDevice->CopyDescriptorsSimple(1, srv, mGBuffer->GetSRV(GBuffer::GBUFFER_DEPTH),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    auto vs = d3dUtil::CompileShader(L"../src/postprocess.hlsl", nullptr, "VS", "vs_5_0");
    auto ps = d3dUtil::CompileShader(L"../src/postprocess.hlsl", nullptr, "PS", "ps_5_0");
    if (!vs || !ps)
    {
        OutputDebugStringA("Failed to compile post-process shaders\n");
        return false;
    }

    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 2;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[1].Constants.ShaderRegister = 0;
    rootParams[1].Constants.Num32BitValues = 8;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[2].Descriptor.ShaderRegister = 1;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 1;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = _countof(rootParams);
    rootSigDesc.pParameters = rootParams;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSig, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob) OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        return false;
    }
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mPostProcessRootSignature)));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.pRootSignature = mPostProcessRootSignature.Get();
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].LogicOpEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    psoDesc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    psoDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    psoDesc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    psoDesc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    psoDesc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    psoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.DepthStencilState.BackFace = psoDesc.DepthStencilState.FrontFace;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPostProcessPSO)));

    return true;
}

bool RenderingSystem::CreateLightingResources()
{

    constexpr UINT shadowMapSize = 2048;
    constexpr UINT cascadeCount = CameraConstants::CascadeCount;

    D3D12_RESOURCE_DESC shadowDesc = {};
    shadowDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    shadowDesc.Width = shadowMapSize;
    shadowDesc.Height = shadowMapSize;
    shadowDesc.DepthOrArraySize = cascadeCount;
    shadowDesc.MipLevels = 1;
    shadowDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    shadowDesc.SampleDesc.Count = 1;
    shadowDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    shadowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE shadowClear = {};
    shadowClear.Format = DXGI_FORMAT_D32_FLOAT;
    shadowClear.DepthStencil.Depth = 1.0f;
    ThrowIfFailed(mDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &shadowDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &shadowClear, IID_PPV_ARGS(&mShadowMap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = cascadeCount;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)));
    for (UINT i = 0; i < cascadeCount; ++i)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D32_FLOAT;
        dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsv.Texture2DArray.FirstArraySlice = i;
        dsv.Texture2DArray.ArraySize = 1;
        auto handle = mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += i * mDsvDescriptorSize;
        mDevice->CreateDepthStencilView(mShadowMap.Get(), &dsv, handle);
    }

    D3D12_DESCRIPTOR_HEAP_DESC lightingHeapDesc = {};
    lightingHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    lightingHeapDesc.NumDescriptors = 4;
    lightingHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&lightingHeapDesc, IID_PPV_ARGS(&mLightingSrvHeap)));
    auto lightingSrv = mLightingSrvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 3; ++i)
    {
        mDevice->CopyDescriptorsSimple(1, lightingSrv, mGBuffer->GetSRV((GBuffer::GBUFFER_TEXTURE_TYPE)i),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        lightingSrv.ptr += mCbvSrvUavDescriptorSize;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrv = {};
    shadowSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    shadowSrv.Format = DXGI_FORMAT_R32_FLOAT;
    shadowSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    shadowSrv.Texture2DArray.ArraySize = cascadeCount;
    shadowSrv.Texture2DArray.MipLevels = 1;
    mDevice->CreateShaderResourceView(mShadowMap.Get(), &shadowSrv, lightingSrv);

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
    D3D12_DESCRIPTOR_RANGE srvRanges[1];

    // SRV для Albedo (t0)
    srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[0].NumDescriptors = 4;
    srvRanges[0].BaseShaderRegister = 0;
    srvRanges[0].RegisterSpace = 0;
    srvRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3] = {};

    // Root Parameter 0: Descriptor table для G-buffer SRVs
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = srvRanges;
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

    // Static Sampler
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    D3D12_STATIC_SAMPLER_DESC& sampler = samplers[0];
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;  // Изменено с WRAP на CLAMP для G-buffer
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MinLOD = 0;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    samplers[1] = sampler;
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].ShaderRegister = 1;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
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

    auto shadowVs = d3dUtil::CompileShader(L"../src/shadow.hlsl", nullptr, "VS", "vs_5_0");
    if (!shadowVs)
        return false;
    D3D12_ROOT_PARAMETER shadowParam = {};
    shadowParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadowParam.Descriptor.ShaderRegister = 0;
    shadowParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC shadowRootDesc = {};
    shadowRootDesc.NumParameters = 1;
    shadowRootDesc.pParameters = &shadowParam;
    shadowRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ThrowIfFailed(D3D12SerializeRootSignature(&shadowRootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSig, &errorBlob));
    ThrowIfFailed(mDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&mShadowRootSignature)));
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPso = {};
    shadowPso.VS = { shadowVs->GetBufferPointer(), shadowVs->GetBufferSize() };
    shadowPso.pRootSignature = mShadowRootSignature.Get();
    D3D12_INPUT_ELEMENT_DESC shadowInput = { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
        D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 };
    shadowPso.InputLayout = { &shadowInput, 1 };
    shadowPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowPso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    shadowPso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    shadowPso.RasterizerState.DepthBias = 1000;
    shadowPso.RasterizerState.SlopeScaledDepthBias = 1.5f;
    shadowPso.RasterizerState.DepthBiasClamp = 0.01f;
    shadowPso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    shadowPso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    shadowPso.DepthStencilState.StencilEnable = FALSE;
    shadowPso.SampleMask = UINT_MAX;
    shadowPso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowPso.SampleDesc.Count = 1;
    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(&shadowPso, IID_PPV_ARGS(&mShadowPSO)));
    mShadowCB = std::make_unique<UploadBuffer<ObjectConstants>>(mDevice, cascadeCount, true);

    return true;
}

void RenderingSystem::ShadowPass(const std::vector<Submesh>& submeshes,
    const std::array<std::vector<uint32_t>, CameraConstants::CascadeCount>& visibleSubmeshIndices,
    ID3D12Resource*, ID3D12Resource*,
    const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView, const D3D12_INDEX_BUFFER_VIEW& indexBufferView,
    const CameraConstants& cameraConstants)
{
    constexpr UINT shadowMapSize = 2048;
    // The scene uses single upload buffers for object and camera constants.
    // Wait for the prior frame before those buffers are overwritten on Update.
    PrepareCommandAllocator(mShadowCommandAllocator.Get(), mLightingFenceValue);
    ThrowIfFailed(mCommandList->Reset(mShadowCommandAllocator.Get(), mShadowPSO.Get()));
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    mCommandList->ResourceBarrier(1, &barrier);
    mCommandList->SetGraphicsRootSignature(mShadowRootSignature.Get());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    mCommandList->IASetIndexBuffer(&indexBufferView);
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)shadowMapSize, (float)shadowMapSize, 0.0f, 1.0f };
    D3D12_RECT rect = { 0, 0, shadowMapSize, shadowMapSize };
    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &rect);
    for (UINT cascade = 0; cascade < CameraConstants::CascadeCount; ++cascade)
    {
        ObjectConstants constants;
        constants.mWorldViewProj = cameraConstants.mCascadeViewProj[cascade];
        mShadowCB->CopyData(cascade, constants);
        auto dsv = mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += cascade * mDsvDescriptorSize;
        mCommandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        mCommandList->OMSetRenderTargets(0, nullptr, false, &dsv);
        mCommandList->SetGraphicsRootConstantBufferView(0, mShadowCB->Resource()->GetGPUVirtualAddress() + cascade * mShadowCB->GetElementSize());
        for (const uint32_t submeshIndex : visibleSubmeshIndices[cascade])
        {
            if (submeshIndex >= submeshes.size())
                continue;
            const Submesh& sm = submeshes[submeshIndex];
            mCommandList->DrawIndexedInstanced(sm.IndexCount, 1, sm.IndexStart, 0, 0);
        }
    }
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    mCommandList->ResourceBarrier(1, &barrier);
    ThrowIfFailed(mCommandList->Close());
    SubmitCommandList(mShadowFenceValue);
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

    PrepareCommandAllocator(mGeometryCommandAllocator.Get(), mGeometryFenceValue);
    ThrowIfFailed(mCommandList->Reset(mGeometryCommandAllocator.Get(), pso));

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

    SubmitCommandList(mGeometryFenceValue);
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
    PrepareCommandAllocator(mLightingCommandAllocator.Get(), mLightingFenceValue);
    ThrowIfFailed(mCommandList->Reset(mLightingCommandAllocator.Get(), lightingPSO));

    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        mSceneColor.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->RSSetViewports(1, &viewport);
    mCommandList->RSSetScissorRects(1, &scissorRect);

    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = mPostProcessRtvHeap->GetCPUDescriptorHandleForHeapStart();
    mCommandList->ClearRenderTargetView(sceneRtv, clearColor, 0, nullptr);

    mCommandList->OMSetRenderTargets(1, &sceneRtv, true, nullptr);

    mCommandList->SetGraphicsRootSignature(lightingRootSignature);
    ID3D12DescriptorHeap* heaps[] = { mLightingSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, mLightingSrvHeap->GetGPUDescriptorHandleForHeapStart());

    if (cameraCB)
    {
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddr = cameraCB->Resource()->GetGPUVirtualAddress();
        mCommandList->SetGraphicsRootConstantBufferView(2, cameraAddr);
    }

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
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

            // Рисуем полноэкранный quad, который VS генерирует по SV_VertexID.
            mCommandList->DrawInstanced(4, 1, 0, 0);
        }
    }

    D3D12_RESOURCE_BARRIER postProcessBarriers[] =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(
            mSceneColor.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(
            backBuffer,
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET)
    };
    mCommandList->ResourceBarrier(_countof(postProcessBarriers), postProcessBarriers);

    // Composite the lit scene to the back buffer with depth of field and chromatic aberration.
    mCommandList->OMSetRenderTargets(1, &rtvHandle, true, nullptr);
    mCommandList->SetPipelineState(mPostProcessPSO.Get());
    mCommandList->SetGraphicsRootSignature(mPostProcessRootSignature.Get());
    ID3D12DescriptorHeap* postProcessHeaps[] = { mPostProcessSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, postProcessHeaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, mPostProcessSrvHeap->GetGPUDescriptorHandleForHeapStart());

    const float postProcessParameters[] =
    {
        2.0f,                         // focus distance
        20.0f,                          // focus range
        6.0f,                          // maximum blur radius in pixels
        5.0f,                         // chromatic aberration in pixels
        1.0f / static_cast<float>(mWidth),
        1.0f / static_cast<float>(mHeight),
        0.0f,
        0.0f
    };
    mCommandList->SetGraphicsRoot32BitConstants(1, _countof(postProcessParameters), postProcessParameters, 0);
    if (cameraCB)
        mCommandList->SetGraphicsRootConstantBufferView(2, cameraCB->Resource()->GetGPUVirtualAddress());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    mCommandList->IASetVertexBuffers(0, 0, nullptr);
    mCommandList->IASetIndexBuffer(nullptr);
    mCommandList->DrawInstanced(4, 1, 0, 0);

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->Close();

    SubmitCommandList(mLightingFenceValue);

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
    mShadowCB.reset();
    mShadowPSO.Reset();
    mShadowRootSignature.Reset();
    mShadowMap.Reset();
    mShadowDsvHeap.Reset();
    mLightingSrvHeap.Reset();
    mSceneColor.Reset();
    mPostProcessRtvHeap.Reset();
    mPostProcessSrvHeap.Reset();
    mPostProcessPSO.Reset();
    mPostProcessRootSignature.Reset();
    mShadowCommandAllocator.Reset();
    mGeometryCommandAllocator.Reset();
    mLightingCommandAllocator.Reset();
}

void RenderingSystem::FlushCommandQueue()
{
    const UINT64 fenceValue = mNextFenceValue++;

    mCommandQueue->Signal(mFence, fenceValue);

    if (mFence->GetCompletedValue() < fenceValue)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        mFence->SetEventOnCompletion(fenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}
