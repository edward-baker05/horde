#include <SDL3/SDL.h>

#include <RmlUi/Core/Input.h>
#include <doctest/doctest.h>

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

TEST_CASE("toRmlButton swaps middle and right and rejects extras") {
    CHECK(toRmlButton(SDL_BUTTON_LEFT) == 0);
    CHECK(toRmlButton(SDL_BUTTON_RIGHT) == 1);
    CHECK(toRmlButton(SDL_BUTTON_MIDDLE) == 2);
    CHECK(toRmlButton(SDL_BUTTON_X1) == -1);
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

TEST_CASE("scaleMouseEvent scales positions and deltas, leaves others") {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 10.0f;
    motion.motion.y = 20.0f;
    motion.motion.xrel = 1.0f;
    motion.motion.yrel = -2.0f;
    scaleMouseEvent(motion, 2.0f);
    CHECK(motion.motion.x == doctest::Approx(20.0f));
    CHECK(motion.motion.y == doctest::Approx(40.0f));
    CHECK(motion.motion.xrel == doctest::Approx(2.0f));
    CHECK(motion.motion.yrel == doctest::Approx(-4.0f));

    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.x = 3.0f;
    button.button.y = 4.0f;
    scaleMouseEvent(button, 1.5f);
    CHECK(button.button.x == doctest::Approx(4.5f));
    CHECK(button.button.y == doctest::Approx(6.0f));

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_A;
    scaleMouseEvent(key, 2.0f);
    CHECK(key.key.scancode == SDL_SCANCODE_A);
}
