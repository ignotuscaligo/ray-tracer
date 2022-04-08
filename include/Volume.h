#pragma once

#include "Hit.h"
#include "Ray.h"
#include "Object.h"

#include <optional>
#include <string>
#include <vector>

class Volume : public Object
{
public:
    Volume();
    Volume(size_t materialIndex);

    void materialIndex(size_t materialIndex);
    size_t materialIndex() const;

    std::optional<Hit> castRay(const Ray& ray, std::vector<Hit>& castBuffer) const;

protected:
    virtual std::optional<Hit> castTransformedRay(const Ray& ray, std::vector<Hit>& castBuffer) const;

private:
    Ray transformRay(const Ray& ray) const;

    size_t m_materialIndex;
};
