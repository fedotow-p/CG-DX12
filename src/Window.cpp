#include "../h/Window.h"
#include <windowsx.h>
#include "../h/DirectXApp.h"

Window::Window(HINSTANCE hInstance, int nCmdShow)
    : hInstance(hInstance), hWnd(nullptr), width(800), height(600) {
    // Конструктор - инициализация полей
}

Window::~Window() {
    if (hWnd) {
        DestroyWindow(hWnd);
    }
}

bool Window::Initialize(const wchar_t* title, int width, int height) {
    this->width = width;
    this->height = height;

    // 1. Регистрация класса окна
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"DirectXWindowClass";
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, L"Failed to register window class", L"Error", MB_OK);
        return false;
    }

    // 2. Рассчитываем размер с учетом рамок окна
    RECT windowRect = { 0, 0, width, height };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    // 3. Создание окна
    hWnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        L"DirectXWindowClass",
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        this  // Передаем this для доступа в WindowProc
    );

    if (!hWnd) {
        MessageBox(NULL, L"Failed to create window", L"Error", MB_OK);
        return false;
    }

    // 4. Сохраняем указатель на Window в данных окна
    SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // 5. Показ окна
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    return true;
}

LRESULT CALLBACK Window::WindowProc(HWND hWnd, UINT message,
    WPARAM wParam, LPARAM lParam) {

    // Получаем указатель на объект Window
    Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    if (window) {
        // Передаем сообщение в InputDevice
        window->GetInputDevice().HandleMessage(hWnd, message, wParam, lParam);
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

    case WM_ENTERSIZEMOVE:
        if (window && window->GetDirectXApp()) {
            window->GetDirectXApp()->StopTimer();
        }
        return 0;

    case WM_EXITSIZEMOVE:
        if (window && window->GetDirectXApp()) {
            window->GetDirectXApp()->StartTimer();
            // TODO: Вызвать OnResize() для пересоздания swap chain
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (window && window->GetDirectXApp()) {
            window->GetDirectXApp()->OnMouseDown(
                wParam,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
        }
        return 0;

    case WM_MOUSEMOVE:
        if (window && window->GetDirectXApp()) {
            window->GetDirectXApp()->OnMouseMove(
                wParam,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
        }
        return 0;

    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        if (window && window->GetDirectXApp()) {
            window->GetDirectXApp()->OnMouseUp(
                wParam,
                GET_X_LPARAM(lParam),
                GET_Y_LPARAM(lParam));
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_MENUCHAR:
        // Don't beep when we alt-enter.
        return MAKELRESULT(0, MNC_CLOSE);

    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
        return 0;

    case WM_SIZE:
        if (window) {
            window->width = LOWORD(lParam);
            window->height = HIWORD(lParam);
            // TODO: Обработка изменения размера окна
        }
        break;

    case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hWnd);
                return 0;
            }
            if (window && window->GetDirectXApp()) {
                window->GetDirectXApp()->OnKeyDown(wParam);
            }
            break;

    }


    return DefWindowProc(hWnd, message, wParam, lParam);
}