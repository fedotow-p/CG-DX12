//
// Created by elm on 26.03.2026.
//
#include "Window.h"

Window::Window(HINSTANCE hInstance, int nCmdShow) : hInstance(hInstance), hWnd(nullptr), width(800), height(600) {}

Window::~Window() {
    if (hWnd != nullptr) {
        DestroyWindow(hWnd);
    }
}

bool Window::Initialize(const wchar_t *windowTitle, int width, int height) {
    this->width = width;
    this->height = height;

    WNDCLASSEX wc ={};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"DirectXWindowClass";

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"RegisterClass failed", L"Error", MB_OK);
        return false;
    }

    RECT windowRect = { 0, 0, width, height };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    hWnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        L"DirectXWindowClass",
        windowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hWnd) {
        MessageBox(NULL, L"Failed to create window", L"Error", MB_OK);
        return false;
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    return true;
}

LRESULT CALLBACK Window::WindowProc(HWND hWnd, UINT message,
    WPARAM wParam, LPARAM lParam) {

    switch (message) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
