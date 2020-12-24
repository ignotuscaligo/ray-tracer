#include "Transform.h"

Vector Transform::forward() const
{
    return rotation * Vector{0, 0, 1.0};
}
