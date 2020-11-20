#include "PngWriter.h"

#include <iostream>

PngWriter::PngWriter(const std::string& filename)
{
    std::cout << "Open file for writing" << std::endl;
    m_file = fopen(filename.c_str(), "wb");
    if (m_file == nullptr) {
        std::cout << "Could not open file " << filename << " for writing" << std::endl;
        return;
    }

    std::cout << "Initialize write structure" << std::endl;
    m_structPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (m_structPtr == nullptr)
    {
        std::cout << "Could not create write structure" << std::endl;
        return;
    }

    std::cout << "Initialize info structure" << std::endl;
    m_infoPtr = png_create_info_struct(m_structPtr);
    if (m_infoPtr == nullptr)
    {
        std::cout << "Could not create info structure" << std::endl;
        return;
    }

    std::cout << "Initialize png IO" << std::endl;
    png_init_io(m_structPtr, m_file);
}

PngWriter::~PngWriter()
{
    if (m_infoPtr != nullptr)
    {
        std::cout << "Release info struct" << std::endl;
        png_free_data(m_structPtr, m_infoPtr, PNG_FREE_ALL, -1);
    }

    if (m_structPtr != nullptr)
    {
        std::cout << "Release png struct" << std::endl;
        png_destroy_write_struct(&m_structPtr, (png_infopp)nullptr);
    }

    if (m_file != nullptr)
    {
        std::cout << "Close file" << std::endl;
        fclose(m_file);
    }
}

bool PngWriter::valid()
{
    return m_file != nullptr &&
        m_structPtr != nullptr &&
        m_infoPtr != nullptr;
}

png_structp PngWriter::structPtr()
{
    return m_structPtr;
}

png_infop PngWriter::infoPtr()
{
    return m_infoPtr;
}

void PngWriter::setTitle(const std::string& title)
{
    if (!valid())
    {
        return;
    }

    std::cout << "Set title" << std::endl;
    png_text title_text;
    title_text.compression = PNG_TEXT_COMPRESSION_NONE;
    title_text.key = "Title";
    title_text.text = (png_charp)title.c_str();
    png_set_text(m_structPtr, m_infoPtr, &title_text, 1);
}

void PngWriter::writeImage(Image& image)
{
    if (!valid())
    {
        return;
    }

    // Write header (8 bit colour depth)
    std::cout << "Set png header" << std::endl;
    png_set_IHDR(m_structPtr, m_infoPtr, image.width(), image.height(),
        8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    std::cout << "Write info" << std::endl;
    png_write_info(m_structPtr, m_infoPtr);

    // Write image data
    std::cout << "Write image data" << std::endl;
    for (int y = 0; y < image.height(); y++)
    {
        png_write_row(m_structPtr, (png_bytep)image.getRow(y));
    }

    // End write
    std::cout << "End image write" << std::endl;
    png_write_end(m_structPtr, nullptr);
}