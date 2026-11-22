// One device of the shell, drawn over the whole window with fullscreen.vert:
// its drop shadow, side buttons, metal band, black glass bezel, the screen
// texture clipped to concentric rounded corners, and its camera (the phone's
// Dynamic Island, the tablet's lens in the bezel). Kind 2 draws the backdrop
// behind the devices instead, and kind 3 a window-sized scene as the backdrop,
// faded to its blur (Screen and Blurred are then that scene). Shapes are signed distances in the device's
// logical px, antialiased over one window px. Output is premultiplied alpha.
//
// SDL_GPU fragment stage binding rules:
//   (t[n], space2) / (s[n], space2) - sampled textures / samplers
//   (b[n], space3)                  - uniform buffers

Texture2D<float4> Screen : register(t0, space2);
Texture2D<float4> Blurred : register(t1, space2);
SamplerState ScreenSampler : register(s0, space2);
SamplerState BlurredSampler : register(s1, space2);

// Must match DeviceUniforms in src/ui/DeviceShell.cpp.
cbuffer DeviceUniforms : register(b0, space3) {
    float2 WindowSize;   // window px
    float2 ScreenOrigin; // screen texture's top-left, window px (whole pixels)
    float2 ScreenPx;     // screen texture size, window px
    float Scale;         // window px per logical px
    float Kind;          // 0 phone, 1 tablet, 2 backdrop, 3 window scene backdrop
    float Bezel;         // logical px from the screen edge to the device edge
    float ScreenRadius;  // logical px; the body's corner is this plus Bezel
    float Landscape;     // phone only: 1 when turned with its top to the left
    float Blur;          // 0..1 fade from the sharp screen to the blurred one
    float Dim;           // brightness of a receding device, 1 in front
    float3 Padding;
};

struct Input {
    float2 Texcoord : TEXCOORD0;
};

float sdRoundRect(float2 p, float2 halfSize, float radius) {
    const float2 q = abs(p) - halfSize + radius;
    return length(max(q, 0.0f)) + min(max(q.x, q.y), 0.0f) - radius;
}

// The outward normal of a rounded rect at p: radial in the corners, axis-aligned along the sides.
float2 roundRectNormal(float2 p, float2 halfSize, float radius) {
    const float2 q = abs(p) - (halfSize - radius);
    float2 n = (q.x > 0.0f && q.y > 0.0f) ? normalize(q) : (q.x > q.y ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f));
    return n * float2(p.x < 0.0f ? -1.0f : 1.0f, p.y < 0.0f ? -1.0f : 1.0f);
}

// Coverage of a shape at signed distance d, over an edge `aa` wide.
float cover(float d, float aa) {
    return saturate(0.5f - d / aa);
}

// Layers a straight-alpha colour over a premultiplied accumulator.
void over(inout float4 acc, float3 rgb, float alpha) {
    acc = float4(rgb * alpha, alpha) + acc * (1.0f - alpha);
}

float4 backdrop(float2 uv) {
    // A dark studio sweep: lighter above, a soft pool of light behind the devices.
    float3 colour = lerp(float3(0.105f, 0.112f, 0.13f), float3(0.035f, 0.038f, 0.045f), uv.y);
    const float2 d = (uv - float2(0.5f, 0.45f)) * float2(WindowSize.x / WindowSize.y, 1.0f);
    colour += float3(0.05f, 0.055f, 0.065f) * exp(-dot(d, d) * 3.0f);
    return float4(colour, 1.0f);
}

// A side button: a short pill standing proud of the band, lit from above.
void sideButton(inout float4 acc, float2 q, float2 centre, float2 halfSize, float3 metal, float aa) {
    const float d = sdRoundRect(q - centre, halfSize, min(halfSize.x, halfSize.y));
    const float shade = 0.8f + 0.35f * saturate(0.5f - (q.y - centre.y) / (2.0f * halfSize.y));
    over(acc, metal * shade, cover(d, aa));
}

float4 main(Input input) : SV_Target0 {
    const float2 pixel = input.Texcoord * WindowSize;
    if (Kind > 2.5f) {
        const float3 sharp = Screen.Sample(ScreenSampler, input.Texcoord).rgb;
        const float3 soft = Blurred.Sample(BlurredSampler, input.Texcoord).rgb;
        return float4(lerp(sharp, soft, Blur) * Dim, 1.0f);
    }
    if (Kind > 1.5f) {
        return backdrop(input.Texcoord);
    }

    const bool phone = Kind < 0.5f;
    const float aa = 1.0f / Scale;

    // Logical px about the screen centre, y down. The phone is shaded in its
    // own upright frame, so a landscape phone turns everything with it: its
    // top (and the island) to the left, its right side up.
    const float2 p = (pixel - (ScreenOrigin + ScreenPx * 0.5f)) / Scale;
    const bool turned = phone && Landscape > 0.5f;
    const float2 q = turned ? float2(-p.y, p.x) : p;
    float2 screenHalf = ScreenPx * 0.5f / Scale;
    screenHalf = turned ? screenHalf.yx : screenHalf;

    const float2 bodyHalf = screenHalf + Bezel;
    const float bodyRadius = ScreenRadius + Bezel;
    const float band = phone ? 2.6f : 2.2f;

    // Titanium for the phone, space-grey aluminium for the tablet.
    const float3 metalDark = phone ? float3(0.24f, 0.24f, 0.25f) : float3(0.25f, 0.26f, 0.28f);
    const float3 metalLight = phone ? float3(0.64f, 0.63f, 0.61f) : float3(0.62f, 0.64f, 0.67f);
    const float frameDim = lerp(1.0f, Dim, 0.7f);

    float4 acc = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // Drop shadow: a wide soft falloff and a tight contact shade, both cast downward.
    // Measured in the window's frame so it always falls down the screen.
    {
        const float2 shadowHalf = turned ? bodyHalf.yx : bodyHalf;
        const float wide = sdRoundRect(p - float2(0.0f, 22.0f), shadowHalf, bodyRadius);
        const float tight = sdRoundRect(p - float2(0.0f, 4.0f), shadowHalf, bodyRadius);
        const float alpha = 0.5f * exp(-max(wide, 0.0f) / 26.0f) * saturate(wide / 6.0f + 1.0f) +
                            0.35f * exp(-max(tight, 0.0f) / 5.0f);
        over(acc, float3(0.0f, 0.0f, 0.0f), saturate(alpha));
    }

    // Side buttons, standing 2.5 px off the band, placed by fraction of the body.
    const float3 buttonMetal = lerp(metalDark, metalLight, 0.45f) * frameDim;
    if (phone) {
        const float h = 2.0f * bodyHalf.y;
        const float x = bodyHalf.x + 0.4f;
        // Left: Action button, volume up, volume down. Right: the side button.
        sideButton(acc, q, float2(-x, -bodyHalf.y + 0.205f * h), float2(2.4f, 0.02f * h), buttonMetal, aa);
        sideButton(acc, q, float2(-x, -bodyHalf.y + 0.29f * h), float2(2.4f, 0.036f * h), buttonMetal, aa);
        sideButton(acc, q, float2(-x, -bodyHalf.y + 0.38f * h), float2(2.4f, 0.036f * h), buttonMetal, aa);
        sideButton(acc, q, float2(x, -bodyHalf.y + 0.33f * h), float2(2.4f, 0.058f * h), buttonMetal, aa);
    } else {
        const float w = 2.0f * bodyHalf.x;
        const float h = 2.0f * bodyHalf.y;
        // Volume up and down on the top long edge near the left corner; the top
        // button at the top of the left short edge.
        const float top = -bodyHalf.y - 0.4f;
        sideButton(acc, q, float2(-bodyHalf.x + 0.10f * w, top), float2(0.03f * w, 2.2f), buttonMetal, aa);
        sideButton(acc, q, float2(-bodyHalf.x + 0.175f * w, top), float2(0.03f * w, 2.2f), buttonMetal, aa);
        sideButton(acc, q, float2(-bodyHalf.x - 0.4f, -bodyHalf.y + 0.12f * h), float2(2.2f, 0.045f * h), buttonMetal,
                   aa);
    }

    // The body: a metal band round black glass.
    const float body = sdRoundRect(q, bodyHalf, bodyRadius);
    const float bodyCover = cover(body, aa);
    if (bodyCover > 0.0f) {
        // The band is a rounded edge lit from the upper left: its brightness
        // follows the edge normal, with a bright rim along the outside.
        const float2 n = roundRectNormal(q, bodyHalf, bodyRadius);
        const float2 lightFrom = turned ? float2(-0.95f, 0.35f) : float2(-0.35f, -0.95f);
        const float lit = dot(n, normalize(lightFrom));
        const float across = saturate(-body / band); // 0 at the outer edge, 1 at the glass
        float3 metal = lerp(metalDark, metalLight, saturate(0.45f + 0.45f * lit));
        metal *= 0.75f + 0.35f * sin(across * 3.14159f);
        metal += 0.25f * saturate(lit) * exp(-abs(-body - 0.6f) / 0.5f);
        over(acc, metal * frameDim, bodyCover);

        // Black glass inside the band, with the faintest sheen towards the top.
        const float glass = sdRoundRect(q, bodyHalf - band, bodyRadius - band);
        const float sheen = 0.025f * saturate(0.5f - q.y / (2.0f * bodyHalf.y));
        over(acc, float3(0.012f, 0.012f, 0.016f) + sheen, cover(glass, aa));

        if (!phone) {
            // The front camera, centred in the top bezel.
            const float2 lens = q - float2(0.0f, -screenHalf.y - Bezel * 0.5f);
            over(acc, float3(0.07f, 0.075f, 0.09f), cover(length(lens) - 3.0f, aa));
            over(acc, float3(0.12f, 0.16f, 0.24f), cover(length(lens - float2(-0.6f, -0.6f)) - 1.1f, aa));
        }
    }

    // The screen, clipped to the corner concentric with the body's.
    const float screenD = sdRoundRect(q, screenHalf, ScreenRadius);
    const float screenCover = cover(screenD, aa);
    if (screenCover > 0.0f) {
        const float2 uv = (pixel - ScreenOrigin) / ScreenPx;
        const float3 sharp = Screen.Sample(ScreenSampler, uv).rgb;
        const float3 soft = Blurred.Sample(BlurredSampler, uv).rgb;
        over(acc, lerp(sharp, soft, Blur) * Dim, screenCover);
    }

    if (phone) {
        // The Dynamic Island, 11 px down from the top of the screen, with the
        // front camera's lens at its right end.
        const float2 island = q - float2(0.0f, -screenHalf.y + 11.0f + 18.5f);
        over(acc, float3(0.0f, 0.0f, 0.0f), cover(sdRoundRect(island, float2(62.5f, 18.5f), 18.5f), aa));
        const float2 lens = island - float2(43.0f, 0.0f);
        over(acc, float3(0.04f, 0.045f, 0.06f) * Dim, cover(length(lens) - 6.0f, aa));
        over(acc, float3(0.1f, 0.13f, 0.22f) * Dim, cover(length(lens - float2(-1.5f, -1.5f)) - 2.0f, aa));
    }

    return acc;
}
