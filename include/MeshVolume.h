#pragma once

#include "Hit.h"
#include "Ray.h"
#include "TriangleTree.h"
#include "Volume.h"

#include <optional>
#include <vector>

class MeshVolume : public Volume
{
public:
    MeshVolume(const std::vector<Triangle>& triangles);

    std::optional<Hit> castRay(const Ray& ray) const override;

private:
    TriangleTree m_tree;
};
