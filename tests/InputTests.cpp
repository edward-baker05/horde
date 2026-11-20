#include <doctest/doctest.h>

#include <RmlUi/Core/Input.h>
#include <SDL3/SDL.h>

#include "ui/RmlInput.hpp"
#include "ui/ScreenInput.hpp"

using namespace horde::ui;

namespace {
DeviceRect tablet() {
    DeviceRect r;
    r.center = {800.0f, 450.0f};
    r.screenSize = {1000.0f, 625.0f};
    r.scale = 1.0f;
    return r;
}
} // namespace

TEST_CASE("toRmlKey maps common keys and rejects the rest") {
    CHECK(toRmlKey(SDLK_ESCAPE) == Rml::Input::KI_ESCAPE);
    CHECK(toRmlKey(SDLK_A) == Rml::Input::KI_A);
    CHECK(toRmlKey(SDLK_KP_0) == Rml::Input::KI_UNKNOWN);
}

TEST_CASE("mapMouseToScreen converts window pixels to screen pixels") {
    const ScreenPoint c = mapMouseToScreen(tablet(), {0.0f, 0.0f}, {800.0f, 450.0f});
    CHECK(c.inside);
    CHECK(c.pixel.x == doctest::Approx(500.0f));
    CHECK(c.pixel.y == doctest::Approx(312.5f));

    const ScreenPoint out = mapMouseToScreen(tablet(), {0.0f, 0.0f}, {100.0f, 100.0f});
    CHECK_FALSE(out.inside);
    CHECK(out.pixel.x == doctest::Approx(0.0f));
    CHECK(out.pixel.y == doctest::Approx(0.0f));

    CHECK_FALSE(mapMouseToScreen(tablet(), {1600.0f, 0.0f}, {800.0f, 450.0f}).inside);
}

TEST_CASE("remapMouseEvent drops outside button-downs but clamps motion") {
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.x = 100.0f;
    down.button.y = 100.0f;
    CHECK_FALSE(remapMouseEvent(down, tablet(), {0.0f, 0.0f}));

    SDL_Event move{};
    move.type = SDL_EVENT_MOUSE_MOTION;
    move.motion.x = 100.0f;
    move.motion.y = 100.0f;
    CHECK(remapMouseEvent(move, tablet(), {0.0f, 0.0f}));
    CHECK(move.motion.x == doctest::Approx(0.0f));
    CHECK(move.motion.y == doctest::Approx(0.0f));
}
