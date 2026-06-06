#pragma once

#include <glm/glm.hpp>

#include <cstddef>

// Owns the GL geometry for the editor viewport's spatial reference overlays:
//
//   * an XZ ground-plane grid (lines on Y=0, like Blender/Maya/Unity), with the
//     two origin axes (the lines through X=0 and Z=0) subtly emphasized, and
//   * an origin gnomon: the three world axes drawn from the origin in the
//     standard colors (X red, Y green, Z blue) so orientation is always clear.
//
// Both are baked into a single per-vertex-colored line VBO and drawn with the
// flat line shader (Shaders.h). Construction is cheap; build() requires a
// current GL context (it uploads the VBO). The geometry is static, so it is
// built once and re-drawn each frame with the camera's view/projection.
//
// GL ownership mirrors RasterMesh: this object holds the VAO/VBO and deletes
// them on destruction. It performs no shader management — the caller binds the
// line shader and sets uView/uProjection, then calls draw().
class ViewportGrid
{
public:
    ViewportGrid() = default;
    ~ViewportGrid();

    ViewportGrid(const ViewportGrid&) = delete;
    ViewportGrid& operator=(const ViewportGrid&) = delete;

    // Build the grid + gnomon line geometry and upload it to a GL VBO.
    // `halfExtent` is how far the grid reaches from the origin along X and Z
    // (in world units); `spacing` is the gap between grid lines. The gnomon arm
    // length is derived from the spacing so it reads at a sensible scale.
    // Requires a current GL context. Safe to call again to rebuild.
    void build(float halfExtent = 20.0f, float spacing = 1.0f);

    // Draw the ground-plane grid lines (and the emphasized origin cross).
    // Requires the line shader bound + its uView/uProjection set. No-op if not
    // built. Typically drawn with depth-testing on.
    void drawGrid() const;

    // Draw the origin gnomon (the three colored axes). Drawn separately so the
    // caller can render it with depth-writes/test disabled — the gnomon arms lie
    // in the Y=0 plane, coplanar with the grid's origin lines, so co-drawing
    // them would z-fight. No-op if not built.
    void drawGnomon() const;

    bool isBuilt() const { return m_vao != 0; }

private:
    void destroy();

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    std::size_t m_gridVertexCount = 0;    // grid lines, at the front of the VBO
    std::size_t m_gnomonVertexCount = 0;  // gnomon arms, after the grid lines
};
