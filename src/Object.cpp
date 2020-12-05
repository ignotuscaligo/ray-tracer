#include "Object.h"

#include <algorithm>

Object::Object(const std::string& iname)
    : name(iname)
{
}

Vector Object::position() const
{
    if (parent)
    {
        return parent->position() + (parent->rotation() * transform.position);
    }
    else
    {
        return transform.position;
    }
}

Quaternion Object::rotation() const
{
    if (parent)
    {
        return parent->rotation() * transform.rotation;
    }
    else
    {
        return transform.rotation;
    }
}

Vector Object::forward() const
{
    return rotation() * Vector(0, 0, 1);
}

void Object::setParent(std::shared_ptr<Object> child, std::shared_ptr<Object> parent)
{
    if (child->parent == parent)
    {
        return;
    }

    if (child->parent)
    {
        auto it = std::find(std::begin(parent->children), std::end(parent->children), child);

        if (it != parent->children.end())
        {
            parent->children.erase(it);
        }

        child->parent = nullptr;
    }

    if (parent)
    {
        parent->children.push_back(child);
        child->parent = parent;
    }
}
