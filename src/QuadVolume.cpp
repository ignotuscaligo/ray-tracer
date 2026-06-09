#include "QuadVolume.h"

QuadVolume::QuadVolume()
    : Volume()
    , m_quad({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0})
{
    registerType<QuadVolume>();
}

QuadVolume::QuadVolume(size_t materialIndex)
    : Volume(materialIndex)
    , m_quad({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0})
{
    registerType<QuadVolume>();
}

QuadVolume::QuadVolume(size_t materialIndex, const Quad& quad)
    : Volume(materialIndex)
    , m_quad(quad)
{
    registerType<QuadVolume>();
}

void QuadVolume::quad(const Quad& quad)
{
    m_quad = quad;
}

const Quad& QuadVolume::quad() const
{
    return m_quad;
}

void QuadVolume::origin(const Vector& origin)
{
    m_quad.origin = origin;
    m_quad.update();
}

Vector QuadVolume::origin() const
{
    return m_quad.origin;
}

void QuadVolume::edgeU(const Vector& edgeU)
{
    m_quad.edgeU = edgeU;
    m_quad.update();
}

Vector QuadVolume::edgeU() const
{
    return m_quad.edgeU;
}

void QuadVolume::edgeV(const Vector& edgeV)
{
    m_quad.edgeV = edgeV;
    m_quad.update();
}

Vector QuadVolume::edgeV() const
{
    return m_quad.edgeV;
}

std::optional<Hit> QuadVolume::castTransformedRay(const Ray& ray, std::vector<Hit>& /*castBuffer*/) const
{
    return rayIntersectsQuad(ray, m_quad);
}
