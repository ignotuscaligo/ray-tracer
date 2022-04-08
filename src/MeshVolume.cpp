#include "MeshVolume.h"

MeshVolume::MeshVolume()
    : Volume()
{
    registerType<MeshVolume>();
}

MeshVolume::MeshVolume(size_t materialIndex, std::shared_ptr<Mesh> mesh)
    : Volume(materialIndex)
    , m_mesh(mesh)
{
    registerType<MeshVolume>();
}

void MeshVolume::mesh(std::shared_ptr<Mesh> mesh)
{
    m_mesh = mesh;
}

std::shared_ptr<Mesh> MeshVolume::mesh() const
{
    return m_mesh;
}

std::optional<Hit> MeshVolume::castTransformedRay(const Ray& ray, std::vector<Hit>& castBuffer) const
{
    return m_mesh->castRay(ray, castBuffer);
}
