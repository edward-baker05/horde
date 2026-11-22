// Status effects on units, shared by every shader that sets or reads them.
// See docs/superpowers/specs/2026-11-04-unit-effects-and-bullets-design.md.
//
// A unit's status and an entry of the status table (what a source applies)
// have the same layout: matches sim::StatusEffect.

#ifndef HORDE_UNIT_STATUS_HLSLI
#define HORDE_UNIT_STATUS_HLSLI

struct UnitStatus {
    float stun;       // seconds left; walks nowhere while stunned
    float slow;       // seconds left
    float slowFactor; // speed multiplier while slowed, 0 to 1
    float burn;       // seconds left
    float burnDps;    // hit points lost per second while burning
    float spare;
};

// Applies `effect` on top of `status`. Nothing stacks: each timer becomes the
// longer of the two, a slow the stronger factor, a burn the higher rate.
void applyStatus(inout UnitStatus status, UnitStatus effect) {
    status.stun = max(status.stun, effect.stun);
    if (effect.slow > 0.0f) {
        status.slowFactor = status.slow > 0.0f ? min(status.slowFactor, effect.slowFactor) : effect.slowFactor;
        status.slow = max(status.slow, effect.slow);
    }
    if (effect.burn > 0.0f) {
        status.burnDps = status.burn > 0.0f ? max(status.burnDps, effect.burnDps) : effect.burnDps;
        status.burn = max(status.burn, effect.burn);
    }
}

#endif
