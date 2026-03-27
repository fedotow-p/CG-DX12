//
// Created by elm on 26.03.2026.
//
#include "Window.h"
#include <windowsx.h>
#include "DirectXApp.h"

Window::Window(HINSTANCE hInstance, int nCmdShow) : hInstance(hInstance), hWnd(nullptr), width(800), height(600) {
    (void)nCmdShow;
}

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

    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    return true;
}

LRESULT CALLBACK Window::WindowProc(HWND hWnd, UINT message,
    WPARAM wParam, LPARAM lParam) {

    Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    if (window) {
        window->GetInputDevice()->HandleMessage(hWnd, message, wParam, lParam);
    }

    switch (message) {
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                if (window && window->GetDirectXApp()) {
                    window->GetDirectXApp()->StopTimer();
                }
            }
            else {
                if (window && window->GetDirectXApp()) {
                    window->GetDirectXApp()->StartTimer();
                }
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hWnd);
                return 0;
            }
            if (window && window->GetDirectXApp()) {
                window->GetDirectXApp()->OnKeyDown(wParam);
            }
            break;

        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
            if (window && window->GetDirectXApp()) {
                window->GetDirectXApp()->OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_MOUSEMOVE:
            if (window && window->GetDirectXApp()) {
                window->GetDirectXApp()->OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
            if (window && window->GetDirectXApp()) {
                window->GetDirectXApp()->OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
