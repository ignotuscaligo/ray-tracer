#pragma once

#include "Mesh.h"
#include "Library.h"

#include <filesystem>
#include <string>

class MeshLibrary : public Library<Mesh>
{
public:
    MeshLibrary();

    void addFromFile(const std::filesystem::path& path);
};
