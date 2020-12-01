#include "Object.h"

#include <algorithm>

Object::Object(const std::string& iname)
    : name(iname)
{
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
