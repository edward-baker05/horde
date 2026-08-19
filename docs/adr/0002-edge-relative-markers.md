# Spawn and exit markers are edge-relative, not positioned

A marker is stored as `{ edge, offset, length }` rather than as a rectangle with
a centre, size and rotation like every other element of a level. Markers must
lie flat against a level edge, may be stretched along that edge but never toward
the centre of the level, and may never overlap one another — and this
representation makes all three of those things unrepresentable rather than
merely checked for.

## Consequences

Moving a marker toward the level's interior is not expressible in the type, so
no validation rule is needed for it. Overlap becomes a one-dimensional interval
test on a single edge, cheap enough to clamp against during a drag so that
overlapping markers never exist even transiently. Resizing a level requires
clamping two scalars per marker rather than re-deriving which edge each marker
was nearest. Markers on different edges occupy separate interval spaces, so
corners need no special handling.

The price is that markers do not share the editor's transform, selection and
handle code with walls: they get their own two-ended handle behaviour and are
excluded from rotation and recolouring. That asymmetry is deliberate — markers
are a distinct domain concept from walls, not a constrained kind of wall.
