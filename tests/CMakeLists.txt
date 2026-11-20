# Unit tests for pure logic. Only src/ files with no SDL runtime or GPU
# dependency are compiled in, so the suite runs headless on CI.
add_executable(horde_tests
    test_main.cpp
    TweenTests.cpp
    DeviceLayoutTests.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/DeviceLayout.cpp)

target_compile_features(horde_tests PRIVATE cxx_std_20)
target_include_directories(horde_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(horde_tests PRIVATE doctest::doctest glm::glm SDL3::SDL3)

if(MSVC)
    target_compile_options(horde_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(horde_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME horde_tests COMMAND horde_tests)
