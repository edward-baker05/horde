// Unit fragment shader, paired with unit.vert.
//
// SpriteMode 0: the flat disc, exactly like sprite.frag (atlas * tint).
// SpriteMode 1: the units atlas holds a ROLE per texel (hair, skin, clothes...)
// in its red channel, not a colour. The unit's palette bases, picked in
// unit.vert from its ID, turn the role into a colour from Palette, then the
// health fade from unit.vert darkens it.
//
// Sprites are drawn in sprite pixels from the unit's centre (Texcoord). Each
// screen fragment snaps to the screen-aligned sprite pixel it is in, and that
// pixel's centre is turned by the unit's facing to find which art pixel to
// show: rotation without tilting the pixels themselves.
//
// SDL_GPU fragment stage binding rules:
//   (t[n], space2) - sampled textures, storage textures, storage buffers
//   (s[n], space2) - samplers
//   (b[n], space3) - uniform buffers

#include "unit_sprites.hlsli"

Texture2D<float4> Atlas : register(t0, space2);
SamplerState AtlasSampler : register(s0, space2);

cbuffer UnitSpriteUniforms : register(b0, space3) {
    float4 Palette[PALETTE_SIZE];
    uint SpriteMode;
    uint Lod;       // sprites: 0 near, 1 mid, 2 far
    uint2 Padding;
};

struct Input {
    float4 Color : TEXCOORD0;
    float2 Texcoord : TEXCOORD1;
    nointerpolation uint4 Pal : TEXCOORD2; // hair, skin, clothes, accessory palette bases
    nointerpolation float2 CellCentre : TEXCOORD3; // atlas texel at the unit's cell centre
    nointerpolation float2 Facing : TEXCOORD4;     // the direction the art's "up" (-y) turns to
};

float3 roleColour(uint role, uint4 pal) {
    switch (role) {
    case ROLE_HAIR:           return Palette[pal.x].rgb;
    case ROLE_HAIR_LIGHT:     return Palette[pal.x + 1].rgb;
    case ROLE_HAIR_DARK:      return Palette[pal.x + 2].rgb;
    case ROLE_SKIN:           return Palette[pal.y].rgb;
    case ROLE_SKIN_LIGHT:     return Palette[pal.y + 1].rgb;
    case ROLE_CLOTHES:        return Palette[pal.z].rgb;
    case ROLE_ACCESSORY:      return Palette[pal.w].rgb;
    case ROLE_ACCESSORY_DARK: return Palette[pal.w + 1].rgb;
    case ROLE_GLASSES:        return Palette[PAL_GLASSES].rgb;
    default:                  return Palette[PAL_GLASSES + 1].rgb; // ROLE_GLINT
    }
}

float4 main(Input input) : SV_Target0 {
    if (SpriteMode != 0 && Lod == 2) {
        // Far: a hard-edged circle filling the unit, whatever its size on
        // screen. Texcoord is in head pixels from the centre (head = 5 across);
        // the threshold matches the 5px art exactly at 1:1 (.XXX. / XXXXX x3).
        if (dot(input.Texcoord, input.Texcoord) > 6.25f) {
            discard;
        }
        float4 centre = Atlas.SampleLevel(AtlasSampler, input.CellCentre / ATLAS_SIZE, 0.0f);
        uint farRole = (uint)round(centre.r * 255.0f);
        return float4(roleColour(farRole, input.Pal) * input.Color.rgb, input.Color.a);
    }
    if (SpriteMode == 0) {
        return Atlas.Sample(AtlasSampler, input.Texcoord) * input.Color;
    }
    // The quad's sprite-pixel grid has its lines on half-integers, so the
    // centre of the sprite pixel this fragment is in is the nearest integer.
    float2 cellPos = floor(input.Texcoord + 0.5f);
    // Undo the facing. The rotation taking art-up (0,-1) to f is
    // R = [[-f.y, -f.x], [f.x, -f.y]]; art = transpose(R) * cellPos.
    float2 f = input.Facing;
    float2 art = float2(-f.y * cellPos.x + f.x * cellPos.y, -f.x * cellPos.x - f.y * cellPos.y);
    if (any(abs(art) > CELL * 0.5f - 0.01f)) {
        discard; // outside this unit's cell: don't read the neighbour's art
    }
    float4 texel = Atlas.SampleLevel(AtlasSampler, (input.CellCentre + art) / ATLAS_SIZE, 0.0f);
    uint role = (uint)round(texel.r * 255.0f);
    if (role == ROLE_EMPTY || texel.a < 0.5f) {
        discard;
    }
    return float4(roleColour(role, input.Pal) * input.Color.rgb, input.Color.a);
}
