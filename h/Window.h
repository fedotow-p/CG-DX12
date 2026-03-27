#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "InputDevice.h"

// Предварительное объявление (чтобы избежать циклических зависимостей)
class DirectXApp;

class Window {
public:
    HWND GetHwnd() const { return hWnd; }
    Window(HINSTANCE hInstance, int nCmdShow);
    ~Window();

    // Установка размеров
    void Resize(int width, int height) {
        this->width = width;
        this->height = height;
    }

    // Инициализация окна
    bool Initialize(const wchar_t* title, int width, int height);

    // Геттеры
    HWND GetHandle() const { return hWnd; }
    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    InputDevice& GetInputDevice() { return inputDevice; }

    // Связь с DirectXApp
    void SetDirectXApp(DirectXApp* app) { mDirectXApp = app; }
    DirectXApp* GetDirectXApp() const { return mDirectXApp; }

    // Оконная процедура (статический метод)
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message,
        WPARAM wParam, LPARAM lParam);

private:
    HINSTANCE hInstance;      // Дескриптор экземпляра приложения
    HWND hWnd;                // Дескриптор окна
    int width;                // Ширина клиентской области
    int height;               // Высота клиентской области
    InputDevice inputDevice;  // Устройство ввода
    DirectXApp* mDirectXApp = nullptr;  // Ссылка на DirectX приложение
};