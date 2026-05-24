#pragma once

#include "Photon.h"
#include "WorkQueue.h"

// EmittingQueue holds PhotonHits that have been raycast but NOT yet had
// daughter photons spawned from them. It exists separately from the
// `hitQueue` (which carries hits awaiting camera-visibility checks for
// splatting) because daughter spawn is back-pressured against the PhotonQueue:
//
//   - PhotonQueue.pull -> raycast -> push bounce hits into both
//     {EmittingQueue, hitQueue}
//   - EmittingQueue.peek -> if PhotonQueue has space for N daughters,
//     pop and spawn; otherwise leave the bounce in the queue
//
// This is the architecturally correct fix for the 32^4 daughter-photon
// fan-out explosion. With a single combined queue and immediate spawn at
// bounce time, four bounces of Lambertian fan-out (32 each) would require
// ~1M slots per source photon. With back-pressure, daughter emission is
// throttled to whatever the raycaster can consume, so the steady-state
// PhotonQueue occupancy stays bounded.
//
// Implementation is just an alias over WorkQueue<PhotonHit>; the queue
// shape and semantics are identical to hitQueue.
using EmittingQueue = WorkQueue<PhotonHit>;
