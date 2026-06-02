#include <catch2/catch_all.hpp>

#include "RasterMesh.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using Catch::Matchers::WithinAbs;

namespace
{

// Write a minimal OBJ to a temp file and return its path. Caller removes it.
std::filesystem::path writeTempObj(const std::string& contents, const std::string& name)
{
    std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path);
    out << contents;
    out.close();
    return path;
}

}  // namespace

TEST_CASE("loadObjMeshData extracts triangles with explicit normals", "[RasterMesh]")
{
    // A single triangle with explicit per-vertex normals pointing +Z.
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vn 0 0 1\n"
        "f 1//1 2//1 3//1\n";

    const std::filesystem::path path = writeTempObj(obj, "rt_editor_tri.obj");
    MeshData data = loadObjMeshData(path.string());
    std::filesystem::remove(path);

    REQUIRE(data.valid);
    REQUIRE(data.triangleCount() == 1);
    REQUIRE(data.vertices.size() == 3);

    // Each vertex carries the +Z normal.
    for (const auto& v : data.vertices)
    {
        REQUIRE_THAT(v.normal.x, WithinAbs(0.0, 1e-5));
        REQUIRE_THAT(v.normal.y, WithinAbs(0.0, 1e-5));
        REQUIRE_THAT(v.normal.z, WithinAbs(1.0, 1e-5));
    }

    // Bounds cover the triangle.
    REQUIRE_THAT(data.minBound.x, WithinAbs(0.0, 1e-5));
    REQUIRE_THAT(data.maxBound.x, WithinAbs(1.0, 1e-5));
    REQUIRE_THAT(data.maxBound.y, WithinAbs(1.0, 1e-5));
}

TEST_CASE("loadObjMeshData synthesizes face normals when the OBJ has none", "[RasterMesh]")
{
    // Triangle in the XY plane wound counter-clockwise => face normal +Z.
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n";

    const std::filesystem::path path = writeTempObj(obj, "rt_editor_nonormal.obj");
    MeshData data = loadObjMeshData(path.string());
    std::filesystem::remove(path);

    REQUIRE(data.valid);
    REQUIRE(data.triangleCount() == 1);

    const glm::vec3 n = data.vertices[0].normal;
    REQUIRE_THAT(n.z, WithinAbs(1.0, 1e-5));
    REQUIRE_THAT(n.x, WithinAbs(0.0, 1e-5));
    REQUIRE_THAT(n.y, WithinAbs(0.0, 1e-5));
}

TEST_CASE("loadObjMeshData triangulates a quad face into two triangles", "[RasterMesh]")
{
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "f 1 2 3 4\n";

    const std::filesystem::path path = writeTempObj(obj, "rt_editor_quad.obj");
    MeshData data = loadObjMeshData(path.string());
    std::filesystem::remove(path);

    REQUIRE(data.valid);
    REQUIRE(data.triangleCount() == 2);
    REQUIRE(data.vertices.size() == 6);
}

TEST_CASE("loadObjMeshData reports failure for a missing file", "[RasterMesh]")
{
    MeshData data = loadObjMeshData("/nonexistent/path/does_not_exist.obj");
    REQUIRE_FALSE(data.valid);
    REQUIRE_FALSE(data.error.empty());
}
