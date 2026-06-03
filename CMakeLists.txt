cmake_minimum_required(VERSION 3.17)

# ===== Setup

project(ray-tracer)

# C++20 is required project-wide. Two things in this codebase depend on it:
#   1. `std::atomic<float>` (P0020R6) — Buffer accumulates raw float energy with
#      lock-free atomic adds. C++17 atomic<float> was specified but
#      `fetch_add` on floating-point atomics is C++20.
#   2. Designated initializers and other ergonomics used throughout.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_BINARY_DIR})

find_package(nlohmann_json REQUIRED)
find_package(PNG REQUIRED)
find_package(tinyobjloader REQUIRED)

# GUI editor dependencies. These are only needed by the `editor` target, but
# find_package is cheap and harmless for the lib/exe builds.
find_package(imgui REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# Detect host architecture so we only feed AVX hints to x86 builds.
# -march=native is meaningful on x86 (enables AVX intrinsics that the
# Vector.cpp fast path relies on). On Apple Silicon (arm64) -march=native is
# rejected by Apple clang; the Vector implementation falls back to scalar.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86")
    set(RAY_TRACER_ARCH_IS_X86 TRUE)
else()
    set(RAY_TRACER_ARCH_IS_X86 FALSE)
endif()

# ===== Library

add_library(ray-tracer-lib)

set_target_properties(ray-tracer-lib
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)

if (UNIX)
    find_package(Threads)

    if (RAY_TRACER_ARCH_IS_X86)
        target_compile_options(ray-tracer-lib
            PUBLIC
                -march=native
        )
    endif()

    target_link_libraries(ray-tracer-lib
        PRIVATE
            ${CMAKE_THREAD_LIBS_INIT}
    )
endif()

target_link_libraries(ray-tracer-lib
    PUBLIC
        nlohmann_json::nlohmann_json
        PNG::PNG
        tinyobjloader::tinyobjloader
)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(ray-tracer-lib
        PRIVATE
            stdc++fs
    )
endif()

target_include_directories(ray-tracer-lib
    PUBLIC
        include/
)

target_sources(ray-tracer-lib
    PRIVATE
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

        include/AnimationQuery.h

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

        include/Sphere.h
        src/Sphere.cpp

        include/Pyramid.h
        src/Pyramid.cpp

        include/Worker.h
        src/Worker.cpp

        include/PlaneVolume.h
        src/PlaneVolume.cpp

        include/SphereVolume.h
        src/SphereVolume.cpp

        include/Camera.h
        src/Camera.cpp

        include/PixelCoords.h

        include/Buffer.h
        src/Buffer.cpp

        include/Material.h
        src/Material.cpp

        include/LambertianMaterial.h
        src/LambertianMaterial.cpp

        include/MirrorMaterial.h
        src/MirrorMaterial.cpp

        include/MicrofacetMaterial.h
        src/MicrofacetMaterial.cpp

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

        include/RenderSettings.h

        include/SceneLoader.h
        src/SceneLoader.cpp

        include/Renderer.h
        src/Renderer.cpp
)

# ===== Executable

add_executable(ray-tracer)

set_target_properties(ray-tracer
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)

target_link_libraries(ray-tracer
    PRIVATE
        ray-tracer-lib
)

target_sources(ray-tracer
    PRIVATE
        src/main.cpp
)

# ===== GUI Editor

# The Dear ImGui Conan package ships the platform/renderer backend sources under
# res/bindings (it does not pre-compile them, because the backend choice is the
# consumer's). We compile exactly the glfw + opengl3 pair from the package's
# resource directory, located via the CMakeDeps-exported package folder.
set(IMGUI_BINDINGS_DIR "${imgui_PACKAGE_FOLDER_RELEASE}/res/bindings")
if (NOT EXISTS "${IMGUI_BINDINGS_DIR}/imgui_impl_glfw.cpp")
    # Fall back to the multi-config variable name if the release-specific one
    # is not set (older/newer CMakeDeps layouts).
    set(IMGUI_BINDINGS_DIR "${imgui_PACKAGE_FOLDER}/res/bindings")
endif()

add_executable(editor)

set_target_properties(editor
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)

target_sources(editor
    PRIVATE
        editor/main.cpp
        editor/EditorApp.h
        editor/EditorApp.cpp
        editor/OrbitCamera.h
        editor/RasterMesh.h
        editor/RasterMesh.cpp
        editor/ObjLoader.cpp
        editor/GlHeaders.h
        editor/Shaders.h
        editor/PngExport.h
        editor/PngExport.cpp
        editor/AutomationServer.h
        editor/AutomationServer.cpp

        # Dear ImGui glfw + opengl3 backends (compiled from the Conan package).
        "${IMGUI_BINDINGS_DIR}/imgui_impl_glfw.cpp"
        "${IMGUI_BINDINGS_DIR}/imgui_impl_opengl3.cpp"
)

target_include_directories(editor
    PRIVATE
        editor/
        "${IMGUI_BINDINGS_DIR}"
)

target_link_libraries(editor
    PRIVATE
        ray-tracer-lib
        imgui::imgui
        glfw
        glm::glm
)

# macOS: OpenGL is provided by the system OpenGL framework. glfw (via Conan)
# pulls in Cocoa/IOKit/CoreVideo transitively, but linking OpenGL explicitly is
# required for the gl3 entry points used by the raster viewport.
if (APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(editor PRIVATE ${OPENGL_FRAMEWORK})
endif()

add_subdirectory(tests)
