#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../h/DirectXApp.h"
#include "../h/Window.h"

#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

int WINAPI WinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow) {
    
    (void)hPrevInstance;
    (void)lpCmdLine;

    // 1. Создаем окно
    Window window(hInstance, nCmdShow);
    if (!window.Initialize(L"DirectX 12 Lab", 800, 600)) {
        MessageBox(NULL, L"Failed to create window", L"Error", MB_OK);
        return 1;
    }

    // 2. Создаем DirectX приложение
    DirectXApp dxApp(window);

    // 3. Связываем окно и DirectXApp (для обработки сообщений)
    window.SetDirectXApp(&dxApp);

    // 4. Инициализируем DirectX
    if (!dxApp.InitializeApp()) {
        MessageBox(NULL, L"DirectX initialization failed", L"Error", MB_OK);
        return 1;
    }

    // 5. Запускаем главный цикл приложения
    return dxApp.Run();
}