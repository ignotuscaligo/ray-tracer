#include "PngWriter.h"

#include <iostream>

PngWriter::PngWriter(const std::filesystem::path& path)
{
    m_file = fopen(path.string().c_str(), "wb");
    if (m_file == nullptr) {
        std::cout << "Could not open file " << path.generic_string() << " for writing" << std::endl;
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

PngWriter::PngWriter(PngWriter&& other) noexcept
    : m_file(std::move(other.m_file))
    , m_structPtr(std::move(other.m_structPtr))
    , m_infoPtr(std::move(other.m_infoPtr))
{
}

PngWriter::~PngWriter()
{
    if (m_infoPtr != nullptr)
    {
        png_free_data(m_structPtr, m_infoPtr, PNG_FREE_ALL, -1);
    }

    if (m_structPtr != nullptr)
    {
        png_destroy_write_struct(&m_structPtr, static_cast<png_infopp>(nullptr));
    }

    if (m_file != nullptr)
    {
        fclose(m_file);
    }
}

PngWriter& PngWriter::operator=(PngWriter&& other) noexcept
{
    m_file = std::move(other.m_file);
    m_structPtr = std::move(other.m_structPtr);
    m_infoPtr = std::move(other.m_infoPtr);
    return *this;
}

bool PngWriter::valid() const noexcept
{
    return m_file != nullptr &&
        m_structPtr != nullptr &&
        m_infoPtr != nullptr;
}

png_structp PngWriter::structPtr() noexcept
{
    return m_structPtr;
}

png_infop PngWriter::infoPtr() noexcept
{
    return m_infoPtr;
}

void PngWriter::setTitle(const std::string& title)
{
    if (!valid())
    {
        return;
    }

    png_text title_text{};
    std::vector<char> titleString;
    titleString.reserve(title.size() + 1);
    strcpy(titleString.data(), title.c_str());
    title_text.compression = PNG_TEXT_COMPRESSION_NONE;
    title_text.key = "Title";
    title_text.text = titleString.data();
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
        16, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(m_structPtr, m_infoPtr);

    // Copy image data
    png_bytepp rowPointers = new png_bytep[sizeof(png_bytep) * image.height()];
    for (size_t y = 0; y < image.height(); ++y)
    {
        rowPointers[y] = new png_byte[png_get_rowbytes(m_structPtr, m_infoPtr)];
        for (size_t x = 0; x < image.width(); ++x)
        {
            for (size_t k = 0; k < 3; ++k)
            {
                png_save_uint_16(&(rowPointers[y][(x * 6) + (k * 2)]), image.getPixel(x, y)[k]);
            }
        }
    }

    // Write image data
    png_write_image(m_structPtr, rowPointers);

    // End write
    png_write_end(m_structPtr, nullptr);

    for (size_t y = 0; y < image.height(); ++y)
    {
        delete[] rowPointers[y];
    }

    delete[] rowPointers;
}

void PngWriter::writeImage(const std::filesystem::path& path, Image& image, const std::string& title)
{
    std::cout << "---" << std::endl;
    std::cout << "Write image " << path.generic_string() << std::endl;
    PngWriter writer(path);

    if (!writer.valid())
    {
        std::cout << "Failed to initialize png writer" << std::endl;
        return;
    }

    // Set title
    writer.setTitle(title);
    writer.writeImage(image);
}
