//
// Created by elm on 27.03.2026.
//

#ifndef CG_DX12_DIRECTXAPP_H
#define CG_DX12_DIRECTXAPP_H

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "Window.h"
#include "ThrowIfFailed.h"

using Microsoft::WRL::ComPtr;

class DirectXApp {
    public:
    DirectXApp(Window& window);
    ~DirectXApp();

    bool Initialize();

    private:
    Window& window;

    ComPtr<IDXGIFactory4> dxgiFactory;
    ComPtr<IDXGIAdapter1> adapter;

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    UINT64 fenceValue = 0;

    ComPtr<IDXGISwapChain> swapChain;
    static const int BufferCount = 2;
    ComPtr<ID3D12Resource> swapChainBuffer[BufferCount];
    int currentBackBuffer = 0;

    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvDescriptorSize = 0;

    bool CreateDXGIFactory();
    bool CreateDevice();
    bool CreateCommandObjects();
    bool CreateSwapChain();
    bool CreateFence();
    bool CreateRTVHeap();
    bool CreateRenderTargetViews();
    void FlushCommandQueue();
};

#endif //CG_DX12_DIRECTXAPP_H
