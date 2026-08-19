# Level shapes are textured quads, not real geometry

The renderer draws exactly one primitive: a quad, rotated about its top-left
corner, sampling a white alpha mask from a shared atlas and tinted by
`Sprite::color`. Rather than add a geometry path for the level editor's circles,
triangles and polylines, we draw them as atlas cells and as thin rotated quads —
so a circle in a level is, literally, a picture of a circle.

## Considered options

- **Atlas-masked quads (chosen).** No new pipeline, no shader edits, and it
  reuses machinery already proven by `SpriteBatch` and by `TechTreeScene`'s
  graph edges. The whole level renders in one draw call. The cost is that shape
  edges are limited by texture resolution, mitigated by regenerating the atlas
  at 4x4 cells of 128px with supersampled alpha.
- **A second pipeline with triangulated, coloured geometry.** Crisp at any zoom
  and handles arbitrary polygons natively, but it is a parallel renderer — new
  shaders, new binding setup, new buffer management — and that is a large piece
  of renderer work that is not level-editor work.
- **Signed distance fields in the fragment shader.** Crisp at any zoom while
  keeping one pipeline, but it breaks the 64-byte `gfx::Sprite` layout that is
  `static_assert`ed against the HLSL struct, and requires regenerating SPIR-V,
  MSL and DXIL. DXIL can only be signed on Windows, so this would also change
  who is able to make a shader change.

## Consequences

The editor's real product is correct level *data*; how prettily that data
rasterises is a separable concern. If shape fidelity becomes a problem, the SDF
option is the intended upgrade and is confined to `gfx::Sprite`, the two sprite
shaders, and `LevelRenderer` — the level data model and the entire editor are
unaffected by it.
