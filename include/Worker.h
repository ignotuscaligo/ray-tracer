#pragma once

#include "Buffer.h"
#include "Camera.h"
#include "Hit.h"
#include "Image.h"
#include "LightQueue.h"
#include "Material.h"
#include "MaterialLibrary.h"
#include "Object.h"
#include "Photon.h"
#include "RandomGenerator.h"
#include "Volume.h"
#include "WorkQueue.h"

#include <atomic>
#include <exception>
#include <memory>
#include <thread>
#include <vector>

/*

Pipeline: Photons <-> Hits -> Final hits -> Write

Photons:
* Emitted / bounced photons to cast against the scene
* Generated by lights and emissive volumes at the beginning of a frame
* Generated at bounce sites depending on material properties and number of bounces allowed

Hits:
* Results of photon casts from the previous queue
* Depending on material properties and remaining bounces, will emit more photons to be calculated
* If visible to the camera (normal facing camera, no objects between hit and camera), added to the final hits queue

Final hits:
* Scanned by "pixel sensors" to be written to final image

If photons are available, process those
Else if hits are available, process those
Else if final hits are available, scan those into the image buffer
Else, sleep

*/

class Worker
{
public:
    Worker(size_t index, size_t fetchSize);

    void start();
    void suspend();
    void resume();
    void stop();
    void exec();
    std::exception_ptr exception();

    std::shared_ptr<Camera> camera;
    std::vector<std::shared_ptr<Object>> objects;
    std::shared_ptr<LightQueue> lightQueue;
    std::shared_ptr<WorkQueue<Photon>> photonQueue;
    std::shared_ptr<WorkQueue<PhotonHit>> hitQueue;
    std::shared_ptr<WorkQueue<PhotonHit>> finalHitQueue;
    std::shared_ptr<MaterialLibrary> materialLibrary;
    std::shared_ptr<Buffer> buffer;
    std::shared_ptr<Image> image;

    size_t emitDuration = 0;
    size_t photonDuration = 0;
    size_t hitDuration = 0;
    size_t writeDuration = 0;

    size_t emitProcessed = 0;
    size_t photonsProcessed = 0;
    size_t hitsProcessed = 0;
    size_t finalHitsProcessed = 0;

private:
    bool processLights();
    bool processPhotons();
    bool processHits();
    bool processFinalHits();

    size_t m_index = 0;
    size_t m_fetchSize = 0;

    std::thread m_thread;

    std::atomic_bool m_running;
    std::atomic_bool m_suspend;

    RandomGenerator m_generator;

    std::vector<Hit> m_castBuffer;
    std::vector<PhotonHit> m_hitBuffer;
    std::vector<PhotonHit> m_volumeHitBuffer;

    std::exception_ptr m_exception;
};
