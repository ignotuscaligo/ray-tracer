// Pure CPU OBJ -> render-vertex extraction. Deliberately GL-free so it can be
// unit-tested without an OpenGL context (the GL upload lives in RasterMesh.cpp).

#include "RasterMesh.h"

#include <tiny_obj_loader.h>

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
