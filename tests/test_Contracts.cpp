#include <catch2/catch_all.hpp>

#include "Contracts.h"
#include "SceneLoader.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

// Contracts.h fires (prints file:line + condition, then std::abort()) on a
// boundary violation when RAYTRACER_CONTRACTS_ENABLED. In a release build the
// macros compile to nothing, so the abort can't happen — the death tests below
// are therefore skipped in release and only assert the zero-overhead form
// (condition parsed but not evaluated).
//
// std::abort() can't be observed in-process without unwinding the harness, so a
// firing contract is checked by fork()ing: the child triggers the contract and
// is expected to die by SIGABRT; the parent asserts on the child's wait status.

namespace
{
// Run `body` in a forked child and return its raw wait status. The child runs no
// Catch assertions (its process image is abandoned), it only exercises `body`.
template <typename Fn>
int runInChild(Fn&& body)
{
    const pid_t pid = fork();
    REQUIRE(pid >= 0);
    if (pid == 0)
    {
        // Child. Catch2 installs a SIGABRT handler that would intercept the
        // contract's abort() and turn it into Catch reporting noise (the child
        // shares the harness image). Reset SIGABRT to the default disposition so
        // the contract's abort() produces a clean SIGABRT the parent can observe
        // via the wait status. Silence stderr so the expected contract message /
        // ASan abort trace doesn't clutter the test log. If `body` returns
        // (contract did NOT fire) exit cleanly with 0 so the parent can tell the
        // difference between "aborted" and "ran to completion".
        std::signal(SIGABRT, SIG_DFL);
        std::fclose(stderr);
        std::forward<Fn>(body)();
        std::_Exit(0);
    }
    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    return status;
}

#if RAYTRACER_CONTRACTS_ENABLED
bool diedByAbort(int status)
{
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}
#endif

bool exitedCleanly(int status)
{
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Write a minimal scene JSON with the given render-config body and return its path.
std::filesystem::path writeScene(const std::string& renderConfigBody)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("contract_scene_" + std::to_string(::getpid()) + "_" +
         std::to_string(std::rand()) + ".json");
    std::ofstream out(path);
    out << R"({
  "$renderConfiguration": {
)" << renderConfigBody << R"(
  },
  "$objects": {}
})";
    out.close();
    return path;
}
}  // namespace

TEST_CASE("PRECONDITION fires on a deliberately bad value", "[contracts]")
{
#if RAYTRACER_CONTRACTS_ENABLED
    const int status = runInChild([] { PRECONDITION(1 == 2); });
    REQUIRE(diedByAbort(status));
#else
    SUCCEED("contracts compiled out (release); abort path absent by design");
#endif
}

TEST_CASE("A valid PRECONDITION does not fire", "[contracts]")
{
#if RAYTRACER_CONTRACTS_ENABLED
    const int status = runInChild([] { PRECONDITION(1 == 1); });
    REQUIRE(exitedCleanly(status));
#else
    SUCCEED("contracts compiled out (release)");
#endif
}

TEST_CASE("SceneLoader rejects a zero-dimension scene via contract", "[contracts][SceneLoader]")
{
    const std::filesystem::path scene = writeScene(R"(    "$width": 0,
    "$height": 256)");

#if RAYTRACER_CONTRACTS_ENABLED
    const int status = runInChild([&] {
        // Expected to abort inside validateRenderSettings (imageWidth > 0).
        SceneLoader::loadFromFile(scene, /*logToStdout=*/false);
    });
    REQUIRE(diedByAbort(status));
#else
    // Release: no contract, so the degenerate scene loads without aborting.
    const int status = runInChild([&] {
        SceneLoader::loadFromFile(scene, /*logToStdout=*/false);
    });
    REQUIRE(exitedCleanly(status));
#endif

    std::filesystem::remove(scene);
}

TEST_CASE("SceneLoader accepts a valid scene without firing", "[contracts][SceneLoader]")
{
    const std::filesystem::path scene = writeScene(R"(    "$width": 128,
    "$height": 128)");

    const int status = runInChild([&] {
        SceneLoader::loadFromFile(scene, /*logToStdout=*/false);
    });
    REQUIRE(exitedCleanly(status));

    std::filesystem::remove(scene);
}
