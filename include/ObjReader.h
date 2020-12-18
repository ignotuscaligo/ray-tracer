#pragma once

#include "Triangle.h"

#include <vector>
#include <string>

namespace ObjReader
{

std::vector<Triangle> loadMesh(const std::string& filename);

}
