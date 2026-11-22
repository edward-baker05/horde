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
The coloured surface filling a level's bounds, beneath everything else. Units
do not collide with it, and it is the only element there is exactly one of.
_Avoid_: floor, backdrop, ground

**Wall**:
A shape placed inside a level that units will collide with and cannot pass
through. Every placed shape is a wall; there is no decorative shape.
_Avoid_: obstacle, collider, barrier, polygon

**Wall kind**:
Which geometry a wall has: rectangle, isoceles triangle, circle, or polyline.
_Avoid_: shape type, primitive

**Wall grid**:
The level overlaid with square cells one unit across, any of which may be
painted solid. Painted cells are walls: units collide with them. Every level
has exactly one wall grid, though it may be empty.
_Avoid_: tilemap, tiles, voxels

**Cell**:
One square of the wall grid: empty, full, or a slope, and when not empty, one
of its palette's colours.
_Avoid_: tile, pixel, block

**Slope**:
A cell solid on one side of its diagonal, giving a 45° surface.
_Avoid_: ramp, diagonal, half-tile

**Palette**:
The eight colours a level's wall grid can use. Cells refer to an entry rather
than holding a colour, so editing an entry recolours every cell that uses it.
_Avoid_: swatches, colour table

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

**Field region**:
A box placed inside a level that units follow in one direction while inside
it, in place of heading for an exit, so the horde is steered around walls
rather than into them. Not a wall: units pass through it. Its rotation sets
the direction of the flow.
_Avoid_: flow field, vector field, zone, current

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

**Turret**:
Something the player buys and places in the prep phase that damages or
obstructs units. Turrets are fixed once the round starts. Every turret is a mount, a blocker or a floor trap.
_Avoid_: tower, defence, weapon

**Mount**:
A turret placed on top of a wall, out of the horde's reach. Units cannot harm
it.
_Avoid_: emplacement

**Blocker**:
A solid turret placed on open ground. Units collide with it and wear it down;
it is removed when it has no hit points left.
_Avoid_: obstacle, barrier

**Floor trap**:
A turret placed on open ground that units walk over rather than collide with.
_Avoid_: hazard

**Prep phase**:
The start of a round, before START, when turrets are bought and placed and no
units spawn.
_Avoid_: build phase, setup

**Leak**:
A unit that reaches an exit during a round. It leaves the level and is counted.
_Avoid_: escape

**Status effect**:
A timed condition a weapon puts on a unit. Re-applying one refreshes it
rather than adding to it.
_Avoid_: debuff, condition, ailment

**Stun**:
A status effect under which a unit stops walking. It can still be pushed by
the crowd and by blasts.
_Avoid_: freeze, paralyse

**Slow**:
A status effect under which a unit walks at a fraction of its speed.
_Avoid_: snare, chill

**Burning**:
A status effect under which a unit loses hit points over time. It does not
spread between units.
_Avoid_: on fire, ignite, damage over time

**Bullet**:
A projectile a weapon fires, which travels across the level, stops at walls
and blockers, and hits units it touches.
_Avoid_: projectile, shot, round

**Pierce**:
How many more units a bullet can hit after its first before it is spent.
_Avoid_: penetration
