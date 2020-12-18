#pragma once

#include "Color.h"
#include "Photon.h"
#include "Vector.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

class Material
{
public:
    Material() = default;
    Material(const std::string& name);

    std::string name() const;

    virtual Color colorForHit(const Vector& pixelDirection, const PhotonHit& photonHit) const;

private:
    std::string m_name;
};

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
