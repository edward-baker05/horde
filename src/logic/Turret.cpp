#include "logic/Turret.hpp"

#include <glm/trigonometric.hpp>

namespace horde::logic {

namespace {

// First guesses, tuned by hand. Index by TurretKind.
std::array<TurretDef, kTurretKindCount> makeDefs() {
    std::array<TurretDef, kTurretKindCount> defs{};

    TurretDef& flamer = defs[static_cast<std::size_t>(TurretKind::Flamer)];
    flamer.name = "Flamer";
    flamer.delivery = TurretDelivery::Cone;
    flamer.directional = true;
    flamer.range = 150.0f;
    flamer.arc = glm::radians(40.0f);
    flamer.damage = 10.0f;
    flamer.burnSeconds = 3.0f;
    flamer.burnDps = 4.0f;
    flamer.cost = 100;

    TurretDef& zapper = defs[static_cast<std::size_t>(TurretKind::Zapper)];
    zapper.name = "Zapper";
    zapper.delivery = TurretDelivery::Pulse;
    zapper.range = 100.0f;
    zapper.period = 0.5f;
    zapper.damage = 5.0f;
    zapper.stunSeconds = 0.75f;
    zapper.cost = 100;

    TurretDef& mortar = defs[static_cast<std::size_t>(TurretKind::Mortar)];
    mortar.name = "Mortar";
    mortar.delivery = TurretDelivery::Shell;
    mortar.blastPush = 80.0f;
    mortar.halfSize = {8.0f, 8.0f};
    mortar.range = 400.0f;
    mortar.period = 2.0f;
    mortar.damage = 40.0f;
    mortar.blastRadius = 40.0f;
    mortar.cost = 200;

    TurretDef& beam = defs[static_cast<std::size_t>(TurretKind::Beam)];
    beam.name = "Beam";
    beam.delivery = TurretDelivery::Sweep;
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
    mine.delivery = TurretDelivery::Mine;
    mine.blastPush = 150.0f;
    mine.placement = TurretClass::FloorTrap;
    mine.halfSize = {4.0f, 4.0f};
    mine.range = 4.0f;
    mine.damage = 20.0f;
    mine.blastRadius = 60.0f;
    mine.cost = 25;

    TurretDef& gunner = defs[static_cast<std::size_t>(TurretKind::Gunner)];
    gunner.name = "Gunner";
    gunner.delivery = TurretDelivery::Gun;
    gunner.range = 250.0f;
    gunner.period = 0.1f;
    gunner.damage = 6.0f;
    gunner.bulletSpeed = 500.0f;
    gunner.pierce = 0;
    gunner.cost = 150;

    // Weapons. See docs/superpowers/specs/2026-11-21-weapon-categories-design.md.

    // Merch: fans given enough merch are satisfied and leave (hit points).

    TurretDef& tshirt = defs[static_cast<std::size_t>(TurretKind::TShirtCannon)];
    tshirt.name = "T-shirt cannon";
    tshirt.blurb = "Lobs a bundle of shirts at the thickest crowd in range.";
    tshirt.category = WeaponCategory::Merch;
    tshirt.spriteId = 1;
    tshirt.delivery = TurretDelivery::Shell;
    tshirt.halfSize = {8.0f, 8.0f};
    tshirt.range = 400.0f;
    tshirt.period = 2.5f;
    tshirt.damage = 35.0f;
    tshirt.blastRadius = 45.0f;
    tshirt.cost = 200;

    TurretDef& slinger = defs[static_cast<std::size_t>(TurretKind::MerchSlinger)];
    slinger.name = "Merch slinger";
    slinger.blurb = "Flings signed photos and vinyl frisbees at the nearest fan; each goes through two more.";
    slinger.category = WeaponCategory::Merch;
    slinger.spriteId = 2;
    slinger.delivery = TurretDelivery::Gun;
    slinger.range = 250.0f;
    slinger.period = 0.25f;
    slinger.damage = 12.0f;
    slinger.bulletSpeed = 350.0f;
    slinger.pierce = 2;
    slinger.cost = 150;

    TurretDef& stand = defs[static_cast<std::size_t>(TurretKind::MerchStand)];
    stand.name = "Merch stand";
    stand.blurb = "A stall in the crowd's way. Fans pressed against it get merch, and wear it down.";
    stand.category = WeaponCategory::Merch;
    stand.spriteId = 3;
    stand.placement = TurretClass::Blocker;
    stand.halfSize = {12.0f, 8.0f};
    stand.directional = true;
    stand.damage = 20.0f;
    stand.maxHp = 60.0f;
    stand.cost = 75;

    // Sound.

    TurretDef& jazz = defs[static_cast<std::size_t>(TurretKind::JazzSpeaker)];
    jazz.name = "Smooth jazz speaker";
    jazz.blurb = "Calms every fan around it to half speed. No damage.";
    jazz.category = WeaponCategory::Sound;
    jazz.spriteId = 4;
    jazz.delivery = TurretDelivery::Cone;
    jazz.range = 120.0f;
    jazz.arc = glm::radians(360.0f); // an aura
    jazz.slowSeconds = 0.5f;         // lingers briefly once a fan walks out of it
    jazz.slowFactor = 0.5f;
    jazz.cost = 125;

    TurretDef& bass = defs[static_cast<std::size_t>(TurretKind::BassStack)];
    bass.name = "Bass stack";
    bass.blurb = "A booming bass drop hurts every fan in range every couple of seconds.";
    bass.category = WeaponCategory::Sound;
    bass.spriteId = 5;
    bass.delivery = TurretDelivery::Pulse;
    bass.halfSize = {8.0f, 8.0f};
    bass.range = 120.0f;
    bass.period = 2.0f;
    bass.damage = 15.0f;
    bass.cost = 150;

    TurretDef& amp = defs[static_cast<std::size_t>(TurretKind::FeedbackAmp)];
    amp.name = "Feedback amp";
    amp.blurb = "A sudden shockwave: stuns fans close by for a second and shoves them back.";
    amp.category = WeaponCategory::Sound;
    amp.spriteId = 6;
    amp.delivery = TurretDelivery::Pulse;
    amp.range = 80.0f;
    amp.period = 3.0f;
    amp.damage = 4.0f;
    amp.blastPush = 60.0f;
    amp.stunSeconds = 1.0f;
    amp.cost = 125;

    // Chemical.

    TurretDef& pepper = defs[static_cast<std::size_t>(TurretKind::PepperSpray)];
    pepper.name = "Pepper spray";
    pepper.blurb = "A short cone of spray that keeps hurting fans for a few seconds after.";
    pepper.category = WeaponCategory::Chemical;
    pepper.spriteId = 7;
    pepper.delivery = TurretDelivery::Cone;
    pepper.directional = true;
    pepper.range = 90.0f;
    pepper.arc = glm::radians(50.0f);
    pepper.damage = 4.0f;
    pepper.burnSeconds = 3.0f;
    pepper.burnDps = 4.0f;
    pepper.cost = 100;

    // Physical.

    TurretDef& barrier = defs[static_cast<std::size_t>(TurretKind::CrowdBarrier)];
    barrier.name = "Crowd barrier";
    barrier.blurb = "Very tough and harmless: herds the crowd where you want it.";
    barrier.category = WeaponCategory::Physical;
    barrier.spriteId = 11;
    barrier.placement = TurretClass::Blocker;
    barrier.halfSize = {25.0f, 5.0f};
    barrier.directional = true;
    barrier.maxHp = 800.0f;
    barrier.cost = 60;

    return defs;
}

} // namespace

const char* weaponCategoryName(WeaponCategory category) {
    switch (category) {
        case WeaponCategory::Merch:
            return "Merch";
        case WeaponCategory::Sound:
            return "Sound";
        case WeaponCategory::Chemical:
            return "Chemical";
        case WeaponCategory::Physical:
            return "Physical";
    }
    return "";
}

glm::vec4 weaponCategoryColor(WeaponCategory category) {
    // 0-255 to match tools/make_weapon_atlas.py's CATEGORY_COLOURS.
    auto rgb = [](float r, float g, float b) { return glm::vec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f); };
    switch (category) {
        case WeaponCategory::Merch:
            return rgb(214, 60, 160); // magenta
        case WeaponCategory::Sound:
            return rgb(60, 120, 220); // blue
        case WeaponCategory::Chemical:
            return rgb(90, 180, 70); // green
        case WeaponCategory::Physical:
            return rgb(230, 140, 40); // orange
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

const char* turretClassName(TurretClass placement) {
    switch (placement) {
        case TurretClass::Mount:
            return "Mount";
        case TurretClass::Blocker:
            return "Blocker";
        case TurretClass::FloorTrap:
            return "Floor trap";
    }
    return "";
}

glm::vec4 turretClassColor(TurretClass placement) {
    switch (placement) {
        case TurretClass::Mount:
            return {0.95f, 0.65f, 0.2f, 1.0f};
        case TurretClass::Blocker:
            return {0.6f, 0.55f, 0.5f, 1.0f};
        case TurretClass::FloorTrap:
            return {0.85f, 0.25f, 0.25f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

glm::vec4 turretColor(const TurretDef& def) {
    return def.category ? weaponCategoryColor(*def.category) : turretClassColor(def.placement);
}

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
