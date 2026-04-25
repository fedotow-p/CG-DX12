#pragma once
#include <DirectXMath.h>

struct CameraConstants
{
    DirectX::XMFLOAT4X4 mInvViewProj;
    DirectX::XMFLOAT3 mCameraPos;
    float mPadding1;
    DirectX::XMFLOAT2 mScreenSize;
    DirectX::XMFLOAT2 mPadding;
    
    CameraConstants()
    {
        DirectX::XMStoreFloat4x4(&mInvViewProj, DirectX::XMMatrixIdentity());
        mScreenSize = DirectX::XMFLOAT2(800.0f, 600.0f);
        mPadding = DirectX::XMFLOAT2(0.0f, 0.0f);
    }
};