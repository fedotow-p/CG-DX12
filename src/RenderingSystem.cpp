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

    if (!CreateLightingResources())
        return false;

    return true;
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
    D3D12_DESCRIPTOR_RANGE srvRanges[3];

    // SRV для Albedo (t0)
    srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[0].NumDescriptors = 1;
    srvRanges[0].BaseShaderRegister = 0;
    srvRanges[0].RegisterSpace = 0;
    srvRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // SRV для Normal (t1)
    srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[1].NumDescriptors = 1;
    srvRanges[1].BaseShaderRegister = 1;
    srvRanges[1].RegisterSpace = 0;
    srvRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // SRV для Depth (t2)
    srvRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRanges[2].NumDescriptors = 1;
    srvRanges[2].BaseShaderRegister = 2;
    srvRanges[2].RegisterSpace = 0;
    srvRanges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[3] = {};

    // Root Parameter 0: Descriptor table для G-buffer SRVs
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 3;
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
    D3D12_STATIC_SAMPLER_DESC sampler = {};
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

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 3;
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

    return true;
}

void RenderingSystem::GeometryPass(
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
    ID3D12Resource* secondaryTexture)
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

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    mCommandList->IASetIndexBuffer(&indexBufferView);

    for (auto& sm : submeshes)
    {
        const Material* mat = nullptr;
        for (auto& m : materials)
        {
            if (m.Name == sm.MaterialName)
            {
                mat = &m;
                break;
            }
        }

        if (!mat) continue;

        // Root Parameter 1: SRV for main texture
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle1 =
            cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle1.ptr += (1 + mat->SrvHeapIndex) * cbvSrvDescriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(1, srvHandle1);

        // Root Parameter 2: SRV for secondary texture
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle2 =
            cbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
        srvHandle2.ptr += (1 + materialCount) * cbvSrvDescriptorSize;

        // Проверяем, нужно ли использовать secondary texture
        bool isFloor = (mat->Name.find("floor") != std::string::npos);
        mCommandList->SetGraphicsRootDescriptorTable(2, isFloor ? srvHandle2 : srvHandle1);

        const float isFlag = IsAnimatedFlagMaterial(*mat) ? 1.0f : 0.0f;
        mCommandList->SetGraphicsRoot32BitConstant(3, *reinterpret_cast<const UINT*>(&isFlag), 0);

        mCommandList->DrawIndexedInstanced(sm.IndexCount, 1, sm.IndexStart, 0, 0);
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
    ID3D12DescriptorHeap* heaps[] = { gBuffer->mSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootDescriptorTable(0, gBuffer->mSrvHeap->GetGPUDescriptorHandleForHeapStart());

    D3D12_GPU_VIRTUAL_ADDRESS cameraAddr = cameraCB->Resource()->GetGPUVirtualAddress();
    mCommandList->SetGraphicsRootConstantBufferView(2, cameraAddr);

    D3D12_GPU_VIRTUAL_ADDRESS baseAddr = lightingCB->Resource()->GetGPUVirtualAddress();
    UINT elementSize = lightingCB->GetElementSize();

    if (cameraCB)
    {
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddr = cameraCB->Resource()->GetGPUVirtualAddress();
        mCommandList->SetGraphicsRootConstantBufferView(2, cameraAddr);
    }

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
