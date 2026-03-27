#include "../h/InputDevice.h"

InputDevice::InputDevice() : mouseX(0), mouseY(0) {
    // Инициализация кнопок мыши
    mouseButtons[0] = mouseButtons[1] = mouseButtons[2] = false;
}

void InputDevice::Update() {
    // Сохраняем предыдущее состояние клавиш для определения нажатия/отпускания
    previousKeys = currentKeys;
}

void InputDevice::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        // Обработка клавиатуры
    case WM_KEYDOWN:
        currentKeys[static_cast<int>(wParam)] = true;
        break;

    case WM_KEYUP:
        currentKeys[static_cast<int>(wParam)] = false;
        break;

    case WM_CHAR:
        // Символьный ввод (с учётом раскладки)
        // Можно сохранять для текстовых полей
        break;

        // Обработка мыши
    case WM_MOUSEMOVE:
        mouseX = LOWORD(lParam);
        mouseY = HIWORD(lParam);
        break;

    case WM_LBUTTONDOWN:
        mouseButtons[0] = true;
        break;
    case WM_LBUTTONUP:
        mouseButtons[0] = false;
        break;

    case WM_RBUTTONDOWN:
        mouseButtons[1] = true;
        break;
    case WM_RBUTTONUP:
        mouseButtons[1] = false;
        break;

    case WM_MBUTTONDOWN:
        mouseButtons[2] = true;
        break;
    case WM_MBUTTONUP:
        mouseButtons[2] = false;
        break;

    case WM_MOUSEWHEEL:
        // Колесо мыши: GET_WHEEL_DELTA_WPARAM(wParam)
        break;
    }
}

bool InputDevice::IsKeyDown(int keyCode) const {
    auto it = currentKeys.find(keyCode);
    return it != currentKeys.end() && it->second;
}

bool InputDevice::IsKeyPressed(int keyCode) const {
    // Клавиша нажата в этом кадре, но не была нажата в предыдущем
    bool current = IsKeyDown(keyCode);

    auto prevIt = previousKeys.find(keyCode);
    bool previous = (prevIt != previousKeys.end()) ? prevIt->second : false;

    return current && !previous;
}

bool InputDevice::IsKeyReleased(int keyCode) const {
    // Клавиша отпущена в этом кадре, но была нажата в предыдущем
    bool current = IsKeyDown(keyCode);

    auto prevIt = previousKeys.find(keyCode);
    bool previous = (prevIt != previousKeys.end()) ? prevIt->second : false;

    return !current && previous;
}

void InputDevice::GetMousePosition(int& x, int& y) const {
    x = mouseX;
    y = mouseY;
}

bool InputDevice::IsMouseButtonDown(int button) const {
    if (button >= 0 && button < 3) {
        return mouseButtons[button];
    }
    return false;
}