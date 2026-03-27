//
// Created by elm on 27.03.2026.
//
#include "InputDevice.h"

InputDevice::InputDevice() : mouseX(0), mouseY(0) {
    mouseButtons[0] = mouseButtons[1] = mouseButtons[2] = false;
}

void InputDevice::Update() {
    previousKeys = currentKeys;
}

void InputDevice::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_KEYDOWN:
            currentKeys[static_cast<int>(wParam)] = true;
            break;

        case WM_KEYUP:
            currentKeys[static_cast<int>(wParam)] = false;
            break;

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
    }
}

bool InputDevice::IsKeyDown(int keyCode) const {
    auto it = currentKeys.find(keyCode);
    return it != currentKeys.end() && it->second;
}

bool InputDevice::IsKeyPressed(int keyCode) const {
    bool current = IsKeyDown(keyCode);
    auto prevIt = previousKeys.find(keyCode);
    bool previous = (prevIt != previousKeys.end()) ? prevIt->second : false;
    return current && !previous;
}

bool InputDevice::IsKeyReleased(int keyCode) const {
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