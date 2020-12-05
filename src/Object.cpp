#include "Object.h"

#include <algorithm>

Object::Object()
    : TypedObject()
{
    registerType<Object>();
}

void Object::name(const std::string& name)
{
    m_name = name;
}

std::string Object::name() const
{
    return m_name;
}

Vector Object::position() const
{
    if (m_parent)
    {
        return m_parent->position() + (m_parent->rotation() * transform.position);
    }
    else
    {
        return transform.position;
    }
}

Quaternion Object::rotation() const
{
    if (m_parent)
    {
        return m_parent->rotation() * transform.rotation;
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
    if (child->m_parent == parent)
    {
        return;
    }

    if (child->m_parent)
    {
        auto it = std::find(std::begin(parent->m_children), std::end(parent->m_children), child);

        if (it != parent->m_children.end())
        {
            parent->m_children.erase(it);
        }

        child->m_parent = nullptr;
    }

    if (parent)
    {
        parent->m_children.push_back(child);
        child->m_parent = parent;
    }
}
