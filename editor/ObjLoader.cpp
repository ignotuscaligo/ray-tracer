// Pure CPU OBJ -> render-vertex extraction. Deliberately GL-free so it can be
// unit-tested without an OpenGL context (the GL upload lives in RasterMesh.cpp).

#include "RasterMesh.h"

#include <tiny_obj_loader.h>

#include <limits>

namespace
{

// Shared extraction: pull triangles from the parsed OBJ into MeshData. When
// `shapeFilter` is non-empty, only the shape with that exact name is extracted
// (the renderer's $mesh-by-name convention); otherwise all shapes are merged.
MeshData extractMeshData(const tinyobj::ObjReader& reader, const std::string& path,
                         const std::string& shapeFilter)
{
    MeshData out;

    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();

    glm::vec3 minB{std::numeric_limits<float>::max()};
    glm::vec3 maxB{std::numeric_limits<float>::lowest()};

    for (const auto& shape : shapes)
    {
        if (!shapeFilter.empty() && shape.name != shapeFilter)
        {
            continue;
        }
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
        out.error = shapeFilter.empty()
                        ? ("OBJ contained no triangles: " + path)
                        : ("OBJ shape '" + shapeFilter + "' not found or empty in: " + path);
        return out;
    }

    out.minBound = minB;
    out.maxBound = maxB;
    out.valid = true;
    return out;
}

tinyobj::ObjReader parseObj(const std::string& path, MeshData& out, bool& ok)
{
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config))
    {
        out.valid = false;
        out.error = reader.Error().empty() ? ("Failed to parse OBJ: " + path)
                                           : reader.Error();
        ok = false;
        return reader;
    }
    ok = true;
    return reader;
}

}  // namespace

MeshData loadObjMeshData(const std::string& path)
{
    MeshData out;
    bool ok = false;
    tinyobj::ObjReader reader = parseObj(path, out, ok);
    if (!ok)
    {
        return out;
    }
    return extractMeshData(reader, path, /*shapeFilter=*/"");
}

MeshData loadObjShapeData(const std::string& path, const std::string& shapeName)
{
    MeshData out;
    bool ok = false;
    tinyobj::ObjReader reader = parseObj(path, out, ok);
    if (!ok)
    {
        return out;
    }
    return extractMeshData(reader, path, shapeName);
}
