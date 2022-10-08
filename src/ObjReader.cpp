#include "ObjReader.h"

#include "Vector.h"

#include <array>
#include <exception>
#include <iostream>
#include <tiny_obj_loader.h>

namespace ObjReader
{

std::vector<std::shared_ptr<Mesh>> loadMeshes(const std::filesystem::path& path)
{
    std::cout << "---" << std::endl;
    std::cout << "Loading meshes from OBJ " << path.generic_string() << std::endl;

    tinyobj::attrib_t attrib;

    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string err;
    bool result = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, path.string().c_str());

    if (!err.empty())
    {
        throw std::runtime_error(std::string("ObjReader::loadMeshes error: ") + err);
    }

    std::vector<std::shared_ptr<Mesh>> meshes;
    std::vector<Triangle> objTriangles;
    std::array<Vector, 3> points;
    std::array<Vector, 3> normals;

    if (result)
    {
        std::cout << "Loaded obj successfully" << std::endl;

        std::cout << "Found " << shapes.size() << " shapes" << std::endl;

        for (const auto& shape : shapes)
        {
            size_t indexOffset = 0;

            for (const size_t vertexCount : shape.mesh.num_face_vertices)
            {
                for (size_t v = 0; v < vertexCount; ++v)
                {
                    tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
                    size_t vertexIndex = 3 * static_cast<size_t>(idx.vertex_index);
                    size_t normalIndex = 3 * static_cast<size_t>(idx.normal_index);
                    points[v].x = attrib.vertices[vertexIndex + 0];
                    points[v].y = attrib.vertices[vertexIndex + 1];
                    points[v].z = attrib.vertices[vertexIndex + 2];

                    normals[v].x = attrib.normals[normalIndex + 0];
                    normals[v].y = attrib.normals[normalIndex + 1];
                    normals[v].z = attrib.normals[normalIndex + 2];
                }

                Triangle triangle{points[0], points[1], points[2]};
                triangle.aNormal = normals[0];
                triangle.bNormal = normals[1];
                triangle.cNormal = normals[2];

                objTriangles.push_back(triangle);

                indexOffset += vertexCount;
            }

            std::cout << "Adding shape " << shape.name << " with " << objTriangles.size() << " triangles" << std::endl;

            meshes.push_back(std::make_shared<Mesh>(shape.name, objTriangles));
            objTriangles.clear();
        }
    }
    else
    {
        std::cout << "Failed to load obj" << std::endl;
    }

    std::cout << "Loaded " << meshes.size() << " meshes" << std::endl;

    return meshes;
}

}
