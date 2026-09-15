#include "../h/Parser.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

using namespace DirectX;

namespace
{
using FbxPropertyValue = std::variant<
    std::monostate,
    int16_t,
    bool,
    int32_t,
    int64_t,
    float,
    double,
    std::string,
    std::vector<uint8_t>,
    std::vector<int32_t>,
    std::vector<int64_t>,
    std::vector<float>,
    std::vector<double>>;

struct FbxProperty
{
    char Type = 0;
    FbxPropertyValue Value;
};

struct FbxNode
{
    std::string Name;
    std::vector<FbxProperty> Properties;
    std::vector<FbxNode> Children;

    const FbxNode* FindChild(std::string_view childName) const
    {
        for (const auto& child : Children)
        {
            if (child.Name == childName)
                return &child;
        }

        return nullptr;
    }
};

struct FbxMaterialData
{
    int64_t Id = 0;
    std::string Name;
    XMFLOAT3 DiffuseColor = { 1.0f, 1.0f, 1.0f };
};

struct FbxTextureData
{
    int64_t Id = 0;
    std::string Name;
    std::string FileName;
    std::string RelativeFileName;
    int64_t VideoId = 0;
};

struct FbxVideoData
{
    int64_t Id = 0;
    std::string FileName;
    std::string RelativeFileName;
};

class ZlibRuntime
{
public:
    using Bytef = unsigned char;
    using uLong = unsigned long;
    using uLongf = unsigned long;
    using UncompressFn = int(__cdecl*)(Bytef*, uLongf*, const Bytef*, uLong);

    static ZlibRuntime& Instance()
    {
        static ZlibRuntime instance;
        return instance;
    }

    bool IsAvailable() const
    {
        return mUncompress != nullptr;
    }

    bool Uncompress(
        const uint8_t* source,
        size_t sourceSize,
        uint8_t* destination,
        size_t destinationSize) const
    {
        if (mUncompress == nullptr)
            return false;

        uLongf actualSize = static_cast<uLongf>(destinationSize);
        const int result = mUncompress(
            reinterpret_cast<Bytef*>(destination),
            &actualSize,
            reinterpret_cast<const Bytef*>(source),
            static_cast<uLong>(sourceSize));

        return result == 0 && actualSize == destinationSize;
    }

private:
    ZlibRuntime()
    {
        const std::array<const char*, 4> candidates =
        {
            "zlib1.dll",
            "C:\\Program Files\\Git\\mingw64\\bin\\zlib1.dll",
            "C:\\Program Files\\Git\\mingw64\\libexec\\git-core\\zlib1.dll",
            "C:\\Program Files\\qemu\\zlib1.dll"
        };

        for (const char* candidate : candidates)
        {
            mModule = LoadLibraryA(candidate);
            if (mModule != nullptr)
            {
                mUncompress = reinterpret_cast<UncompressFn>(
                    GetProcAddress(mModule, "uncompress"));

                if (mUncompress != nullptr)
                    break;

                FreeLibrary(mModule);
                mModule = nullptr;
            }
        }
    }

    ~ZlibRuntime()
    {
        if (mModule != nullptr)
            FreeLibrary(mModule);
    }

    HMODULE mModule = nullptr;
    UncompressFn mUncompress = nullptr;
};

uint8_t ReadU8(const std::vector<uint8_t>& data, size_t offset)
{
    return data[offset];
}

template <typename T>
T ReadScalar(const std::vector<uint8_t>& data, size_t offset)
{
    T value{};
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

std::string SanitizeFbxString(const std::string& value)
{
    const size_t terminator = value.find('\0');
    return value.substr(0, terminator);
}

std::string ToLower(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return value;
}

std::string ResolveTexturePath(const std::string& modelFilename, const std::string& referencedPath)
{
    if (referencedPath.empty())
        return {};

    namespace fs = std::filesystem;

    const fs::path modelPath(modelFilename);
    const fs::path modelDir = modelPath.parent_path();
    const std::string modelStem = modelPath.stem().string();
    const std::string modelStemLower = ToLower(modelStem);

    fs::path reference(referencedPath);
    const fs::path baseName = reference.filename();

    std::vector<fs::path> directories =
    {
        modelDir,
        modelDir / "textures",
        modelDir / "textures" / modelStem,
        modelDir / "textures" / modelStemLower
    };

    std::vector<fs::path> fileCandidates;

    if (reference.is_absolute() && fs::exists(reference))
        return reference.string();

    if (!reference.empty())
        fileCandidates.push_back(reference);

    for (const auto& dir : directories)
    {
        if (!baseName.empty())
            fileCandidates.push_back(dir / baseName);
    }

    const std::vector<std::string> preferredExtensions = { ".dds", ".tga", ".png", ".jpg", ".jpeg" };
    std::vector<fs::path> finalCandidates;

    for (const auto& candidate : fileCandidates)
    {
        finalCandidates.push_back(candidate);

        if (!candidate.has_extension())
        {
            for (const auto& extension : preferredExtensions)
                finalCandidates.push_back(candidate.string() + extension);
        }
        else
        {
            for (const auto& extension : preferredExtensions)
            {
                fs::path withExtension = candidate;
                withExtension.replace_extension(extension);
                finalCandidates.push_back(withExtension);
            }
        }
    }

    for (const auto& candidate : finalCandidates)
    {
        if (fs::exists(candidate))
            return candidate.lexically_normal().string();
    }

    return {};
}

const std::string* GetStringProperty(const FbxNode& node, size_t index)
{
    if (index >= node.Properties.size())
        return nullptr;

    return std::get_if<std::string>(&node.Properties[index].Value);
}

int64_t GetIntegralProperty(const FbxNode& node, size_t index, int64_t fallback = 0)
{
    if (index >= node.Properties.size())
        return fallback;

    if (const auto* value = std::get_if<int64_t>(&node.Properties[index].Value))
        return *value;

    if (const auto* value = std::get_if<int32_t>(&node.Properties[index].Value))
        return *value;

    if (const auto* value = std::get_if<int16_t>(&node.Properties[index].Value))
        return *value;

    return fallback;
}

template <typename T>
const std::vector<T>* GetArrayProperty(const FbxNode& node, size_t index)
{
    if (index >= node.Properties.size())
        return nullptr;

    return std::get_if<std::vector<T>>(&node.Properties[index].Value);
}

bool IsNullRecord(const std::vector<uint8_t>& data, size_t offset, size_t headerSize)
{
    if (offset + headerSize > data.size())
        return false;

    for (size_t i = 0; i < headerSize; ++i)
    {
        if (data[offset + i] != 0)
            return false;
    }

    return true;
}

template <typename T>
std::vector<T> ReadArrayData(
    const std::vector<uint8_t>& data,
    size_t& offset,
    uint32_t arrayLength,
    uint32_t encoding,
    uint32_t compressedLength)
{
    const size_t byteCount = static_cast<size_t>(arrayLength) * sizeof(T);
    std::vector<uint8_t> bytes(byteCount);

    if (encoding == 0)
    {
        std::memcpy(bytes.data(), data.data() + offset, byteCount);
        offset += byteCount;
    }
    else
    {
        const auto& zlib = ZlibRuntime::Instance();
        if (!zlib.IsAvailable())
            return {};

        if (!zlib.Uncompress(
                data.data() + offset,
                compressedLength,
                bytes.data(),
                byteCount))
        {
            return {};
        }

        offset += compressedLength;
    }

    std::vector<T> result(arrayLength);
    std::memcpy(result.data(), bytes.data(), byteCount);
    return result;
}

bool ParseFbxProperty(const std::vector<uint8_t>& data, size_t& offset, FbxProperty& outProperty)
{
    if (offset >= data.size())
        return false;

    const char type = static_cast<char>(ReadU8(data, offset++));
    outProperty.Type = type;

    switch (type)
    {
        case 'Y':
            outProperty.Value = ReadScalar<int16_t>(data, offset);
            offset += sizeof(int16_t);
            return true;
        case 'C':
            outProperty.Value = ReadU8(data, offset) != 0;
            offset += sizeof(uint8_t);
            return true;
        case 'I':
            outProperty.Value = ReadScalar<int32_t>(data, offset);
            offset += sizeof(int32_t);
            return true;
        case 'F':
            outProperty.Value = ReadScalar<float>(data, offset);
            offset += sizeof(float);
            return true;
        case 'D':
            outProperty.Value = ReadScalar<double>(data, offset);
            offset += sizeof(double);
            return true;
        case 'L':
            outProperty.Value = ReadScalar<int64_t>(data, offset);
            offset += sizeof(int64_t);
            return true;
        case 'S':
        case 'R':
        {
            const uint32_t length = ReadScalar<uint32_t>(data, offset);
            offset += sizeof(uint32_t);
            outProperty.Value = std::string(
                reinterpret_cast<const char*>(data.data() + offset),
                length);
            offset += length;
            return true;
        }
        case 'b':
        case 'i':
        case 'l':
        case 'f':
        case 'd':
        {
            const uint32_t arrayLength = ReadScalar<uint32_t>(data, offset);
            const uint32_t encoding = ReadScalar<uint32_t>(data, offset + 4);
            const uint32_t compressedLength = ReadScalar<uint32_t>(data, offset + 8);
            offset += 12;

            switch (type)
            {
                case 'b':
                    outProperty.Value = ReadArrayData<uint8_t>(
                        data, offset, arrayLength, encoding, compressedLength);
                    return true;
                case 'i':
                    outProperty.Value = ReadArrayData<int32_t>(
                        data, offset, arrayLength, encoding, compressedLength);
                    return true;
                case 'l':
                    outProperty.Value = ReadArrayData<int64_t>(
                        data, offset, arrayLength, encoding, compressedLength);
                    return true;
                case 'f':
                    outProperty.Value = ReadArrayData<float>(
                        data, offset, arrayLength, encoding, compressedLength);
                    return true;
                case 'd':
                    outProperty.Value = ReadArrayData<double>(
                        data, offset, arrayLength, encoding, compressedLength);
                    return true;
            }
            break;
        }
        default:
            return false;
    }

    return false;
}

bool ParseFbxNode(
    const std::vector<uint8_t>& data,
    size_t& offset,
    uint32_t version,
    FbxNode& outNode)
{
    const size_t headerSize = version >= 7500 ? 25u : 13u;
    if (offset + headerSize > data.size())
        return false;

    const uint64_t endOffset = version >= 7500
        ? ReadScalar<uint64_t>(data, offset)
        : ReadScalar<uint32_t>(data, offset);
    const uint64_t propertyCount = version >= 7500
        ? ReadScalar<uint64_t>(data, offset + 8)
        : ReadScalar<uint32_t>(data, offset + 4);
    const uint64_t propertyListLength = version >= 7500
        ? ReadScalar<uint64_t>(data, offset + 16)
        : ReadScalar<uint32_t>(data, offset + 8);
    const uint8_t nameLength = ReadU8(data, offset + headerSize - 1);

    if (endOffset == 0)
        return false;

    offset += headerSize;
    outNode.Name.assign(
        reinterpret_cast<const char*>(data.data() + offset),
        nameLength);
    offset += nameLength;

    outNode.Properties.resize(static_cast<size_t>(propertyCount));
    const size_t propertyStart = offset;

    for (auto& property : outNode.Properties)
    {
        if (!ParseFbxProperty(data, offset, property))
            return false;
    }

    if (offset - propertyStart != propertyListLength)
        offset = propertyStart + static_cast<size_t>(propertyListLength);

    while (offset < endOffset)
    {
        if (IsNullRecord(data, offset, headerSize))
        {
            offset += headerSize;
            break;
        }

        FbxNode child;
        if (!ParseFbxNode(data, offset, version, child))
            return false;

        outNode.Children.push_back(std::move(child));
    }

    offset = static_cast<size_t>(endOffset);
    return true;
}

bool LoadFbxTree(const std::string& filename, FbxNode& root)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file)
        return false;

    const std::vector<uint8_t> bytes{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};

    if (bytes.size() < 27)
        return false;

    static constexpr char FbxMagic[] = "Kaydara FBX Binary  ";
    if (std::memcmp(bytes.data(), FbxMagic, sizeof(FbxMagic) - 1) != 0)
        return false;

    const uint32_t version = ReadScalar<uint32_t>(bytes, 23);
    size_t offset = 27;

    root = {};
    root.Name = "Root";

    const size_t headerSize = version >= 7500 ? 25u : 13u;
    while (offset + headerSize <= bytes.size())
    {
        if (IsNullRecord(bytes, offset, headerSize))
            break;

        FbxNode child;
        if (!ParseFbxNode(bytes, offset, version, child))
            return false;

        root.Children.push_back(std::move(child));
    }

    return true;
}

void CenterVertices(std::vector<Vertex>& vertices)
{
    if (vertices.empty())
        return;

    XMFLOAT3 minP = vertices.front().position;
    XMFLOAT3 maxP = vertices.front().position;

    for (const auto& vertex : vertices)
    {
        minP.x = std::min(minP.x, vertex.position.x);
        minP.y = std::min(minP.y, vertex.position.y);
        minP.z = std::min(minP.z, vertex.position.z);

        maxP.x = std::max(maxP.x, vertex.position.x);
        maxP.y = std::max(maxP.y, vertex.position.y);
        maxP.z = std::max(maxP.z, vertex.position.z);
    }

    const XMFLOAT3 center =
    {
        (minP.x + maxP.x) * 0.5f,
        (minP.y + maxP.y) * 0.5f,
        (minP.z + maxP.z) * 0.5f
    };

    for (auto& vertex : vertices)
    {
        vertex.position.x -= center.x;
        vertex.position.y -= center.y;
        vertex.position.z -= center.z;
    }
}

void NormalizeVertices(std::vector<Vertex>& vertices, float targetRadius)
{
    if (vertices.empty())
        return;

    float maxDistanceSq = 0.0f;
    for (const auto& vertex : vertices)
    {
        const float distanceSq =
            vertex.position.x * vertex.position.x +
            vertex.position.y * vertex.position.y +
            vertex.position.z * vertex.position.z;
        maxDistanceSq = (std::max)(maxDistanceSq, distanceSq);
    }

    if (maxDistanceSq <= 1e-8f)
        return;

    const float scale = targetRadius / std::sqrt(maxDistanceSq);
    for (auto& vertex : vertices)
    {
        vertex.position.x *= scale;
        vertex.position.y *= scale;
        vertex.position.z *= scale;
    }
}

void GatherObjectData(
    const FbxNode& node,
    std::vector<FbxMaterialData>& materials,
    std::unordered_map<int64_t, FbxTextureData>& textures,
    std::unordered_map<int64_t, FbxVideoData>& videos)
{
    if (node.Name == "Material")
    {
        FbxMaterialData material;
        material.Id = GetIntegralProperty(node, 0);

        if (const auto* name = GetStringProperty(node, 1))
            material.Name = SanitizeFbxString(*name);

        if (const auto* properties70 = node.FindChild("Properties70"))
        {
            for (const auto& propertyNode : properties70->Children)
            {
                if (propertyNode.Name != "P")
                    continue;

                const auto* propertyName = GetStringProperty(propertyNode, 0);
                if (propertyName == nullptr)
                    continue;

                if (*propertyName == "DiffuseColor" && propertyNode.Properties.size() >= 7)
                {
                    material.DiffuseColor.x = static_cast<float>(
                        std::get<double>(propertyNode.Properties[4].Value));
                    material.DiffuseColor.y = static_cast<float>(
                        std::get<double>(propertyNode.Properties[5].Value));
                    material.DiffuseColor.z = static_cast<float>(
                        std::get<double>(propertyNode.Properties[6].Value));
                }
            }
        }

        if (material.Name.empty())
            material.Name = "Material_" + std::to_string(materials.size());

        materials.push_back(material);
    }
    else if (node.Name == "Texture")
    {
        FbxTextureData texture;
        texture.Id = GetIntegralProperty(node, 0);

        if (const auto* name = GetStringProperty(node, 1))
            texture.Name = SanitizeFbxString(*name);

        if (const auto* fileName = node.FindChild("FileName"))
        {
            if (const auto* value = GetStringProperty(*fileName, 0))
                texture.FileName = SanitizeFbxString(*value);
        }

        if (const auto* relativeFileName = node.FindChild("RelativeFilename"))
        {
            if (const auto* value = GetStringProperty(*relativeFileName, 0))
                texture.RelativeFileName = SanitizeFbxString(*value);
        }

        textures[texture.Id] = texture;
    }
    else if (node.Name == "Video")
    {
        FbxVideoData video;
        video.Id = GetIntegralProperty(node, 0);

        if (const auto* fileName = node.FindChild("Filename"))
        {
            if (const auto* value = GetStringProperty(*fileName, 0))
                video.FileName = SanitizeFbxString(*value);
        }

        if (const auto* relativeFileName = node.FindChild("RelativeFilename"))
        {
            if (const auto* value = GetStringProperty(*relativeFileName, 0))
                video.RelativeFileName = SanitizeFbxString(*value);
        }

        videos[video.Id] = video;
    }

    for (const auto& child : node.Children)
        GatherObjectData(child, materials, textures, videos);
}

std::string PickTextureReference(
    const std::string& modelFilename,
    const FbxTextureData& texture,
    const std::unordered_map<int64_t, FbxVideoData>& videos)
{
    std::vector<std::string> references;

    if (!texture.RelativeFileName.empty())
        references.push_back(texture.RelativeFileName);
    if (!texture.FileName.empty())
        references.push_back(texture.FileName);

    if (texture.VideoId != 0)
    {
        const auto videoIt = videos.find(texture.VideoId);
        if (videoIt != videos.end())
        {
            if (!videoIt->second.RelativeFileName.empty())
                references.push_back(videoIt->second.RelativeFileName);
            if (!videoIt->second.FileName.empty())
                references.push_back(videoIt->second.FileName);
        }
    }

    for (const auto& reference : references)
    {
        const std::string resolved = ResolveTexturePath(modelFilename, reference);
        if (!resolved.empty())
            return resolved;
    }

    return {};
}

bool BuildFbxMaterials(
    const std::string& filename,
    const FbxNode& root,
    std::vector<ParsedMaterial>& outMaterials)
{
    const FbxNode* objects = root.FindChild("Objects");
    if (objects == nullptr)
        return false;

    std::vector<FbxMaterialData> materials;
    std::unordered_map<int64_t, FbxTextureData> textures;
    std::unordered_map<int64_t, FbxVideoData> videos;
    GatherObjectData(*objects, materials, textures, videos);

    const FbxNode* connections = root.FindChild("Connections");
    std::unordered_map<int64_t, std::unordered_map<std::string, int64_t>> materialTextures;

    if (connections != nullptr)
    {
        for (const auto& connection : connections->Children)
        {
            if (connection.Name != "C")
                continue;

            const auto* relation = GetStringProperty(connection, 0);
            if (relation == nullptr || connection.Properties.size() < 3)
                continue;

            const int64_t sourceId = GetIntegralProperty(connection, 1);
            const int64_t destinationId = GetIntegralProperty(connection, 2);

            if (*relation == "OO")
            {
                auto textureIt = textures.find(destinationId);
                if (textureIt != textures.end())
                {
                    textureIt->second.VideoId = sourceId;
                }
            }
            else if (*relation == "OP")
            {
                const auto* propertyName = GetStringProperty(connection, 3);
                if (propertyName == nullptr)
                    continue;

                materialTextures[destinationId][*propertyName] = sourceId;
            }
        }
    }

    auto pickTextureForMaterial =
        [&](const FbxMaterialData& material) -> std::string
    {
        const auto connectionIt = materialTextures.find(material.Id);
        if (connectionIt == materialTextures.end())
            return {};

        static const std::array<std::string_view, 4> preferredProperties =
        {
            "DiffuseColor",
            "BaseColor",
            "Maya|baseColor",
            ""
        };

        for (const auto property : preferredProperties)
        {
            for (const auto& [name, textureId] : connectionIt->second)
            {
                const std::string loweredName = ToLower(name);
                const bool matchesProperty =
                    property.empty()
                    || loweredName == ToLower(std::string(property))
                    || (property == "DiffuseColor" && loweredName.find("diffuse") != std::string::npos)
                    || (property == "BaseColor" && loweredName.find("base") != std::string::npos);

                if (!matchesProperty)
                    continue;

                const auto textureIt = textures.find(textureId);
                if (textureIt == textures.end())
                    continue;

                const std::string resolved = PickTextureReference(filename, textureIt->second, videos);
                if (!resolved.empty())
                    return resolved;
            }
        }

        for (const auto& [_, textureId] : connectionIt->second)
        {
            const auto textureIt = textures.find(textureId);
            if (textureIt == textures.end())
                continue;

            const std::string resolved = PickTextureReference(filename, textureIt->second, videos);
            if (!resolved.empty())
                return resolved;
        }

        return {};
    };

    outMaterials.clear();

    for (const auto& material : materials)
    {
        ParsedMaterial parsed;
        parsed.Name = material.Name;
        parsed.Kd = material.DiffuseColor;
        parsed.DiffuseMap = pickTextureForMaterial(material);
        outMaterials.push_back(std::move(parsed));
    }

    if (outMaterials.empty())
    {
        ParsedMaterial fallback;
        fallback.Name = "Earth";

        for (const auto& [_, texture] : textures)
        {
            const std::string candidate = PickTextureReference(filename, texture, videos);
            const std::string lowered = ToLower(candidate);
            if (lowered.find("alb") != std::string::npos
                || lowered.find("albedo") != std::string::npos
                || lowered.find("base") != std::string::npos)
            {
                fallback.DiffuseMap = candidate;
                break;
            }
        }

        if (fallback.DiffuseMap.empty())
            fallback.DiffuseMap = ResolveTexturePath(filename, "Earth_ALB.dds");

        outMaterials.push_back(std::move(fallback));
    }

    return true;
}

struct LayerElementData
{
    std::string MappingType;
    std::string ReferenceType;
    std::vector<double> DirectDoubles;
    std::vector<int32_t> Indices;
    std::vector<int32_t> Materials;
};

LayerElementData ParseLayerElement(const FbxNode* node, const char* directArrayName, const char* indexArrayName)
{
    LayerElementData result;
    if (node == nullptr)
        return result;

    if (const auto* mapping = node->FindChild("MappingInformationType"))
    {
        if (const auto* value = GetStringProperty(*mapping, 0))
            result.MappingType = *value;
    }

    if (const auto* reference = node->FindChild("ReferenceInformationType"))
    {
        if (const auto* value = GetStringProperty(*reference, 0))
            result.ReferenceType = *value;
    }

    if (const auto* direct = node->FindChild(directArrayName))
    {
        if (const auto* values = GetArrayProperty<double>(*direct, 0))
            result.DirectDoubles = *values;
        else if (const auto* materialValues = GetArrayProperty<int32_t>(*direct, 0))
            result.Materials = *materialValues;
    }

    if (indexArrayName != nullptr)
    {
        if (const auto* indices = node->FindChild(indexArrayName))
        {
            if (const auto* values = GetArrayProperty<int32_t>(*indices, 0))
                result.Indices = *values;
        }
    }

    return result;
}

XMFLOAT3 ResolveNormal(
    const LayerElementData& normals,
    int controlPointIndex,
    int polygonVertexIndex)
{
    if (normals.DirectDoubles.empty())
        return { 0.0f, 1.0f, 0.0f };

    int directIndex = normals.MappingType == "ByControlPoint"
        ? controlPointIndex
        : polygonVertexIndex;

    if (normals.ReferenceType == "IndexToDirect" && directIndex < static_cast<int>(normals.Indices.size()))
        directIndex = normals.Indices[directIndex];

    const size_t baseIndex = static_cast<size_t>(directIndex) * 3;
    if (baseIndex + 2 >= normals.DirectDoubles.size())
        return { 0.0f, 1.0f, 0.0f };

    return
    {
        static_cast<float>(normals.DirectDoubles[baseIndex + 0]),
        static_cast<float>(normals.DirectDoubles[baseIndex + 1]),
        static_cast<float>(normals.DirectDoubles[baseIndex + 2])
    };
}

XMFLOAT2 ResolveUv(
    const LayerElementData& uvs,
    int controlPointIndex,
    int polygonVertexIndex)
{
    if (uvs.DirectDoubles.empty())
        return { 0.0f, 0.0f };

    int directIndex = uvs.MappingType == "ByControlPoint"
        ? controlPointIndex
        : polygonVertexIndex;

    if (uvs.ReferenceType == "IndexToDirect" && directIndex < static_cast<int>(uvs.Indices.size()))
        directIndex = uvs.Indices[directIndex];

    const size_t baseIndex = static_cast<size_t>(directIndex) * 2;
    if (baseIndex + 1 >= uvs.DirectDoubles.size())
        return { 0.0f, 0.0f };

    return
    {
        static_cast<float>(uvs.DirectDoubles[baseIndex + 0]),
        static_cast<float>(1.0 - uvs.DirectDoubles[baseIndex + 1])
    };
}

std::string MaterialNameForPolygon(
    const LayerElementData& materials,
    int polygonIndex,
    const std::vector<ParsedMaterial>& parsedMaterials)
{
    if (parsedMaterials.empty())
        return "Default";

    int materialIndex = 0;
    if (materials.MappingType == "ByPolygon" && polygonIndex < static_cast<int>(materials.Materials.size()))
        materialIndex = materials.Materials[polygonIndex];
    else if (!materials.Materials.empty())
        materialIndex = materials.Materials.front();

    materialIndex = std::clamp(materialIndex, 0, static_cast<int>(parsedMaterials.size()) - 1);
    return parsedMaterials[materialIndex].Name;
}
}

bool LoadOBJ(
    const std::string& filename,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    std::vector<Submesh>& outSubmeshes)
{
    outVertices.clear();
    outIndices.clear();
    outSubmeshes.clear();
    std::string currentMaterial;
    uint32_t currentStartIndex = 0;

    std::ifstream file(filename);
    if (!file.is_open())
        return false;

    constexpr float OBJ_SCALE = 0.01f;

    std::vector<XMFLOAT3> positions;
    std::vector<XMFLOAT3> normals;
    std::vector<XMFLOAT2> texcoords;
    std::string line;

    while (std::getline(file, line))
    {
        if (line.rfind("v ", 0) == 0)
        {
            XMFLOAT3 position{};
            std::sscanf(line.c_str(), "v %f %f %f", &position.x, &position.y, &position.z);
            position.x *= OBJ_SCALE;
            position.y *= OBJ_SCALE;
            position.z *= OBJ_SCALE;
            positions.push_back(position);
        }
        else if (line.rfind("vt ", 0) == 0)
        {
            XMFLOAT2 uv{};
            std::sscanf(line.c_str(), "vt %f %f", &uv.x, &uv.y);
            texcoords.push_back(uv);
        }
        else if (line.rfind("vn ", 0) == 0)
        {
            XMFLOAT3 normal{};
            std::sscanf(line.c_str(), "vn %f %f %f", &normal.x, &normal.y, &normal.z);
            normals.push_back(normal);
        }
        else if (line.rfind("usemtl ", 0) == 0)
        {
            if (!currentMaterial.empty() && outIndices.size() > currentStartIndex)
            {
                Submesh submesh;
                submesh.MaterialName = currentMaterial;
                submesh.IndexStart = currentStartIndex;
                submesh.IndexCount = static_cast<uint32_t>(outIndices.size()) - currentStartIndex;
                outSubmeshes.push_back(submesh);
            }

            currentMaterial = line.substr(7);
            currentStartIndex = static_cast<uint32_t>(outIndices.size());
        }
        else if (line.rfind("f ", 0) == 0)
        {
            std::vector<int> positionIndices;
            std::vector<int> uvIndices;
            std::vector<int> normalIndices;

            std::stringstream stream(line.substr(2));
            std::string vertexToken;

            while (stream >> vertexToken)
            {
                int p = 0;
                int t = 0;
                int n = 0;
                std::sscanf(vertexToken.c_str(), "%d/%d/%d", &p, &t, &n);
                positionIndices.push_back(p);
                uvIndices.push_back(t);
                normalIndices.push_back(n);
            }

            for (size_t i = 1; i + 1 < positionIndices.size(); ++i)
            {
                const int ids[3] = { 0, static_cast<int>(i), static_cast<int>(i) + 1 };

                for (int k = 0; k < 3; ++k)
                {
                    Vertex vertex{};
                    const int positionIndex = positionIndices[ids[k]] - 1;
                    const int uvIndex = uvIndices[ids[k]] - 1;
                    const int normalIndex = normalIndices[ids[k]] - 1;

                    vertex.position = positions[positionIndex];
                    vertex.normal = normals[normalIndex];
                    vertex.texcoord = texcoords[uvIndex];

                    outVertices.push_back(vertex);
                    outIndices.push_back(static_cast<uint32_t>(outVertices.size() - 1));
                }
            }
        }
    }

    if (outVertices.empty())
        return false;

    CenterVertices(outVertices);

    if (!currentMaterial.empty() && outIndices.size() > currentStartIndex)
    {
        Submesh submesh;
        submesh.MaterialName = currentMaterial;
        submesh.IndexStart = currentStartIndex;
        submesh.IndexCount = static_cast<uint32_t>(outIndices.size()) - currentStartIndex;
        outSubmeshes.push_back(submesh);
    }

    return true;
}

bool LoadFBX(
    const std::string& filename,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    std::vector<Submesh>& outSubmeshes,
    std::vector<ParsedMaterial>& outMaterials)
{
    outVertices.clear();
    outIndices.clear();
    outSubmeshes.clear();
    outMaterials.clear();

    FbxNode root;
    if (!LoadFbxTree(filename, root))
        return false;

    if (!BuildFbxMaterials(filename, root, outMaterials))
        return false;

    const FbxNode* objects = root.FindChild("Objects");
    if (objects == nullptr)
        return false;

    const FbxNode* geometry = nullptr;
    for (const auto& child : objects->Children)
    {
        if (child.Name == "Geometry")
        {
            geometry = &child;
            break;
        }
    }

    if (geometry == nullptr)
        return false;

    const auto* vertexData = geometry->FindChild("Vertices");
    const auto* polygonData = geometry->FindChild("PolygonVertexIndex");

    const auto* controlPoints = vertexData != nullptr
        ? GetArrayProperty<double>(*vertexData, 0)
        : nullptr;
    const auto* polygonIndices = polygonData != nullptr
        ? GetArrayProperty<int32_t>(*polygonData, 0)
        : nullptr;

    if (controlPoints == nullptr || polygonIndices == nullptr || controlPoints->empty() || polygonIndices->empty())
        return false;

    const LayerElementData normalLayer = ParseLayerElement(
        geometry->FindChild("LayerElementNormal"),
        "Normals",
        "NormalIndex");
    const LayerElementData uvLayer = ParseLayerElement(
        geometry->FindChild("LayerElementUV"),
        "UV",
        "UVIndex");
    const LayerElementData materialLayer = ParseLayerElement(
        geometry->FindChild("LayerElementMaterial"),
        "Materials",
        nullptr);

    std::vector<Vertex> polygonVertices;
    std::string currentMaterialName;
    uint32_t currentSubmeshStart = 0;
    int polygonIndex = 0;
    int polygonVertexIndex = 0;

    for (const int32_t rawIndex : *polygonIndices)
    {
        const bool isPolygonEnd = rawIndex < 0;
        const int controlPointIndex = isPolygonEnd ? -rawIndex - 1 : rawIndex;
        const size_t baseIndex = static_cast<size_t>(controlPointIndex) * 3;

        if (baseIndex + 2 >= controlPoints->size())
            return false;

        Vertex vertex{};
        vertex.position =
        {
            static_cast<float>((*controlPoints)[baseIndex + 0]),
            static_cast<float>((*controlPoints)[baseIndex + 1]),
            static_cast<float>((*controlPoints)[baseIndex + 2])
        };
        vertex.normal = ResolveNormal(normalLayer, controlPointIndex, polygonVertexIndex);
        vertex.texcoord = ResolveUv(uvLayer, controlPointIndex, polygonVertexIndex);

        polygonVertices.push_back(vertex);
        ++polygonVertexIndex;

        if (!isPolygonEnd)
            continue;

        const std::string materialName = MaterialNameForPolygon(materialLayer, polygonIndex, outMaterials);

        if (currentMaterialName.empty())
        {
            currentMaterialName = materialName;
            currentSubmeshStart = static_cast<uint32_t>(outIndices.size());
        }
        else if (materialName != currentMaterialName && outIndices.size() > currentSubmeshStart)
        {
            Submesh submesh;
            submesh.MaterialName = currentMaterialName;
            submesh.IndexStart = currentSubmeshStart;
            submesh.IndexCount = static_cast<uint32_t>(outIndices.size()) - currentSubmeshStart;
            outSubmeshes.push_back(submesh);

            currentMaterialName = materialName;
            currentSubmeshStart = static_cast<uint32_t>(outIndices.size());
        }

        for (size_t i = 1; i + 1 < polygonVertices.size(); ++i)
        {
            const Vertex triangle[3] =
            {
                polygonVertices[0],
                polygonVertices[i],
                polygonVertices[i + 1]
            };

            for (const Vertex& triangleVertex : triangle)
            {
                outVertices.push_back(triangleVertex);
                outIndices.push_back(static_cast<uint32_t>(outVertices.size() - 1));
            }
        }

        polygonVertices.clear();
        ++polygonIndex;
    }

    if (outVertices.empty())
        return false;

    CenterVertices(outVertices);
    NormalizeVertices(outVertices, 2.0f);

    if (!currentMaterialName.empty() && outIndices.size() > currentSubmeshStart)
    {
        Submesh submesh;
        submesh.MaterialName = currentMaterialName;
        submesh.IndexStart = currentSubmeshStart;
        submesh.IndexCount = static_cast<uint32_t>(outIndices.size()) - currentSubmeshStart;
        outSubmeshes.push_back(submesh);
    }

    if (outSubmeshes.empty())
    {
        Submesh submesh;
        submesh.MaterialName = outMaterials.empty() ? "Default" : outMaterials.front().Name;
        submesh.IndexStart = 0;
        submesh.IndexCount = static_cast<uint32_t>(outIndices.size());
        outSubmeshes.push_back(submesh);
    }

    return true;
}

bool LoadMTL(
    const std::string& filename,
    std::vector<ParsedMaterial>& materials)
{
    materials.clear();

    std::ifstream file(filename);
    if (!file.is_open())
        return false;

    std::string line;
    ParsedMaterial current;

    while (std::getline(file, line))
    {
        if (line.rfind("newmtl ", 0) == 0)
        {
            if (!current.Name.empty())
                materials.push_back(current);

            current = ParsedMaterial{};
            current.Name = line.substr(7);
        }
        else if (line.rfind("map_Kd ", 0) == 0)
        {
            current.DiffuseMap = line.substr(7);
        }
        else if (line.rfind("Kd ", 0) == 0)
        {
            std::stringstream stream(line.substr(3));
            stream >> current.Kd.x >> current.Kd.y >> current.Kd.z;
        }
    }

    if (!current.Name.empty())
        materials.push_back(current);

    return true;
}
