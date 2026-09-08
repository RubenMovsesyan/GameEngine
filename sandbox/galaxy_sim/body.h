#pragma once

#include <common.h>

// One star's simulation state, in scaled units:
//   length : 1 unit = 1 kpc  (3.0857e19 m)
//   mass   : 1 unit = 1 solar mass  (1.98e30 kg)  -> `mass` is literally 0.08 .. 100
// Held in the private CUDA sim buffer (SharedInstanceBuffer::simStatePtr, Sync::SimPriority);
// the pack kernel turns it into a graphics::primitives::scene::PointInstance for the ring.
//
// Fixed at 32 bytes so it drops into the same single-size allocation the ring slots use.
struct Body {
    f32 pos[3];
    f32 vel[3];
    f32 mass;
    f32 pad;
};

static_assert(sizeof(Body) == 32, "Body must stay 32 bytes (matches the ring slot size)");
