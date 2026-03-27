//
// Created by elm on 27.03.2026.
//
#include "DirectXApp.h"



#pragma comment( lib, "d3d12.lib" )
#pragma comment( lib, "dxgi.lib" )

DirectXApp::DirectXApp(Window& window) : window(window) {}

DirectXApp::~DirectXApp() {
    FlushCommandQueue();
}

bool DirectXApp::Initialize() {
    #if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
        }
    }
    #endif

    if (!CreateDXGIFactory()) return false;
    if (!CreateDevice()) return false;
    if (!CreateCommandObjects()) return false;
    if (!CreateSwapChain()) return false;
    if (!CreateFence()) return false;
    if (!CreateRTVHeap()) return false;
    if (!CreateRenderTargetViews()) return false;

    BuildVertexBuffer();
    BuildRootSignature();
    BuildPipelineState();

    return true;
}

bool DirectXApp::CreateDXGIFactory() {
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) return false;
    return true;
}

bool DirectXApp::CreateDevice() {
    for (UINT i=0; ;i++) {
        ComPtr<IDXGIAdapter1> currentAdapter;
        HRESULT hr = dxgiFactory->EnumAdapters1(i, &currentAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;

        DXGI_ADAPTER_DESC1 desc;
        currentAdapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        if (SUCCEEDED(D3D12CreateDevice(currentAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
            adapter = currentAdapter;
            return true;
        }
    }

    HRESULT hr = dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
    if (FAILED(hr)) return false;

    hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    return SUCCEEDED(hr);
}

bool DirectXApp::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(hr)) return false;

    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&commandAllocator));
    if (FAILED(hr)) return false;

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
    if (FAILED(hr)) return false;

    commandList->Close();
    return true;
}

bool DirectXApp::CreateSwapChain() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = window.GetWidth();
    sd.BufferDesc.Height = window.GetHeight();
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = BufferCount;
    sd.OutputWindow = window.GetHandle();
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    HRESULT hr = dxgiFactory->CreateSwapChain(commandQueue.Get(), &sd, &swapChain);
    return SUCCEEDED(hr);
}

bool DirectXApp::CreateFence() {
    HRESULT hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    return SUCCEEDED(hr);
}

bool DirectXApp::CreateRTVHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = BufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    HRESULT hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap));
    if (FAILED(hr)) return false;

    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    return true;
}

bool DirectXApp::CreateRenderTargetViews() {
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < BufferCount; i++) {
        HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainBuffer[i]));
        if (FAILED(hr)) return false;

        device->CreateRenderTargetView(swapChainBuffer[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize;
    }

    return true;
}

void DirectXApp::FlushCommandQueue() {
    fenceValue++;
    commandQueue->Signal(fence.Get(), fenceValue);

    if (fence->GetCompletedValue() < fenceValue) {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        fence->SetEventOnCompletion(fenceValue, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

void DirectXApp::BuildVertexBuffer() {
    Vertex vertices[] = {
        {DirectX::XMFLOAT3(0.0f, 0.5f, 0.0f), DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f)},
        {DirectX::XMFLOAT3(0.5f, -0.5f, 0.0f), DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) },
        {DirectX::XMFLOAT3(-0.5f, -0.5f, 0.0f), DirectX::XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f) }
    };

    UINT vertexBufferSize = sizeof(vertices);

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = vertexBufferSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertexBuffer)));

    void* mappedData;
    vertexBuffer->Map(0, nullptr, &mappedData);
    memcpy(mappedData, vertices, vertexBufferSize);
    vertexBuffer->Unmap(0, nullptr);

    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = sizeof(Vertex);
    vertexBufferView.SizeInBytes = vertexBufferSize;
}

void DirectXApp::BuildRootSignature() {
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 0;
    rootSigDesc.pParameters = nullptr;
    rootSigDesc.NumStaticSamplers = 0;
    rootSigDesc.pStaticSamplers = nullptr;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1, &serializedRootSig, &errorBlob);
    ThrowIfFailed(hr);

    hr = device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&rootSignature));
    ThrowIfFailed(hr);
}

void DirectXApp::BuildShaders() {

}

void DirectXApp::BuildPipelineState() {
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> errorBlob;

    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

    const char* vsCode =
        "struct VSInput {"
        "    float3 position : POSITION;"
        "    float4 color : COLOR;"
        "};"
        "struct PSInput {"
        "    float4 position : SV_POSITION;"
        "    float4 color : COLOR;"
        "};"
        "PSInput VS(VSInput input) {"
        "    PSInput output;"
        "    output.position = float4(input.position, 1.0);"
        "    output.color = input.color;"
        "    return output;"
        "}";

    HRESULT hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr,
        "VS", "vs_5_0", compileFlags, 0, &vertexShader, &errorBlob);
    if (FAILED(hr)) {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        ThrowIfFailed(hr);
    }

    const char* psCode =
        "struct PSInput {"
        "    float4 position : SV_POSITION;"
        "    float4 color : COLOR;"
        "};"
        "float4 PS(PSInput input) : SV_TARGET {"
        "    return input.color;"
        "}";

    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr,
        "PS", "ps_5_0", compileFlags, 0, &pixelShader, &errorBlob);
    if (FAILED(hr)) {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
        ThrowIfFailed(hr);
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.InputLayout = { inputLayout, 2 };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr);
}

void DirectXApp::Update() {

}

void DirectXApp::Draw() {
    commandAllocator->Reset();
    commandList->Reset(commandAllocator.Get(), pipelineState.Get());

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = swapChainBuffer[currentBackBuffer].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    commandList->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT viewport = { 0, 0, (float)window.GetWidth(), (float)window.GetHeight(), 0, 1 };
    D3D12_RECT scissorRect = { 0, 0, window.GetWidth(), window.GetHeight() };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += currentBackBuffer * rtvDescriptorSize;

    float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    commandList->OMSetRenderTargets(1, &rtvHandle, true, nullptr);

    commandList->SetGraphicsRootSignature(rootSignature.Get());

    commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    commandList->DrawInstanced(3, 1, 0, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList->ResourceBarrier(1, &barrier);

    commandList->Close();

    ID3D12CommandList* cmdLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(1, cmdLists);

    swapChain->Present(1, 0);
    currentBackBuffer = (currentBackBuffer + 1) % BufferCount;

    FlushCommandQueue();
}

void DirectXApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(window.GetHandle());
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

        if (mPitch > DirectX::XM_PIDIV2 - 0.1f)
            mPitch = DirectX::XM_PIDIV2 - 0.1f;
        if (mPitch < -DirectX::XM_PIDIV2 + 0.1f)
            mPitch = -DirectX::XM_PIDIV2 + 0.1f;
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void DirectXApp::OnKeyDown(WPARAM wParam)
{
}
