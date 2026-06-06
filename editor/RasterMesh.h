#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <vector>

// A single render-ready vertex: position + normal, interleaved for a GL VBO.
struct RasterVertex
{
    glm::vec3 position;
    glm::vec3 normal;
};

// CPU-side mesh data extracted from an OBJ file, plus its axis-aligned bounds.
// This is the pure-data product of OBJ loading; uploading to GL is a separate
// step (RasterMesh::upload) so the extraction is unit-testable without a GL
// context.
struct MeshData
{
    std::vector<RasterVertex> vertices;  // 3 per triangle (non-indexed)
    glm::vec3 minBound{0.0f};
    glm::vec3 maxBound{0.0f};
    bool valid = false;
    std::string error;

    std::size_t triangleCount() const { return vertices.size() / 3; }
};

// Load an OBJ file into flat triangle data with per-vertex normals. If the OBJ
// has no normals, face normals are synthesized. On failure, MeshData::valid is
// false and MeshData::error describes the problem. Pure CPU work — no GL calls.
MeshData loadObjMeshData(const std::string& path);

// Load a single NAMED sub-shape (an OBJ `o <name>` group) from an OBJ file. This
// is how the renderer's scene format references mesh pieces ($mesh: "Floor",
// "Knot", ...): one OBJ contains several named objects and a MeshVolume picks one
// by name. If `shapeName` is empty, behaves like loadObjMeshData (all shapes).
// On a missing shape, MeshData::valid is false. Pure CPU work — no GL calls.
MeshData loadObjShapeData(const std::string& path, const std::string& shapeName);

// Owns the GL VAO/VBO for a mesh and knows how to draw it. Construction is
// cheap; upload() requires a current GL context.
class RasterMesh
{
public:
    RasterMesh() = default;
    ~RasterMesh();

    RasterMesh(const RasterMesh&) = delete;
    RasterMesh& operator=(const RasterMesh&) = delete;

    // Upload CPU mesh data into a GL VBO/VAO. Replaces any previously uploaded
    // mesh. Requires a current GL context.
    void upload(const MeshData& data);

    void draw() const;

    bool hasMesh() const { return m_vertexCount > 0; }
    std::size_t vertexCount() const { return m_vertexCount; }

    glm::vec3 minBound() const { return m_minBound; }
    glm::vec3 maxBound() const { return m_maxBound; }

private:
    void destroy();

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    std::size_t m_vertexCount = 0;
    glm::vec3 m_minBound{0.0f};
    glm::vec3 m_maxBound{0.0f};
};
