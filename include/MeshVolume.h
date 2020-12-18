#pragma once

#include "Hit.h"
#include "Ray.h"
#include "Tree.h"
#include "Volume.h"

#include <optional>
#include <vector>

class MeshVolume : public Volume
{
public:
    MeshVolume(size_t materialIndex, const std::vector<Triangle>& triangles);

protected:
    std::optional<Hit> castTransformedRay(const Ray& ray) const override;

private:
    Tree<Triangle> m_tree;
};
