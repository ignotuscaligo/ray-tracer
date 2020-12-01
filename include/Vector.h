#pragma once

enum class Axis
{
    X,
    Y,
    Z
};

Axis nextAxis(Axis axis);

struct Vector
{
    float x;
    float y;
    float z;

    Vector();
    Vector(float ix, float iy, float iz);

    float getAxis(Axis axis) const;
    float operator[](Axis axis) const;

    Vector operator=(const Vector& rhs);
    Vector operator+=(const Vector& rhs);
    Vector operator/=(float rhs);

    float magnitude() const;
    void normalize();
};

static Vector operator+(const Vector& lhs, const Vector& rhs)
{
	return Vector(
		lhs.x + rhs.x,
		lhs.y + rhs.y,
		lhs.z + rhs.z
	);
}

static Vector operator-(const Vector& point)
{
    return Vector(
        -point.x,
        -point.y,
        -point.z
    );
}

static Vector operator-(const Vector& lhs, const Vector& rhs)
{
    return Vector(
        lhs.x - rhs.x,
        lhs.y - rhs.y,
        lhs.z - rhs.z
    );
}

static Vector operator*(const Vector& lhs, const float& rhs)
{
    return Vector(
        lhs.x * rhs,
        lhs.y * rhs,
        lhs.z * rhs
    );
}

Vector cross(const Vector& a, const Vector& b);
float dot(const Vector& a, const Vector& b);
