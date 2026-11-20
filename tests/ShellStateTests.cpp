#include <doctest/doctest.h>

#include "scene/LevelMode.hpp"
#include "ui/ShellState.hpp"

#include <cstddef>
#include <vector>

using namespace horde::ui;

namespace {
ScenePresentation pres(DeviceKind d, Orientation o = Orientation::Portrait, bool overlay = false) {
    return {d, o, overlay};
}
} // namespace

TEST_CASE("computeTargets: phone menu") {
    std::vector<ScenePresentation> s{pres(DeviceKind::Phone)};
    auto t = computeTargets(s);
    CHECK(t.phone == Placement::Centered);
    CHECK(t.tablet == Placement::Hidden);
}

TEST_CASE("computeTargets: tablet round over menu") {
    std::vector<ScenePresentation> s{pres(DeviceKind::Phone), pres(DeviceKind::Tablet)};
    auto t = computeTargets(s);
    CHECK(t.phone == Placement::Hidden);
    CHECK(t.tablet == Placement::Centered);
}

TEST_CASE("computeTargets: phone overlay over tablet backgrounds the tablet") {
    std::vector<ScenePresentation> s{pres(DeviceKind::Phone), pres(DeviceKind::Tablet),
                                     pres(DeviceKind::Phone, Orientation::Portrait, true)};
    auto t = computeTargets(s);
    CHECK(t.phone == Placement::Centered);
    CHECK(t.tablet == Placement::Background);
}

TEST_CASE("computeTargets: scene without a device hides both") {
    std::vector<ScenePresentation> s{pres(DeviceKind::Phone), pres(DeviceKind::None)};
    auto t = computeTargets(s);
    CHECK(t.phone == Placement::Hidden);
    CHECK(t.tablet == Placement::Hidden);
}

TEST_CASE("computeTargets: overlay without a device hides both") {
    std::vector<ScenePresentation> s{pres(DeviceKind::Phone), pres(DeviceKind::Tablet),
                                     pres(DeviceKind::None, Orientation::Landscape, true)};
    auto t = computeTargets(s);
    CHECK(t.phone == Placement::Hidden);
    CHECK(t.tablet == Placement::Hidden);
}

TEST_CASE("computeTargets: landscape phone orientation") {
    std::vector<ScenePresentation> s{pres(DeviceKind::Phone),
                                     pres(DeviceKind::Phone, Orientation::Landscape)};
    auto t = computeTargets(s);
    CHECK(t.phone == Placement::Centered);
    CHECK(t.phoneOrientation == Orientation::Landscape);
}

TEST_CASE("DeviceAnimator slides in and settles") {
    DeviceAnimator a;
    CHECK(a.settled());
    CHECK_FALSE(a.visible());
    a.setTarget(Placement::Centered);
    a.update(0.175f);
    CHECK(a.visible());
    CHECK(a.offscreen() > 0.0f);
    CHECK(a.offscreen() < 1.0f);
    a.update(0.2f);
    CHECK(a.settled());
    CHECK(a.offscreen() == doctest::Approx(0.0f));
}

TEST_CASE("DeviceAnimator background blur") {
    DeviceAnimator a;
    a.setTarget(Placement::Background);
    a.update(0.25f);
    CHECK(a.blur() == doctest::Approx(1.0f));
}

TEST_CASE("DeviceAnimator retarget mid-slide does not jump") {
    DeviceAnimator a;
    a.setTarget(Placement::Centered);
    a.update(0.1f);
    const float before = a.offscreen();
    a.setTarget(Placement::Hidden);
    CHECK(a.offscreen() == doctest::Approx(before));
    a.update(0.05f);
    CHECK(a.offscreen() > before);
}

TEST_CASE("computeTargets: a dev-mode level over the menu hides both devices") {
    std::vector<ScenePresentation> s{pres(DeviceKind::Phone), horde::scene::levelPresentation(horde::scene::LevelMode::Dev)};
    auto t = computeTargets(s);
    CHECK(t.phone == Placement::Hidden);
    CHECK(t.tablet == Placement::Hidden);
}
