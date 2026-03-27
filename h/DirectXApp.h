#pragma once

#include <DirectXMath.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
#include "../h/ObjectConstants.h"
#include "../h/Timer.h"
#include "../h/UploadBuffer.h"
#include "../h/vertex.h"
#include "Material.h"
#include "MathHelper.h"
#include "Submesh.h"
#include "ThrowIfFailed.h"
#include "Window.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class DirectXApp {
public:
    DirectXApp(Window& window);
    ~DirectXApp();

    bool Initialize();
    void Shutdown();

    // Основные методы фреймворка
    int Run();
    virtual bool InitializeApp();
    virtual void Update(const Timer& gt);
    virtual void Draw(const Timer& gt);
    void BuildObj(const std::string& path);
    virtual void CalculateFrameStats();

    // Управление таймером
    void StopTimer() { mTimer.Stop(); }
    void StartTimer() { mTimer.Start(); }
    bool IsPaused() const { return mAppPaused; }
    Timer& GetTimer() { return mTimer; }

    // Методы для мыши
    virtual void OnMouseDown(WPARAM btnState, int x, int y);
    virtual void OnMouseUp(WPARAM btnState, int x, int y);
    virtual void OnMouseMove(WPARAM btnState, int x, int y);

    // Обработка изменения размера
    virtual void OnResize();

    // Обработка клавиатуры
    virtual void OnKeyDown(WPARAM wParam);

    void SetDirectXApp(DirectXApp* app) { dxApp = app; }
    DirectXApp* GetDirectXApp() const { return dxApp; }

private:
    float mYaw = 0.0f;
    float mPitch = 0.0f;
    float mUVOffsetU = 0.0f;
    float mUVOffsetV = 0.0f;
    float mUVScaleU = 1.0f;
    float mUVScaleV = 1.0f;
    bool mAnimateTextures = false;

    std::vector<Submesh> mSubmeshes;
    std::vector<Material> mMaterials;
    void CreateTextureFromTGA(
        const std::string& path,
        Microsoft::WRL::ComPtr<ID3D12Resource>& texture);

    DirectXApp* dxApp = nullptr;

    XMFLOAT3 mEyePos = XMFLOAT3(0.0f, 0.0f, 0.0f);


    Window& window;

    // DXGI
    ComPtr<IDXGIFactory4> dxgiFactory;
    ComPtr<IDXGIAdapter1> adapter;

    // D3D12
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> mCommandQueue;
    ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
    ComPtr<ID3D12GraphicsCommandList> mCommandList;
    ComPtr<ID3D12Fence> mFence;
    UINT64 mFenceValue = 0;

    // SwapChain
    ComPtr<IDXGISwapChain> mSwapChain;
    static const int SwapChainBufferCount = 2;
    ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
    int mCurrBackBuffer = 0;

    // Дескрипторы
    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    ComPtr<ID3D12DescriptorHeap> mCbvHeap;  // Для CBV дескрипторов
    ComPtr<ID3D12Resource> mDepthStencilBuffer;

    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvUavDescriptorSize = 0;

    // Форматы
    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // Размеры
    int mClientWidth = 800;
    int mClientHeight = 600;

    // Viewport и Scissor
    D3D12_VIEWPORT mScreenViewport;
    D3D12_RECT mScissorRect;

    // Таймер и состояние
    Timer mTimer;
    bool mAppPaused = false;
    bool mResizing = false;
    int mFrameCount = 0;
    float mTimeElapsed = 0.0f;
    std::wstring mMainWndCaption = L"DirectX 12 Framework";

    // =========== Geometry ===========
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    Microsoft::WRL::ComPtr<ID3D12Resource> mVertexBufferGPU;
    Microsoft::WRL::ComPtr<ID3D12Resource> mVertexBufferUploader;
    D3D12_VERTEX_BUFFER_VIEW mVertexBufferView;
    Microsoft::WRL::ComPtr<ID3D12Resource> mIndexBufferGPU;
    Microsoft::WRL::ComPtr<ID3D12Resource> mIndexBufferUploader;
    D3D12_INDEX_BUFFER_VIEW mIndexBufferView;

    // =========== Shaders ===========
    Microsoft::WRL::ComPtr<ID3DBlob> mvsByteCode = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> mpsByteCode = nullptr;

    // =========== Constant Buffer ===========
    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;

    // =========== Root Signature и PSO ===========
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mWireframePSO;  // Второй PSO для проволочного каркаса
    bool mWireframeMode = false;  // Флаг режима отображения

    // Математика для камеры
    float mTheta = 1.5f * XM_PI;
    float mPhi = XM_PIDIV4;
    float mRadius = 5.0f;
    POINT mLastMousePos;
    XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    UINT mIndexCount;

    // Вспомогательные методы инициализации
    bool CreateDXGIFactory();
    bool GetHardwareAdapter();
    bool CreateD3DDevice();
    bool CreateCommandObjects();
    bool CreateFence();
    void FlushCommandQueue();
    bool CreateSwapChain();
    void QueryDescriptorSizes();
    bool CreateDescriptorHeaps();
    bool CreateRenderTargetViews();
    bool CreateDepthStencilBuffer();
    void CreateViewportAndScissor();
    void SetViewportAndScissor();

    // Методы для геометрии и шейдеров
    void BuildInputLayout();
    //void BuildVertexBuffer();
    //void BuildIndexBuffer();
    void BuildShaders();
    void BuildConstantBuffer();
    void BuildRootSignature();
    void BuildPSO();
    void BuildWireframePSO();  // Новый метод для создания проволочного PSO
    void CreateColorTexture(
        const DirectX::XMFLOAT3& color,
        Microsoft::WRL::ComPtr<ID3D12Resource>& texture);

    // Методы для доступа к ресурсам
    ID3D12Resource* CurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const {
        return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
    }
};