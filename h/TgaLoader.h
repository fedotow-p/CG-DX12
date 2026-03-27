#pragma once
#include <string>
#include <vector>

struct TgaImage
{
    int width;
    int height;
    int channels;
    std::vector<unsigned char> data;
};

bool LoadTGA(const std::string& filename, TgaImage& outImage);