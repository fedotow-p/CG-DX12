#pragma once
#include <DirectXMath.h>

struct CameraConstants
{
    static constexpr UINT CascadeCount = 4;
    DirectX::XMFLOAT4X4 mInvViewProj;
    DirectX::XMFLOAT4X4 mView;
    DirectX::XMFLOAT4X4 mCascadeViewProj[CascadeCount];
    DirectX::XMFLOAT3 mCameraPos;
    float mPadding1;
    DirectX::XMFLOAT2 mScreenSize;
    DirectX::XMFLOAT2 mPadding;
    DirectX::XMFLOAT4 mCascadeSplits;

    UINT mVisualizeCascades = 0;
    DirectX::XMFLOAT3 mPadding2;
    
    CameraConstants()
    {
        DirectX::XMStoreFloat4x4(&mInvViewProj, DirectX::XMMatrixIdentity());
        DirectX::XMStoreFloat4x4(&mView, DirectX::XMMatrixIdentity());
        for (auto& matrix : mCascadeViewProj)
            DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixIdentity());
        mScreenSize = DirectX::XMFLOAT2(800.0f, 600.0f);
        mPadding = DirectX::XMFLOAT2(0.0f, 0.0f);
        mCascadeSplits = DirectX::XMFLOAT4(10.0f, 30.0f, 70.0f, 150.0f);

        mVisualizeCascades = 0;
        mPadding2 = {0.0f, 0.0f, 0.0f};
    }
};
