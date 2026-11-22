// Moves every live bullet one physics step and lets it hit (see bullets.hlsli).
// Runs after the grid build and reorder, so the grid's keys name units' current
// indices.
//
// A bullet moves in sub-steps of at most a unit radius, so it can't skip past
// a unit or through a thin wall. At each sub-step it is spent on leaving the
// level, or on reaching a wall once it has been clear of walls (a gun mounted
// on a wall fires from inside it). It checks the 3x3 cells around it, and on
// touching a unit other than the last it hit, adds its damage to the unit's
// PendingDamage (unit_finalize applies it this step), sets its status
// effects, and is spent unless it has pierce left.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

struct UnitData {
    float2 pos;
    float2 vel;
};

// t0..t2: walls, declared by walls.hlsli
StructuredBuffer<uint> CellRanges : register(t3, space0);
StructuredBuffer<uint2> SortedKeys : register(t4, space0);
StructuredBuffer<UnitData> Units : register(t5, space0);
StructuredBuffer<uint> Ids : register(t6, space0);

#include "bullets.hlsli"
#include "unit_state.hlsli"
#include "unit_status.hlsli"

StructuredBuffer<UnitStatus> StatusTable : register(t7, space0);
RWStructuredBuffer<UnitData> Bullets : register(u0, space1);
RWStructuredBuffer<float2> BulletPrev : register(u1, space1);
RWStructuredBuffer<BulletParams> Params : register(u2, space1);
// Thousandths of a hit point per unit, applied and cleared by unit_finalize.
RWStructuredBuffer<uint> PendingDamage : register(u3, space1);
RWStructuredBuffer<UnitStatus> Status : register(u4, space1);

#define MAX_SUBSTEPS 32

// Matches GPUBulletStepUniforms in sim/GpuCollidePhysics.cpp
cbuffer BulletStepParams : register(b0, space2) {
    float Dt;
    uint BulletCount;
    float UnitRadius;
    float CellSize;
    uint2 GridDim;
    float2 WorldSize;
    uint2 WallBinDim;
    float WallBinSize;
    uint WallCount;
};

#define WALL_PRIMS_SLOT t0
#define WALL_BINS_SLOT t1
#define WALL_BIN_INDICES_SLOT t2
#include "walls.hlsli"

// Hits whatever units in `cell` a bullet at `centre` is touching. Returns
// false once the bullet is spent.
bool hitIn(int2 cell, float2 centre, inout BulletParams bullet) {
    if (any(cell < 0) || any(cell >= (int2)GridDim)) {
        return true;
    }
    uint c = (uint)cell.x + (uint)cell.y * GridDim.x;
    uint start = CellRanges[c * 2];
    if (start == 0xFFFFFFFFu) {
        return true;
    }
    uint end = CellRanges[c * 2 + 1];
    float reach = UnitRadius + bullet.radius;
    for (uint k = start; k < end; ++k) {
        uint unit = SortedKeys[k].y;
        float2 d = Units[unit].pos + UnitRadius - centre;
        if (dot(d, d) >= reach * reach) {
            continue;
        }
        uint id = Ids[unit];
        if (id == bullet.lastHit) {
            continue;
        }
        InterlockedAdd(PendingDamage[unit], (uint)(bullet.damage * MILLI_HP));
        if (bullet.status != 0) {
            // Another bullet setting this unit's status at once may win: either is fine.
            UnitStatus status = Status[unit];
            applyStatus(status, StatusTable[bullet.status]);
            Status[unit] = status;
        }
        bullet.lastHit = id;
        if (bullet.pierce == 0) {
            return false;
        }
        --bullet.pierce;
    }
    return true;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= BulletCount) {
        return;
    }
    UnitData shot = Bullets[index];
    BulletPrev[index] = shot.pos;
    if (isDead(shot.pos)) {
        return;
    }

    BulletParams bullet = Params[index];
    bool alive = bullet.life > 0.0f;
    bullet.life -= Dt;

    float2 centre = shot.pos + bullet.radius;
    float2 move = shot.vel * Dt;
    uint steps = clamp((uint)ceil(length(move) / UnitRadius), 1u, (uint)MAX_SUBSTEPS);
    float2 hop = move / steps;
    for (uint s = 0; s < steps && alive; ++s) {
        centre += hop;
        if (any(centre < 0.0f) || any(centre > WorldSize)) {
            alive = false;
            break;
        }
        if (wallClearance(centre) < bullet.radius) {
            if (bullet.clear != 0) {
                alive = false;
                break;
            }
            continue; // still leaving the wall it was fired from
        }
        bullet.clear = 1;

        int2 home = (int2)floor(centre / CellSize);
        for (int dy = -1; dy <= 1 && alive; ++dy) {
            for (int dx = -1; dx <= 1 && alive; ++dx) {
                alive = hitIn(home + int2(dx, dy), centre, bullet);
            }
        }
    }

    if (!alive || bullet.life <= 0.0f) {
        shot.pos = float2(DEAD_POSITION, DEAD_POSITION);
        shot.vel = float2(0.0f, 0.0f);
    } else {
        shot.pos = centre - bullet.radius;
    }
    Bullets[index] = shot;
    Params[index] = bullet;
}
