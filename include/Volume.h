#pragma once

#include "Hit.h"
#include "Ray.h"
#include "Object.h"

#include <optional>
#include <string>

class Volume : public Object
{
public:
    Volume(size_t materialIndex);

    std::optional<Hit> castRay(const Ray& ray) const;

protected:
    virtual std::optional<Hit> castTransformedRay(const Ray& ray) const;

private:
    Ray transformRay(const Ray& ray) const;

    size_t m_materialIndex;
};
