#include "MeshVolume.h"

MeshVolume::MeshVolume(size_t materialIndex, std::shared_ptr<Mesh> mesh)
    : Volume(materialIndex)
    , m_mesh(mesh)
{
    registerType<MeshVolume>();
}

std::optional<Hit> MeshVolume::castTransformedRay(const Ray& ray, std::vector<Hit>& castBuffer) const
{
    return m_mesh->castRay(ray, castBuffer);
}
