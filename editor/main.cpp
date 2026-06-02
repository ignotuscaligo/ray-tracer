// Ray Tracer Editor — GUI model/scene editor.
//
// Phase 1: opens a GLFW window with an OpenGL 3.2 core context, initializes
// Dear ImGui (glfw + opengl3 backends), and runs a render loop drawing a simple
// panel. Later phases add a raster viewport, an orbit camera, and a "Render"
// button wired to the path tracer in ray-tracer-lib.
//
// macOS note: OpenGL is deprecated on macOS but the 3.2+ core profile still
// works. We request 3.2 core + forward-compat, which is the highest profile
// Apple's GL stack exposes.

#include "EditorApp.h"

#include <cstdio>
#include <exception>

int main(int argc, char** argv)
{
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
