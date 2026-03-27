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

