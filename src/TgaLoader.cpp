#include "../h/TgaLoader.h"
#include <fstream>

bool LoadTGA(const std::string& filename, TgaImage& outImage)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file)
        return false;

    unsigned char header[18];
    file.read((char*)header, 18);

    outImage.width = header[12] | (header[13] << 8);
    outImage.height = header[14] | (header[15] << 8);
    outImage.channels = header[16] / 8;

    int imageSize = outImage.width * outImage.height * outImage.channels;
    outImage.data.resize(imageSize);

    file.read((char*)outImage.data.data(), imageSize);

    return true;
}