// GL ownership for a raster mesh. The pure OBJ -> vertex extraction lives in
// editor/ObjLoader.cpp (GL-free, unit-tested); this file only does GL.

#include "RasterMesh.h"

#include "GlHeaders.h"

RasterMesh::~RasterMesh()
{
    destroy();
}

void RasterMesh::destroy()
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
    m_vertexCount = 0;
}

void RasterMesh::upload(const MeshData& data)
{
    destroy();

    if (data.vertices.empty())
    {
        return;
    }

    m_vertexCount = data.vertices.size();
    m_minBound = data.minBound;
    m_maxBound = data.maxBound;

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(data.vertices.size() * sizeof(RasterVertex)),
        data.vertices.data(),
        GL_STATIC_DRAW);

    // location 0: position (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex),
        reinterpret_cast<void*>(offsetof(RasterVertex, position)));

    // location 1: normal (vec3)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE, sizeof(RasterVertex),
        reinterpret_cast<void*>(offsetof(RasterVertex, normal)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void RasterMesh::draw() const
{
    if (!m_vao || m_vertexCount == 0)
    {
        return;
    }
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertexCount));
    glBindVertexArray(0);
}
