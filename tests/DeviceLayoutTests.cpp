#include <doctest/doctest.h>

#include "ui/DeviceLayout.hpp"

using namespace horde::ui;

TEST_CASE("phone portrait is height-limited in a landscape window") {
    const DeviceSpec spec = deviceSpec(DeviceKind::Phone, Orientation::Portrait);
    const DeviceRect r = fitDevice(spec, {1600.0f, 900.0f});
    CHECK((844.0f + 24.0f) * r.scale == doctest::Approx(900.0f * (1.0f - 0.12f)).epsilon(0.5 / 792.0));
    CHECK(r.center.x == doctest::Approx(800.0f));
    CHECK(r.center.y == doctest::Approx(450.0f));
}

TEST_CASE("tablet is height-limited in 1600x900 and centred") {
    const DeviceSpec spec = deviceSpec(DeviceKind::Tablet, Orientation::Landscape);
    const DeviceRect r = fitDevice(spec, {1600.0f, 900.0f});
    CHECK((800.0f + 48.0f) * r.scale == doctest::Approx(792.0f).epsilon(0.5 / 792.0));
    CHECK(r.center.x == doctest::Approx(800.0f));
    CHECK(r.center.y == doctest::Approx(450.0f));
}

TEST_CASE("tablet is width-limited in a portrait window") {
    const DeviceSpec spec = deviceSpec(DeviceKind::Tablet, Orientation::Landscape);
    const DeviceRect r = fitDevice(spec, {900.0f, 1600.0f});
    CHECK((1280.0f + 48.0f) * r.scale == doctest::Approx(900.0f * 0.88f).epsilon(0.5 / 792.0));
}

TEST_CASE("phone landscape swaps the screen dimensions") {
    const DeviceSpec spec = deviceSpec(DeviceKind::Phone, Orientation::Landscape);
    CHECK(spec.screenLogical.x == 844.0f);
    CHECK(spec.screenLogical.y == 390.0f);
}

TEST_CASE("screenPixelSize rounds and never collapses below one pixel") {
    const DeviceRect r{{0.0f, 0.0f}, {100.4f, 0.2f}, 1.0f};
    const glm::ivec2 px = screenPixelSize(r);
    CHECK(px.x == 100);
    CHECK(px.y == 1);
}
