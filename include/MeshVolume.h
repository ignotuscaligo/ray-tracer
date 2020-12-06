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
    MeshVolume(const std::vector<Triangle>& triangles);

    std::optional<Hit> castRay(const Ray& ray) const override;

private:
    Tree<Triangle> m_tree;
};
