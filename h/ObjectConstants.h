#pragma once
#include <DirectXMath.h>

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 mWorld;
    DirectX::XMFLOAT4X4 mWorldViewProj;
    DirectX::XMFLOAT4 mUVTransform;
    DirectX::XMFLOAT4 mChessboardParams;

    ObjectConstants()
    {
        DirectX::XMStoreFloat4x4(&mWorldViewProj, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&mWorld, DirectX::XMMatrixIdentity());
        mUVTransform = DirectX::XMFLOAT4(1.0f, 1.0f, 0.0f, 0.0f);
        mChessboardParams = DirectX::XMFLOAT4(1.0f, 15.0f, 0.0f, 0.0f);
    }
};