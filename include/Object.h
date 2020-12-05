#pragma once

#include "Transform.h"
#include "TypedObject.h"

#include <memory>
#include <string>
#include <vector>

class Object : public TypedObject
{
public:
    Object();

    void name(const std::string& name);
    std::string name() const;

    Vector position() const;
    Quaternion rotation() const;
    Vector forward() const;

    static void setParent(std::shared_ptr<Object> child, std::shared_ptr<Object> parent);

    Transform transform;

private:
    std::string m_name;
    std::shared_ptr<Object> m_parent;
    std::vector<std::shared_ptr<Object>> m_children;
};
