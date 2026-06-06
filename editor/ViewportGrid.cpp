// GL ownership + geometry baking for the viewport grid and origin gnomon.
// Pure GL here; the line shader lives in Shaders.h and is bound by the caller.

#include "ViewportGrid.h"

#include "GlHeaders.h"

#include <cmath>
#include <vector>

namespace
{

// One line vertex: position + per-vertex RGB color, interleaved for the VBO and
// matching the kLineVertexShader attribute layout (loc 0 = position, 1 = color).
struct LineVertex
{
    glm::vec3 position;
    glm::vec3 color;
};

void pushLine(std::vector<LineVertex>& out,
              const glm::vec3& a,
              const glm::vec3& b,
              const glm::vec3& color)
{
    out.push_back({a, color});
    out.push_back({b, color});
}

}  // namespace

ViewportGrid::~ViewportGrid()
{
    destroy();
}

void ViewportGrid::destroy()
{
    if (m_vbo)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    m_gridVertexCount = 0;
    m_gnomonVertexCount = 0;
}

void ViewportGrid::build(float halfExtent, float spacing)
{
    destroy();

    if (spacing <= 0.0f || halfExtent <= 0.0f)
    {
        return;
    }

    // Color palette. Neutral grey for ordinary grid lines; a slightly brighter
    // grey for the two lines that pass through the origin (the X=0 and Z=0
    // lines) so the origin cross reads without competing with the gnomon. The
    // gnomon arms use the canonical X=red, Y=green, Z=blue.
    const glm::vec3 gridColor(0.32f, 0.32f, 0.36f);
    const glm::vec3 originLineColor(0.55f, 0.55f, 0.60f);
    const glm::vec3 axisX(0.90f, 0.20f, 0.20f);
    const glm::vec3 axisY(0.20f, 0.85f, 0.25f);
    const glm::vec3 axisZ(0.25f, 0.45f, 0.95f);

    std::vector<LineVertex> verts;

    // Number of grid lines on each side of the origin along one axis.
    const int lines = static_cast<int>(std::floor(halfExtent / spacing));
    const float extent = lines * spacing;

    // Lines parallel to X (varying Z) and lines parallel to Z (varying X). The
    // i==0 line passes through the origin and gets the emphasized color; we draw
    // those origin lines in the grid color here and overlay the colored gnomon
    // axes on top, so the ground plane keeps a clean neutral cross while the
    // gnomon supplies the orientation cue. (Emphasis via originLineColor.)
    for (int i = -lines; i <= lines; ++i)
    {
        const float t = i * spacing;
        const bool isOrigin = (i == 0);
        const glm::vec3 c = isOrigin ? originLineColor : gridColor;

        // Line parallel to the X axis at Z = t.
        pushLine(verts, glm::vec3(-extent, 0.0f, t), glm::vec3(extent, 0.0f, t), c);
        // Line parallel to the Z axis at X = t.
        pushLine(verts, glm::vec3(t, 0.0f, -extent), glm::vec3(t, 0.0f, extent), c);
    }

    // Grid lines occupy the front of the VBO; record the count so drawGrid()
    // draws exactly them.
    m_gridVertexCount = verts.size();

    // Origin gnomon: three axes drawn from the origin in standard colors. Length
    // is a few grid cells so it reads clearly against the grid. Stored after the
    // grid lines; drawGnomon() draws this tail with depth disabled so the colored
    // arms sit on top of the neutral origin cross instead of z-fighting it.
    const float arm = spacing * 3.0f;
    pushLine(verts, glm::vec3(0.0f), glm::vec3(arm, 0.0f, 0.0f), axisX);
    pushLine(verts, glm::vec3(0.0f), glm::vec3(0.0f, arm, 0.0f), axisY);
    pushLine(verts, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, arm), axisZ);

    m_gnomonVertexCount = verts.size() - m_gridVertexCount;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(verts.size() * sizeof(LineVertex)),
        verts.data(),
        GL_STATIC_DRAW);

    // location 0: position (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
        reinterpret_cast<void*>(offsetof(LineVertex, position)));

    // location 1: color (vec3)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
        reinterpret_cast<void*>(offsetof(LineVertex, color)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ViewportGrid::drawGrid() const
{
    if (!m_vao || m_gridVertexCount == 0)
    {
        return;
    }
    glBindVertexArray(m_vao);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(m_gridVertexCount));
    glBindVertexArray(0);
}

void ViewportGrid::drawGnomon() const
{
    if (!m_vao || m_gnomonVertexCount == 0)
    {
        return;
    }
    glBindVertexArray(m_vao);
    glDrawArrays(GL_LINES, static_cast<GLint>(m_gridVertexCount),
                 static_cast<GLsizei>(m_gnomonVertexCount));
    glBindVertexArray(0);
}
