//
// Created by elm on 26.03.2026.
//
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Window.h"
#include "DirectXApp.h"

int WINAPI WinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow) {

    Window window(hInstance, nCmdShow);
    if (!window.Initialize(L"DirectX12 Window", 800, 600)) {
        return 1;
    }

    DirectXApp dxApp(window);
    if (!dxApp.Initialize()) {
        MessageBox(NULL, L"DirectX Initialization Failed", L"Error", MB_OK);
        return 1;
    }

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        else {

        }
    }

    return static_cast<int>(msg.wParam);
}