#pragma once

#include "Hit.h"
#include "Mesh.h"
#include "Ray.h"
#include "Volume.h"

#include <optional>
#include <vector>
#include <memory>

class MeshVolume : public Volume
{
public:
    MeshVolume();
    MeshVolume(size_t materialIndex, std::shared_ptr<Mesh> mesh);

    void mesh(std::shared_ptr<Mesh> mesh);
    std::shared_ptr<Mesh> mesh() const;

protected:
    std::optional<Hit> castTransformedRay(const Ray& ray, std::vector<Hit>& castBuffer) const override;

private:
    std::shared_ptr<Mesh> m_mesh;
};
