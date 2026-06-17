cmake_minimum_required(VERSION 3.17)

enable_testing()

find_package(Catch2 3 REQUIRED)

add_executable(tests)

find_package(Threads REQUIRED)

target_link_libraries(tests
    PRIVATE
        ray-tracer-lib
        ray-tracer-flags
        Catch2::Catch2WithMain
        glm::glm
        Threads::Threads
)

# Editor non-GUI logic is tested here. We compile only the GL-free editor
# sources (ObjLoader.cpp, OrbitCamera.h, PngExport.cpp, AutomationServer.cpp) so
# the test binary needs no OpenGL context or linkage and stays headless-clean.
# The GL-touching code (EditorApp, RasterMesh GL upload) is deliberately
# excluded; it is exercised by running the editor through the automation port
# (see editor/AUTOMATION.md), not by this headless target. PNG::PNG and
# nlohmann_json are already pulled in transitively via ray-tracer-lib.
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
        test_Property.cpp
        test_FurnaceLedger.cpp
        test_CameraRoundTrip.cpp
        test_BSDFConsistency.cpp
        test_MirrorDirectParity.cpp
        test_Determinism.cpp
        test_GatherRadianceUnit.cpp
        test_SelfHitLive.cpp
        test_ProbeDefaults.cpp
        test_CountEquivalence.cpp
        test_VarianceConvergence.cpp
        test_InverseSquareFalloff.cpp
        test_ShutterBrightness.cpp
        test_Sphere.cpp
        test_Quad.cpp
        test_TriangleWatertight.cpp
        test_SelfHitEpsilon.cpp
        test_CameraExposure.cpp
        test_CameraProjection.cpp
        test_BSDF.cpp
        test_Contracts.cpp
        test_Dielectric.cpp
        test_Emitter.cpp
        test_DensityGrid.cpp
        test_BounceStore.cpp
        test_SplatRadiusFloor.cpp
        test_AreaLight.cpp
        test_EmissiveGather.cpp
        test_MirrorGather.cpp
        test_ProbeGather.cpp
        test_MultiCameraProbe.cpp
        test_MinorityFresnelGather.cpp
        test_AnimatedGather.cpp
        test_DepthOfField.cpp
        test_CameraMotionBlur.cpp
        test_DecayTermination.cpp
        test_SinglePhotonScatter.cpp
        test_SplatToCamera.cpp
        test_main.cpp
        test_OrbitCamera.cpp
        test_RasterMesh.cpp
        test_PngExport.cpp
        test_PngWriter.cpp
        test_PngWriteSession.cpp
        test_AutomationServer.cpp
        ${CMAKE_SOURCE_DIR}/editor/ObjLoader.cpp
        ${CMAKE_SOURCE_DIR}/editor/PngExport.cpp
        ${CMAKE_SOURCE_DIR}/editor/AutomationServer.cpp
)

include(CTest)
include(Catch)
catch_discover_tests(tests)
