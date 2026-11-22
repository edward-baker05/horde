#include "logic/Turret.hpp"

#include <glm/trigonometric.hpp>

namespace horde::logic {

namespace {

// First guesses, tuned by hand. Index by TurretKind.
std::array<TurretDef, kTurretKindCount> makeDefs() {
    std::array<TurretDef, kTurretKindCount> defs{};

    TurretDef& flamer = defs[static_cast<std::size_t>(TurretKind::Flamer)];
    flamer.name = "Flamer";
    flamer.directional = true;
    flamer.range = 150.0f;
    flamer.arc = glm::radians(40.0f);
    flamer.damage = 10.0f;
    flamer.burnSeconds = 3.0f;
    flamer.burnDps = 4.0f;
    flamer.cost = 100;

    TurretDef& zapper = defs[static_cast<std::size_t>(TurretKind::Zapper)];
    zapper.name = "Zapper";
    zapper.range = 100.0f;
    zapper.period = 0.5f;
    zapper.damage = 5.0f;
    zapper.stunSeconds = 0.75f;
    zapper.cost = 100;

    TurretDef& mortar = defs[static_cast<std::size_t>(TurretKind::Mortar)];
    mortar.name = "Mortar";
    mortar.halfSize = {8.0f, 8.0f};
    mortar.range = 400.0f;
    mortar.period = 2.0f;
    mortar.damage = 40.0f;
    mortar.blastRadius = 40.0f;
    mortar.cost = 200;

    TurretDef& beam = defs[static_cast<std::size_t>(TurretKind::Beam)];
    beam.name = "Beam";
    beam.directional = true;
    beam.range = 200.0f;
    beam.arc = glm::radians(30.0f);
    beam.period = 5.0f;
    beam.damage = 60.0f;
    beam.blastRadius = 10.0f;
    beam.cost = 200;

    TurretDef& spikes = defs[static_cast<std::size_t>(TurretKind::SpikeBlock)];
    spikes.name = "Spike block";
    spikes.placement = TurretClass::Blocker;
    spikes.halfSize = {10.0f, 10.0f};
    spikes.directional = true;
    spikes.damage = 30.0f;
    spikes.maxHp = 100.0f;
    spikes.cost = 75;

    TurretDef& barricade = defs[static_cast<std::size_t>(TurretKind::Barricade)];
    barricade.name = "Barricade";
    barricade.placement = TurretClass::Blocker;
    barricade.halfSize = {20.0f, 5.0f};
    barricade.directional = true;
    barricade.maxHp = 500.0f;
    barricade.cost = 50;

    TurretDef& mine = defs[static_cast<std::size_t>(TurretKind::Mine)];
    mine.name = "Mine";
    mine.placement = TurretClass::FloorTrap;
    mine.halfSize = {4.0f, 4.0f};
    mine.range = 4.0f;
    mine.damage = 20.0f;
    mine.blastRadius = 60.0f;
    mine.cost = 25;

    TurretDef& gunner = defs[static_cast<std::size_t>(TurretKind::Gunner)];
    gunner.name = "Gunner";
    gunner.range = 250.0f;
    gunner.period = 0.1f;
    gunner.damage = 6.0f;
    gunner.bulletSpeed = 500.0f;
    gunner.pierce = 0;
    gunner.cost = 150;

    return defs;
}

} // namespace

const TurretDef& turretDef(TurretKind kind) {
    static const std::array<TurretDef, kTurretKindCount> defs = makeDefs();
    return defs[static_cast<std::size_t>(kind)];
}

Turret makeTurret(TurretKind kind, glm::vec2 center, float facing) {
    Turret turret;
    turret.kind = kind;
    turret.center = center;
    turret.facing = facing;
    turret.hp = turretDef(kind).maxHp;
    return turret;
}

} // namespace horde::logic
