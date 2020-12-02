#pragma once

#include "Hit.h"
#include "Ray.h"

#include <optional>

class Volume
{
public:
    virtual std::optional<Hit> castRay(const Ray& ray) const;
};
