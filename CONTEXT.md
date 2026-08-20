# horde

A top-down game in which waves of units cross a hand-authored arena. This
glossary covers the level-authoring vocabulary; it is a glossary only, and
carries no implementation detail.

## Language

**Level**:
A rectangular playable arena together with everything placed inside it. The
unit of authoring, and what a level file contains exactly one of.
_Avoid_: map, arena, stage

**Background**:
The coloured surface filling a level's bounds, beneath everything else. The
only element of a level that units do not collide with, and the only one there
is exactly one of.
_Avoid_: floor, backdrop, ground

**Wall**:
A shape placed inside a level that units will collide with and cannot pass
through. Every placed shape is a wall; there is no decorative shape.
_Avoid_: obstacle, collider, barrier, polygon

**Wall kind**:
Which geometry a wall has: rectangle, isoceles triangle, circle, or polyline.
_Avoid_: shape type, primitive

**Polyline**:
A wall kind consisting of an ordered run of points joined by straight segments
of uniform thickness. It need not be closed — a single straight segment is a
valid polyline.
_Avoid_: path, custom shape, freehand

**Spawn**:
A marker on a level's edge at which units enter the level. Not a wall.
_Avoid_: spawner, entrance, start point

**Exit**:
A marker on a level's edge which units leave the level through. Not a wall.
Any exit is a valid destination for units from any spawn.
_Avoid_: goal, end point, sink

**Marker**:
A spawn or an exit. Markers lie flat against a level edge, may be stretched
along that edge but not into the level, and may not overlap one another. Every
level has at least one of each at all times.
_Avoid_: node, point

**Grid snapping**:
An authoring aid, toggleable and off by default, that constrains placement and
rotation to fixed increments. Walls remain freely placeable off-grid and
off-axis when it is disabled.
_Avoid_: snap-to-grid, magnetism

**Valid level**:
A level with no condition that blocks saving. A wall lying outside the level's
bounds, or one with degenerate geometry, makes a level invalid; it must be
corrected or deleted before the level can be written to disk.
_Avoid_: clean level, well-formed level
