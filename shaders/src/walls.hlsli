// Unit-vs-wall collision, shared by every shader that keeps units out of walls.
// Mirrors sim/WallColliders.cpp, which builds the data and does the same maths
// on the CPU.
//
// The including shader, before including this, defines the read-only buffer
// slots for the three wall buffers and declares, in its cbuffer,
// uint2 WallBinDim, float WallBinSize and uint WallCount:
//
//   #define WALL_PRIMS_SLOT t3
//   #define WALL_BINS_SLOT t4
//   #define WALL_BIN_INDICES_SLOT t5

#ifndef HORDE_WALLS_HLSLI
#define HORDE_WALLS_HLSLI

// Matches sim::WallPrimitive. See sim/WallColliders.hpp for what p0..p2 mean per kind.
struct WallPrimitive {
    float2 p0;
    float2 p1;
    float2 p2;
    uint kind;
    float radius;
};

StructuredBuffer<WallPrimitive> WallPrims : register(WALL_PRIMS_SLOT, space0);   // every wall piece, world space
StructuredBuffer<uint2> WallBins : register(WALL_BINS_SLOT, space0);             // per bin: (first, count)
StructuredBuffer<uint> WallBinIndices : register(WALL_BIN_INDICES_SLOT, space0); // primitive indices by bin

static const uint WALL_BOX = 0;
static const uint WALL_CIRCLE = 1;
static const uint WALL_CAPSULE = 2;
static const uint WALL_TRIANGLE = 3;

float signOrOne(float v) {
    return v < 0.0f ? -1.0f : 1.0f;
}

float2 closestOnSegment(float2 p, float2 a, float2 b) {
    float2 ab = b - a;
    float lengthSq = dot(ab, ab);
    float t = lengthSq > 1e-6f ? saturate(dot(p - a, ab) / lengthSq) : 0.0f;
    return a + ab * t;
}

// Signed distance from `p` to the primitive's surface (negative inside) and
// the outward normal there.
float wallDistance(WallPrimitive w, float2 p, out float2 normal) {
    if (w.kind == WALL_BOX) {
        float2 axisY = float2(-w.p1.y, w.p1.x);
        float2 d = p - w.p0;
        float2 q = float2(dot(d, w.p1), dot(d, axisY));
        float2 e = abs(q) - w.p2;
        float2 local;
        float distance;
        if (e.x > 0.0f || e.y > 0.0f) {
            float2 outside = max(e, 0.0f);
            distance = length(outside);
            local = outside * float2(signOrOne(q.x), signOrOne(q.y)) / max(distance, 1e-6f);
        } else {
            distance = max(e.x, e.y);
            local = e.x > e.y ? float2(signOrOne(q.x), 0.0f) : float2(0.0f, signOrOne(q.y));
        }
        normal = w.p1 * local.x + axisY * local.y;
        return distance;
    }

    if (w.kind == WALL_CIRCLE) {
        float2 d = p - w.p0;
        float len = length(d);
        normal = len > 1e-6f ? d / len : float2(0.0f, -1.0f);
        return len - w.radius;
    }

    if (w.kind == WALL_CAPSULE) {
        float2 d = p - closestOnSegment(p, w.p0, w.p1);
        float len = length(d);
        float2 along = w.p1 - w.p0;
        float2 fallback = length(along) > 1e-6f ? normalize(float2(-along.y, along.x)) : float2(0.0f, -1.0f);
        normal = len > 1e-6f ? d / len : fallback;
        return len - w.radius;
    }

    // Triangle
    float2 corners[3] = {w.p0, w.p1, w.p2};
    float bestSq = 3.4e38f;
    float2 best = w.p0;
    int sideSigns = 0;
    [unroll]
    for (int i = 0; i < 3; ++i) {
        float2 a = corners[i];
        float2 b = corners[(i + 1) % 3];
        float2 c = closestOnSegment(p, a, b);
        float distSq = dot(p - c, p - c);
        if (distSq < bestSq) {
            bestSq = distSq;
            best = c;
        }
        float2 ab = b - a;
        float2 ap = p - a;
        sideSigns += (ab.x * ap.y - ab.y * ap.x) >= 0.0f ? 1 : -1;
    }
    bool inside = sideSigns == 3 || sideSigns == -3;
    float len = sqrt(bestSq);
    normal = len > 1e-6f ? (inside ? best - p : p - best) / len : float2(0.0f, -1.0f);
    return inside ? -len : len;
}

// Pushes a disc of `radius` centred at `centre` out of every wall near it.
float2 resolveWalls(float2 centre, float radius) {
    if (WallCount == 0) {
        return centre;
    }

    int2 cell = clamp((int2)floor(centre / WallBinSize), int2(0, 0), (int2)WallBinDim - 1);
    uint2 bin = WallBins[(uint)cell.x + (uint)cell.y * WallBinDim.x];

    for (uint i = 0; i < bin.y; ++i) {
        float2 n;
        float d = wallDistance(WallPrims[WallBinIndices[bin.x + i]], centre, n);
        if (d < radius) {
            centre += n * (radius - d);
        }
    }
    return centre;
}

#endif
