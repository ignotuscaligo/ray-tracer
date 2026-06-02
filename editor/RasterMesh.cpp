#include "RasterMesh.h"

#include "GlHeaders.h"

#include <tiny_obj_loader.h>

#include <algorithm>
#include <limits>

MeshData loadObjMeshData(const std::string& path)
{
    MeshData out;

    tinyobj::ObjReaderConfig config;
    config.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config))
    {
        out.valid = false;
        out.error = reader.Error().empty() ? ("Failed to parse OBJ: " + path)
                                           : reader.Error();
        return out;
    }

    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();

    glm::vec3 minB{std::numeric_limits<float>::max()};
    glm::vec3 maxB{std::numeric_limits<float>::lowest()};

    for (const auto& shape : shapes)
    {
        const auto& mesh = shape.mesh;
        size_t indexOffset = 0;
        for (size_t f = 0; f < mesh.num_face_vertices.size(); ++f)
        {
            const int fv = mesh.num_face_vertices[f];
            // Config triangulates, so fv should be 3; guard anyway.
            if (fv != 3)
            {
                indexOffset += fv;
                continue;
            }

            glm::vec3 p[3];
            glm::vec3 n[3];
            bool haveNormals = true;

            for (int v = 0; v < 3; ++v)
            {
                const tinyobj::index_t idx = mesh.indices[indexOffset + v];
                p[v] = glm::vec3(
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]);

                if (idx.normal_index >= 0 && !attrib.normals.empty())
                {
                    n[v] = glm::vec3(
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]);
                }
                else
                {
                    haveNormals = false;
                }
            }

            if (!haveNormals)
            {
                // Synthesize a face normal from the triangle winding.
                const glm::vec3 faceNormal =
                    glm::normalize(glm::cross(p[1] - p[0], p[2] - p[0]));
                n[0] = n[1] = n[2] = faceNormal;
            }

            for (int v = 0; v < 3; ++v)
            {
                out.vertices.push_back(RasterVertex{p[v], n[v]});
                minB = glm::min(minB, p[v]);
                maxB = glm::max(maxB, p[v]);
            }

            indexOffset += 3;
        }
    }

    if (out.vertices.empty())
    {
        out.valid = false;
        out.error = "OBJ contained no triangles: " + path;
        return out;
    }

    out.minBound = minB;
    out.maxBound = maxB;
    out.valid = true;
    return out;
}

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
