#pragma once

#include "Emitter.h"
#include "WorkQueue.h"

// EmitterQueue holds compact daughter-photon PRODUCERS (Wave 3), replacing the
// Wave 2 EmittingQueue that stored full PhotonHits awaiting eager N-daughter
// fan-out.
//
//   - PhotonQueue.pull -> raycast -> for each bounceable hit, push ONE compact
//     Emitter here (carrying a remaining-daughter count) in parallel with the
//     full-hit push to hitQueue (the splat path).
//   - EmitterQueue.pull -> RESERVE space in PhotonQueue (claim-output-first) ->
//     generate as many daughters as fit, decrement the emitter's remaining
//     count; requeue the emitter if it still has daughters, drop it when done.
//
// Because an emitter is generated lazily into already-reserved space, the
// photon queue never needs N contiguous free slots per hit (Wave 2's eager
// requirement), and this queue stores a compact ~sizeof(Emitter) producer
// instead of an N-multiplied stream of PhotonHits. That is the memory win.
//
// Like EmittingQueue this is just a typed WorkQueue; the queue shape and the
// claim-output-first back-pressure semantics are unchanged.
using EmitterQueue = WorkQueue<Emitter>;
