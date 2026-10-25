// Step 1 of the gpu-collide pipeline: steer each unit toward the goal and
// predict its new position. Collisions then correct these predicted positions,
// and unit_finalize.comp derives velocity from how far each unit really moved.
//
// SDL_GPU compute binding rules:
//   (t[n], space0) - read-only storage buffers
//   (u[n], space1) - read-write storage buffers
//   (b[n], space2) - uniform buffers

struct UnitData {
    float2 pos;
    float2 vel;
};

RWStructuredBuffer<UnitData> Units : register(u0, space1);
RWStructuredBuffer<float2> PrevPos : register(u1, space1);

// Matches GPUIntegrateUniforms in sim/GpuCollidePhysics.cpp
cbuffer IntegrateParams : register(b0, space2) {
    float2 Goal;
    float Dt;
    uint UnitCount;
    float MaxSpeed;
    float Steering; // how quickly velocity turns toward the desired velocity, per second
    float2 Padding;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint index = dispatchThreadId.x;
    if (index >= UnitCount) {
        return;
    }

    UnitData unit = Units[index];
    PrevPos[index] = unit.pos;

    float2 toGoal = Goal - unit.pos;
    float distance = length(toGoal);
    float2 desired = distance > 0.001f ? toGoal / distance * MaxSpeed : float2(0.0f, 0.0f);

    unit.vel += (desired - unit.vel) * saturate(Steering * Dt);
    unit.pos += unit.vel * Dt;

    Units[index] = unit;
}
