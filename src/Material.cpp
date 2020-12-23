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

void MaterialLibrary::addMaterial(std::shared_ptr<Material> material)
{
    if (m_indexMap.count(material->name()) > 0)
    {
        throw std::runtime_error("Library already contains material named " + material->name());
    }

    m_indexMap.insert_or_assign(material->name(), m_materials.size());
    m_materials.push_back(material);
}

size_t MaterialLibrary::indexForName(const std::string& name) const
{
    if (m_indexMap.count(name) == 0)
    {
        throw std::runtime_error("Library does not contain material named " + name);
    }

    return m_indexMap.at(name);
}

std::shared_ptr<Material> MaterialLibrary::fetchMaterial(const std::string& name) const
{
    return m_materials[indexForName(name)];
}

std::shared_ptr<Material> MaterialLibrary::fetchMaterialByIndex(size_t index) const
{
    if (index >= m_materials.size())
    {
        throw std::runtime_error("Library does not contain material with index " + std::to_string(index));
    }

    return m_materials[index];
}
