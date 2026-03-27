//
// Created by elm on 27.03.2026.
//

#ifndef CG_DX12_INPUTDEVICE_H
#define CG_DX12_INPUTDEVICE_H

#include <windows.h>
#include <unordered_map>

class InputDevice {
    public:
    InputDevice();

    void Update();

    void HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool IsKeyDown(int keyCode) const;
    bool IsKeyPressed(int keyCode) const;
    bool IsKeyReleased(int keyCode) const;

    void GetMousePosition(int& x, int& y) const;
    bool IsMouseButtonDown(int button) const;

    private:
    std::unordered_map<int, bool> currentKeys;
    std::unordered_map<int, bool> previousKeys;

    int mouseX, mouseY;

    bool mouseButtons[3];
};

#endif //CG_DX12_INPUTDEVICE_H
