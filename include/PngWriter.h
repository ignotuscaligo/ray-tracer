#pragma once

#include "Image.h"

#include <png.h>
#include <stdio.h>
#include <string>

class PngWriter
{
public:
    PngWriter(const std::string& filename);
    PngWriter(const PngWriter& other) = delete;
    PngWriter(PngWriter&& other) noexcept;
    ~PngWriter();

    PngWriter& operator=(const PngWriter& other) = delete;
    PngWriter& operator=(PngWriter&& other) noexcept;

    bool valid() const noexcept;

    png_structp structPtr() noexcept;
    png_infop infoPtr() noexcept;

    void setTitle(const std::string& title);
    void writeImage(Image& image);

    static void writeImage(const std::string& filename, Image& image, const std::string& title);

private:
    FILE* m_file = nullptr;
    png_structp m_structPtr = nullptr;
    png_infop m_infoPtr = nullptr;
};
