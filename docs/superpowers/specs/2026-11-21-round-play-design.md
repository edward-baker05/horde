# Round play — design

## Intent

horde has a crowd simulation that can move up to a million units smoothly, a level editor, and a tablet UI for rounds, but no game. This pass makes one **round** playable from start to finish: place towers, upgrade them, click to do damage, and win or lose.

The player is mostly a **monitor of towers**. In a round they have three verbs:
- **Place** a tower from the tray.
- **Upgrade** a tower in place. Upgrading also heals it to full.
- **Click** the feed to do damage directly. Early campaign rounds depend on clicking heavily and later ones less. It guarantees a round can't softlock, and it gives the campaign something besides towers to upgrade later.

What "feels good" is smoothness. The crowd should move and fill space like a real crowd, and nothing added here should break that. Blasts don't need to look natural, but they must not make the crowd jitter or stall.

## Goals

- A playable round in Player mode on the tablet: round money, four towers, two abilities, a click, upgrades and repairs, a win and a loss, and a results screen.
- Towers attract part of the crowd. Units pile against them, and contact wears them down.
- Spawn speed rises as the player kills, so a fast win is earned by killing fast all the way through.
- Upgrade money for winning is computed and saved, ready for the campaign.
- Round settings are authored per level in the editor.

## Non-goals

- The campaign: a level sequence, progression between rounds, spending upgrade money, and the tech tree or shop. That is the next project.
- Per-unit freeze timers (Frost is an area slow for now).
- Selling towers.
- Tower art beyond simple atlas shapes, sounds, and polish on transitions or effects.
- Multiple cameras or pan and zoom in the feed.
- Changes to Dev mode, presets or `--bench`, beyond keeping them working.

## Anti-goals

- Anything that makes the crowd boil, jitter or teleport. For example, tower attraction must not move units bodily as the debug pull explosion does.
- Stalling the frame to read back from the GPU. Every per-frame read must be asynchronous.
- Gameplay rules and balance numbers written in HLSL. Tuning a tower must not need a shader recompile.
- Changing benchmark timings: Dev and `--bench` must run the simulation as before.

## Approaches considered

1. **CPU rules, GPU crowd effects, small async reports back — chosen.**
   - A CPU round module owns money, towers, cooldowns, upgrades, spawn rate, win and loss.
   - Each step it hands the simulation tower colliders, attractors, slow zones and blasts.
   - The GPU returns small counters, read about a frame late without stalling: kills, units that reached an exit, contacts per tower, and a coarse density map for aiming.
   - Balance stays in tested C++. The cost is new shader plumbing.
2. **Rules on the GPU as well**, with towers aiming and firing in a compute pass.
   - No latency.
   - Gameplay ends up in untestable HLSL, and every tweak needs a recompile.
3. **Read all units back and run the game on the CPU.**
   - Simplest.
   - Megabytes of readback per frame at 100k–1M units stalls frames, which defeats the point.

## Decided design

### Round rules

- **Units.** A round has a fixed `totalUnits` that stream in at the spawns. In Player mode a unit that reaches an exit **leaves the level** and counts as **leaked**. There is no respawn.
- **Loss.** `leaked >= leakLimit`.
- **Win.** Every unit has spawned and none remain alive, with `leaked < leakLimit`.
- **Round money:**
  - The round starts with `startMoney`.
  - Each kill adds `killReward`.
  - It pays for towers, upgrades, repairs and abilities, and doesn't carry between rounds.
- **Spawn rate:**
  - `rate = baseSpawnRate × (1 + spawnBoost × killed / totalUnits)`, in units per second.
  - It's re-applied as kill counts arrive.
  - Killing early brings the rest of the horde in sooner.
- **Upgrade money (external).** Winning awards `winReward + max(0, parSeconds − roundSeconds) × bonusPerSecond`. It's shown on the results screen and added to a running total in a small save file in the user's pref directory (`SDL_GetPrefPath`). Nothing spends it yet. A loss awards nothing.
- **Round time** counts only while the round is unpaused.

### Round settings in the level file

- A level gets an optional `round` object. It bumps the level format to version 3, and version 2 levels still load with default round settings.
- The fields are `totalUnits`, `unitHp`, `leakLimit`, `startMoney`, `killReward`, `baseSpawnRate`, `spawnBoost`, `winReward`, `parSeconds` and `bonusPerSecond`.
- Campaign rounds will range from about 100 units early to 1,000,000 in the final level. This pass targets **10,000** as the default.
- **Defaults** (for levels with no `round` object, and as the editor's starting values):

  | Field | Default |
  |---|---|
  | `totalUnits` | 10,000 |
  | `unitHp` | 10 |
  | `leakLimit` | 200 |
  | `startMoney` | 400 |
  | `killReward` | 1 |
  | `baseSpawnRate` | 30 / s |
  | `spawnBoost` | 4 (up to 150 / s at the end) |
  | `winReward` | 100 |
  | `parSeconds` | 240 |
  | `bonusPerSecond` | 1 |
- The editor's Level panel (ImGui, as now) gets a "Round" section to edit them.
- The editor's play-test plays the round with these settings.

### Towers and abilities

The tray shows six items:

| Item | Kind | Behaviour |
|---|---|---|
| **Sniper** ◆ | tower | Slow fire, very small blast (2–3 units), heavy damage |
| **Gunner** ▮ | tower | Fast fire, small blast, light damage |
| **Pulse** ◯ | tower | Large blast with strong knockback (shove) and light damage |
| **Frost** ▲ | tower | Units inside its radius move at a fraction of their speed |
| **Bomb** ● | ability | Tap the feed: one large damaging blast. Costs money and has a cooldown |
| **Gravity** ■ | ability | Tap the feed: a pull held for a few seconds, the existing negative-strength explosion with no damage. Costs money and has a cooldown |

**The click.** Tapping the feed with nothing selected fires a small free blast, with a short cooldown, at the tap. It's always available.

**What towers share.** These rules apply to all four towers. Abilities don't attract, collide or take damage.
- **Solid.** A tower is a solid circle that units crowd round, as they would round a pillar. Towers are **not** baked into the level's walls. They're a small table of their own, at most 32, that every unit tests in the same wall-push step, so walls are never rebuilt.
- **Grows in.** A newly placed tower's radius grows from 0 to full over about a second. Each step the wall push moves overlapping units out by a few world units, and the collision passes carry that on through the crowd. A tower can be dropped into a crowd and eases units aside rather than shoving them. Placing over units is allowed. A destroyed tower's collider is removed at once.
- **Attraction.** Inside a tower's pull radius, measured from its centre, a unit **steers** toward the tower instead of the exit. It turns at a steering rate and walks at walking speed, through the same steering code field regions use. It is never displaced directly, unlike the debug pull explosion.
  - Attraction is **loose**: only a share of units respond, chosen by a hash of each unit's stable ID, so part of the crowd peels off and the rest carries on.
  - Overlapping pulls blend.
  - Field regions keep precedence over attraction, so a level's steering still holds. *(My call; see "Calls made" below.)*
- **Contact damage.** Each step, a unit within touching distance of a tower adds one to that tower's contact counter on the GPU. The tower loses `contactDamage × contacts × step time` health, with `contactDamage` in HP per touching unit per second, so bigger pile-ups wear it down faster. At 0 HP the tower is destroyed and removed. Its collider and pull go with it, and the crowd heads for the exit again.
- **Aiming.** A firing tower aims at the most crowded coarse cell within its range (see "GPU reports" below), at a recent position of a real unit in that cell. That way small blasts land on units, not between them. A tower with nothing in range doesn't fire.
- **Upgrades.** Each tower has three levels, and upgrading is priced higher each time. An upgrade raises the tower's main stat, its range and its maximum health, and heals it to full. The main stats are:
  - Sniper: damage.
  - Gunner: fire rate.
  - Pulse: knockback and blast radius.
  - Frost: slow strength. Each level cuts the speed factor, so 0.4 becomes about 0.3 and then 0.2. At the top level, a **repair** for a smaller price heals it to full.
- **Placement.**
  - It isn't allowed on walls, the painted wall grid, other towers or spawn and exit bands, or if the player can't afford it. It **is** allowed over units (see "Grows in").
  - The ghost shows red where placement is invalid.
  - The tray greys out items the player can't afford.
  - There's a cap of 32 towers per round, matching the GPU tables.

**Starting numbers.** These are placeholders to tune in play, and they live in one C++ table.

| | Cost | Range | Fire | Blast radius | Damage | Other |
|---|---|---|---|---|---|---|
| Sniper | 150 | 400 | every 1 s | 6 | 20 | |
| Gunner | 100 | 250 | 8 / s | 6 | 5 | |
| Pulse | 200 | 200 | every 3 s | 120 | 2 | shove 80 |
| Frost | 175 | 180 (zone) | — | — | — | speed × 0.4 |
| Bomb | 50 / use | — | 8 s cooldown | 80 | 20 | |
| Gravity | 75 / use | — | 15 s cooldown | 200 | 0 | pull, 3 s |
| Click | free | — | 0.25 s cooldown | 12 | 10 | |

- Units have 10 HP (`unitHp`).
- Tower bodies have a radius of 20 world units. A placed tower can be tapped within a minimum on-screen radius, since the fitted feed is about 0.3 px per world unit.
- Towers pull from 150 world units from their centre.
- Towers have 200 HP and lose 0.3 HP per touching unit per second. A fully ringed tower has about 28 units touching and lasts about 25 s.
- Upgrade levels 2 and 3 cost 0.75× and 1.25× the base cost. Each adds 40% to the main stat (Frost: see "Upgrades" above), 10% to range and 50% to max HP.
- A repair costs 25% of the base cost.
- Unit max HP stays at the existing 30.

### Interacting in the round

- **Placing.** Tap a tower in the tray to select it, and a ghost follows the cursor with its range ring. Tap the feed to place it. Placing deselects; tapping the same item again cancels.
- **Abilities.** Tap Bomb or Gravity, then tap the feed. Each tray item shows its cooldown.
- **Inspecting.** Tap a placed tower to select it. A small crisp panel by the tower shows its name, level, HP, and an Upgrade or Repair button with the cost. Tapping elsewhere closes the panel.
- **Clicking.** Tapping empty feed with nothing selected fires the click.
- **Esc** pauses, as now, and pausing stops the round clock and the simulation.

### Showing it

- **In the feed.** These are drawn through `SpriteBatch` and get the CCTV treatment:
  - towers as atlas shapes, tinted by kind
  - a thin HP bar over each damaged tower
  - blasts as the existing explosion discs, briefly
  - Frost zones as a faint disc
- **In the overlay.** These are RmlUi and drawn crisp:
  - **Money** is added to the OSD.
  - The **WAVE** label becomes the horde count, e.g. `HORDE 3120/5000` for units killed or leaked out of the total.
  - **BASE** pips show the leak budget left, in 10 pips scaled to `leakLimit`.
  - The tray shows the six items with costs and cooldowns, plus the ghost and the tower panel.
- **Results.** When the round ends, a results panel on the tablet shows WIN or LOSS, the time, kills, leaked, and the upgrade money earned and new total on a win. It has Retry and Quit to menu.

### GPU reports and plumbing

The simulation gains these per-step inputs, all bounded tables in its uniforms:
- **Tower colliders.** Up to 32 circles, each with its current (growing) radius, pushed out of in the same place as walls: every collision pass and `unit_finalize`. `WallColliders` and `GpuWalls` are unchanged. The CPU placement check tests towers separately.
- **Attractors.** Up to 32: centre, radius, steering strength and the share of units that respond. Applied in `unit_integrate` beside field regions.
- **Slow zones.** Up to 32: centre, radius and speed factor. They scale max speed in `unit_integrate`.
- **Blasts.** The existing `Explosion`. `kMaxExplosions` rises from 8 so a few fast-firing towers, the click and abilities fit at once. Zero-duration blasts free their slot after one step.
- **Exit behaviour.** A third option beside respawn and stop: *remove*. Player rounds use it, while Dev and the benchmark keep their current respawn default.
- **Runtime spawn rate.** The physics system takes a new spawn rate without a restart.

The GPU writes small counters, which the CPU reads back **asynchronously** through a transfer buffer and fence, without waiting:
- Cumulative **kills**, counted when a unit's health first reaches 0, and **leaked** units.
- Cumulative **contacts** per tower slot. A slot is zeroed when a tower is placed in it.
- A **coarse density map** for aiming, at a few reads per second. Cells are about 32 world units, so a 2000×2000 level is about 4k cells. Each cell holds a unit count and the position of one unit in it.

The current `readHealth` stalls. It stays for the Dev panel but isn't used by the round.

### Where code goes

- **Round rules** go in a plain C++ module with no SDL or GPU: money, tower state, cooldowns, upgrades, spawn rate, win and loss, given reports and returning the step's inputs for the simulation. It's covered by doctest in `tests/`.
- **Tower and ability stats** go in one table beside it.
- **`LevelScene` in Player mode** owns a round and wires it to the physics system, input and overlay.
- **`RoundOverlay`** gains the bindings and events above.
- **Round settings** go in `logic::Level` and `LevelIO`.
- **Shader changes** go in `unit_integrate`, `unit_finalize` and probably a new small coarse-density pass.

## Constraints

- **Shaders.** Changed shaders must be recompiled and committed. SPIR-V and MSL can be built on Linux (`tools/compile_shaders.sh`). **DXIL is only built on Windows**, so the DXIL files need regenerating there before Windows builds run the new shaders.
- **Other systems.** `PhysicsSystem` has other implementations. Round features may be GPU-collide only, and Player mode uses `gpu-collide` already.
- **Benchmark.** `--bench` and Dev mode must behave and time as before.
- **Build and tests.** `./build.sh debug` must build on Linux, and the doctest suite must pass.

## Calls made (check these)

- Towers are a separate collider table rather than wall primitives, and grow in by radius rather than with an explosion shove.
- Body radius 20, pull radius 150, contact damage 0.3 HP per unit per second, unit HP 10, and the round defaults.
- The upgrade main stats, and range +10% per level.
- Field regions take precedence over tower attraction.
- Attraction is loose by unit ID hash. A fixed share responds, rather than every unit in range.
- Aiming uses a coarse density map plus a sample position per cell, refreshed a few times a second.
- Round settings live in the level file, edited in the editor, until the campaign exists.
- Upgrade money goes in a save file in the SDL pref directory.
- The WAVE label becomes a horde count, since there are no discrete waves.
- The 32-tower cap.
- All the starting numbers.

## Left to the builder

- Exact shader layout of the new tables and counters, and whether contacts are counted in `unit_finalize` or the last collision pass.
- How the async readback is double-buffered and how stale a report may be.
- Tower panel and results panel layout within the existing tablet styling.
- Atlas cells or shapes used to draw towers.
- How many coarse-density reads per second.
- Coarse cell size.

## Verification

- The doctest suite passes, including new round-rules tests:
  - money
  - placement cost
  - upgrade and repair
  - cooldowns
  - spawn-rate formula
  - win and loss edges
  - reward formula
  - level IO round-trip for `round`
- `./build.sh debug` builds and runs on Linux.
- Manual walk-through on the default level:
  - Play from the menu.
  - Place each tower.
  - Watch part of the crowd peel off and pile on a tower, and the tower's HP fall.
  - Upgrade, which heals the tower.
  - Repair at max level.
  - Use Bomb and Gravity.
  - Click-kill.
  - Lose by letting units leak.
  - Win by killing all units.
  - See upgrade money saved, then Retry and Quit to menu.
- The crowd still flows smoothly round towers, with no boiling or teleporting.
- `--bench` runs as before.
- Dev ground still respawns at exits.
