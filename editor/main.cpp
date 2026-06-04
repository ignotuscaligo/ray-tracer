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
#include "Worker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
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

        // The CLI resolution argument overrides the global render-config resolution
        // and is imposed only on cameras that did NOT declare their own $width/$height.
        // Multi-camera scenes that set per-camera resolutions keep them; single-camera
        // scenes behave exactly as before (the CLI res wins).
        if (resolution > 0)
        {
            scene.settings.imageWidth = static_cast<size_t>(resolution);
            scene.settings.imageHeight = static_cast<size_t>(resolution);
            for (auto& cam : scene.cameras)
            {
                if (cam && !cam->hasResolutionOverride())
                {
                    cam->setFromRenderConfiguration(
                        scene.settings.imageWidth, scene.settings.imageHeight);
                }
            }
        }
        if (photonsMillions > 0)
        {
            scene.settings.photonsPerLight = static_cast<size_t>(photonsMillions) * 1000000;
        }

        std::printf("render-test: rendering %s at %zux%zu, %zu photons/light...\n",
                    scenePath.c_str(), scene.settings.imageWidth,
                    scene.settings.imageHeight, scene.settings.photonsPerLight);

        WorkerDebug::resetDropCounters();

        RenderResult result = Renderer::renderFrame(scene);

        // Surface the forward-pipeline drop totals after the queues have drained.
        // Nonzero values mean photons/bounce-hits were discarded under queue
        // saturation; the back-pressure fix must drive these to zero.
        std::printf("dropped: emitting=%zu requeue=%zu hit=%zu final=%zu total=%zu\n",
                    WorkerDebug::droppedEmitting(),
                    WorkerDebug::droppedRequeue(),
                    WorkerDebug::droppedHit(),
                    WorkerDebug::droppedFinal(),
                    WorkerDebug::droppedTotal());

        // Wave 3 memory evidence: high-water-mark slot occupancy of each queue.
        // The emitter queue now holds compact producers (a fraction of a
        // PhotonHit each) and the photon queue no longer needs N-daughter
        // contiguous headroom per bounce-hit.
        std::printf("peak-occupancy: photon=%zu hit=%zu emitter=%zu final=%zu\n",
                    result.peakPhotonQueue,
                    result.peakHitQueue,
                    result.peakEmitterQueue,
                    result.peakFinalQueue);

        // Wave 4a deposit evidence: how many non-delta bounce hits were stored in
        // the persistent cloud, the cloud's preallocated footprint, whether the
        // budget cap was hit (and how many appends were dropped if so), and the
        // hash-grid stats built over the deposits.
        if (result.bounceCloud)
        {
            const BounceCloud& cloud = *result.bounceCloud;
            const double mib = static_cast<double>(cloud.memoryBytes()) / (1024.0 * 1024.0);
            std::printf("bounce-cloud: deposited=%zu capacity=%zu footprint=%.1f MiB "
                        "budget-hit=%s dropped=%zu\n",
                        cloud.size(), cloud.capacity(), mib,
                        cloud.budgetHit() ? "yes" : "no", cloud.droppedCount());
        }
        if (result.hashGrid)
        {
            std::printf("hash-grid: cells=%zu points=%zu cell-size=%.3f\n",
                        result.hashGrid->cellCount(),
                        result.hashGrid->pointCount(),
                        result.hashGrid->cellSize());
        }

        std::printf("photon-pass: %.2f s (shared across %zu camera(s))\n",
                    result.photonPassSeconds, result.cameras.size());

        // Wave 6 MULTI-CAMERA output. The photon pass / cloud / grid above were a
        // single shared solve; each camera ran its own gather. Report per-camera
        // gather diagnostics + timing + mean luminance, and write one PNG per camera.
        //
        // Output naming:
        //   - 1 camera, no $outputName  -> the <out.png> path given on the CLI
        //     (exact single-camera back-compat).
        //   - otherwise                 -> <out-stem>_<name>.png next to <out.png>,
        //     where <name> is the camera's $outputName (or cam<i> if unnamed).
        const std::filesystem::path outBase(outPath);
        const std::filesystem::path outDir = outBase.parent_path();
        const std::string outStem = outBase.stem().string();
        const std::string outExt =
            outBase.has_extension() ? outBase.extension().string() : std::string(".png");

        const bool singleUnnamed =
            result.cameras.size() == 1 && result.cameras.front().outputName.empty();

        for (size_t i = 0; i < result.cameras.size(); ++i)
        {
            const CameraRender& cr = result.cameras[i];
            const Gather::GatherResult& g = cr.gather;

            std::string name = cr.outputName;
            if (name.empty())
            {
                name = "cam" + std::to_string(i);
            }

            std::filesystem::path camOut;
            if (singleUnnamed)
            {
                camOut = outBase;
            }
            else
            {
                camOut = outDir / (outStem + "_" + name + outExt);
            }

            const size_t w = cr.camera ? cr.camera->width() : 0;
            const size_t h = cr.camera ? cr.camera->height() : 0;
            const int bf = cr.camera ? cr.camera->bounceFilter() : -1;
            const int lf = cr.camera ? cr.camera->lightFilter() : -1;

            std::printf(
                "camera[%zu] name=%s res=%zux%zu bounceFilter=%d lightFilter=%d "
                "gather-time=%.3f s mean-luminance=%.4f\n",
                i, name.c_str(), w, h, bf, lf, cr.gatherSeconds, cr.meanLuminance);
            std::printf(
                "  gather: hit=%zu gathered=%zu delta-black=%zu miss=%zu "
                "max-radius=%.3f mean-deposits/gather=%.1f max-radiance=%.2f\n",
                g.pixelsHit, g.pixelsGathered, g.pixelsDelta, g.pixelsMiss,
                g.maxGatherRadius, g.meanDepositsPerGather, g.maxRadiance);

            if (cr.image)
            {
                PngWriter::writeImage(camOut, *cr.image, "render-test");
                std::printf("  wrote %s\n", camOut.c_str());
            }
        }

        std::printf("render-test: done (%zu image(s))\n", result.cameras.size());
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

    // Parse optional flags:
    //   --automation-port <N>   enable the localhost (127.0.0.1) command port
    //   --script <file.json>    run a list of commands non-interactively, exit
    //   <path.obj>              initial mesh to load (first non-flag arg)
    uint16_t automationPort = 0;
    std::string scriptPath;
    std::string meshPath;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--automation-port" && i + 1 < argc)
        {
            automationPort = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--script" && i + 1 < argc)
        {
            scriptPath = argv[++i];
        }
        else if (!arg.empty() && arg[0] != '-' && meshPath.empty())
        {
            meshPath = arg;
        }
    }

    EditorApp app;

    if (!meshPath.empty())
    {
        app.setInitialMeshPath(meshPath);
    }
    if (automationPort != 0)
    {
        app.setAutomationPort(automationPort);
    }

    try
    {
        if (!app.initialize())
        {
            std::fprintf(stderr, "Failed to initialize editor.\n");
            return 1;
        }

        // Script mode: a GL context exists (initialize created the window), so
        // load_mesh / render / screenshot all work. Run the commands, then exit
        // without entering the interactive loop.
        if (!scriptPath.empty())
        {
            int code = app.runScript(scriptPath);
            app.shutdown();
            return code;
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
