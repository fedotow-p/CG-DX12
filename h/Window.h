//
// Created by elm on 26.03.2026.
//

#ifndef CG_DX12_WINDOW_H
#define CG_DX12_WINDOW_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "InputDevice.h"

class DirectXApp;

class Window {
public:
    Window(HINSTANCE hInstance, int nCmdShow);
    ~Window();

    bool Initialize(const wchar_t* windowTitle, int width, int height);
    HWND GetHandle() const { return hWnd; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    InputDevice* GetInputDevice() const { return inputDevice; }

    void SetDirectXApp(DirectXApp* app) {mDirectXApp = app;}
    DirectXApp* GetDirectXApp() const { return mDirectXApp; }

    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HINSTANCE hInstance;
    HWND hWnd;
    int width, height;
    InputDevice* inputDevice;
    DirectXApp* mDirectXApp = nullptr;
};

#endif //CG_DX12_WINDOW_H
