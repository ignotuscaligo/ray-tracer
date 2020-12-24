#include "MaterialLibrary.h"

#include "Color.h"
#include "DiffuseMaterial.h"

MaterialLibrary::MaterialLibrary()
{
    addMaterial(std::make_shared<DiffuseMaterial>("Default"));
    addMaterial(std::make_shared<DiffuseMaterial>("White", Color(1.0f, 1.0f, 1.0f)));
    addMaterial(std::make_shared<DiffuseMaterial>("Black", Color(0.0f, 0.0f, 0.0f)));
    addMaterial(std::make_shared<DiffuseMaterial>("Red", Color(1.0f, 0.0f, 0.0f)));
    addMaterial(std::make_shared<DiffuseMaterial>("Yellow", Color(1.0f, 1.0f, 0.0f)));
    addMaterial(std::make_shared<DiffuseMaterial>("Green", Color(0.0f, 1.0f, 0.0f)));
    addMaterial(std::make_shared<DiffuseMaterial>("Cyan", Color(0.0f, 1.0f, 1.0f)));
    addMaterial(std::make_shared<DiffuseMaterial>("Blue", Color(0.0f, 0.0f, 1.0f)));
    addMaterial(std::make_shared<DiffuseMaterial>("Magenta", Color(1.0f, 0.0f, 1.0f)));
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
