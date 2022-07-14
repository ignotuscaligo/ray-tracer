#include "MeshLibrary.h"

#include "ObjReader.h"

#include <memory>
#include <vector>

MeshLibrary::MeshLibrary()
    : Library<Mesh>()
{
}

void MeshLibrary::addFromFile(const std::filesystem::path& path)
{
    std::vector<std::shared_ptr<Mesh>> meshes = ObjReader::loadMeshes(path);

    for (auto& mesh : meshes)
    {
        if (mesh->name().empty())
        {
            mesh->name("mesh_" + std::to_string(size()));
        }

        add(mesh);
    }
}
