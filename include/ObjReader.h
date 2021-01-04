#pragma once

#include "Triangle.h"
#include "Mesh.h"

#include <memory>
#include <vector>
#include <string>

namespace ObjReader
{

std::vector<Triangle> loadTriangles(const std::string& filename);
std::vector<std::shared_ptr<Mesh>> loadMeshes(const std::string& filename);

}
