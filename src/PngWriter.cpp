#include "PngWriter.h"

#include <iostream>

PngWriter::PngWriter(const std::string& filename)
{
    m_file = fopen(filename.c_str(), "wb");
    if (m_file == nullptr) {
        std::cout << "Could not open file " << filename << " for writing" << std::endl;
        return;
    }

    m_structPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (m_structPtr == nullptr)
    {
        std::cout << "Could not create write structure" << std::endl;
        return;
    }

    m_infoPtr = png_create_info_struct(m_structPtr);
    if (m_infoPtr == nullptr)
    {
        std::cout << "Could not create info structure" << std::endl;
        return;
    }

    png_init_io(m_structPtr, m_file);
}

PngWriter::~PngWriter()
{
    if (m_infoPtr != nullptr)
    {
        png_free_data(m_structPtr, m_infoPtr, PNG_FREE_ALL, -1);
    }

    if (m_structPtr != nullptr)
    {
        png_destroy_write_struct(&m_structPtr, (png_infopp)nullptr);
    }

    if (m_file != nullptr)
    {
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
    png_set_IHDR(m_structPtr, m_infoPtr, image.width(), image.height(),
        8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(m_structPtr, m_infoPtr);

    // Write image data
    for (int y = 0; y < image.height(); y++)
    {
        png_write_row(m_structPtr, (png_bytep)image.getRow(y));
    }

    // End write
    png_write_end(m_structPtr, nullptr);
}

void PngWriter::writeImage(const std::string& filename, Image& image, const std::string& title)
{
    std::cout << "---" << std::endl;
    std::cout << "Write image " << filename << std::endl;
    PngWriter writer(filename);

    if (!writer.valid())
    {
        std::cout << "Failed to initialize png writer" << std::endl;
        return;
    }

    // Set title
    writer.setTitle(title);
    writer.writeImage(image);
}
