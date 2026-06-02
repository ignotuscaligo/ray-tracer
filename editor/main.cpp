// Ray Tracer Editor — GUI model/scene editor.
//
// Opens a GLFW window with an OpenGL 3.2 core context, initializes Dear ImGui
// (glfw + opengl3 backends), shows an OBJ mesh in an orbit-camera raster
// viewport, and has a "Render" button wired to the photon path tracer in
// ray-tracer-lib.
//
// macOS note: OpenGL is deprecated on macOS but the 3.2+ core profile still
// works. We request 3.2 core + forward-compat, which is the highest profile
// Apple's GL stack exposes.
//
// Headless mode: `editor --render-test <scene.json> <out.png> [resolution]
// [photonsMillions]` skips all GUI/GL initialization and exercises the public
// render API directly — load the scene, render a frame, write a PNG. This is
// how the render path is verified in environments without a display.

#include "EditorApp.h"

#include "Image.h"
#include "PngWriter.h"
#include "Renderer.h"
#include "SceneLoader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

namespace
{

int runRenderTest(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr,
                     "Usage: %s --render-test <scene.json> <out.png> "
                     "[resolution] [photonsMillions]\n",
                     argv[0]);
        return 2;
    }

    const std::string scenePath = argv[2];
    const std::string outPath = argv[3];
    const int resolution = (argc > 4) ? std::atoi(argv[4]) : 128;
    const int photonsMillions = (argc > 5) ? std::atoi(argv[5]) : 2;

    try
    {
        LoadedScene scene = SceneLoader::loadFromFile(scenePath, /*logToStdout=*/false);

        if (resolution > 0)
        {
            scene.settings.imageWidth = static_cast<size_t>(resolution);
            scene.settings.imageHeight = static_cast<size_t>(resolution);
            if (scene.camera)
            {
                scene.camera->setFromRenderConfiguration(
                    scene.settings.imageWidth, scene.settings.imageHeight);
            }
        }
        if (photonsMillions > 0)
        {
            scene.settings.photonsPerLight = static_cast<size_t>(photonsMillions) * 1000000;
        }

        std::printf("render-test: rendering %s at %zux%zu, %zu photons/light...\n",
                    scenePath.c_str(), scene.settings.imageWidth,
                    scene.settings.imageHeight, scene.settings.photonsPerLight);

        RenderResult result = Renderer::renderFrame(scene);
        PngWriter::writeImage(outPath, *result.image, "render-test");

        std::printf("render-test: wrote %s\n", outPath.c_str());
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "render-test error: %s\n", e.what());
        return 1;
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--render-test") == 0)
    {
        return runRenderTest(argc, argv);
    }

    EditorApp app;

    // Allow an explicit OBJ path on the command line; otherwise the app falls
    // back to a bundled mesh (see EditorApp).
    if (argc > 1)
    {
        app.setInitialMeshPath(argv[1]);
    }

    try
    {
        if (!app.initialize())
        {
            std::fprintf(stderr, "Failed to initialize editor.\n");
            return 1;
        }

        app.run();
        app.shutdown();
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "Editor error: %s\n", e.what());
        app.shutdown();
        return 1;
    }

    return 0;
}
