#include <doctest/doctest.h>

#include "ui/Tween.hpp"

using namespace horde::ui;

TEST_CASE("easeOutCubic endpoints and midpoint") {
    CHECK(easeOutCubic(0.0f) == doctest::Approx(0.0f));
    CHECK(easeOutCubic(1.0f) == doctest::Approx(1.0f));
    CHECK(easeOutCubic(0.5f) == doctest::Approx(0.875f));
}

TEST_CASE("tween completes after its duration") {
    Tween t{0.0f, 1.0f, 0.35f, 0.0f};
    CHECK_FALSE(t.done());
    t.update(0.35f);
    CHECK(t.done());
    CHECK(t.value() == doctest::Approx(1.0f));
}

TEST_CASE("retarget restarts from the current value") {
    Tween t{0.0f, 1.0f, 0.35f, 0.0f};
    t.update(0.175f);
    const float mid = t.value();
    CHECK(mid == doctest::Approx(0.875f));
    t.retarget(0.0f, 0.35f);
    CHECK(t.value() == doctest::Approx(mid));
    CHECK_FALSE(t.done());
    t.update(0.35f);
    CHECK(t.value() == doctest::Approx(0.0f));
}
