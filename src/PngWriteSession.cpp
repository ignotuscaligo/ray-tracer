#include "PngWriteSession.h"

#include <cstring>

namespace
{

// Largest image dimension we accept. libpng's own hard cap is 1,000,000 per
// side; we mirror that so a bogus dimension is rejected up front rather than
// deep inside png_set_IHDR.
constexpr uint32_t kMaxDimension = 1000000U;

}  // namespace

PngWriteSession::PngWriteSession(const std::filesystem::path& path) noexcept
{
    m_file = fopen(path.string().c_str(), "wb");
    if (m_file == nullptr)
    {
        return;
    }

    m_structPtr =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (m_structPtr == nullptr)
    {
        reset();
        return;
    }

    m_infoPtr = png_create_info_struct(m_structPtr);
    if (m_infoPtr == nullptr)
    {
        reset();
        return;
    }

    png_init_io(m_structPtr, m_file);
}

PngWriteSession::PngWriteSession(PngWriteSession&& other) noexcept
    : m_file(other.m_file)
    , m_structPtr(other.m_structPtr)
    , m_infoPtr(other.m_infoPtr)
    , m_written(other.m_written)
{
    other.m_file = nullptr;
    other.m_structPtr = nullptr;
    other.m_infoPtr = nullptr;
    other.m_written = false;
}

PngWriteSession& PngWriteSession::operator=(PngWriteSession&& other) noexcept
{
    if (this != &other)
    {
        reset();
        m_file = other.m_file;
        m_structPtr = other.m_structPtr;
        m_infoPtr = other.m_infoPtr;
        m_written = other.m_written;
        other.m_file = nullptr;
        other.m_structPtr = nullptr;
        other.m_infoPtr = nullptr;
        other.m_written = false;
    }
    return *this;
}

PngWriteSession::~PngWriteSession()
{
    reset();
}

void PngWriteSession::reset() noexcept
{
    if (m_structPtr != nullptr)
    {
        // png_destroy_write_struct frees the info struct too when given its
        // address; pass &m_infoPtr so PNG_FREE_ALL data is released exactly
        // once. It nulls both pointers it is handed.
        png_destroy_write_struct(&m_structPtr,
                                 m_infoPtr != nullptr ? &m_infoPtr : nullptr);
    }
    m_structPtr = nullptr;
    m_infoPtr = nullptr;

    if (m_file != nullptr)
    {
        fclose(m_file);
        m_file = nullptr;
    }
}

bool PngWriteSession::valid() const noexcept
{
    return m_file != nullptr && m_structPtr != nullptr && m_infoPtr != nullptr;
}

bool PngWriteSession::writeRgb16(uint32_t width, uint32_t height,
                                 const std::vector<png_uint_16>& pixels,
                                 const std::string& title) noexcept
{
    if (!valid() || m_written)
    {
        return false;
    }

    // Validate dimensions and that the pixel buffer matches the 3-channel
    // (RGB) claim before touching libpng.
    if (width == 0 || height == 0 || width > kMaxDimension ||
        height > kMaxDimension)
    {
        return false;
    }
    const size_t expected =
        static_cast<size_t>(width) * static_cast<size_t>(height) * 3U;
    if (pixels.size() < expected)
    {
        return false;
    }

    // setjmp landing pad: any libpng error longjmps back here. The row buffer
    // is RAII (std::vector), so unwinding to this point leaks nothing.
    if (setjmp(png_jmpbuf(m_structPtr)))
    {
        return false;
    }

    png_set_IHDR(m_structPtr, m_infoPtr, width, height, 16, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);

    if (!title.empty())
    {
        png_text titleText{};
        // png_set_text does not modify key/text; back the text with mutable
        // member storage (survives a longjmp) and const_cast the literal key.
        m_titleScratch.assign(title.size() + 1, '\0');
        std::memcpy(m_titleScratch.data(), title.data(), title.size());
        titleText.compression = PNG_TEXT_COMPRESSION_NONE;
        titleText.key = const_cast<png_charp>("Title");
        titleText.text = m_titleScratch.data();
        png_set_text(m_structPtr, m_infoPtr, &titleText, 1);
    }

    png_write_info(m_structPtr, m_infoPtr);

    // One contiguous row buffer reused per row: width * 3 channels * 2 bytes.
    // png_save_uint_16 writes each channel big-endian, matching the previous
    // writer's on-disk bytes exactly. Held as a member so a libpng longjmp
    // mid-write does not leak it.
    const size_t rowBytes = static_cast<size_t>(width) * 3U * 2U;
    m_rowScratch.assign(rowBytes, 0);
    for (uint32_t y = 0; y < height; ++y)
    {
        const png_uint_16* src =
            pixels.data() + static_cast<size_t>(y) * width * 3U;
        for (uint32_t x = 0; x < width; ++x)
        {
            for (uint32_t k = 0; k < 3U; ++k)
            {
                png_save_uint_16(
                    &m_rowScratch[(static_cast<size_t>(x) * 6U) + (k * 2U)],
                    src[(static_cast<size_t>(x) * 3U) + k]);
            }
        }
        png_write_row(m_structPtr, m_rowScratch.data());
    }

    png_write_end(m_structPtr, nullptr);
    m_written = true;
    return true;
}

bool PngWriteSession::writeRgba8(uint32_t width, uint32_t height,
                                 const uint8_t* pixels) noexcept
{
    if (!valid() || m_written)
    {
        return false;
    }

    if (width == 0 || height == 0 || width > kMaxDimension ||
        height > kMaxDimension || pixels == nullptr)
    {
        return false;
    }

    if (setjmp(png_jmpbuf(m_structPtr)))
    {
        return false;
    }

    png_set_IHDR(m_structPtr, m_infoPtr, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);
    png_write_info(m_structPtr, m_infoPtr);

    const size_t rowBytes = static_cast<size_t>(width) * 4U;
    // Member storage so a libpng longjmp mid-write does not leak it.
    m_rowPointers.assign(height, nullptr);
    for (uint32_t y = 0; y < height; ++y)
    {
        // const_cast is safe: libpng does not modify row data on write.
        m_rowPointers[y] =
            const_cast<png_bytep>(pixels + static_cast<size_t>(y) * rowBytes);
    }

    png_write_image(m_structPtr, m_rowPointers.data());
    png_write_end(m_structPtr, nullptr);
    m_written = true;
    return true;
}
