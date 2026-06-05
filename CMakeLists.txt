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

# Export compile_commands.json for clang-tidy / IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ===== Static-analysis / sanitizer harness
#
# These options drive a "ray-tracer-flags" INTERFACE target that we link only
# into the project's OWN targets (ray-tracer-lib, ray-tracer, tests, editor).
# We deliberately do NOT set any of these as global CMAKE_CXX_FLAGS, because
# that would also rebuild the Conan dependencies with these flags — sanitizers
# in particular must NOT leak into deps (the runtime would be linked twice and
# warnings/-Werror in third-party headers are not ours to fix).
option(RAYTRACER_WERROR "Treat project warnings as errors (-Werror)" OFF)
# -Wshadow is clean on this codebase and wired as a FAILING CI job: a shadowed
# variable in numeric/index-heavy renderer code is a real footgun (an inner
# `i`/`r`/`t` silently hiding an outer one), and the cleanup cost was bounded.
option(RAYTRACER_WSHADOW "Enable -Wshadow on project targets" OFF)
# -Wconversion surfaces many findings on a double<->float<->size_t-heavy renderer;
# most are benign value-preserving narrowings. It is wired as an INFORMATIONAL
# (non-failing) CI job, NOT -Werror, until the remaining findings are triaged to
# zero. The genuinely dangerous narrowings (data-losing / sign-changing ones in
# hot math and indices) are fixed in-tree; see the CI note in .github/workflows.
option(RAYTRACER_WCONVERSION "Enable -Wconversion on project targets" OFF)
set(RAYTRACER_SANITIZE "" CACHE STRING
    "Semicolon/comma list of sanitizers to enable on project targets, \
e.g. \"address;undefined\". Empty disables sanitizers.")

add_library(ray-tracer-flags INTERFACE)

# Baseline warning set for GCC/Clang. MSVC is not a CI target here.
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ray-tracer-flags INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )

    # Vector.h deliberately uses an anonymous struct inside a union to alias the
    # AVX `__m256d data` lane with named `.x/.y/.z` accessors. Anonymous structs
    # are a GNU/MSVC extension (not ISO C++), so -Wpedantic flags every TU that
    # includes Vector.h. The suppression is applied locally in Vector.h via a
    # _Pragma guard (portable across GCC and Clang) rather than globally here, so
    # -Wpedantic still polices the rest of the codebase.

    if (RAYTRACER_WSHADOW)
        target_compile_options(ray-tracer-flags INTERFACE -Wshadow)
    endif()

    if (RAYTRACER_WCONVERSION)
        target_compile_options(ray-tracer-flags INTERFACE -Wconversion)
    endif()

    if (RAYTRACER_WERROR)
        target_compile_options(ray-tracer-flags INTERFACE -Werror)
    endif()
endif()

# Sanitizers. Accept either ';' or ',' separated values and normalize to the
# comma form the compiler wants (-fsanitize=address,undefined).
if (RAYTRACER_SANITIZE)
    string(REPLACE ";" "," _rt_san "${RAYTRACER_SANITIZE}")
    message(STATUS "ray-tracer: sanitizers enabled: ${_rt_san}")
    target_compile_options(ray-tracer-flags INTERFACE
        -fsanitize=${_rt_san}
        -fno-omit-frame-pointer
        -g
    )
    # Keep design-by-contract assertions (include/Contracts.h) ACTIVE under a
    # sanitizer build even though it is otherwise a Release config (NDEBUG set).
    # A contract failure aborts, which the sanitizer/test harness catches — so
    # the highest-leverage place to run contracts is exactly the sanitized job.
    target_compile_definitions(ray-tracer-flags INTERFACE
        RAYTRACER_SANITIZE_BUILD=1
    )
    # Sanitizer runtime must be on the link line for every project target.
    target_link_options(ray-tracer-flags INTERFACE
        -fsanitize=${_rt_san}
    )
endif()

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

# Project warning / sanitizer flags. PRIVATE compile flags (so they apply when
# compiling ray-tracer-lib's own TUs but are not forced onto deps), but the
# sanitizer link options must reach final executables, so the link side is made
# to propagate via the executables linking ray-tracer-flags directly below.
target_link_libraries(ray-tracer-lib PRIVATE ray-tracer-flags)

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

        include/PngWriteSession.h
        src/PngWriteSession.cpp

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

        include/DielectricMaterial.h
        src/DielectricMaterial.cpp

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

        include/AreaLight.h
        src/AreaLight.cpp

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

        include/DensityGrid.h
        src/DensityGrid.cpp

        include/MirrorGather.h
        src/MirrorGather.cpp

        include/EmissiveGather.h
        src/EmissiveGather.cpp
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
        ray-tracer-flags
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
        ray-tracer-flags
        imgui::imgui
        glfw
        glm::glm
)

# The Dear ImGui backend sources are third-party; they are compiled into the
# editor target but are not ours to keep warning-clean. Suppress warnings on
# just those two TUs so -Wall/-Wextra/-Werror only police our own code. (The
# sanitizer/-g flags from ray-tracer-flags still apply, which is fine.)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(
        "${IMGUI_BINDINGS_DIR}/imgui_impl_glfw.cpp"
        "${IMGUI_BINDINGS_DIR}/imgui_impl_opengl3.cpp"
        PROPERTIES COMPILE_OPTIONS "-w"
    )
endif()

# macOS: OpenGL is provided by the system OpenGL framework. glfw (via Conan)
# pulls in Cocoa/IOKit/CoreVideo transitively, but linking OpenGL explicitly is
# required for the gl3 entry points used by the raster viewport.
if (APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(editor PRIVATE ${OPENGL_FRAMEWORK})
endif()

add_subdirectory(tests)
