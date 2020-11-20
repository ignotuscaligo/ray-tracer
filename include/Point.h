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

    Point operator=(const Point& rhs);
    Point operator+=(const Point& rhs);
    Point operator/=(float rhs);
};

static Point operator+(const Point& lhs, const Point& rhs)
{
	return Point(
		lhs.x + rhs.x,
		lhs.y + rhs.y,
		lhs.z + rhs.z
	);
}
