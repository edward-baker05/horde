// Optional last stage of the gpu-collide pipeline: blends each unit's
// velocity toward the weighted mean of the units touching it (XSPH viscosity,
// Macklin & Mueller 2013, "Position Based Fluids"), so a crowd moves as a body
// instead of every unit carrying its own collision kicks into the next step.
// Without it, a dense crowd boils: units shoved different ways each step keep
// those shoves as velocity, run into new overlaps, and the crowd mixes.
//
// Reads InUnits and writes OutUnits, over the step's grid (built on the
// predicted positions; close enough after the collision passes). Same layout
// as unit_collide.comp, whose uniforms it shares.

#include "collide_common.hlsli"

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= UnitCount) {
        return;
    }

    UnitData unit = InUnits[index];
    if (isDead(unit.pos)) {
        OutUnits[index] = unit;
        return;
    }

    // Touching neighbours, weighted toward the closest.
    int2 cell = cellOf(unit.pos);
    float2 velSum = float2(0.0f, 0.0f);
    float weightSum = 0.0f;
    for (int dy = -1; dy <= 1; ++dy) {
        uint start;
        uint end;
        rowKeys(cell, dy, start, end);
        for (uint s = start; s < end; ++s) {
            uint other = keyUnit(s);
            if (other == index) {
                continue;
            }
            UnitData neighbour = InUnits[other];
            float2 diff = unit.pos - neighbour.pos;
            float distSq = dot(diff, diff);
            if (distSq >= Diameter * Diameter) {
                continue;
            }
            float weight = 1.0f - sqrt(distSq) / Diameter;
            velSum += neighbour.vel * weight;
            weightSum += weight;
        }
    }
    if (weightSum > 0.0f) {
        unit.vel += (velSum / weightSum - unit.vel) * Smoothing;
    }
    OutUnits[index] = unit;
}
