#pragma once

#include "Transform.h"

#include <memory>
#include <string>
#include <vector>

struct Object
{
    std::string name;
    Transform transform;
    std::shared_ptr<Object> parent;
    std::vector<std::shared_ptr<Object>> children;

    Object() = default;
    Object(const std::string& iname);

    static void setParent(std::shared_ptr<Object> child, std::shared_ptr<Object> parent);
};
