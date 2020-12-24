#pragma once

#include "Material.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class MaterialLibrary
{
public:
    void addMaterial(std::shared_ptr<Material> material);
    size_t indexForName(const std::string& name) const;
    std::shared_ptr<Material> fetchMaterial(const std::string& name) const;
    std::shared_ptr<Material> fetchMaterialByIndex(size_t index) const;

private:
    std::vector<std::shared_ptr<Material>> m_materials;
    std::unordered_map<std::string, size_t> m_indexMap;
};
