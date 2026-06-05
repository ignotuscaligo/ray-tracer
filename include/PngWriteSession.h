#pragma once

#include <cstdint>
#include <filesystem>
#include <png.h>
#include <stdio.h>
#include <string>
#include <vector>

// One RAII owner for the libpng write handles {FILE*, png_structp, png_infop}.
//
// This is the single safe surface for emitting PNGs from anywhere in the
// codebase. It exists to fix four real hazards that two earlier hand-rolled
// libpng paths (src/PngWriter.cpp and editor/PngExport.cpp) shared:
//
//   1. Move that copied the raw pointers without nulling the source -> a
//      double png_destroy_write_struct / fclose when both objects destruct.
//   2. A raw new[]/delete[] row buffer that leaked on any early return and was
//      over-allocated by sizeof(png_bytep).
//   3. No setjmp landing pad: a libpng error longjmps into nothing -> UB.
//   4. Two independent implementations of the same thing, free to drift.
//
// The destructor always tears down exactly once, in the correct order
// (png_destroy_write_struct then fclose). The type is non-copyable; move nulls
// the source so the moved-from object is a harmless empty shell. A setjmp error
// handler is installed inside the write call (the longjmp target frame must stay
// live for the duration of the libpng calls), so a libpng error becomes a
// `false` return instead of undefined behavior.
//
// Callers do not touch the raw png_* API. Two narrow entry points cover the two
// pixel formats actually produced today:
//
//   * writeRgb16 — 16-bit RGB, the headless renderer's Image output.
//   * writeRgba8 — 8-bit RGBA, the editor's screenshot output.
//
// Both validate dimensions / channel layout / writable path up front and reject
// bad input before any libpng handle is created.
class PngWriteSession
{
public:
    // Opens `path` for binary writing and creates the libpng write + info
    // structs. On any failure the session is left !valid() and writes are
    // rejected; construction never throws.
    explicit PngWriteSession(const std::filesystem::path& path) noexcept;

    PngWriteSession(const PngWriteSession&) = delete;
    PngWriteSession& operator=(const PngWriteSession&) = delete;

    // Move transfers ownership and nulls the source so exactly one destructor
    // runs the libpng/fclose teardown. This is the fix for the historical
    // double-free.
    PngWriteSession(PngWriteSession&& other) noexcept;
    PngWriteSession& operator=(PngWriteSession&& other) noexcept;

    ~PngWriteSession();

    // True iff the file opened and both libpng structs were created.
    bool valid() const noexcept;

    // Encode `width`*`height` pixels packed as 16-bit RGB (3 channels, native
    // endianness in `pixels`, written big-endian per the PNG spec) and finish
    // the stream. `pixels` must hold width*height*3 png_uint_16 values, row
    // major, top-left origin. Optional non-empty `title` is written as a tEXt
    // "Title" entry. Returns false (without UB) on invalid input or any libpng
    // error. A session may be written at most once.
    bool writeRgb16(uint32_t width, uint32_t height,
                    const std::vector<png_uint_16>& pixels,
                    const std::string& title = {}) noexcept;

    // Encode `width`*`height` pixels packed as 8-bit RGBA (4 bytes per pixel,
    // row major, top-left origin) and finish the stream. `pixels` must hold at
    // least width*height*4 bytes. Returns false (without UB) on invalid input
    // or any libpng error. A session may be written at most once.
    bool writeRgba8(uint32_t width, uint32_t height,
                    const uint8_t* pixels) noexcept;

private:
    // Releases whatever handles are currently owned, exactly once each, in the
    // correct order. Safe to call on an already-empty session.
    void reset() noexcept;

    FILE* m_file = nullptr;
    png_structp m_structPtr = nullptr;
    png_infop m_infoPtr = nullptr;
    bool m_written = false;

    // Scratch buffers held as members on purpose: libpng reports errors via
    // longjmp, which abandons the stack WITHOUT running destructors of locals
    // live across the jump. Keeping the row/title scratch in the object means
    // the session destructor frees them on the libpng-error path too, so a
    // longjmp during a write leaks nothing.
    std::vector<png_byte> m_rowScratch;
    std::vector<png_bytep> m_rowPointers;
    std::vector<char> m_titleScratch;
};
