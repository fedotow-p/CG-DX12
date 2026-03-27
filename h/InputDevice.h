#pragma once
#include <windows.h>
#include <unordered_map>

class InputDevice {
public:
    InputDevice();

    // Обновление состояния (вызывать каждый кадр)
    void Update();

    // Обработка оконных сообщений
    void HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Клавиатура
    bool IsKeyDown(int keyCode) const;     // Клавиша нажата
    bool IsKeyPressed(int keyCode) const;  // Клавиша только что нажата
    bool IsKeyReleased(int keyCode) const; // Клавиша только что отпущена

    // Мышь
    void GetMousePosition(int& x, int& y) const;
    bool IsMouseButtonDown(int button) const;

private:
    // Состояния клавиш (текущее и предыдущее)
    std::unordered_map<int, bool> currentKeys;
    std::unordered_map<int, bool> previousKeys;

    // Позиция мыши
    int mouseX, mouseY;

    // Состояния кнопок мыши (0 = левая, 1 = правая, 2 = средняя)
    bool mouseButtons[3];
};