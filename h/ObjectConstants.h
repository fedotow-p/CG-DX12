#pragma once
#include <DirectXMath.h>

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 mWorldViewProj;
    DirectX::XMFLOAT4 mUVTransform;

    ObjectConstants()
    {
        DirectX::XMStoreFloat4x4(&mWorldViewProj, DirectX::XMMatrixIdentity());
        mUVTransform = DirectX::XMFLOAT4(1.0f, 1.0f, 0.0f, 0.0f);
    }
};