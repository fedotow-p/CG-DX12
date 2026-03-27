#include "../h/DirectXApp.h"
#include <DirectXMath.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <string>
#include "../h/ThrowIfFailed.h"
#include "../h/Parser.h"
#include "../h/TgaLoader.h"
#include "../h/d3dUtil.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

struct CD3DX12_RESOURCE_BARRIER_HELPER {
    static D3D12_RESOURCE_BARRIER Transition(
        _In_ ID3D12Resource* pResource,
        D3D12_RESOURCE_STATES stateBefore,
        D3D12_RESOURCE_STATES stateAfter,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = pResource;
        barrier.Transition.StateBefore = stateBefore;
        barrier.Transition.StateAfter = stateAfter;
        barrier.Transition.Subresource = subresource;
        return barrier;
    }
};

struct CD3DX12_DEFAULT {};
extern const DECLSPEC_SELECTANY CD3DX12_DEFAULT D3D12_DEFAULT;

struct CD3DX12_RASTERIZER_DESC : public D3D12_RASTERIZER_DESC
{
    CD3DX12_RASTERIZER_DESC() = default;
    explicit CD3DX12_RASTERIZER_DESC(const D3D12_RASTERIZER_DESC& o) : D3D12_RASTERIZER_DESC(o) {}
    explicit CD3DX12_RASTERIZER_DESC(CD3DX12_DEFAULT)
    {
        FillMode = D3D12_FILL_MODE_SOLID;
        CullMode = D3D12_CULL_MODE_BACK;
        FrontCounterClockwise = FALSE;
        DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        DepthClipEnable = TRUE;
        MultisampleEnable = FALSE;
        AntialiasedLineEnable = FALSE;
        ForcedSampleCount = 0;
        ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    }
};

struct CD3DX12_BLEND_DESC : public D3D12_BLEND_DESC
{
    CD3DX12_BLEND_DESC() = default;
    explicit CD3DX12_BLEND_DESC(const D3D12_BLEND_DESC& o) : D3D12_BLEND_DESC(o) {}
    explicit CD3DX12_BLEND_DESC(CD3DX12_DEFAULT)
    {
        AlphaToCoverageEnable = FALSE;
        IndependentBlendEnable = FALSE;
        const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
        {
            FALSE,FALSE,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP,
            D3D12_COLOR_WRITE_ENABLE_ALL,
        };
        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
            RenderTarget[i] = defaultRenderTargetBlendDesc;
    }
};

struct CD3DX12_DEPTH_STENCIL_DESC : public D3D12_DEPTH_STENCIL_DESC
{
    CD3DX12_DEPTH_STENCIL_DESC() = default;
    explicit CD3DX12_DEPTH_STENCIL_DESC(const D3D12_DEPTH_STENCIL_DESC& o) : D3D12_DEPTH_STENCIL_DESC(o) {}
    explicit CD3DX12_DEPTH_STENCIL_DESC(CD3DX12_DEFAULT)
    {
        DepthEnable = TRUE;
        DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        StencilEnable = FALSE;
        StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp =
        { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS };
        FrontFace = defaultStencilOp;
        BackFace = defaultStencilOp;
    }
};

DirectXApp::DirectXApp(Window& window) : window(window)
{
    XMStoreFloat4x4(&mWorld, XMMatrixIdentity());
    XMStoreFloat4x4(&mView, XMMatrixIdentity());
    XMStoreFloat4x4(&mProj, XMMatrixIdentity());
}

DirectXApp::~DirectXApp() {
    Shutdown();
}

// =========== Mouse Metod ==========
void DirectXApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(window.GetHwnd());
}

void DirectXApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void DirectXApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if (btnState & MK_RBUTTON)
    {
        float sensitivity = 0.005f;

        float dx = (x - mLastMousePos.x) * sensitivity;
        float dy = (y - mLastMousePos.y) * sensitivity;

        mYaw += dx;
        mPitch += dy;

        if (mPitch > XM_PIDIV2 - 0.1f)
            mPitch = XM_PIDIV2 - 0.1f;

        if (mPitch < -XM_PIDIV2 + 0.1f)
            mPitch = -XM_PIDIV2 + 0.1f;
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

// =========== Input Layout ===========
void DirectXApp::BuildInputLayout()
{
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

// =========== Shader ===========
void DirectXApp::BuildShaders()
{
    mvsByteCode = d3dUtil::CompileShader(
        L"../src/shaders.hlsl",
        nullptr,
        "VS",
        "vs_5_0"
    );

    mpsByteCode = d3dUtil::CompileShader(
        L"../src/shaders.hlsl",
        nullptr,
        "PS",
        "ps_5_0"
    );

    MessageBox(NULL, L"SUCCESS! Shaders compiled", L"Info", MB_OK);
}

// =========== CBV ===========
void DirectXApp::BuildConstantBuffer()
{
    // Upload Buffer
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(
        device.Get(),
        1,
        true
    );

    // Initialization of matrix
    ObjectConstants objConstants;
    XMMATRIX view = XMMatrixIdentity();
    XMMATRIX proj = XMMatrixOrthographicLH(10.0f, 10.0f, 0.1f, 100.0f);
    XMMATRIX viewProj = view * proj;
    XMStoreFloat4x4(&objConstants.mWorldViewProj, XMMatrixTranspose(viewProj));

    // Initialization UV transform
    objConstants.mUVTransform = XMFLOAT4(2.0f, 2.0f, 0.0f, 0.0f); // scale 2x для тайлинга

    mObjectCB->CopyData(0, objConstants);

    // Make CBV (Constant Buffer View) in a heap of descriptors
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = objCBByteSize;

    // Getting descriptors from CBV heap
    D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle = mCbvHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateConstantBufferView(&cbvDesc, cbvHandle);

    MessageBox(NULL, L"Constant buffer and CBV created", L"Info", MB_OK);
}

// =========== Root Signature ===========
void DirectXApp::BuildRootSignature()
{
    // ===== CBV range (b0)
    D3D12_DESCRIPTOR_RANGE cbvRange = {};
    cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    cbvRange.NumDescriptors = 1;
    cbvRange.BaseShaderRegister = 0;
    cbvRange.RegisterSpace = 0;
    cbvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // ===== SRV range (t0)
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2];

    // Slot 0 → CBV
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[0].DescriptorTable.pDescriptorRanges = &cbvRange;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Slot 1 → SRV
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // ===== Static Sampler (s0)
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParameters;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &sampler;
    rootSigDesc.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;

    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf()));

    ThrowIfFailed(device->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)));
}

// =========== PSO (Pipeline State Object) ===========
void DirectXApp::BuildPSO()
{
    // Making description PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

    // VS shader
    psoDesc.VS = {
        reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize()
    };
    // PS shader
    psoDesc.PS = {
        reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
        mpsByteCode->GetBufferSize()
    };

    // 2. Input Layout
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };

    // 3. Root signature
    psoDesc.pRootSignature = mRootSignature.Get();

    // 4. Растеризатор (используем CD3DX12_RASTERIZER_DESC как на слайде)
    D3D12_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    rasterDesc.DepthClipEnable = TRUE;

    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    // 5. Blend State (как на слайде)
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;

    auto& rtBlend = blendDesc.RenderTarget[0];
    rtBlend.BlendEnable = TRUE;
    rtBlend.LogicOpEnable = FALSE;
    rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    // 6. Depth/Stencil State (как на слайде)
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    // 7. Sample Mask
    psoDesc.SampleMask = UINT_MAX;

    // 8. Примитивы
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // 9. Render Targets
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mBackBufferFormat;

    // 10. Формат Depth/Stencil
    psoDesc.DSVFormat = mDepthStencilFormat;

    // 11. Multisampling
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    // 12. Создание PSO
    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create PSO", L"Error", MB_OK);
        return;
    }

    MessageBox(NULL, L"PSO created successfully (Solid Mode)", L"Info", MB_OK);
}

// =========== Wireframe PSO ===========
void DirectXApp::BuildWireframePSO()
{
    // Создаем описание PSO для проволочного каркаса
    D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePsoDesc;
    ZeroMemory(&wireframePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

    // 1. Шейдеры (те же самые)
    wireframePsoDesc.VS = {
        reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
        mvsByteCode->GetBufferSize()
    };
    wireframePsoDesc.PS = {
        reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
        mpsByteCode->GetBufferSize()
    };

    // 2. Input Layout
    wireframePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };

    // 3. Корневая сигнатура
    wireframePsoDesc.pRootSignature = mRootSignature.Get();

    // 4. Растеризатор - НАСТРОЙКА ДЛЯ ПРОВОЛОЧНОГО КАРКАСА
    wireframePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    wireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;  // ПРОВОЛОЧНЫЙ КАРКАС
    wireframePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;       // БЕЗ ОБРЕЗКИ

    // 5. Blend State
    wireframePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    // 6. Depth/Stencil State
    wireframePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    // 7. Sample Mask
    wireframePsoDesc.SampleMask = UINT_MAX;

    // 8. Примитивы
    wireframePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    // 9. Render Targets
    wireframePsoDesc.NumRenderTargets = 1;
    wireframePsoDesc.RTVFormats[0] = mBackBufferFormat;

    // 10. Формат Depth/Stencil
    wireframePsoDesc.DSVFormat = mDepthStencilFormat;

    // 11. Multisampling
    wireframePsoDesc.SampleDesc.Count = 1;
    wireframePsoDesc.SampleDesc.Quality = 0;

    // 12. Создание PSO
    HRESULT hr = device->CreateGraphicsPipelineState(&wireframePsoDesc, IID_PPV_ARGS(&mWireframePSO));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create Wireframe PSO", L"Error", MB_OK);
        return;
    }

    MessageBox(NULL, L"Wireframe PSO created successfully", L"Info", MB_OK);
}
// =========== Остальные методы ===========
void DirectXApp::BuildObj(const std::string& path)
{
    MessageBoxA(nullptr, "BuildObj called", "DEBUG", MB_OK);

    // Очистить старые данные
    mSubmeshes.clear();

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Загружаем OBJ с сабмешами
    if (!LoadOBJ(path, vertices, indices, mSubmeshes))
    {
        MessageBoxA(nullptr, "Failed to load OBJ", "Error", MB_OK);
        return;
    }

    mIndexCount = static_cast<UINT>(indices.size());

    UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
    UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(uint32_t));

    // ====================================================
    //                VERTEX BUFFER
    // ====================================================

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = vbByteSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mVertexBufferGPU)));

    void* mappedData = nullptr;
    mVertexBufferGPU->Map(0, nullptr, &mappedData);
    memcpy(mappedData, vertices.data(), vbByteSize);
    mVertexBufferGPU->Unmap(0, nullptr);

    mVertexBufferView.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
    mVertexBufferView.StrideInBytes = sizeof(Vertex);
    mVertexBufferView.SizeInBytes = vbByteSize;

    // ====================================================
    //                INDEX BUFFER
    // ====================================================

    D3D12_RESOURCE_DESC ibDesc = {};
    ibDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    ibDesc.Width = ibByteSize;
    ibDesc.Height = 1;
    ibDesc.DepthOrArraySize = 1;
    ibDesc.MipLevels = 1;
    ibDesc.SampleDesc.Count = 1;
    ibDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mIndexBufferGPU)));

    mIndexBufferGPU->Map(0, nullptr, &mappedData);
    memcpy(mappedData, indices.data(), ibByteSize);
    mIndexBufferGPU->Unmap(0, nullptr);

    mIndexBufferView.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
    mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
    mIndexBufferView.SizeInBytes = ibByteSize;
}

void DirectXApp::Shutdown() {
    FlushCommandQueue();

    // Освобождаем PSO
    mPSO.Reset();
    mWireframePSO.Reset();
    mRootSignature.Reset();

    for (int i = 0; i < SwapChainBufferCount; i++) {
        mSwapChainBuffer[i].Reset();
    }
    mDepthStencilBuffer.Reset();
    mRtvHeap.Reset();
    mDsvHeap.Reset();
    mCbvHeap.Reset();
    mSwapChain.Reset();

    mVertexBufferGPU.Reset();
    mVertexBufferUploader.Reset();
    mIndexBufferGPU.Reset();
    mIndexBufferUploader.Reset();

    if (mCommandList) {
        mCommandList.Reset();
    }

    mFence.Reset();
    mDirectCmdListAlloc.Reset();
    mCommandQueue.Reset();
    device.Reset();
    adapter.Reset();
    dxgiFactory.Reset();
}

bool DirectXApp::CreateDXGIFactory() {
    UINT factoryFlags = 0;
    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) {
        MessageBox(NULL, L"CreateDXGIFactory2 failed", L"Error", MB_OK);
        return false;
    }
    return true;
}

bool DirectXApp::GetHardwareAdapter() {
    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(dxgiFactory.As(&factory6))) {
        for (UINT adapterIndex = 0; ; ++adapterIndex) {
            ComPtr<IDXGIAdapter1> currentAdapter;
            HRESULT hr = factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                IID_PPV_ARGS(&currentAdapter));

            if (FAILED(hr)) break;

            DXGI_ADAPTER_DESC1 desc;
            currentAdapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

            if (SUCCEEDED(D3D12CreateDevice(currentAdapter.Get(),
                D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr))) {
                adapter = currentAdapter;
                return true;
            }
        }
    }
    return false;
}

bool DirectXApp::CreateD3DDevice() {
    if (!GetHardwareAdapter()) {
        HRESULT hr = dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        if (FAILED(hr)) {
            MessageBox(NULL, L"No hardware adapter found and WARP failed", L"Error", MB_OK);
            return false;
        }
        MessageBox(NULL, L"Using WARP software adapter", L"Info", MB_OK);
    }

    HRESULT hr = D3D12CreateDevice(
        adapter.Get(),
        D3D_FEATURE_LEVEL_12_0,
        IID_PPV_ARGS(&device)
    );

    if (FAILED(hr)) {
        MessageBox(NULL, L"D3D12CreateDevice failed", L"Error", MB_OK);
        return false;
    }

    return true;
}

bool DirectXApp::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create command queue", L"Error", MB_OK);
        return false;
    }

    hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&mDirectCmdListAlloc)
    );
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create command allocator", L"Error", MB_OK);
        return false;
    }

    hr = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        mDirectCmdListAlloc.Get(),
        nullptr,
        IID_PPV_ARGS(&mCommandList)
    );
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create command list", L"Error", MB_OK);
        return false;
    }

    mCommandList->Close();
    return true;
}

bool DirectXApp::CreateFence() {
    HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create fence", L"Error", MB_OK);
        return false;
    }
    mFenceValue = 0;
    return true;
}

void DirectXApp::FlushCommandQueue() {
    mFenceValue++;
    mCommandQueue->Signal(mFence.Get(), mFenceValue);

    if (mFence->GetCompletedValue() < mFenceValue) {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        mFence->SetEventOnCompletion(mFenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

bool DirectXApp::CreateSwapChain() {
    RECT clientRect;
    GetClientRect(window.GetHandle(), &clientRect);
    mClientWidth = clientRect.right - clientRect.left;
    mClientHeight = clientRect.bottom - clientRect.top;

    mSwapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = mClientWidth;
    sd.BufferDesc.Height = mClientHeight;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = mBackBufferFormat;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SwapChainBufferCount;
    sd.OutputWindow = window.GetHandle();
    sd.Windowed = true;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    HRESULT hr = dxgiFactory->CreateSwapChain(
        mCommandQueue.Get(),
        &sd,
        &mSwapChain
    );

    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create swap chain", L"Error", MB_OK);
        return false;
    }

    return true;
}

void DirectXApp::QueryDescriptorSizes() {
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

bool DirectXApp::CreateDescriptorHeaps() {
    // 1. RTV куча
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;

    HRESULT hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create RTV descriptor heap", L"Error", MB_OK);
        return false;
    }

    // 2. DSV куча
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;

    hr = device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create DSV descriptor heap", L"Error", MB_OK);
        return false;
    }

    // 3. CBV/SRV/UAV куча
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = 1 + 200; // 1 CBV + 1 SRV
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;

    hr = device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap));
    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create CBV descriptor heap", L"Error", MB_OK);
        return false;
    }

    return true;
}

bool DirectXApp::CreateRenderTargetViews() {
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < SwapChainBufferCount; i++) {
        ComPtr<ID3D12Resource> backBuffer;
        HRESULT hr = mSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) {
            MessageBox(NULL, L"Failed to get swap chain buffer", L"Error", MB_OK);
            return false;
        }

        mSwapChainBuffer[i] = backBuffer;
        device->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.ptr += mRtvDescriptorSize;
    }

    return true;
}

bool DirectXApp::CreateDepthStencilBuffer() {
    D3D12_RESOURCE_DESC depthStencilDesc = {};
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Width = mClientWidth;
    depthStencilDesc.Height = mClientHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = mDepthStencilFormat;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear = {};
    optClear.Format = mDepthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(&mDepthStencilBuffer)
    );

    if (FAILED(hr)) {
        MessageBox(NULL, L"Failed to create depth stencil buffer", L"Error", MB_OK);
        return false;
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = mDepthStencilFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    device->CreateDepthStencilView(
        mDepthStencilBuffer.Get(),
        &dsvDesc,
        mDsvHeap->GetCPUDescriptorHandleForHeapStart()
    );

    return true;
}

void DirectXApp::CreateViewportAndScissor() {
    mScreenViewport.TopLeftX = 0.0f;
    mScreenViewport.TopLeftY = 0.0f;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f;
    mScreenViewport.MaxDepth = 1.0f;

    mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}

void DirectXApp::SetViewportAndScissor() {
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);
}

bool DirectXApp::Initialize() {
    #if defined(_DEBUG)
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
            {
                debugController->EnableDebugLayer();
            }
        }
    #endif
    MessageBox(NULL, L"Starting DirectX 12 initialization...", L"Info", MB_OK);

    // Основные этапы инициализации
    if (!CreateDXGIFactory()) return false;
    if (!CreateD3DDevice()) return false;
    if (!CreateCommandObjects()) return false;
    if (!CreateFence()) return false;
    if (!CreateSwapChain()) return false;

    QueryDescriptorSizes();

    if (!CreateDescriptorHeaps()) return false;
    if (!CreateRenderTargetViews()) return false;
    if (!CreateDepthStencilBuffer()) return false;

    CreateViewportAndScissor();

   //Геометрия и ресурсы
    BuildInputLayout();
   //BuildVertexBuffer();
   //BuildIndexBuffer();
    BuildObj("../assets/sponza.obj");
    std::vector<ParsedMaterial> parsed;
    LoadMTL("../assets/sponza.mtl", parsed);

    UINT srvIndex = 0;

    for (auto& p : parsed)
    {
        Material mat;
        mat.Name = p.Name;
        mat.SrvHeapIndex = srvIndex++;

        if (!p.DiffuseMap.empty())
        {
            CreateTextureFromTGA(
                "../assets/" + p.DiffuseMap,
                mat.DiffuseTexture);
        }
        else
        {
            CreateColorTexture(p.Kd, mat.DiffuseTexture);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE hDescriptor =
            mCbvHeap->GetCPUDescriptorHandleForHeapStart();

        hDescriptor.ptr += (1 + mat.SrvHeapIndex) * mCbvSrvUavDescriptorSize;

        device->CreateShaderResourceView(
            mat.DiffuseTexture.Get(),
            &srvDesc,
            hDescriptor);

        mMaterials.push_back(mat);
    }
    BuildRootSignature();
    BuildShaders();
    BuildPSO();
    BuildWireframePSO();  
    BuildConstantBuffer();

    // Инициализация проекционной матрицы
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * XM_PI,
        (float)mClientWidth / (float)mClientHeight, 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);

    mTimer.Reset();
    return true;
}

bool DirectXApp::InitializeApp() {
    return Initialize();
}

ID3D12Resource* DirectXApp::CurrentBackBuffer() const {
    return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXApp::CurrentBackBufferView() const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += mCurrBackBuffer * mRtvDescriptorSize;
    return handle;
}

void DirectXApp::OnResize() {
    // TODO: реализовать позже
}

// Обработка клавиатуры
void DirectXApp::OnKeyDown(WPARAM wParam)
{
    // Проверяем, активное ли наше окно
    HWND activeWindow = GetActiveWindow();
    if (activeWindow != window.GetHwnd()) {
        OutputDebugStringA("Window not active!\n");
        return;
    }
    // Пробел переключает режим отображения
    if (wParam == VK_F2) {
        mWireframeMode = !mWireframeMode;

        if (mWireframeMode) {
            SetWindowText(window.GetHandle(), L"DirectX 12 Framework - Wireframe Mode (Press SPACE to switch)");
        }
        else {
            SetWindowText(window.GetHandle(), L"DirectX 12 Framework - Solid Mode (Press SPACE to switch)");
        }
    }

    // Клавиша T включает/выключает анимацию текстур
    if (wParam == 'T') {
        mAnimateTextures = !mAnimateTextures;
    }

    // Клавиша R сбрасывает UV параметры
    if (wParam == 'R') {
        mUVScaleU = 1.0f;
        mUVScaleV = 1.0f;
        mUVOffsetU = 0.0f;
        mUVOffsetV = 0.0f;
    }
}

int DirectXApp::Run() {
    MSG msg = { 0 };
    mTimer.Reset();

    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            mTimer.Tick();
            if (!mAppPaused) {
                CalculateFrameStats();
                Update(mTimer);
                Draw(mTimer);
            }
            else {
                Sleep(100);
            }
        }
    }
    return (int)msg.wParam;
}

void DirectXApp::CalculateFrameStats() {
    mFrameCount++;
    if ((mTimer.TotalTime() - mTimeElapsed) >= 1.0f) {
        float fps = (float)mFrameCount;
        float mspf = 1000.0f / fps;

        std::wstring windowText = mMainWndCaption;
        if (mWireframeMode) {
            windowText += L" - Wireframe Mode";
        }
        else {
            windowText += L" - Solid Mode";
        }
        windowText += L" FPS: " + std::to_wstring(fps);
        windowText += L" MSPF: " + std::to_wstring(mspf);
        windowText += L" (Press SPACE to switch modes)";

        SetWindowText(window.GetHandle(), windowText.c_str());

        mFrameCount = 0;
        mTimeElapsed += 1.0f;
    }
}

void DirectXApp::Update(const Timer& gt)
{
    float dt = gt.DeltaTime();
    float speed = 50.0f;

    // ===== Forward Vector =====
    XMFLOAT3 forward =
    {
        cosf(mPitch) * cosf(mYaw),
        sinf(mPitch),
        cosf(mPitch) * sinf(mYaw)
    };

    XMVECTOR forwardVec = XMLoadFloat3(&forward);
    forwardVec = XMVector3Normalize(forwardVec);

    XMVECTOR rightVec = XMVector3Normalize(
        XMVector3Cross(
            XMVectorSet(0, 1, 0, 0),
            forwardVec));

    // ===== Movement =====
    XMVECTOR pos = XMLoadFloat3(&mEyePos);

    if (GetAsyncKeyState('W') & 0x8000)
        pos += forwardVec * speed * dt;

    if (GetAsyncKeyState('S') & 0x8000)
        pos -= forwardVec * speed * dt;

    if (GetAsyncKeyState('A') & 0x8000)
        pos -= rightVec * speed * dt;

    if (GetAsyncKeyState('D') & 0x8000)
        pos += rightVec * speed * dt;

    if (GetAsyncKeyState(VK_UP) & 0x8000)
        pos += XMVectorSet(0, 1, 0, 0) * speed * dt;

    if (GetAsyncKeyState(VK_DOWN) & 0x8000)
        pos -= XMVectorSet(0, 1, 0, 0) * speed * dt;

    XMStoreFloat3(&mEyePos, pos);

    // ===== View Matrix =====
    XMVECTOR target = pos + forwardVec;
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);

    // ===== Projection =====
    XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XM_PIDIV4,
        (float)mClientWidth / (float)mClientHeight,
        0.1f,
        1000.0f);

    XMStoreFloat4x4(&mProj, proj);

    // ===== TEXTURE ANIMATION =====
    if (mAnimateTextures)
    {
        // Анимация: UV смещение меняется со временем
        mUVOffsetU += dt * 0.1f;  // Медленный сдвиг по U
        mUVOffsetV += dt * 0.05f; // Медленный сдвиг по V

        // Зацикливаем, чтобы не уходило в бесконечность
        if (mUVOffsetU > 1.0f) mUVOffsetU -= 1.0f;
        if (mUVOffsetV > 1.0f) mUVOffsetV -= 1.0f;
    }

    // Управление тайлингом с клавиатуры
    if (GetAsyncKeyState('1') & 0x8000) mUVScaleU += dt * 2.0f;
    if (GetAsyncKeyState('2') & 0x8000) mUVScaleU -= dt * 2.0f;
    if (GetAsyncKeyState('3') & 0x8000) mUVScaleV += dt * 2.0f;
    if (GetAsyncKeyState('4') & 0x8000) mUVScaleV -= dt * 2.0f;

    // Ограничения
    mUVScaleU = max(0.1f, mUVScaleU);
    mUVScaleV = max(0.1f, mUVScaleV);

    // ===== WVP и UV Transform =====
    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX worldViewProj = world * view * proj;

    ObjectConstants objConstants;
    XMStoreFloat4x4(&objConstants.mWorldViewProj,
        XMMatrixTranspose(worldViewProj));

    // Устанавливаем UV transform для тайлинга и анимации
    objConstants.mUVTransform.x = mUVScaleU;  // scaleU
    objConstants.mUVTransform.y = mUVScaleV;  // scaleV
    objConstants.mUVTransform.z = mUVOffsetU; // offsetU
    objConstants.mUVTransform.w = mUVOffsetV; // offsetV

    mObjectCB->CopyData(0, objConstants);
}

void DirectXApp::Draw(const Timer& gt)
{
    if (mIndexCount == 0)
        return;

    mDirectCmdListAlloc->Reset();

    if (mWireframeMode)
        mCommandList->Reset(mDirectCmdListAlloc.Get(), mWireframePSO.Get());
    else
        mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get());

    D3D12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER_HELPER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

    mCommandList->ResourceBarrier(1, &barrier);

    SetViewportAndScissor();

    const float clearColor[] = { 0.53f, 0.81f, 0.98f, 1.0f };

    auto rtvHandle = CurrentBackBufferView();
    auto dsvHandle = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

    mCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        dsvHandle,
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f,
        0,
        0,
        nullptr);

    mCommandList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, heaps);

    // CBV (b0)
    mCommandList->SetGraphicsRootDescriptorTable(
        0,
        mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
    mCommandList->IASetIndexBuffer(&mIndexBufferView);

    for (auto& sm : mSubmeshes)
    {
        // 🔎 Найти материал
        Material* mat = nullptr;

        for (auto& m : mMaterials)
        {
            if (m.Name == sm.MaterialName)
            {
                mat = &m;
                break;
            }
        }

        if (!mat)
        {
            MessageBoxA(nullptr, sm.MaterialName.c_str(), "Missing Material", MB_OK);
            continue;
        }

        //if (mat->DiffuseMap.empty())
        //{
        //    MessageBoxA(nullptr, mat->Name.c_str(), "NO TEXTURE", MB_OK);
        //}

        // 📌 Установить SRV конкретного материала
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle =
            mCbvHeap->GetGPUDescriptorHandleForHeapStart();

        srvHandle.ptr += (1 + mat->SrvHeapIndex) * mCbvSrvUavDescriptorSize;

        mCommandList->SetGraphicsRootDescriptorTable(1, srvHandle);

        // 🎨 Нарисовать только этот submesh
        mCommandList->DrawIndexedInstanced(
            sm.IndexCount,
            1,
            sm.IndexStart,
            0,
            0);
    }

    // === PRESENT ===

    barrier =
        CD3DX12_RESOURCE_BARRIER_HELPER::Transition(
            CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);

    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    mSwapChain->Present(0, 0);
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}

void DirectXApp::CreateTextureFromTGA(
    const std::string& path,
    Microsoft::WRL::ComPtr<ID3D12Resource>& texture)
{
    TgaImage image;
    if (!LoadTGA(path, image))
    {
        throw std::runtime_error("Failed to load TGA: " + path);
    }

    // ===== FIX RGB → RGBA =====
    UINT pixelSize = image.data.size() / (image.width * image.height);

    if (pixelSize == 3)
    {
        std::vector<uint8_t> converted;
        converted.resize(image.width * image.height * 4);

        for (UINT i = 0; i < image.width * image.height; i++)
        {
            converted[i * 4 + 0] = image.data[i * 3 + 0];
            converted[i * 4 + 1] = image.data[i * 3 + 1];
            converted[i * 4 + 2] = image.data[i * 3 + 2];
            converted[i * 4 + 3] = 255;
        }

        image.data = std::move(converted);
        pixelSize = 4;
    }

    // ===== TEXTURE RESOURCE =====
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = image.width;
    texDesc.Height = image.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)));

    // ===== UPLOAD BUFFER =====
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(
        &texDesc, 0, 1, 0,
        nullptr, nullptr, nullptr,
        &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    // ===== COPY DATA =====
    void* mapped = nullptr;
    uploadBuffer->Map(0, nullptr, &mapped);

    BYTE* dest = reinterpret_cast<BYTE*>(mapped);
    BYTE* srcData = image.data.data();

    UINT rowPitch = (image.width * 4 + 255) & ~255;

    for (UINT y = 0; y < image.height; y++)
    {
        memcpy(
            dest + y * rowPitch,
            srcData + y * image.width * 4,
            image.width * 4);
    }

    uploadBuffer->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

    device->GetCopyableFootprints(
        &texDesc, 0, 1, 0,
        &src.PlacedFootprint,
        nullptr, nullptr, nullptr);

    mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr);
    mCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    mCommandList->ResourceBarrier(1, &barrier);
    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    FlushCommandQueue();
}

void DirectXApp::CreateColorTexture(
    const DirectX::XMFLOAT3& color,
    Microsoft::WRL::ComPtr<ID3D12Resource>& texture)
{
    UINT r = (UINT)(color.x * 255.0f);
    UINT g = (UINT)(color.y * 255.0f);
    UINT b = (UINT)(color.z * 255.0f);

    UINT pixel = (255 << 24) | (b << 16) | (g << 8) | r;

    // ---- TEXTURE (DEFAULT heap) ----
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)));

    // ---- UPLOAD BUFFER ----
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(
        &texDesc, 0, 1, 0,
        nullptr, nullptr, nullptr,
        &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.SampleDesc.Count = 1;

    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    // ---- MAP ----
    void* mapped = nullptr;
    uploadBuffer->Map(0, nullptr, &mapped);
    memcpy(mapped, &pixel, sizeof(UINT));
    uploadBuffer->Unmap(0, nullptr);

    // ---- COPY ----
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;

    device->GetCopyableFootprints(
        &texDesc, 0, 1, 0,
        &src.PlacedFootprint,
        nullptr, nullptr, nullptr);

    mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr);

    mCommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    mCommandList->ResourceBarrier(1, &barrier);

    mCommandList->Close();

    ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    FlushCommandQueue();
}