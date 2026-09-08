#pragma once

#include <DirectXMath.h>

struct Frustum
{
    enum PlaneIndex
    {
        Left = 0,
        Right,
        Bottom,
        Top,
        Near,
        Far,
        PlaneCount
    };

    DirectX::XMFLOAT4 Planes[PlaneCount] = {};

    static Frustum FromViewProjection(DirectX::FXMMATRIX viewProjection)
    {
        using namespace DirectX;

        // The renderer transforms row vectors. Transposing makes the clip
        // matrix rows usable as plane coefficients.
        const XMMATRIX clip = XMMatrixTranspose(viewProjection);

        Frustum frustum;
        XMStoreFloat4(&frustum.Planes[Left],
            XMPlaneNormalize(clip.r[3] + clip.r[0]));
        XMStoreFloat4(&frustum.Planes[Right],
            XMPlaneNormalize(clip.r[3] - clip.r[0]));
        XMStoreFloat4(&frustum.Planes[Bottom],
            XMPlaneNormalize(clip.r[3] + clip.r[1]));
        XMStoreFloat4(&frustum.Planes[Top],
            XMPlaneNormalize(clip.r[3] - clip.r[1]));
        // Direct3D uses a [0, 1] depth range, so z >= 0 is the near plane.
        XMStoreFloat4(&frustum.Planes[Near],
            XMPlaneNormalize(clip.r[2]));
        XMStoreFloat4(&frustum.Planes[Far],
            XMPlaneNormalize(clip.r[3] - clip.r[2]));
        return frustum;
    }

    bool IntersectsAabb(
        const DirectX::XMFLOAT3& boundsMin,
        const DirectX::XMFLOAT3& boundsMax) const
    {
        constexpr float kPlaneTolerance = -1.0e-4f;

        for (const DirectX::XMFLOAT4& plane : Planes)
        {
            const DirectX::XMFLOAT3 positiveVertex =
            {
                plane.x >= 0.0f ? boundsMax.x : boundsMin.x,
                plane.y >= 0.0f ? boundsMax.y : boundsMin.y,
                plane.z >= 0.0f ? boundsMax.z : boundsMin.z
            };

            const float distance =
                plane.x * positiveVertex.x
                + plane.y * positiveVertex.y
                + plane.z * positiveVertex.z
                + plane.w;
            if (distance < kPlaneTolerance)
                return false;
        }

        return true;
    }
};
