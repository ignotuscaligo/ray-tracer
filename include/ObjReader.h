#pragma once

#include "Triangle.h"
#include "Mesh.h"

#include <filesystem>
#include <memory>
#include <vector>
#include <string>

namespace ObjReader
{

std::vector<std::shared_ptr<Mesh>> loadMeshes(const std::filesystem::path& path);

}
