cmake_minimum_required(VERSION 3.17)

project(ray-tracer)

add_subdirectory(libs/tinyobjloader)

add_executable(ray-tracer)

set_target_properties(ray-tracer
    PROPERTIES
        CXX_STANDARD 17
)

if (WIN32)
    set_target_properties(ray-tracer
        PROPERTIES
            VS_PACKAGE_REFERENCES "libpng_1.6.28.1"
    )
endif()

target_sources(ray-tracer
    PRIVATE
        src/main.cpp

        include/Pixel.h

        include/Image.h
        src/Image.cpp

        include/PngWriter.h
        src/PngWriter.cpp

        include/Point.h
        src/Point.cpp

        include/Triangle.h
        src/Triangle.cpp

        include/TriangleTree.h
        src/TriangleTree.cpp

        include/Utility.h
        src/Utility.cpp

        include/Bounds.h
        src/Bounds.cpp

        include/Ray.h
        src/Ray.cpp
)

target_include_directories(ray-tracer
    PRIVATE
        include/
)

target_link_libraries(ray-tracer
    PRIVATE
        tinyobjloader
)