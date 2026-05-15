#pragma once

#include <string>
#include <vector>
#include "Submesh.h"
#include "Vertex.h"

struct ParsedMaterial
{
    std::string Name;
    std::string DiffuseMap;
    DirectX::XMFLOAT3 Kd = { 1.0f, 1.0f, 1.0f };
};

bool LoadOBJ(
    const std::string& filename,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    std::vector<Submesh>& outSubmeshes);

bool LoadFBX(
    const std::string& filename,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    std::vector<Submesh>& outSubmeshes,
    std::vector<ParsedMaterial>& outMaterials);

bool LoadMTL(
    const std::string& filename,
    std::vector<ParsedMaterial>& materials);
