#pragma once

#include "Hit.h"
#include "Ray.h"
#include "Object.h"

#include <optional>
#include <string>

class Volume : public Object
{
public:
    Volume();

    virtual std::optional<Hit> castRay(const Ray& ray) const;
};
