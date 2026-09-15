#define NOMINMAX
#include "../h/DdsLoader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace
{
constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(a)
        | (static_cast<uint32_t>(b) << 8)
        | (static_cast<uint32_t>(c) << 16)
        | (static_cast<uint32_t>(d) << 24);
}

struct DdsPixelFormat
{
    uint32_t Size = 0;
    uint32_t Flags = 0;
    uint32_t FourCC = 0;
    uint32_t RGBBitCount = 0;
    uint32_t RBitMask = 0;
    uint32_t GBitMask = 0;
    uint32_t BBitMask = 0;
    uint32_t ABitMask = 0;
};

struct DdsHeader
{
    uint32_t Size = 0;
    uint32_t Flags = 0;
    uint32_t Height = 0;
    uint32_t Width = 0;
    uint32_t PitchOrLinearSize = 0;
    uint32_t Depth = 0;
    uint32_t MipMapCount = 0;
    std::array<uint32_t, 11> Reserved1 = {};
    DdsPixelFormat PixelFormat = {};
    uint32_t Caps = 0;
    uint32_t Caps2 = 0;
    uint32_t Caps3 = 0;
    uint32_t Caps4 = 0;
    uint32_t Reserved2 = 0;
};

struct DdsHeaderDx10
{
    uint32_t DxgiFormat = 0;
    uint32_t ResourceDimension = 0;
    uint32_t MiscFlag = 0;
    uint32_t ArraySize = 0;
    uint32_t MiscFlags2 = 0;
};

constexpr uint32_t DDS_RESOURCE_MISC_TEXTURECUBE = 0x4;
constexpr uint32_t DDSCAPS2_CUBEMAP = 0x00000200;

bool IsBitMask(const DdsPixelFormat& pf, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    return pf.RBitMask == r
        && pf.GBitMask == g
        && pf.BBitMask == b
        && pf.ABitMask == a;
}

DXGI_FORMAT GetDxgiFormat(const DdsPixelFormat& pf, const DdsHeaderDx10* dx10Header)
{
    if (dx10Header != nullptr)
    {
        return static_cast<DXGI_FORMAT>(dx10Header->DxgiFormat);
    }

    if (pf.FourCC == MakeFourCC('D', 'X', 'T', '1'))
        return DXGI_FORMAT_BC1_UNORM;
    if (pf.FourCC == MakeFourCC('D', 'X', 'T', '3'))
        return DXGI_FORMAT_BC2_UNORM;
    if (pf.FourCC == MakeFourCC('D', 'X', 'T', '5'))
        return DXGI_FORMAT_BC3_UNORM;

    if (pf.RGBBitCount == 32)
    {
        if (IsBitMask(pf, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        if (IsBitMask(pf, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000))
            return DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    return DXGI_FORMAT_UNKNOWN;
}

bool IsBlockCompressed(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
            return true;
        default:
            return false;
    }
}

uint32_t BytesPerBlock(DXGI_FORMAT format)
{
    switch (format)
    {
        case DXGI_FORMAT_BC1_UNORM:
            return 8;
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
            return 16;
        case DXGI_FORMAT_R16G16_FLOAT:
            return 4;
        case DXGI_FORMAT_R32G32_FLOAT:
            return 8;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return 4;
        default:
            return 0;
    }
}
}

bool LoadDDS(const std::string& filename, DdsImage& outImage)
{
    outImage = {};

    std::ifstream file(filename, std::ios::binary);
    if (!file)
        return false;

    uint32_t magic = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!file || magic != MakeFourCC('D', 'D', 'S', ' '))
        return false;

    DdsHeader header = {};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.Size != 124 || header.PixelFormat.Size != 32)
        return false;

    DdsHeaderDx10 dx10Header = {};
    DdsHeaderDx10* dx10Ptr = nullptr;
    if (header.PixelFormat.FourCC == MakeFourCC('D', 'X', '1', '0'))
    {
        file.read(reinterpret_cast<char*>(&dx10Header), sizeof(dx10Header));
        if (!file)
            return false;

        dx10Ptr = &dx10Header;
    }

    const DXGI_FORMAT format = GetDxgiFormat(header.PixelFormat, dx10Ptr);
    if (format == DXGI_FORMAT_UNKNOWN)
        return false;

    const bool isBlockCompressed = IsBlockCompressed(format);
    const uint32_t elementSize = BytesPerBlock(format);
    if (elementSize == 0)
        return false;

    const uint32_t width = header.Width;
    const uint32_t height = header.Height;
    const uint32_t mipCount = (std::max)(1u, header.MipMapCount);
    const bool isCubeMap = (dx10Ptr && (dx10Header.MiscFlag & DDS_RESOURCE_MISC_TEXTURECUBE) != 0)
        || (header.Caps2 & DDSCAPS2_CUBEMAP) != 0;
    const uint32_t arraySize = (dx10Ptr ? dx10Header.ArraySize : 1u) * (isCubeMap ? 6u : 1u);
    if (arraySize == 0)
        return false;

    outImage.Width = width;
    outImage.Height = height;
    outImage.MipCount = mipCount;
    outImage.Format = format;
    outImage.ArraySize = arraySize;
    outImage.IsCubeMap = isCubeMap;
    size_t totalSize = 0;
    for (uint32_t slice = 0; slice < arraySize; ++slice)
    {
        uint32_t mipWidth = width;
        uint32_t mipHeight = height;
        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            const uint32_t rowPitch = isBlockCompressed
                ? (std::max)(1u, (mipWidth + 3) / 4) * elementSize
                : mipWidth * elementSize;
            const uint32_t rowCount = isBlockCompressed
                ? (std::max)(1u, (mipHeight + 3) / 4)
                : mipHeight;
            const uint32_t slicePitch = rowPitch * rowCount;
            outImage.Subresources.push_back({ rowPitch, rowCount, slicePitch, totalSize });
            totalSize += slicePitch;
            mipWidth = (std::max)(1u, mipWidth >> 1);
            mipHeight = (std::max)(1u, mipHeight >> 1);
        }
    }
    outImage.Data.resize(totalSize);
    file.read(reinterpret_cast<char*>(outImage.Data.data()), static_cast<std::streamsize>(totalSize));
    if (!outImage.Subresources.empty())
    {
        const auto& first = outImage.Subresources.front();
        outImage.RowPitch = first.RowPitch;
        outImage.RowCount = first.RowCount;
        outImage.SlicePitch = first.SlicePitch;
    }
    return file.good();
}
