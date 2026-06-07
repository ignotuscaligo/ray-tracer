// ray-tracer executable entry point.
//
// Scene parsing and the photon-pipeline orchestration that used to live inline
// here now live in the library (SceneLoader + Renderer) so the GUI editor can
// drive the same code path. This file is now a thin CLI wrapper: parse args,
// load the scene, render each frame, write PNGs, print timing.

#include "Image.h"
#include "MirrorGather.h"
#include "PngWriter.h"
#include "Renderer.h"
#include "SceneLoader.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    std::cout << "Hello!" << std::endl;

    if (argc == 1)
    {
        std::cout << "Please provide a project file to process" << std::endl;
        return 1;
    }
    else if (argc > 2)
    {
        std::cout << "Too many arguments, only 1 required" << std::endl;
        return 1;
    }

    std::filesystem::path projectFilePath = argv[1];
    if (!std::filesystem::is_regular_file(projectFilePath))
    {
        std::cout << "Provided path is invalid: " << projectFilePath << std::endl;
        return 1;
    }

    try
    {
        LoadedScene scene = SceneLoader::loadFromFile(projectFilePath, /*logToStdout=*/true);

        const size_t startFrame = scene.settings.startFrame;
        const size_t endFrame = scene.settings.endFrame;
        const size_t frameCount = (endFrame - startFrame) + 1;
        const size_t pixelCount = scene.settings.imageWidth * scene.settings.imageHeight;

        std::cout << "---" << std::endl;
        std::cout << "Rendering image at " << scene.settings.imageWidth << " px by "
                  << scene.settings.imageHeight << " px" << std::endl;

        for (size_t frame = startFrame; frame <= endFrame; ++frame)
        {
            std::cout << "---" << std::endl;
            std::cout << "Rendering frame " << frame + 1 << " / " << frameCount << std::endl;

            const std::chrono::time_point renderStart = std::chrono::system_clock::now();

            // Map this frame index to TIME so the keyframed scene is sampled at the
            // right instant: the shutter opens at frameOffset + frame/frameRate and
            // the Renderer integrates over [t_open, t_open + shutterTime). This is
            // the single point that turns a frame number into a scene time; a static
            // scene with shutterTime 0 ignores it (all photons share one instant and
            // there are no animated transforms).
            const double frameRate = (scene.settings.frameRate > 0.0) ? scene.settings.frameRate : 24.0;
            scene.settings.frameTime =
                scene.settings.frameOffset + static_cast<double>(frame) / frameRate;

            std::cout << "Frame time t=" << scene.settings.frameTime << "s"
                      << " shutter=" << scene.settings.shutterTime << "s" << std::endl;

            RenderResult render = Renderer::renderFrame(scene);

            const std::chrono::time_point renderEnd = std::chrono::system_clock::now();
            const std::chrono::microseconds renderDuration =
                std::chrono::duration_cast<std::chrono::microseconds>(renderEnd - renderStart);

            // Storage pivot: mirror-gather diagnostics for the primary camera. The
            // direct image comes from the forward splat; reflections come from the
            // density grid. Reports how many delta (mirror) pixels reflected the
            // grid vs stayed black.
            const MirrorGather::Result& m = render.mirror;
            std::cout << "Mirror: delta-pixels=" << m.pixelsDelta
                      << " reflected=" << m.pixelsReflected
                      << " black=" << m.pixelsBlack
                      << std::endl;

            std::string fileName = scene.renderName + "." + std::to_string(frame) + ".png";
            std::filesystem::path outputPath = scene.renderPath / fileName;
            PngWriter::writeImage(outputPath, *render.image, scene.renderName);

            std::cout << "Wrote " << outputPath.generic_string() << std::endl;
            std::cout << "Render time:" << std::endl;
            std::cout << "|- total:        " << renderDuration.count() / 1000 << " ms" << std::endl;
            if (pixelCount > 0)
            {
                std::cout << "|- average / px: " << renderDuration.count() / pixelCount << " us" << std::endl;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cout << "ERROR: " << e.what() << std::endl;
    }

    std::cout << "Goodbye!" << std::endl;

    return 0;
}
