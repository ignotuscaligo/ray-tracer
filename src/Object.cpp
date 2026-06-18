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
    if (auto parent = m_parent.lock())
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
    if (auto parent = m_parent.lock())
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

std::shared_ptr<Object> Object::getChild(const std::string& name) const
{
    for (auto& child : m_children)
    {
        if (child->name() == name)
        {
            return child;
        }
    }

    return {};
}

void Object::setParent(std::shared_ptr<Object> child, std::shared_ptr<Object> parent)
{
    if (child->m_parent.lock() == parent)
    {
        return;
    }

    if (child->m_parent.lock())
    {
        auto it = std::find(std::begin(parent->m_children), std::end(parent->m_children), child);

        if (it != parent->m_children.end())
        {
            parent->m_children.erase(it);
        }

        child->m_parent.reset();
    }

    if (parent)
    {
        parent->m_children.push_back(child);
        child->m_parent = parent;
    }
}
