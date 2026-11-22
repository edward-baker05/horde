#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

#include "scene/LevelMode.hpp"
#include "ui/ShellState.hpp"

using namespace horde::ui;

namespace {
ScenePresentation pres(DeviceKind d, Orientation o = Orientation::Portrait, bool overlay = false) {
    return {d, o, overlay};
}
} // namespace

TEST_CASE("levelPresentation: player is landscape tablet") {
    const ScenePresentation p = horde::scene::levelPresentation(horde::scene::LevelMode::Player);
    CHECK(p.device == DeviceKind::Tablet);
    CHECK(p.orientation == Orientation::Landscape);
}

TEST_CASE("computeTargets: player level over phone menu") {
    std::vector<ScenePresentation> s{pres(DeviceKind::Phone),
                                     horde::scene::levelPresentation(horde::scene::LevelMode::Player)};
    auto t = computeTargets(s);
    CHECK(t.tablet == Placement::Centered);
    CHECK(t.phone == Placement::Hidden);
}

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
    std::vector<ScenePresentation> s{pres(DeviceKind::Phone), pres(DeviceKind::Phone, Orientation::Landscape)};
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
    std::vector<ScenePresentation> s{pres(DeviceKind::Phone),
                                     horde::scene::levelPresentation(horde::scene::LevelMode::Dev)};
    auto t = computeTargets(s);
    CHECK(t.phone == Placement::Hidden);
    CHECK(t.tablet == Placement::Hidden);
}

TEST_CASE("fitFeed: level fits above the tray, centred in the feed") {
    using horde::scene::fitFeed;
    // 2000 square level on a 1280x800 screen with a 116 px tray: the feed is
    // 684 tall, so height limits the zoom.
    const horde::scene::FeedFit fit = fitFeed({2000.0f, 2000.0f}, {1280.0f, 800.0f}, 116.0f);
    CHECK(fit.zoom == doctest::Approx(0.95f * 684.0f / 2000.0f));
    // The camera centres the screen; the level's middle must land on the
    // feed's middle, tray/2 px above the screen's.
    const float feedMiddleY = 400.0f - 58.0f;
    const float levelMiddleOnScreen = 400.0f + (1000.0f - fit.center.y) * fit.zoom;
    CHECK(levelMiddleOnScreen == doctest::Approx(feedMiddleY));
    CHECK(fit.center.x == doctest::Approx(1000.0f));
}

TEST_CASE("fitFeed: no tray is a plain fit") {
    const horde::scene::FeedFit fit = horde::scene::fitFeed({4000.0f, 1000.0f}, {1280.0f, 800.0f}, 0.0f);
    CHECK(fit.zoom == doctest::Approx(0.95f * 1280.0f / 4000.0f));
    CHECK(fit.center.x == doctest::Approx(2000.0f));
    CHECK(fit.center.y == doctest::Approx(500.0f));
}
