#pragma once

enum class Axis
{
    X,
    Y,
    Z
};

Axis nextAxis(Axis axis);

struct Point
{
    float x;
    float y;
    float z;

    Point();
    Point(float ix, float iy, float iz);

    float getAxis(Axis axis) const;
    float operator[](Axis axis) const;

    Point operator=(const Point& rhs);
    Point operator+=(const Point& rhs);
    Point operator/=(float rhs);

    float magnitude() const;
    void normalize();
};

static Point operator+(const Point& lhs, const Point& rhs)
{
	return Point(
		lhs.x + rhs.x,
		lhs.y + rhs.y,
		lhs.z + rhs.z
	);
}

static Point operator-(const Point& point)
{
    return Point(
        -point.x,
        -point.y,
        -point.z
    );
}

static Point operator-(const Point& lhs, const Point& rhs)
{
    return Point(
        lhs.x - rhs.x,
        lhs.y - rhs.y,
        lhs.z - rhs.z
    );
}

static Point operator*(const Point& lhs, const float& rhs)
{
    return Point(
        lhs.x * rhs,
        lhs.y * rhs,
        lhs.z * rhs
    );
}

Point cross(const Point& a, const Point& b);
float dot(const Point& a, const Point& b);
