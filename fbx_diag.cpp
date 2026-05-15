#include <iostream>
#include <vector>
#include "h/Parser.h"

int main()
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Submesh> submeshes;
    std::vector<ParsedMaterial> materials;

    if (!LoadFBX("assets/Earth.fbx", vertices, indices, submeshes, materials))
    {
        std::cout << "LoadFBX failed\n";
        return 1;
    }

    std::cout << "vertices=" << vertices.size() << " indices=" << indices.size() << " submeshes=" << submeshes.size() << " materials=" << materials.size() << "\n";

    float minX = vertices[0].position.x, maxX = vertices[0].position.x;
    float minY = vertices[0].position.y, maxY = vertices[0].position.y;
    float minZ = vertices[0].position.z, maxZ = vertices[0].position.z;
    float maxR2 = 0.0f;

    for (const auto& v : vertices)
    {
        minX = (minX < v.position.x) ? minX : v.position.x;
        maxX = (maxX > v.position.x) ? maxX : v.position.x;
        minY = (minY < v.position.y) ? minY : v.position.y;
        maxY = (maxY > v.position.y) ? maxY : v.position.y;
        minZ = (minZ < v.position.z) ? minZ : v.position.z;
        maxZ = (maxZ > v.position.z) ? maxZ : v.position.z;
        float r2 = v.position.x*v.position.x + v.position.y*v.position.y + v.position.z*v.position.z;
        maxR2 = (maxR2 > r2) ? maxR2 : r2;
    }

    std::cout << "bounds x:[" << minX << "," << maxX << "] y:[" << minY << "," << maxY << "] z:[" << minZ << "," << maxZ << "]\n";
    std::cout << "maxRadius=" << sqrt(maxR2) << "\n";

    for (size_t i = 0; i < submeshes.size(); ++i)
    {
        std::cout << "submesh[" << i << "] material=" << submeshes[i].MaterialName
                  << " start=" << submeshes[i].IndexStart << " count=" << submeshes[i].IndexCount << "\n";
    }

    for (size_t i = 0; i < materials.size(); ++i)
    {
        std::cout << "material[" << i << "] name=" << materials[i].Name
                  << " diffuse=" << materials[i].DiffuseMap << "\n";
    }

    return 0;
}
