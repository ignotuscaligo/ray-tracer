#pragma once

#include "Image.h"

#include <png.h>
#include <stdio.h>
#include <string>

class PngWriter
{
public:
    PngWriter(const std::string& filename);
    ~PngWriter();

    bool valid();

    png_structp structPtr();
    png_infop infoPtr();

    void setTitle(const std::string& title);
    void writeImage(Image& image);

private:
    FILE* m_file = nullptr;
    png_structp m_structPtr = nullptr;
    png_infop m_infoPtr = nullptr;
};
