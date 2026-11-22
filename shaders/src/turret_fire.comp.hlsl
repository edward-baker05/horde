// Fires guns: one thread per shot this step. Each finds the nearest unit
// within its gun's range on this step's grid, leads it by its velocity over
// the bullet's flight time, adds a little spread, and writes a new bullet
// into the pool slot the CPU gave the shot (see bullets.hlsli). With nobody
// in range the slot gets a spent bullet.
//
// The search walks square rings of cells outward from the gun, and stops once
// a ring can't hold anything nearer than the best so far.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

struct UnitData {
    float2 pos;
    float2 vel;
};

// Per cell [first key, one past the last), or 0xFFFFFFFF first when empty.
StructuredBuffer<uint> CellRanges : register(t0, space0);
// Sorted (cell, unit index) keys: y names the unit in Units.
StructuredBuffer<uint2> SortedKeys : register(t1, space0);
StructuredBuffer<UnitData> Units : register(t2, space0);
RWStructuredBuffer<UnitData> Bullets : register(u0, space1);
RWStructuredBuffer<float2> BulletPrev : register(u1, space1);

#include "bullets.hlsli"
#include "unit_state.hlsli"

RWStructuredBuffer<BulletParams> Params : register(u2, space1);

#define MAX_SHOTS 64

// Matches GPUFireUniforms in sim/GpuCollidePhysics.cpp
cbuffer FireParams : register(b0, space2) {
    uint2 GridDim;
    float CellSize;
    uint ShotCount;
    float UnitRadius;
    float BulletRadius;
    float Spread; // radians either side of the aim
    uint Seed;    // varies the spread from step to step
    // Per shot: xy = the gun's centre, z = range, w = bullet speed.
    float4 ShotAim[MAX_SHOTS];
    // Per shot: x = damage, y = life in seconds, zw unused.
    float4 ShotHit[MAX_SHOTS];
    // Per shot: x = pool slot, y = pierce, z = status table index, w unused.
    uint4 ShotIds[MAX_SHOTS];
};

// PCG hash: a well-mixed uint from a uint.
uint hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Checks every unit in `cell` against the best so far.
void nearestIn(int2 cell, float2 from, inout float bestSq, inout uint best) {
    if (any(cell < 0) || any(cell >= (int2)GridDim)) {
        return;
    }
    uint c = (uint)cell.x + (uint)cell.y * GridDim.x;
    uint start = CellRanges[c * 2];
    if (start == 0xFFFFFFFFu) {
        return;
    }
    uint end = CellRanges[c * 2 + 1];
    for (uint k = start; k < end; ++k) {
        uint unit = SortedKeys[k].y;
        float2 d = Units[unit].pos + UnitRadius - from;
        float distSq = dot(d, d);
        if (distSq < bestSq) {
            bestSq = distSq;
            best = unit;
        }
    }
}

[numthreads(MAX_SHOTS, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint shot = dispatchThreadId.x;
    if (shot >= ShotCount) {
        return;
    }

    float2 gun = ShotAim[shot].xy;
    float range = ShotAim[shot].z;
    float speed = ShotAim[shot].w;
    uint slot = ShotIds[shot].x;

    int2 home = (int2)floor(gun / CellSize);
    int rings = (int)ceil(range / CellSize) + 1;
    float bestSq = range * range;
    uint best = NO_UNIT;
    for (int r = 0; r <= rings; ++r) {
        // Nothing on this ring or beyond can be nearer than (r - 1) cells.
        float nearest = max(r - 1, 0) * CellSize;
        if (best != NO_UNIT && nearest * nearest > bestSq) {
            break;
        }
        if (r == 0) {
            nearestIn(home, gun, bestSq, best);
            continue;
        }
        for (int i = -r; i <= r; ++i) {
            nearestIn(home + int2(i, -r), gun, bestSq, best);
            nearestIn(home + int2(i, r), gun, bestSq, best);
        }
        for (int j = -r + 1; j <= r - 1; ++j) {
            nearestIn(home + int2(-r, j), gun, bestSq, best);
            nearestIn(home + int2(r, j), gun, bestSq, best);
        }
    }

    if (best == NO_UNIT) {
        Bullets[slot].pos = float2(DEAD_POSITION, DEAD_POSITION);
        Bullets[slot].vel = float2(0.0f, 0.0f);
        return;
    }

    // Lead the target: where it will be when the bullet gets there.
    UnitData target = Units[best];
    float2 at = target.pos + UnitRadius;
    float flight = length(at - gun) / max(speed, 1.0f);
    float2 aim = at + target.vel * flight - gun;
    float angle = atan2(aim.y, aim.x);
    uint h = hash(slot ^ hash(Seed));
    angle += (float(h & 0xFFFFu) / 65535.0f * 2.0f - 1.0f) * Spread;
    float2 dir = float2(cos(angle), sin(angle));

    float2 corner = gun - BulletRadius;
    Bullets[slot].pos = corner;
    Bullets[slot].vel = dir * speed;
    BulletPrev[slot] = corner;

    BulletParams params;
    params.damage = ShotHit[shot].x;
    params.life = ShotHit[shot].y;
    params.pierce = ShotIds[shot].y;
    params.status = ShotIds[shot].z;
    params.lastHit = NO_UNIT;
    params.radius = BulletRadius;
    params.clear = 0;
    params.padding = 0;
    Params[slot] = params;
}
