cmake_minimum_required(VERSION 3.17)

enable_testing()

find_package(Catch2 3 REQUIRED)

add_executable(tests)

target_link_libraries(tests
    PRIVATE
        ray-tracer-lib
        Catch2::Catch2WithMain
        glm::glm
)

# Editor non-GUI logic (OBJ->vertex extraction, orbit camera math) is tested
# here. We compile only the GL-free editor sources (ObjLoader.cpp, OrbitCamera.h)
# so the test binary needs no OpenGL context or linkage.
target_include_directories(tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/editor
)

set_target_properties(tests
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)

target_sources(tests
    PRIVATE
        test_Bounds.cpp
        test_main.cpp
        test_OrbitCamera.cpp
        test_RasterMesh.cpp
        ${CMAKE_SOURCE_DIR}/editor/ObjLoader.cpp
)

include(CTest)
include(Catch)
catch_discover_tests(tests)
