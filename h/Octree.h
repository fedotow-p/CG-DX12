#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "Frustum.h"
#include "Submesh.h"

struct OctreeTraversalStats
{
    UINT TotalSubmeshes = 0;
    UINT BoundedSubmeshes = 0;
    UINT UnboundedSubmeshes = 0;
    UINT CandidateSubmeshes = 0;
    UINT CulledSubmeshes = 0;
    UINT NodesTested = 0;
    UINT NodesRejected = 0;
};

class SubmeshOctree
{
public:
    void Rebuild(const std::vector<Submesh>& submeshes)
    {
        mNodes.clear();
        mUnboundedSubmeshes.clear();
        mTotalSubmeshes = static_cast<UINT>(submeshes.size());
        mBoundedSubmeshes = 0;

        std::vector<Entry> entries;
        entries.reserve(submeshes.size());

        DirectX::XMFLOAT3 boundsMin(
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)());
        DirectX::XMFLOAT3 boundsMax(
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)());

        for (uint32_t index = 0; index < submeshes.size(); ++index)
        {
            const Submesh& submesh = submeshes[index];
            if (!HasFiniteBounds(submesh))
            {
                mUnboundedSubmeshes.push_back(index);
                continue;
            }

            entries.push_back({ index, submesh.BoundsMin, submesh.BoundsMax });
            boundsMin.x = (std::min)(boundsMin.x, submesh.BoundsMin.x);
            boundsMin.y = (std::min)(boundsMin.y, submesh.BoundsMin.y);
            boundsMin.z = (std::min)(boundsMin.z, submesh.BoundsMin.z);
            boundsMax.x = (std::max)(boundsMax.x, submesh.BoundsMax.x);
            boundsMax.y = (std::max)(boundsMax.y, submesh.BoundsMax.y);
            boundsMax.z = (std::max)(boundsMax.z, submesh.BoundsMax.z);
        }

        mBoundedSubmeshes = static_cast<UINT>(entries.size());
        if (entries.empty())
            return;

        const DirectX::XMFLOAT3 center(
            (boundsMin.x + boundsMax.x) * 0.5f,
            (boundsMin.y + boundsMax.y) * 0.5f,
            (boundsMin.z + boundsMax.z) * 0.5f);
        const float largestExtent = (std::max)(
            (std::max)(boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y),
            boundsMax.z - boundsMin.z);
        const float halfExtent = (std::max)(largestExtent * 0.5f, 1.0e-3f) + 1.0e-4f;

        Node root;
        root.PartitionBoundsMin = { center.x - halfExtent, center.y - halfExtent, center.z - halfExtent };
        root.PartitionBoundsMax = { center.x + halfExtent, center.y + halfExtent, center.z + halfExtent };
        root.Entries = std::move(entries);
        mNodes.push_back(std::move(root));
        BuildNode(0, 0);
    }

    void GatherVisible(
        const Frustum& frustum,
        std::vector<uint32_t>& visibleSubmeshIndices,
        OctreeTraversalStats& stats) const
    {
        visibleSubmeshIndices.clear();
        stats = {};
        stats.TotalSubmeshes = mTotalSubmeshes;
        stats.BoundedSubmeshes = mBoundedSubmeshes;
        stats.UnboundedSubmeshes = static_cast<UINT>(mUnboundedSubmeshes.size());

        if (!mNodes.empty())
            GatherNodeVisible(0, frustum, visibleSubmeshIndices, stats);

        visibleSubmeshIndices.insert(
            visibleSubmeshIndices.end(),
            mUnboundedSubmeshes.begin(),
            mUnboundedSubmeshes.end());
        stats.CandidateSubmeshes = static_cast<UINT>(visibleSubmeshIndices.size());
    }

    bool Empty() const { return mNodes.empty(); }

private:
    static constexpr size_t kMaxEntriesPerNode = 8;
    static constexpr uint32_t kMaxDepth = 6;

    struct Entry
    {
        uint32_t SubmeshIndex;
        DirectX::XMFLOAT3 BoundsMin;
        DirectX::XMFLOAT3 BoundsMax;
    };

    struct Node
    {
        DirectX::XMFLOAT3 PartitionBoundsMin = {};
        DirectX::XMFLOAT3 PartitionBoundsMax = {};
        DirectX::XMFLOAT3 BoundsMin = {};
        DirectX::XMFLOAT3 BoundsMax = {};
        std::array<int, 8> Children = { -1, -1, -1, -1, -1, -1, -1, -1 };
        std::vector<Entry> Entries;
        UINT SubtreeEntryCount = 0;
    };

    static bool HasFiniteBounds(const Submesh& submesh)
    {
        return submesh.HasBounds
            && std::isfinite(submesh.BoundsMin.x)
            && std::isfinite(submesh.BoundsMin.y)
            && std::isfinite(submesh.BoundsMin.z)
            && std::isfinite(submesh.BoundsMax.x)
            && std::isfinite(submesh.BoundsMax.y)
            && std::isfinite(submesh.BoundsMax.z)
            && submesh.BoundsMin.x <= submesh.BoundsMax.x
            && submesh.BoundsMin.y <= submesh.BoundsMax.y
            && submesh.BoundsMin.z <= submesh.BoundsMax.z;
    }

    static int GetChildIndex(const Entry& entry, const DirectX::XMFLOAT3& center)
    {
        const DirectX::XMFLOAT3 entryCenter(
            (entry.BoundsMin.x + entry.BoundsMax.x) * 0.5f,
            (entry.BoundsMin.y + entry.BoundsMax.y) * 0.5f,
            (entry.BoundsMin.z + entry.BoundsMax.z) * 0.5f);
        const int x = entryCenter.x >= center.x ? 1 : 0;
        const int y = entryCenter.y >= center.y ? 1 : 0;
        const int z = entryCenter.z >= center.z ? 1 : 0;

        return x | (y << 1) | (z << 2);
    }

    static void GetChildBounds(
        const DirectX::XMFLOAT3& parentBoundsMin,
        const DirectX::XMFLOAT3& parentBoundsMax,
        int childIndex,
        DirectX::XMFLOAT3& boundsMin,
        DirectX::XMFLOAT3& boundsMax)
    {
        const DirectX::XMFLOAT3 center(
            (parentBoundsMin.x + parentBoundsMax.x) * 0.5f,
            (parentBoundsMin.y + parentBoundsMax.y) * 0.5f,
            (parentBoundsMin.z + parentBoundsMax.z) * 0.5f);
        boundsMin = {
            (childIndex & 1) ? center.x : parentBoundsMin.x,
            (childIndex & 2) ? center.y : parentBoundsMin.y,
            (childIndex & 4) ? center.z : parentBoundsMin.z
        };
        boundsMax = {
            (childIndex & 1) ? parentBoundsMax.x : center.x,
            (childIndex & 2) ? parentBoundsMax.y : center.y,
            (childIndex & 4) ? parentBoundsMax.z : center.z
        };
    }

    void BuildNode(int nodeIndex, uint32_t depth)
    {
        if (depth < kMaxDepth && mNodes[nodeIndex].Entries.size() > kMaxEntriesPerNode)
        {
            const Node& node = mNodes[nodeIndex];
            const DirectX::XMFLOAT3 center(
                (node.PartitionBoundsMin.x + node.PartitionBoundsMax.x) * 0.5f,
                (node.PartitionBoundsMin.y + node.PartitionBoundsMax.y) * 0.5f,
                (node.PartitionBoundsMin.z + node.PartitionBoundsMax.z) * 0.5f);

            std::array<std::vector<Entry>, 8> childEntries;
            std::vector<Entry> retainedEntries;
            retainedEntries.reserve(node.Entries.size());
            for (const Entry& entry : node.Entries)
            {
                const int childIndex = GetChildIndex(entry, center);
                childEntries[childIndex].push_back(entry);
            }

            size_t movedEntries = 0;
            for (const std::vector<Entry>& entries : childEntries)
                movedEntries += entries.size();

            if (movedEntries > 0)
            {
                mNodes[nodeIndex].Entries = std::move(retainedEntries);
                for (int childIndex = 0; childIndex < 8; ++childIndex)
                {
                    if (childEntries[childIndex].empty())
                        continue;

                    Node child;
                    GetChildBounds(
                        mNodes[nodeIndex].PartitionBoundsMin,
                        mNodes[nodeIndex].PartitionBoundsMax,
                        childIndex,
                        child.PartitionBoundsMin,
                        child.PartitionBoundsMax);
                    child.Entries = std::move(childEntries[childIndex]);
                    const int childNodeIndex = static_cast<int>(mNodes.size());
                    mNodes.push_back(std::move(child));
                    mNodes[nodeIndex].Children[childIndex] = childNodeIndex;
                }

                const std::array<int, 8> childNodeIndices = mNodes[nodeIndex].Children;
                for (const int childNodeIndex : childNodeIndices)
                {
                    if (childNodeIndex >= 0)
                        BuildNode(childNodeIndex, depth + 1);
                }
            }
        }

        DirectX::XMFLOAT3 boundsMin(
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)(),
            (std::numeric_limits<float>::max)());
        DirectX::XMFLOAT3 boundsMax(
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)(),
            (std::numeric_limits<float>::lowest)());
        const auto includeBounds = [&boundsMin, &boundsMax](
            const DirectX::XMFLOAT3& entryBoundsMin,
            const DirectX::XMFLOAT3& entryBoundsMax)
        {
            boundsMin.x = (std::min)(boundsMin.x, entryBoundsMin.x);
            boundsMin.y = (std::min)(boundsMin.y, entryBoundsMin.y);
            boundsMin.z = (std::min)(boundsMin.z, entryBoundsMin.z);
            boundsMax.x = (std::max)(boundsMax.x, entryBoundsMax.x);
            boundsMax.y = (std::max)(boundsMax.y, entryBoundsMax.y);
            boundsMax.z = (std::max)(boundsMax.z, entryBoundsMax.z);
        };

        UINT subtreeEntryCount = static_cast<UINT>(mNodes[nodeIndex].Entries.size());
        for (const Entry& entry : mNodes[nodeIndex].Entries)
            includeBounds(entry.BoundsMin, entry.BoundsMax);
        for (const int childNodeIndex : mNodes[nodeIndex].Children)
        {
            if (childNodeIndex >= 0)
            {
                subtreeEntryCount += mNodes[childNodeIndex].SubtreeEntryCount;
                includeBounds(
                    mNodes[childNodeIndex].BoundsMin,
                    mNodes[childNodeIndex].BoundsMax);
            }
        }
        mNodes[nodeIndex].BoundsMin = boundsMin;
        mNodes[nodeIndex].BoundsMax = boundsMax;
        mNodes[nodeIndex].SubtreeEntryCount = subtreeEntryCount;
    }

    void GatherNodeVisible(
        int nodeIndex,
        const Frustum& frustum,
        std::vector<uint32_t>& visibleSubmeshIndices,
        OctreeTraversalStats& stats) const
    {
        const Node& node = mNodes[nodeIndex];
        ++stats.NodesTested;
        if (!frustum.IntersectsAabb(node.BoundsMin, node.BoundsMax))
        {
            ++stats.NodesRejected;
            stats.CulledSubmeshes += node.SubtreeEntryCount;
            return;
        }

        for (const Entry& entry : node.Entries)
        {
            if (frustum.IntersectsAabb(entry.BoundsMin, entry.BoundsMax))
                visibleSubmeshIndices.push_back(entry.SubmeshIndex);
            else
                ++stats.CulledSubmeshes;
        }

        for (const int childNodeIndex : node.Children)
        {
            if (childNodeIndex >= 0)
                GatherNodeVisible(childNodeIndex, frustum, visibleSubmeshIndices, stats);
        }
    }

    std::vector<Node> mNodes;
    std::vector<uint32_t> mUnboundedSubmeshes;
    UINT mTotalSubmeshes = 0;
    UINT mBoundedSubmeshes = 0;
};
