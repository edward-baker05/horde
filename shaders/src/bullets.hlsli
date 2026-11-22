// Bullets, shared by turret_fire (which writes new ones) and bullet_step
// (which moves them and lets them hit). See sim/GpuCollidePhysics and
// docs/superpowers/specs/2026-11-04-unit-effects-and-bullets-design.md.
//
// A bullet's position and velocity live in a buffer laid out like units'
// (UnitData: top-left corner, velocity), so UnitRenderer draws the pool as it
// draws units, and a spent bullet sits at DEAD_POSITION like a dead unit.

#ifndef HORDE_BULLETS_HLSLI
#define HORDE_BULLETS_HLSLI

// Matches GPUBulletParams in sim/GpuCollidePhysics.cpp
struct BulletParams {
    float damage;  // hit points per unit hit
    float life;    // seconds before it is spent anyway
    uint pierce;   // units it can still hit after the next
    uint status;   // status table index, 0 for none
    uint lastHit;  // the ID of the last unit it hit, so it never hits one twice in a row
    float radius;
    uint clear;    // nonzero once it has been outside every wall: a gun on a wall fires from inside one
    uint padding;
};

// No unit has this ID.
static const uint NO_UNIT = 0xFFFFFFFFu;

// Bullet damage is counted into PendingDamage in these, as whole numbers.
#define MILLI_HP 1000.0f

#endif
