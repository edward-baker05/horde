# Round Play Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make one round playable start to finish in Player mode. The player places and upgrades four towers, uses two abilities and a click, and wins or loses, with upgrade money saved on a win.

**Architecture:** The game rules live in a plain C++ `logic::Round` that doctest covers. It turns player actions and GPU reports into per-step crowd effects: tower colliders, attractors, slow zones and blasts. `GpuCollidePhysics` applies those effects in its existing shaders. It returns small cumulative counters (kills, leaks, contacts per tower) and a coarse density map through a non-blocking readback. `LevelScene` (Player mode) and `RoundOverlay` connect the two to input and the tablet UI.

**Tech Stack:** C++20, SDL3 GPU, HLSL compiled by shadercross (SPIR-V/MSL on Linux), RmlUi, doctest, nlohmann::json, glm.

**Spec:** `docs/superpowers/specs/2026-11-21-round-play-design.md`

## Global Constraints

- **Uncommitted menu work.** The working tree has the user's uncommitted menu work in `CMakeLists.txt`, `src/scene/LevelScene.*`, `src/scene/LevelMode.hpp`, `src/scene/MainMenuScene.*`, `assets/ui/*` and `tests/ShellStateTests.cpp`. Before Task 1, ask the user to commit it, or get their go-ahead to fold it in. Never stage hunks you didn't write.
- **Files that stay uncommitted.** The spec and this plan stay uncommitted unless the user says otherwise.
- **Commit messages:** shorthand, at most 3 short sentences, no `Co-Authored-By`, no "Generated with" line, no session links.
- **Shader rebuilds.** Changed shaders are rebuilt with `tools/compile_shaders.sh <stem>...`, which writes SPIR-V and MSL, and the compiled output is committed. **DXIL can only be built on Windows.** Every task that changes a shader lists that shader's stem under "DXIL to rebuild", and the final task collects them all.
- **Keep Dev mode and the benchmark unchanged.** Dev mode, presets and `--bench` must behave and time as before. Round features switch on only through `FlowSettings::removeAtExit` and `PhysicsSystem::setCrowdEffects`, and Player mode is the only caller of either.
- **Tower limit.** There are at most `sim::kMaxTowers = 32` towers. `sim::kMaxExplosions` rises to 32.
- **No stalling readbacks.** Nothing in Player mode may call `readHealth`, `readUnits` or any other readback that waits on the GPU.
- **Code style.** Match the existing code: comments explain why, `horde::` namespaces, run `clang-format` on new files.
- **Build and test commands:**
  - Build: `cmake --build --preset linux-debug`
  - Tests: `./build/linux-debug/bin/horde_tests`
  - Game: `./build/linux-debug/bin/horde`

## Review Focus

1. **A selected or inspected tower is destroyed.** The panel must close and nothing may touch the stale slot. *(Round test in Task 3, scene check in Task 9.)*
2. **A tower is placed hard against a wall.** A growing tower must never pin units against a wall and push them through it. Placement needs clearance of at least body radius + one unit diameter from walls. *(TowerSite test in Task 5.)*
3. **The leak limit and the last kill land in the same report.** The loss wins. *(Round test in Task 3.)*
4. **Readback reports arrive out of order or stale.** An older counter report must never refund money, repeat damage or count a kill twice. Counters only move forward. *(Round test in Task 3.)*
5. **Input after the round has ended.** Clicks, placements, abilities and upgrades do nothing, and the clock stops. Esc does not open the pause over the results. *(Round test in Task 3, scene check in Task 10.)*

---

### Task 1: Round settings in the level file

**Files:**
- Modify: `src/logic/Level.hpp` (add `RoundSettings`, `Level::round`)
- Modify: `src/logic/LevelIO.hpp` (`kLevelFormatVersion = 3`)
- Modify: `src/logic/LevelIO.cpp` (read and write `round`)
- Modify: `tests/CMakeLists.txt` (add `LevelIoTests.cpp`, plus `src/logic/Level.cpp`, `LevelIO.cpp`, and whatever they link against: `LevelGeometry.cpp`, `WallGridShapes.cpp`, `LevelValidation.cpp` as needed; add `nlohmann_json` to `target_link_libraries` if it isn't header-only through an existing target)
- Test: `tests/LevelIoTests.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace horde::logic {
  struct RoundSettings {
      std::uint32_t totalUnits = 10000;
      std::uint32_t unitHp = 10;
      std::uint32_t leakLimit = 200;
      int startMoney = 400;
      int killReward = 1;
      float baseSpawnRate = 30.0f; // units per second
      float spawnBoost = 4.0f;
      int winReward = 100;
      float parSeconds = 240.0f;
      float bonusPerSecond = 1.0f;
  };
  // in struct Level:
  RoundSettings round;
  }
  ```
  JSON: an optional top-level `"round"` object whose keys are the field names above. A missing key takes its default. A missing object gives all defaults.

- [ ] **Step 1: Write the failing tests** in `tests/LevelIoTests.cpp`. Each one saves to and loads from a file under `std::filesystem::temp_directory_path()`.
  - `TEST_CASE("level round settings round-trip")`: set every `RoundSettings` field to a non-default value, `saveLevel`, `loadLevel`, then `CHECK` each field is equal.
  - `TEST_CASE("version 2 level without round loads defaults")`: write a minimal v2 JSON by hand, `{"version":2,"size":[100,100],"markers":[...one spawn, one exit...]}`, using the marker format `LevelIO.cpp` reads. Then `CHECK(loaded->round.totalUnits == 10000)`, `CHECK(loaded->round.leakLimit == 200)` and `CHECK(loaded->round.startMoney == 400)`.
  - `TEST_CASE("invalid round settings are rejected")`: `"round": {"totalUnits": 0}` loads as `std::nullopt` with a non-empty error. The same goes for `leakLimit: 0`, `unitHp: 0` and a negative `baseSpawnRate`.
- [ ] **Step 2: Run the tests and confirm they fail.** `cmake --build --preset linux-debug --target horde_tests && ./build/linux-debug/bin/horde_tests -tc="*round*"`. Expected: a compile error (no `RoundSettings`).
- [ ] **Step 3: Implement.** Add `RoundSettings` and `Level::round`, bump the version to 3 (keep `kOldestLevelFormatVersion = 1`), read `round` in `loadLevel` with the file's existing `readFloat` style, and write it in `saveLevel`. Validation:
  - `totalUnits`, `leakLimit` and `unitHp` are at least 1.
  - `baseSpawnRate`, `spawnBoost`, `parSeconds`, `bonusPerSecond`, `startMoney`, `killReward` and `winReward` are at least 0.
- [ ] **Step 4: Run the tests and confirm they pass.** Same command. Expected: all pass, and the existing suites still pass.
- [ ] **Step 5: Commit.** `git add src/logic/Level.hpp src/logic/LevelIO.hpp src/logic/LevelIO.cpp tests/LevelIoTests.cpp tests/CMakeLists.txt && git commit -m "Round settings in level files, format v3"`

---

### Task 2: Crowd-effect types and the tower stats table

**Files:**
- Create: `src/sim/CrowdEffects.hpp` (glm and std only: no SDL)
- Modify: `src/sim/PhysicsSystem.hpp` (include `CrowdEffects.hpp`; move `ExplosionFalloff`, `Explosion` and `kMaxExplosions` out of it into the new header, unchanged except `kMaxExplosions = 32`)
- Create: `src/logic/TowerStats.hpp`, `src/logic/TowerStats.cpp`
- Modify: `CMakeLists.txt` (add `src/logic/TowerStats.cpp`), `tests/CMakeLists.txt` (add `TowerStatsTests.cpp`, `src/logic/TowerStats.cpp`)
- Test: `tests/TowerStatsTests.cpp`

**Interfaces:**
- Produces (in `horde::sim`, `CrowdEffects.hpp`):
  ```cpp
  inline constexpr std::size_t kMaxTowers = 32;
  inline constexpr std::size_t kMaxExplosions = 32;
  enum class ExplosionFalloff { Constant, Linear, Quadratic };  // moved, unchanged
  struct Explosion { ... };                                     // moved, unchanged
  struct TowerCollider { glm::vec2 center{0.0f}; float radius = 0.0f; }; // radius 0: empty slot
  struct Attractor { glm::vec2 center{0.0f}; float radius = 0.0f; float strength = 0.0f; }; // strength: turn rate /s
  struct SlowZone { glm::vec2 center{0.0f}; float radius = 0.0f; float speedFactor = 1.0f; };
  struct CrowdEffects {
      std::array<TowerCollider, kMaxTowers> towers{}; // by tower slot: contacts are reported per slot
      std::vector<Attractor> attractors;              // at most kMaxTowers
      std::vector<SlowZone> slowZones;                // at most kMaxTowers
      float attractShare = 0.0f;                      // share of units (by ID hash) that respond
  };
  // Cumulative since the level started; never decrease.
  struct RoundCounters {
      std::uint32_t killed = 0;
      std::uint32_t leaked = 0;
      std::array<std::uint32_t, kMaxTowers> contacts{}; // unit-steps touching each tower slot
  };
  struct DensityMap {
      glm::ivec2 dim{0, 0};
      float cellSize = 32.0f;
      std::vector<std::uint32_t> counts; // dim.x * dim.y, row-major
      std::vector<glm::vec2> samples;    // centre of one unit in each cell; meaningless where the count is 0
  };
  ```
- Produces (in `horde::logic`, `TowerStats.hpp`):
  ```cpp
  enum class TowerKind : std::uint8_t { Sniper, Gunner, Pulse, Frost };
  enum class AbilityKind : std::uint8_t { Bomb, Gravity };
  inline constexpr int kTowerLevels = 3;
  inline constexpr float kTowerBodyRadius = 20.0f;
  inline constexpr float kTowerPullRadius = 150.0f;
  inline constexpr float kTowerGrowSeconds = 1.0f;
  inline constexpr float kContactDamagePerSecond = 0.3f; // per touching unit
  inline constexpr float kAttractShare = 0.6f;
  inline constexpr float kAttractStrength = 3.0f;        // turn rate, per second
  // A blast as the simulation takes it: sim::Explosion without the centre.
  struct BlastStats { float radius = 0.0f; float damage = 0.0f; float strength = 0.0f; float duration = 0.0f; };
  struct TowerStats {
      int cost = 0;
      float range = 0.0f;
      float firePeriod = 0.0f;  // seconds between shots; 0 never fires
      BlastStats blast;
      float speedFactor = 1.0f; // Frost's slow zone; 1 for the rest
      float maxHp = 200.0f;
  };
  TowerStats towerStats(TowerKind kind, int level); // level 1..kTowerLevels
  int upgradeCost(TowerKind kind, int toLevel);     // toLevel 2 or 3
  int repairCost(TowerKind kind);
  struct AbilityStats { int cost = 0; float cooldown = 0.0f; BlastStats blast; };
  AbilityStats abilityStats(AbilityKind kind);
  AbilityStats clickStats(); // cost 0
  const char* towerName(TowerKind kind);
  const char* abilityName(AbilityKind kind);
  ```
- **Level-1 values** (from the spec):

  | | cost | range | firePeriod | radius | damage | strength | duration | speedFactor |
  |---|---|---|---|---|---|---|---|---|
  | Sniper | 150 | 400 | 1.0 | 6 | 20 | 0 | 0 | 1 |
  | Gunner | 100 | 250 | 0.125 | 6 | 5 | 0 | 0 | 1 |
  | Pulse | 200 | 200 | 3.0 | 120 | 2 | 80 | 0.25 | 1 |
  | Frost | 175 | 180 | 0 | — | — | — | — | 0.4 |

  - Bomb: cost 50, cooldown 8, blast radius 80, damage 20, strength 20, duration 0.
  - Gravity: cost 75, cooldown 15, blast radius 200, damage 0, strength −150, duration 3.
  - Click: cost 0, cooldown 0.25, blast radius 12, damage 10, strength 0, duration 0.
- **Each level above 1:**
  - range ×1.1 and maxHp ×1.5, compounding per level
  - Sniper damage ×1.4
  - Gunner firePeriod ÷1.4
  - Pulse strength and radius ×1.4
  - Frost speedFactor is 0.4, 0.3 or 0.2 by level
- **Costs:** `upgradeCost` is `round(base × 0.75)` to level 2 and `round(base × 1.25)` to level 3. `repairCost` is `round(base × 0.25)`.

- [ ] **Step 1: Write the failing tests** in `tests/TowerStatsTests.cpp`:
  - `CHECK(towerStats(TowerKind::Sniper, 1).cost == 150)`
  - `CHECK(towerStats(TowerKind::Sniper, 2).blast.damage == doctest::Approx(28.0f))`
  - `CHECK(towerStats(TowerKind::Gunner, 3).firePeriod == doctest::Approx(0.125f / 1.96f))`
  - `CHECK(towerStats(TowerKind::Frost, 3).speedFactor == doctest::Approx(0.2f))`
  - `CHECK(towerStats(TowerKind::Pulse, 2).range == doctest::Approx(220.0f))`
  - `CHECK(towerStats(TowerKind::Pulse, 3).maxHp == doctest::Approx(450.0f))`
  - `CHECK(upgradeCost(TowerKind::Gunner, 2) == 75)`, `CHECK(upgradeCost(TowerKind::Gunner, 3) == 125)`, `CHECK(repairCost(TowerKind::Pulse) == 50)`
  - `CHECK(abilityStats(AbilityKind::Gravity).blast.strength < 0.0f)`, `CHECK(clickStats().cost == 0)`
- [ ] **Step 2: Run the tests and confirm they fail.** `cmake --build --preset linux-debug --target horde_tests`. Expected: a compile error.
- [ ] **Step 3: Implement** `TowerStats.cpp` and `CrowdEffects.hpp`, and move the explosion types. `falloffName` stays declared in `PhysicsSystem.hpp`.
- [ ] **Step 4: Run the tests and the full build, and confirm they pass.** `cmake --build --preset linux-debug && ./build/linux-debug/bin/horde_tests`. Expected: the build succeeds and all tests pass.
- [ ] **Step 5: Commit.** `git commit -m "Crowd effect types, tower and ability stats"` with the files above staged.

---

### Task 3: Round rules — economy, towers, outcome

**Files:**
- Create: `src/logic/Round.hpp`, `src/logic/Round.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt` (add `src/logic/Round.cpp` and `RoundTests.cpp`)
- Test: `tests/RoundTests.cpp`

**Interfaces:**
- Consumes: `logic::RoundSettings` (Task 1); `TowerStats.hpp` and `sim/CrowdEffects.hpp` (Task 2)
- Produces (in `horde::logic`):
  ```cpp
  enum class RoundOutcome { Playing, Won, Lost };
  enum class PlaceError { None, CannotAfford, Full, Blocked, Ended };
  struct Tower {
      TowerKind kind = TowerKind::Sniper;
      int level = 1;
      glm::vec2 center{0.0f};
      float hp = 0.0f;
      float grow = 0.0f;     // 0..1 over kTowerGrowSeconds; the collider radius scales with it
      float cooldown = 0.0f; // seconds until it may fire
      std::uint32_t contactBaseline = 0;
  };
  struct Offer { enum class Kind { None, Upgrade, Repair } kind = Kind::None; int cost = 0; };

  class Round {
  public:
      Round(const RoundSettings& settings, float stepSeconds);
      int money() const;
      RoundOutcome outcome() const;
      float seconds() const;
      std::uint32_t killed() const;
      std::uint32_t leaked() const;
      const RoundSettings& settings() const;
      const std::array<std::optional<Tower>, sim::kMaxTowers>& towers() const;
      float spawnRate() const;   // baseSpawnRate * (1 + spawnBoost * min(killed, total) / total)
      int upgradeMoney() const;  // Won: winReward + floor(max(0, par - seconds) * bonusPerSecond); else 0

      // siteClear: the caller's wall, marker and bounds check (sim::towerSiteClear, Task 5)
      PlaceError canPlace(TowerKind kind, glm::vec2 center, bool siteClear) const;
      int place(TowerKind kind, glm::vec2 center, bool siteClear); // slot, or -1
      int towerAt(glm::vec2 point, float pickRadius) const;        // slot, or -1
      Offer offer(int slot) const;                                 // None for empty or bad slots
      bool buyOffer(int slot);

      void applyCounters(const sim::RoundCounters& counters);
      // Task 4 adds update(), useAbility(), click(), takeBlasts() and effects().
  };
  }
  ```
- **Rules to pin:**
  - `canPlace` checks in this order: `Ended` (outcome isn't Playing), then `Blocked` (`!siteClear`, or within `2 × kTowerBodyRadius` of another tower's centre), then `Full` (32 towers), then `CannotAfford`.
  - `place` takes the first free slot, sets `hp` to the level-1 `maxHp`, `grow = 0`, and `contactBaseline` to the last applied `contacts[slot]`.
  - `offer` returns `Upgrade` with `upgradeCost(kind, level + 1)` below level 3, and `Repair` with `repairCost` at level 3. An upgrade raises the level and sets hp to the new `maxHp`. A repair restores hp to `maxHp`.
  - `applyCounters` takes each field as `max(old, new)`, so a stale report changes nothing. Money rises by `(newKilled − oldKilled) × killReward`. Each live tower loses `(contacts[slot] − baseline) × kContactDamagePerSecond × stepSeconds`, then its baseline is set to the new count. A tower at or below 0 hp empties its slot. The outcome is then decided:
    - `Lost` if `leaked >= leakLimit`. This is checked first, so it wins over a simultaneous win.
    - Otherwise `Won` if `killed + leaked >= totalUnits`.
  - Once the outcome isn't `Playing`, every action returns failure (`-1`, `false` or `Ended`) and `applyCounters` changes nothing.

- [ ] **Step 1: Write the failing tests** in `tests/RoundTests.cpp`, using a helper `RoundSettings small()` with `totalUnits=100, leakLimit=10, startMoney=400, killReward=2`, and `stepSeconds = 1.0f/60`:
  - **Placement:** `"placing spends money and fills a slot"`: `place(Gunner, {500,500}, true) == 0`, `money() == 300`, `towers()[0]->hp == 200`.
  - **Affordability:** `"cannot afford"`: with startMoney 100, `canPlace(Sniper, …, true) == PlaceError::CannotAfford`.
  - **Blocked:** `"blocked by site or another tower"`: `canPlace(Gunner, p, false) == Blocked`; after placing at `{500,500}`, `canPlace(Gunner, {530,500}, true) == Blocked` and `canPlace(Gunner, {541,500}, true) == None`.
  - **Full:** `"full at 32 towers"`: with startMoney 100000, place 32 spaced 50 apart; the 33rd is `Full`.
  - **Upgrade and repair:** `"upgrade heals and raises level; repair at max"`: damage a tower through contacts, `buyOffer` gives level 2 and hp equal to `towerStats(kind,2).maxHp`, a second upgrade gives level 3, then `offer().kind == Repair`, and `buyOffer` restores full hp with money falling by `repairCost`.
  - **Kill money:** `"kills pay and stale reports are ignored"`: `applyCounters({killed=5})` makes money 410; then `applyCounters({killed=3})` keeps money 410 and `killed() == 5`.
  - **Contact damage:** `"contact damage wears a tower down and destroys it"`: with stepSeconds 1/60, contacts[0]=600 → hp `200 − 600×0.3/60 = 197`; contacts[0]=40600 → slot empty, `offer(0).kind == None`, `towerAt({500,500}, 25) == -1`.
  - **Baseline:** `"a tower placed into a used slot starts from that slot's count"`: apply contacts[0]=1000, destroy the tower, place a new one in slot 0, apply contacts[0]=1000 again → no damage.
  - **Loss beats win:** `"leak limit loses, even on the last kill"`: `applyCounters({killed=90, leaked=10})` gives `Lost` and `upgradeMoney() == 0`.
  - **Win:** `"all dealt with under the limit wins"`: `{killed=95, leaked=5}` gives `Won`.
  - **Ended:** `"ended rounds ignore actions"`: after `Won`, `place` returns -1, `buyOffer` returns false, and money is unchanged.
  - **Spawn rate:** `"spawn rate rises with kills"`: base 30, boost 4, total 100 → `spawnRate()` is 30 at 0 kills, 90 at 50 and 150 at 100.
- [ ] **Step 2: Run the tests and confirm they fail.** `cmake --build --preset linux-debug --target horde_tests`. Expected: a compile error.
- [ ] **Step 3: Implement** `Round` to the rules above. `seconds()` stays 0 until Task 4 adds `update`. Test `upgradeMoney` in Task 4 once `seconds` moves.
- [ ] **Step 4: Run the tests and confirm they pass.** `./build/linux-debug/bin/horde_tests -tc="*"`. Expected: all pass.
- [ ] **Step 5: Commit.** `git commit -m "Round rules: money, towers, upgrades, outcome"`

---

### Task 4: Round rules — time, firing, abilities, effects

**Files:**
- Modify: `src/logic/Round.hpp`, `src/logic/Round.cpp`
- Test: `tests/RoundTests.cpp`

**Interfaces:**
- Consumes: Task 3's `Round`
- Produces (added to `Round`):
  ```cpp
  // Advances the clock, cooldowns and tower growth, and fires towers that are
  // ready at their densest target. Does nothing once the round has ended.
  void update(float dt, const sim::DensityMap* density);
  bool useAbility(AbilityKind kind, glm::vec2 at); // pays, starts the cooldown, queues the blast
  bool click(glm::vec2 at);
  float abilityCooldown(AbilityKind kind) const;    // seconds left
  float clickCooldown() const;
  std::vector<sim::Explosion> takeBlasts();         // queued blasts, oldest first; empties the queue
  sim::CrowdEffects effects() const;
  ```
- **Rules to pin:**
  - **Growth.** `grow` rises at `dt / kTowerGrowSeconds`, capped at 1. A tower only fires, pulls and slows once `grow == 1`. Its collider is always present, with radius `kTowerBodyRadius × grow`.
  - **Firing.** Firing happens only when `firePeriod > 0`, `cooldown <= 0` and the density map has a cell with `count > 0` whose cell centre is within `range` of the tower. The target is the cell with the highest count; on a tie, the one nearest the tower. The tower aims at that cell's `sample`, and its cooldown resets to `firePeriod`. With no target, the cooldown stays at 0, so the tower fires the moment one appears.
  - **Blasts.** A `sim::Explosion` is built from `BlastStats`: `falloff = Linear`, `decay = Constant`, `fling = false`.
  - **Effects:**
    - `towers[slot]` holds `{center, kTowerBodyRadius × grow}` for every live tower.
    - Every fully grown tower adds an attractor `{center, kTowerPullRadius, kAttractStrength}`.
    - Each fully grown Frost adds a slow zone `{center, range, speedFactor}`.
    - `attractShare = kAttractShare`.

- [ ] **Step 1: Write the failing tests:**
  - **Clock:** `"clock runs while playing and stops at the end"`. `update(2.0f, nullptr)` gives `seconds() == 2`. Then win, call `update(5.0f, nullptr)`, and `seconds()` is still 2.
  - **Upgrade money:** `"upgrade money rewards a fast win"`. With winReward 100, par 240 and bonusPerSecond 1, win at 40 s and `upgradeMoney() == 300`. Win at 300 s and it's 100.
  - **Growth:** `"towers grow in, then act"`. After `place`, `effects().towers[0].radius == 0` and there are no attractors. After `update(0.5)` the radius is 10. After `update(0.6)` the radius is 20 and there is 1 attractor.
  - **Aiming:** `"gunner fires at the densest cell in range"`. Build a `DensityMap` of dim {10,10} and cellSize 32. Put count 3 at a cell within 250 of the tower, with sample `S`, and count 9 at a cell beyond 250. A grown Gunner's `update(0.01, &map)` then gives `takeBlasts().size() == 1` with `center == S` and damage 5. An immediate second `update(0.01, &map)` gives no blast, because it's on cooldown.
  - **Frost:** `"frost never fires and makes a slow zone"`. A grown Frost with a dense map gives no blasts, plus `effects().slowZones == {center, 180, 0.4}`.
  - **Abilities:** `"abilities cost money and cool down"`. `useAbility(Bomb, p)` makes money −50, queues one blast with radius 80, and gives `abilityCooldown(Bomb) == 8`. A second use fails with money unchanged. After `update(8.0)` it succeeds again.
  - **Click:** `"click is free with a short cooldown"`. `click(p)` queues a blast with radius 12 and damage 10. An immediate second `click` is false, and it's true again after `update(0.25)`.
  - **Gravity:** `"gravity pulls without damage"`. The queued blast has `strength < 0`, `damage == 0` and `duration == 3`.
- [ ] **Step 2: Run the tests and confirm they fail.** Expected: a compile error (no `update`).
- [ ] **Step 3: Implement** the rules above.
- [ ] **Step 4: Run the tests and confirm they pass.** `./build/linux-debug/bin/horde_tests`. Expected: all pass.
- [ ] **Step 5: Commit.** `git commit -m "Round firing, abilities, click and crowd effects"`

---

### Task 5: Tower site check and the upgrade-money save file

**Files:**
- Create: `src/sim/TowerSite.hpp`, `src/sim/TowerSite.cpp`
- Create: `src/core/SaveData.hpp`, `src/core/SaveData.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt` (add both, plus `src/sim/WallColliders.cpp` and its dependencies to the tests)
- Test: `tests/TowerSiteTests.cpp`, `tests/SaveDataTests.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace horde::sim {
  // Whether a tower of radius kTowerBodyRadius may stand at `center`: fully
  // inside the level, at least body radius + unitSize clear of every wall
  // (including the painted grid, which `walls` already holds), and not
  // overlapping any spawn or exit band.
  bool towerSiteClear(const logic::Level& level, const WallColliders& walls, glm::vec2 center, float unitSize);
  }
  namespace horde::save {
  struct SaveData { std::int64_t upgradeMoney = 0; };
  SaveData load(const std::filesystem::path& path); // missing or unreadable: zero
  bool write(const SaveData& data, const std::filesystem::path& path);
  std::filesystem::path defaultPath();              // SDL_GetPrefPath("horde", "horde") / "save.json"
  }
  ```
  `WallColliders::build` must be called with `reach >= kTowerBodyRadius + unitSize`, or `nearest` won't see walls far enough out. So `towerSiteClear` builds no colliders of its own. Its caller (Task 9) builds a second `WallColliders` with that reach, once per level.

- [ ] **Step 1: Write the failing tests.**
  - **Site (`TowerSiteTests.cpp`).** `makeDefaultLevel()` (2000², spawn on the whole west edge, exit on the whole east edge) plus one rectangle wall centred at {1000,1000} with half extents {50,50}. Colliders are built with reach `20 + 5`.
    - `towerSiteClear(level, walls, {600,600}, 5)` is true.
    - `{1000,1000}` is false (in the wall).
    - `{1000,1070}` is false: 20 from the wall face, under 20 + 5.
    - `{1000,1080}` is true: exactly 30 clear.
    - `{30,600}` is false: inside the level, but it overlaps the 12-wide spawn band.
    - `{45,600}` is true: clear of the band.
    - `{1970,600}` is false (overlaps the exit band).
    - `{600,10}` is false (partly outside the level).
  - **Save (`SaveDataTests.cpp`).** Using a temp path:
    - `load(missing).upgradeMoney == 0`.
    - `write({1234})` then `load` gives 1234.
    - A file holding `not json` loads as 0.
- [ ] **Step 2: Run the tests and confirm they fail.** Expected: a compile error.
- [ ] **Step 3: Implement.** The site check uses `WallColliders::nearest` and `logic::markerRect`. The save file is `{"upgradeMoney": N}` written with nlohmann::json.
- [ ] **Step 4: Run the tests and confirm they pass.** `./build/linux-debug/bin/horde_tests`. Expected: all pass.
- [ ] **Step 5: Commit.** `git commit -m "Tower site check, upgrade money save file"`

---

### Task 6: GPU — tower colliders, leaving at exits, counters, async readback

**Files:**
- Modify: `shaders/src/walls.hlsli` (`#define MAX_TOWERS 32`; `resolveWalls` and `wallClearance` also push out of and measure against tower circles with radius > 0, read from `TowerCount` and `Towers[MAX_TOWERS]` (xy = centre, z = radius), which the including shader declares in its cbuffer, as it already does `WallCount`)
- Modify: `shaders/src/collide_common.hlsli` (`CollideParams` gains `uint TowerCount; uint3 TowerPadding; float4 Towers[MAX_TOWERS];` at the end)
- Modify: `shaders/src/unit_finalize.comp.hlsl`:
  - replace `RespawnOnExit` with `uint ExitMode` (0 stop, 1 respawn, 2 remove)
  - add `TowerCount`/`Towers` to `FinalizeParams`
  - add `RWStructuredBuffer<uint> Counters : register(u3, space1)`
- Modify: `src/sim/GpuUnitCollisions.cpp` (`GPUCollideUniforms` mirrors the new fields; `static_assert` size `80 + 16 + 16 * 32`; fills towers from `GpuWalls`)
- Modify: `src/sim/GpuWalls.hpp/.cpp` (CPU-side tower table)
- Modify: `src/sim/GpuCollidePhysics.hpp/.cpp`
- Modify: `src/sim/PhysicsSystem.hpp` (`FlowSettings::removeAtExit`, new virtuals)
- Create: `src/sim/GpuReadback.hpp`, `src/sim/GpuReadback.cpp`
- Modify: `src/scene/LevelScene.cpp` (Dev panel line, for verification)
- Modify: `CMakeLists.txt` (add `GpuReadback.cpp`)
- Compiled shaders: `unit_finalize.comp`, `unit_collide.comp`, `unit_smooth.comp`, and any other stem that includes `walls.hlsli` or `collide_common.hlsli` (`grep -l` to list them)

**Interfaces:**
- Consumes: `sim::CrowdEffects`, `sim::RoundCounters`, `sim::kMaxTowers` (Task 2)
- Produces:
  ```cpp
  // FlowSettings:
  bool removeAtExit = false; // wins over respawnAtExit: the unit leaves the level and counts as leaked
  // PhysicsSystem (defaults do nothing / return false):
  virtual bool supportsRounds() const;
  virtual void setCrowdEffects(const CrowdEffects& effects); // takes effect from the next step
  virtual bool pollCounters(RoundCounters& out);             // a newer report than last time; never waits
  // GpuWalls:
  void setTowers(const std::array<TowerCollider, kMaxTowers>& towers);
  std::uint32_t towerCount() const;        // highest live slot + 1
  const glm::vec4* towerUniforms() const;  // kMaxTowers entries: (centre, radius, 0)
  // GpuReadback:
  class GpuReadback {
  public:
      bool init(SDL_GPUDevice* device, Uint32 size, Uint32 slots = 3);
      void shutdown();
      // Downloads `size` bytes of `source` in a command buffer of its own,
      // submitted now. False when every slot is still in flight.
      bool request(SDL_GPUBuffer* source);
      // The newest copy that has finished since the last call, if any.
      // Checks fences with SDL_QueryGPUFence; never waits.
      bool poll(std::vector<std::byte>& out);
  };
  ```
- **Counters buffer layout:** `uint[2 + kMaxTowers]`: `[0]` killed, `[1]` leaked, `[2 + slot]` contacts. It's zeroed at init and never cleared.
- **Shader rules (`unit_finalize`):**
  - After the final `resolveWalls`, each tower with `z > 0` where `distance(centre, xy) <= z + Radius + 0.5` gets `InterlockedAdd(Counters[2 + i], 1)`.
  - With `ExitMode == 2`, reaching an exit sets the unit to `DEAD_POSITION` and adds 1 to `Counters[1]`, with health untouched.
  - The existing despawn on `Health <= 0` adds 1 to `Counters[0]`. Dead units return early on later steps, so each death counts once.
- **C++ rules:**
  - `GpuCollidePhysics` creates the counters buffer and a `GpuReadback` sized for it.
  - `update()` calls `request` once per call when a slot is free.
  - `pollCounters` decodes the newest result.
  - `setCrowdEffects` stores the effects and calls `m_walls.setTowers`.
  - `supportsRounds()` returns true.
- **Dev panel:** in `LevelScene::healthUi`, when `pollCounters` returns true, keep the result. Show `Killed %u, leaked %u` under the health line.

- [ ] **Step 1: Make the shader edits and compile them.** `tools/compile_shaders.sh unit_finalize.comp unit_collide.comp unit_smooth.comp` (plus any other stems found). Expected: SPIR-V and MSL are written with no errors.
- [ ] **Step 2: Make the C++ edits and build.** `cmake --build --preset linux-debug`. Expected: the build succeeds and the `static_assert`s hold.
- [ ] **Step 3: Check in Dev mode.** `./build/linux-debug/bin/horde --level assets/levels/BINGLE.level.json`. In the panel, arm the explosion with Blast damage 100 and right-click the crowd. Expected:
  - The "Killed" count rises by about the number of units hit, and stays put afterwards.
  - "leaked" stays 0, because Dev mode respawns.
  - The frame rate and the crowd's look are unchanged.
- [ ] **Step 4: Check the benchmark is unchanged.** `python3 tools/bench.py --physics gpu-collide --units 10k,100k --duration 5`. Expected: it runs to completion, with timings within noise of a run on the previous commit (`git stash` is not safe with the user's tree; compare against numbers recorded before Step 1).
- [ ] **Step 5: Commit.** `git commit -m "Tower colliders, exit removal, round counters, async readback"` with the shaders, compiled SPIR-V/MSL and C++ staged.

DXIL to rebuild: `unit_finalize.comp`, `unit_collide.comp`, `unit_smooth.comp`, plus any other includer.

---

### Task 7: GPU — attraction, slow zones, more blasts, live spawn rate

**Files:**
- Modify: `shaders/src/unit_integrate.comp.hlsl`:
  - `#define MAX_EXPLOSIONS 32`, `#define MAX_TOWERS 32`
  - cbuffer gains `uint AttractorCount; uint SlowCount; float AttractShare; uint EffectsPadding; float4 Attractors[MAX_TOWERS]; float4 SlowZones[MAX_TOWERS];`
- Modify: `src/sim/GpuCollidePhysics.cpp` (`GPUIntegrateUniforms` mirrors it; `static_assert` updated; packs from the stored `CrowdEffects`)
- Modify: `src/sim/PhysicsSystem.hpp` (`virtual void setSpawnRate(float perSecond)`, default no-op)
- Compiled shaders: `unit_integrate.comp`

**Interfaces:**
- Consumes: `CrowdEffects` stored by `setCrowdEffects` (Task 6)
- Produces: `GpuCollidePhysics::setSpawnRate` sets `m_config.flow.spawnRate`, which `spawnPending` reads.
- **Shader rules:**
  - **Steering precedence:** field region first, then attractor, then exit.
  - **Who is attracted:** a unit responds to attractors only if `float(hash(id) & 0xFFFF) / 65535 < AttractShare`. Use `wobble`'s style of ID hash with a different constant, so the attracted share isn't correlated with the wobble.
  - **Attraction:** for each attractor whose radius contains the unit's centre, add `normalize(attractor.xy − centre) × attractor.w`. If the sum is non-zero, the desired velocity is its direction × `MaxSpeed` and the turn rate is its length.
  - **Slowing:** the speed factor is the minimum `w` of the slow zones containing the centre, else 1. It multiplies `desired` after the congestion scale.

- [ ] **Step 1: Make the shader edit and compile it.** `tools/compile_shaders.sh unit_integrate.comp`. Expected: no errors.
- [ ] **Step 2: Make the C++ edits and build.** `cmake --build --preset linux-debug`. Expected: it succeeds.
- [ ] **Step 3: Check Dev mode for regressions.** Run as in Task 6 Step 3. Expected:
  - The crowd flows as before.
  - Fire 10 explosions quickly; all of them show, where earlier only 8 could at once.
- [ ] **Step 4: Commit.** `git commit -m "Tower attraction, frost slow zones, 32 blasts, live spawn rate"`

DXIL to rebuild: `unit_integrate.comp`.

---

### Task 8: GPU — coarse density map for aiming

**Files:**
- Create: `shaders/src/unit_density.comp.hlsl`:
  - `t0` units (read)
  - `u0` `RWStructuredBuffer<uint4> Density` (x = count, yz = `asuint` of one unit's centre)
  - cbuffer `{ uint UnitCount; float Radius; float InvCellSize; uint DimX; uint DimY; uint3 Padding; }`
- Modify: `src/sim/GpuCollidePhysics.hpp/.cpp`
- Modify: `src/sim/PhysicsSystem.hpp` (`virtual bool pollDensity(DensityMap& out)`, default false)
- Compiled shaders: `unit_density.comp`

**Interfaces:**
- Consumes: `GpuReadback` (Task 6), `sim::DensityMap` (Task 2)
- Produces: `pollDensity` returns true with a fresh map, about 10 times a second.
- **Rules:**
  - Cell size is 32 world units, and `dim = ceil(worldSize / 32)`.
  - Every `kDensityEverySteps = 6` steps, but only while `setCrowdEffects` has been called at least once (so Dev and the benchmark never pay for it): clear the buffer with the existing `buffer_fill.comp` (as `GpuSpatialGrid` does), dispatch `unit_density` after `unit_finalize`, then `request` a readback.
  - **Shader:** for a live unit, `InterlockedAdd(Density[c].x, 1)` and a plain store of its centre into `.yz`. Whichever store lands last is fine.

- [ ] **Step 1: Write the shader and compile it.** `tools/compile_shaders.sh unit_density.comp`. Expected: no errors.
- [ ] **Step 2: Make the C++ edits and build.** `cmake --build --preset linux-debug`. Expected: it succeeds.
- [ ] **Step 3: Commit.** `git commit -m "Coarse density map for tower aiming"`. It's checked working in Task 9 Step 4.

DXIL to rebuild: `unit_density.comp`.

---

### Task 9: Player-mode round in LevelScene — rules, input, feed drawing

**Files:**
- Modify: `src/scene/LevelScene.hpp`, `src/scene/LevelScene.cpp`
- Modify: `src/scene/RoundOverlay.hpp/.cpp` (only `clearSelection()` here; the rest is in Task 10)

**Interfaces:**
- Consumes: `logic::Round` (Tasks 3–4), `sim::towerSiteClear` (Task 5), the physics virtuals (Tasks 6–8), `RoundOverlay::selected()`
- Produces:
  - `LevelScene` members: `std::optional<logic::Round> m_round` and `std::shared_ptr<sim::WallColliders> m_siteWalls` (built with reach `kTowerBodyRadius + enemy_size`).
  - `RoundOverlay::clearSelection()`.
  - Tray index → action: 0 Sniper, 1 Gunner, 2 Pulse, 3 Frost, 4 Bomb, 5 Gravity.
- **Rules:**
  - **Entering a round.** In `onEnter`, Player mode only:
    - Set `unitCount = level.round.totalUnits`, `spawn = Markers`, `spawnRate = level.round.baseSpawnRate`, `health.maxHp = level.round.unitHp` and `removeAtExit = true`.
    - If the created physics doesn't `supportsRounds()`, log it and recreate it with `sim::kDefaultPhysics`.
    - Construct `m_round` with stepSeconds `1 / physicsRate`.
  - **`update(dt)`, Player mode.** Only the active scene updates, so pausing stops all of this. While the round is Playing:
    - Feed `pollCounters` into `applyCounters`.
    - Call `m_round->update(dt, newDensity ? &map : nullptr)`, keeping the last map.
    - Pass each of `takeBlasts()` to `addExplosion`.
    - Call `setCrowdEffects(effects())` and `setSpawnRate(spawnRate())`.
    - Once the outcome isn't Playing, stop stepping physics (`m_stepsThisFrame = 0`), so the feed freezes behind the results.
  - **Left click on the feed** (Player mode, world point from `m_camera.screenToWorld`):
    - Tray 0–3: `place`, with `siteClear = towerSiteClear(...)`, then `clearSelection()`.
    - Tray 4–5: `useAbility`, then `clearSelection()`.
    - Otherwise, `towerAt(world, max(kTowerBodyRadius, 12 / zoom))`. A hit selects that tower for inspection (stored in `int m_inspected`); a miss calls `click(world)` and clears `m_inspected`.
    - `m_inspected` is also cleared when its slot is empty (Review Focus 1).
  - **Drawing in `render`, Player mode** (through `SpriteBatch`, so it gets the CCTV effect):
    - Each tower is an atlas shape of size `2 × kTowerBodyRadius × grow`, tinted by kind with the colours in Task 10.
    - A damaged tower gets an HP bar above it: two solid `atlasCell(1,1,4,4)` quads.
    - Each grown Frost gets a faint disc over its slow zone.
    - The inspected tower and the placement ghost get a range ring (disc, alpha 0.08).
    - The ghost follows the cursor (`SDL_GetMouseState`, mapped like the click), tinted red when `canPlace != None`.

- [ ] **Step 1: Implement** the rules above.
- [ ] **Step 2: Build.** `cmake --build --preset linux-debug`. Expected: it succeeds.
- [ ] **Step 3: Run the tests.** `./build/linux-debug/bin/horde_tests`. Expected: all pass.
- [ ] **Step 4: Walk through it by hand.** `./build/linux-debug/bin/horde`, then Play on the default level. Expected:
  - Units stream in at 30/s.
  - A Gunner placed in the crowd's path grows in over about 1 s and eases units aside without a pop.
  - Part of the crowd peels off and piles round the Gunner.
  - The Gunner fires at the crowd and units die.
  - Its HP bar falls.
  - Units reaching the exit disappear and don't respawn.
  - A Frost visibly slows units in its zone.
  - A click kills a few units.
  - Bomb and Gravity work at the tap point.
  - Placing next to a wall or on a spawn band is refused, with a red ghost.
- [ ] **Step 5: Commit.** `git commit -m "Player round: towers, abilities, click in the feed"`

---

### Task 10: Round overlay — HUD, tray state, tower panel, results

**Files:**
- Modify: `src/scene/RoundOverlay.hpp/.cpp`
- Modify: `assets/ui/round.rml`, `assets/ui/tablet.rcss`
- Modify: `src/scene/LevelScene.cpp` (drives the overlay; handles the panel and results actions; Esc while ended)

**Interfaces:**
- Consumes: `logic::Round` queries, `save::load/write/defaultPath` (Task 5)
- Produces (in `RoundOverlay`):
  ```cpp
  struct Hud { int money = 0; std::uint32_t dealt = 0; std::uint32_t total = 0; std::uint32_t leaked = 0; std::uint32_t leakLimit = 1; };
  void setHud(const Hud& hud);
  // Per tray item: whether it's affordable now, and seconds of cooldown left (0: ready).
  void setTrayState(int index, bool affordable, float cooldown);
  struct TowerPanel { Rml::String name; int level = 1; int hp = 0; int maxHp = 0;
                      Rml::String action; int cost = 0; bool affordable = false; glm::vec2 screen{0.0f}; };
  void showTowerPanel(const TowerPanel& panel);
  void hideTowerPanel();
  bool takeOfferPressed();
  struct Results { bool won = false; float seconds = 0.0f; std::uint32_t killed = 0; std::uint32_t leaked = 0;
                   int earned = 0; std::int64_t total = 0; };
  void showResults(const Results& results);
  enum class ResultAction { None, Retry, Quit };
  ResultAction takeResultAction();
  ```
- **Tray:** Sniper `diamond #f4ca84`, Gunner `bar #90d7ff`, Pulse `ring #76e2e2`, Frost `tri ▲ #a1e4ae`, Bomb `circle #ffa8a0`, Gravity `square #d4bcff`. Costs come from `towerStats` and `abilityStats`. An unaffordable item gets a `disabled` class and can't be picked. An item on cooldown shows its whole seconds left.
- **HUD:**
  - The OSD gains a `$ {money}` line.
  - The wave label becomes `HORDE {dealt}/{total}` (zero-padded like the other OSD text).
  - The 10 base pips show `ceil(10 × (leakLimit − leaked) / leakLimit)` lit.
- **Tower panel:** a small crisp card at `screen` (the tower's screen position, kept on the tablet) with the name, `LV n`, `HP a/b`, and a button reading `UPGRADE $c` or `REPAIR $c`, disabled when unaffordable.
- **Results:** a centred card reading `WIN` or `LOSS`, with time `mm:ss`, kills and leaked. On a win it also shows `+$earned` upgrade money and `TOTAL $total`. Buttons: `RETRY` and `QUIT`.
- **LevelScene:**
  - Each frame, push the HUD, tray state and the panel for `m_inspected`.
  - When `takeOfferPressed()` is true, call `buyOffer(m_inspected)`.
  - On the first frame the outcome is Won: `save::load(defaultPath())`, add `upgradeMoney()`, `write`, then `showResults` with `earned` and the new total. On a loss, `earned = 0` and the total is whatever `load` returns.
  - Retry is `scenes->replace(std::make_unique<LevelScene>(m_options))`.
  - Quit leaves the level as PauseScene's quit does (a single `pop` from the level, since no pause is open).
  - Esc is ignored once the round has ended.
- **Tray clicks:** clicks on the tray, panel or results are taken by RmlUi before the scene (`App.cpp`), so they never reach the feed.

- [ ] **Step 1: Implement** the RML, RCSS and C++ above.
- [ ] **Step 2: Build and run the tests.** `cmake --build --preset linux-debug && ./build/linux-debug/bin/horde_tests`. Expected: the build succeeds and all tests pass.
- [ ] **Step 3: Walk through it by hand.** On the default level:
  - Money starts at 400 and rises with kills.
  - Unaffordable items grey out, and cooldowns count down.
  - Tapping a tower opens its panel, and Upgrade raises the level and refills HP.
  - At level 3 the button reads Repair.
  - A tower destroyed while its panel is open closes the panel.
  - Let units leak until the pips run out: LOSS appears, the feed freezes, and Esc does nothing.
  - Retry restarts the round.
  - Win a round by editing the level to 200 units: WIN shows the earned money, and quitting and winning again shows the total growing.
  - Quit returns to the menu.
- [ ] **Step 4: Commit.** `git commit -m "Round HUD, tower panel, results and saved upgrade money"`

---

### Task 11: Editor — round settings

**Files:**
- Modify: `src/editor/EditorUi.cpp` (`drawLevelPanel`), `src/editor/EditorState.hpp`, `src/scene/EditorScene.cpp`

**Interfaces:**
- Consumes: `logic::RoundSettings` (Task 1)
- **Rules:**
  - `drawLevelPanel` gains a `SeparatorText(tr("Round"))` section with an input per `RoundSettings` field, clamped to Task 1's validation ranges. Edits mark the level dirty, the same way the other level edits do.
  - Remove the editor's test-units control (`EditorState::testUnits` and its `InputInt`). The play-test is a round now, so `round.totalUnits` sets the unit count.
  - The play-test keeps `options.mode = LevelMode::Player`.

- [ ] **Step 1: Implement** the rules above.
- [ ] **Step 2: Build.** `cmake --build --preset linux-debug`. Expected: it succeeds.
- [ ] **Step 3: Walk through it by hand.**
  - Open the editor, set Total units to 300 and Start money to 1000, then play-test. The HUD shows `/300` and `$ 1000`.
  - Save, reload the level, and check the values persist.
  - Quitting the play-test returns to the editor.
- [ ] **Step 4: Commit.** `git commit -m "Editor round settings, play-test runs the round"`

---

### Task 12: Final verification

- [ ] **Step 1: Run the full build and tests.** `cmake --build --preset linux-debug && ./build/linux-debug/bin/horde_tests`. Expected: all pass.
- [ ] **Step 2: Walk through the spec's checklist by hand.** Use the "Verification" section of the spec, in a fresh run.
- [ ] **Step 3: Check the benchmark.** `python3 tools/bench.py --physics gpu-collide --units 10k,100k,1m --duration 5`. Expected: it completes, with timings within noise of the numbers recorded in Task 6 Step 4.
- [ ] **Step 4: Check Dev mode.** Dev ground from the menu: units still respawn at exits, and the panel's restart and explosion controls work.
- [ ] **Step 5: Report the DXIL list.** Tell the user which DXIL files need rebuilding on Windows with `tools/compile_shaders.sh`, run from a Windows shell: `unit_finalize.comp`, `unit_collide.comp`, `unit_smooth.comp` (plus other includers found in Task 6), `unit_integrate.comp` and `unit_density.comp`. Until they're rebuilt, D3D12 builds will fail at runtime.
