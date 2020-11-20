#include "Utility.h"

#include <iostream>

void printPoint(const Point& point)
{
    std::cout << "(" << point.x << ", " << point.y << ", " << point.z << ")";
}

void printTriangle(const Triangle& triangle)
{
    std::cout << "<";
    printPoint(triangle.a);
    std::cout << ", ";
    printPoint(triangle.b);
    std::cout << ", ";
    printPoint(triangle.c);
    std::cout << ">";
}
