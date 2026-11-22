// Instanced unit vertex shader for direct GPU rendering.
//
// Reads unit positions directly from the compute-updated storage buffer.
//
// Two modes, picked by SpriteMode:
//   0: every unit is the same tinted atlas cell (UvRect): the flat disc.
//   1: crowd sprites. Each unit's look - hairstyle, accessory, colours - comes
//      from hashing its stable ID, so it costs no memory and survives the
//      units being reordered. Lod picks the near / mid / low art, or 3 for a
//      plain hard-edged circle at the unit's own size (unit_sprite.frag).
//      With Rotate, near and mid art turn to face the way the unit walks;
//      unit_sprite.frag rotates which sprite pixel each screen-aligned sprite
//      pixel shows, so the art stays crisp pixels rather than tilted squares.
//      See tools/make_unit_atlas.py and unit_sprites.hlsli.
//
// SDL_GPU vertex stage binding rules:
//   (t[n], space0) - sampled textures, storage textures, storage buffers
//   (s[n], space0) - samplers
//   (b[n], space1) - uniform buffers

struct UnitData {
    float2 pos;
    float2 vel;
};

StructuredBuffer<UnitData> Units : register(t0, space0);
// Where each unit was one physics step earlier, when MaxJump > 0. Physics runs
// at a fixed rate below the frame rate, so drawing blends the last two steps.
StructuredBuffer<float2> PrevPos : register(t1, space0);
// Each unit's hit points, when InvMaxHealth > 0.
StructuredBuffer<float> Health : register(t2, space0);
// Each unit's stable ID, when HasIds: its index changes when units are
// reordered by cell, so the marked unit is picked by ID.
StructuredBuffer<uint> Ids : register(t3, space0);
// Where each unit is by ID, when HasSlots: instance i draws the unit whose ID
// is i. Draw order is then ID order, which stays put when units are
// reordered, so overlapping units don't swap which is on top every step.
StructuredBuffer<uint> Slots : register(t4, space0);

#include "unit_state.hlsli"
#include "unit_sprites.hlsli"
#include "unit_status.hlsli"

// Each unit's status effects, when HasStatus: tinted on top of the health fade.
StructuredBuffer<UnitStatus> Status : register(t5, space0);

cbuffer UnitUniforms : register(b0, space1) {
    float4x4 ViewProjection;
    float4 UvRect;   // xy = top-left, zw = bottom-right
    float4 Color;    // tint color
    float2 UnitSize; // width, height
    float Alpha;     // 0 = draw at PrevPos, 1 = draw at Units[].pos
    float MaxJump;   // 0: no PrevPos. Moves longer than this (a respawn) are not blended.
    float InvMaxHealth; // 1 / max hit points; 0: no Health buffer, draw everyone at full health
    uint HasIds;        // 0: no Ids buffer, a unit's ID is its index
    uint HasSlots;      // 0: no Slots buffer, draw in index order
    uint SpriteMode;    // 0: flat disc from UvRect, 1: crowd sprites
    uint Lod;           // sprites: 0 near, 1 mid, 2 low, 3 circle
    uint Species;       // sprites: 0 humans, 1 aliens
    uint Seed;          // sprites: varies every unit's look per level
    uint IntegerScale;  // sprites: round sprite pixels to whole screen pixels (never below one)
    float2 ViewportSize; // pixels, for snapping sprites to the pixel grid
    float PixelsPerUnit; // screen pixels per world unit
    uint Rotate;         // sprites: face the direction of travel
    uint DirectionSteps; // sprites: snap facing to this many directions; 0 = free
    float MinTurnSpeed;  // sprites: slower than this, keep facing up (no direction to read)
    float Time;         // seconds, for burning units' flicker
    uint HasStatus;     // 0: no Status buffer
    uint MarkFirst;     // nonzero: draw the unit with ID 0 red, so it can be followed
    float3 Padding;
};

struct Output {
    float4 Color : TEXCOORD0; // disc: final tint; sprites: multiplier on the palette colour
    float2 Texcoord : TEXCOORD1;
    nointerpolation uint4 Pal : TEXCOORD2; // sprites: palette bases (hair, skin, clothes, accessory)
    nointerpolation float2 CellCentre : TEXCOORD3; // sprites: atlas texel at the unit's cell centre
    nointerpolation float2 Facing : TEXCOORD4;     // sprites: unit direction the art's "up" turns to
    float4 Position : SV_Position;
};

static const uint TriangleIndices[6] = {0, 1, 2, 3, 2, 1};

static const float2 QuadCorners[4] = {
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 1.0f),
};

// Integer hash with good avalanche (lowbias32).
uint hashU(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// A unit's look from its ID: which atlas variant, and its palette bases.
void appearance(uint unitId, out uint variant, out uint4 pal) {
    uint h1 = hashU(unitId ^ hashU(Seed));
    uint h2 = hashU(h1 + 0x9e3779b9u);
    if (Species == 0) {
        uint style = h1 % HUMAN_STYLE_COUNT;
        h1 /= HUMAN_STYLE_COUNT;
        uint accessory = ACCESSORY_TABLE[h1 % ACCESSORY_TABLE_SIZE];
        h1 /= ACCESSORY_TABLE_SIZE;
        variant = style * ACCESSORY_COUNT + accessory;
        pal.x = PAL_HUMAN_HAIR + HUMAN_HAIR_TABLE[h1 % HUMAN_HAIR_TABLE_SIZE] * 3;
        pal.y = PAL_HUMAN_SKIN + (h2 % HUMAN_SKIN_COUNT) * 2;
        h2 /= HUMAN_SKIN_COUNT;
        pal.z = PAL_HUMAN_CLOTHES + (h2 % HUMAN_CLOTHES_COUNT);
        h2 /= HUMAN_CLOTHES_COUNT;
        pal.w = accessory == ACCESSORY_CAP ? PAL_HUMAN_CAP + (h2 % HUMAN_CAP_COUNT) * 2
                                           : PAL_HUMAN_ACCESSORY + (h2 % HUMAN_ACCESSORY_COUNT) * 2;
    } else {
        variant = ALIEN_VARIANT_BASE + h1 % ALIEN_STYLE_COUNT;
        h1 /= ALIEN_STYLE_COUNT;
        pal.x = PAL_ALIEN_SKIN + (h1 % ALIEN_SKIN_COUNT) * 3;
        pal.y = pal.x; // aliens have no skin role; their skin is drawn in the hair roles
        pal.z = PAL_ALIEN_SUIT + (h2 % ALIEN_SUIT_COUNT);
        h2 /= ALIEN_SUIT_COUNT;
        pal.w = PAL_ALIEN_TIP + (h2 % ALIEN_TIP_COUNT);
    }
}

Output main(uint id : SV_VertexID) {
    uint instance = id / 6;
    uint unitIndex = HasSlots != 0 ? Slots[instance] : instance;
    uint corner = TriangleIndices[id % 6];

    UnitData unit = Units[unitIndex];
    uint unitId = HasIds != 0 ? Ids[unitIndex] : unitIndex;

    float2 pos = unit.pos;
    if (MaxJump > 0.0f) {
        float2 prev = PrevPos[unitIndex];
        if (distance(prev, pos) <= MaxJump) {
            pos = lerp(prev, pos, Alpha);
        }
    }

    Output output;
    output.Pal = uint4(0, 0, 0, 0);
    output.CellCentre = float2(0.0f, 0.0f);
    output.Facing = float2(0.0f, -1.0f);
    float2 world;
    // The quad's top-left in world space, which sprites snap to the pixel grid.
    float2 snapOrigin = pos;

    if (SpriteMode == 0) {
        float2 texcoords[4] = {
            float2(UvRect.x, UvRect.y),
            float2(UvRect.z, UvRect.y),
            float2(UvRect.x, UvRect.w),
            float2(UvRect.z, UvRect.w),
        };
        world = pos + QuadCorners[corner] * UnitSize;
        output.Texcoord = texcoords[corner];
    } else {
        uint variant;
        appearance(unitId, variant, output.Pal);

        // Positions are top-left corners; sprites are drawn around the centre.
        float2 centre = pos + UnitSize * 0.5f;
        uint lod = min(Lod, 2u); // the circle is coloured from the low art
        float2 cell = float2(variant % ATLAS_COLUMNS, lod * ROWS_PER_LOD + variant / ATLAS_COLUMNS) * CELL;
        float2 cellCentre = cell + CELL * 0.5f;

        // The head is LOD_HEAD sprite pixels across and spans the unit, at
        // every zoom: sprites never grow past their unit.
        float texel = UnitSize.x / LOD_HEAD[lod];
        if (IntegerScale != 0 && PixelsPerUnit > 0.0f) {
            texel = max(1.0f, round(texel * PixelsPerUnit)) / PixelsPerUnit;
        }
        float halfWindow = LOD_WINDOW[lod] * 0.5f;
        float2 offset = (QuadCorners[corner] * 2.0f - 1.0f) * halfWindow;
        world = centre + offset * texel;
        snapOrigin = centre - halfWindow * texel;
        // unit_sprite.frag works in sprite pixels from the centre. The circle
        // LOD is a hard-edged circle the unit's size, coloured by the low art's
        // centre texel (hair, skin or cap): scaling 5px art below 5 screen
        // pixels makes a plus.
        output.Texcoord = offset;
        output.CellCentre = cellCentre;

        // Art faces up (-y). Turn it to the way the unit walks.
        float speed = length(unit.vel);
        if (Rotate != 0 && Lod < 3 && speed > MinTurnSpeed) {
            float2 dir = unit.vel / speed;
            if (DirectionSteps > 0) {
                float step = 6.28318530718f / float(DirectionSteps);
                float angle = round(atan2(dir.y, dir.x) / step) * step;
                dir = float2(cos(angle), sin(angle));
            }
            output.Facing = dir;
        }
    }

    float4 clip = mul(ViewProjection, float4(world, 0.0f, 1.0f));
    if (SpriteMode != 0 && ViewportSize.x > 0.0f) {
        // Shift the whole quad so its top-left lands on a screen pixel, or a
        // moving unit's pixels crawl as it slides between them.
        float2 originClip = mul(ViewProjection, float4(snapOrigin, 0.0f, 1.0f)).xy;
        float2 originPx = (originClip * 0.5f + 0.5f) * ViewportSize;
        clip.xy += (round(originPx) - originPx) / ViewportSize * 2.0f;
    }
    // A dead unit's quad collapses to one point outside clip space, so it
    // draws nothing.
    if (isDead(unit.pos)) {
        clip = mul(ViewProjection, float4(unit.pos, 0.0f, 1.0f));
    }
    output.Position = clip;

    // Full health draws in Color (white for sprites, so the palette shows
    // true), fading linearly to black at 0.
    float health = InvMaxHealth > 0.0f ? saturate(Health[unitIndex] * InvMaxHealth) : 1.0f;
    // Disc: the tint itself. Sprites: a multiplier on the palette colour, so
    // the status tints below colour them the same way.
    float4 tint = SpriteMode == 0 ? float4(Color.rgb * health, Color.a) : float4(health, health, health, 1.0f);
    if (HasStatus != 0) {
        UnitStatus status = Status[unitIndex];
        if (status.slow > 0.0f) {
            tint.rgb = lerp(tint.rgb, float3(0.6f, 0.4f, 1.0f), 0.55f);
        }
        if (status.stun > 0.0f) {
            tint.rgb = lerp(tint.rgb, float3(0.75f, 0.88f, 1.0f), 0.7f);
        }
        if (status.burn > 0.0f) {
            // Each unit flickers out of step with the next.
            float flicker = 0.5f + 0.5f * sin(Time * 18.0f + float(unitId) * 1.7f);
            tint.rgb = lerp(tint.rgb, float3(1.0f, 0.45f, 0.08f), 0.55f + 0.35f * flicker);
        }
    }
    // Unit 0 can be drawn red so it can be followed; sprites are crowd texture, so they never are.
    output.Color = (SpriteMode == 0 && MarkFirst != 0 && unitId == 0) ? float4(1.0f, 0.0f, 0.0f, 1.0f) : tint;
    return output;
}
