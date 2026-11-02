// Whether a unit is still in play, shared by every pass that reads units.
//
// A unit that runs out of hit points is despawned in place rather than
// removed: unit_finalize moves it to DEAD_POSITION, far outside any level,
// and from then on every pass skips it. The grid leaves it out of every cell,
// so nothing collides with it; integrate, collide and finalize leave it where
// it is; and unit.vert draws nothing for it. Matches sim::kDeadPosition.

#ifndef HORDE_UNIT_STATE_HLSLI
#define HORDE_UNIT_STATE_HLSLI

static const float DEAD_POSITION = -1.0e30f;

// The broadphase cell recorded for a dead unit: none.
static const uint NO_CELL = 0xFFFFFFFFu;

bool isDead(float2 pos) {
    return pos.x < -1.0e29f;
}

#endif
