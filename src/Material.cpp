#include "Material.h"

#include <stdexcept>

Material::Material(const std::string& name)
    : m_name(name)
{
}

std::string Material::name() const
{
    return m_name;
}
