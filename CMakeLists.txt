cmake_minimum_required(VERSION 3.17)

project(ray-tracer)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

add_executable(ray-tracer)

set_target_properties(ray-tracer
    PROPERTIES
        CXX_STANDARD 17
)

target_link_libraries(ray-tracer
    PRIVATE
        CONAN_PKG::nlohmann_json
        CONAN_PKG::libpng
        CONAN_PKG::tinyobjloader
)

target_include_directories(ray-tracer
    PRIVATE
        include/
)

target_sources(ray-tracer
    PRIVATE
        src/main.cpp

        include/Pixel.h

        include/Image.h
        src/Image.cpp

        include/PngWriter.h
        src/PngWriter.cpp

        include/Vector.h
        src/Vector.cpp

        include/Triangle.h
        src/Triangle.cpp

        include/Tree.h
        src/Tree.cpp

        include/Utility.h
        src/Utility.cpp

        include/Bounds.h
        src/Bounds.cpp

        include/Ray.h
        src/Ray.cpp

        include/Object.h
        src/Object.cpp

        include/Transform.h
        src/Transform.cpp

        include/Quaternion.h
        src/Quaternion.cpp

        include/Hit.h
        src/Hit.cpp

        include/Volume.h
        src/Volume.cpp

        include/MeshVolume.h
        src/MeshVolume.cpp

        include/Color.h
        src/Color.cpp

        include/Photon.h
        src/Photon.cpp

        include/WorkQueue.h
        src/WorkQueue.cpp

        include/TypedObject.h

        include/Light.h
        src/Light.cpp

        include/OmniLight.h
        src/OmniLight.cpp

        include/Plane.h
        src/Plane.cpp

        include/Pyramid.h
        src/Pyramid.cpp

        include/Worker.h
        src/Worker.cpp

        include/PlaneVolume.h
        src/PlaneVolume.cpp

        include/Camera.h
        src/Camera.cpp

        include/PixelCoords.h

        include/Buffer.h
        src/Buffer.cpp

        include/Material.h
        src/Material.cpp

        include/DiffuseMaterial.h
        src/DiffuseMaterial.cpp

        include/CauchyMaterial.h
        src/CauchyMaterial.cpp

        include/ObjReader.h
        src/ObjReader.cpp

        include/LightQueue.h
        src/LightQueue.cpp

        include/RandomGenerator.h
        src/RandomGenerator.cpp

        include/AngleGenerator.h
        src/AngleGenerator.cpp

        include/MaterialLibrary.h
        src/MaterialLibrary.cpp

        include/SpotLight.h
        src/SpotLight.cpp

        include/ParallelLight.h
        src/ParallelLight.cpp

        include/Library.h
        src/Library.cpp

        include/MeshLibrary.h
        src/MeshLibrary.cpp

        include/Mesh.h

        include/EnumFlag.h
)
