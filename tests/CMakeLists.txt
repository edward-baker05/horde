# Unit tests for pure logic. Only src/ files with no SDL runtime or GPU
# dependency are compiled in, so the suite runs headless on CI. Input mapping
# also links RmlUi (headers and Rml::Context, no initialisation); RmlSystem is
# SDL-runtime glue and stays out.
add_executable(horde_tests
    test_main.cpp
    TweenTests.cpp
    DeviceLayoutTests.cpp
    ShellStateTests.cpp
    InputTests.cpp
    LevelIoTests.cpp
    RoundTests.cpp
    TurretEffectsTests.cpp
    SaveDataTests.cpp
    ${PROJECT_SOURCE_DIR}/src/sim/TurretEffects.cpp
    ${PROJECT_SOURCE_DIR}/src/sim/WallColliders.cpp
    ${PROJECT_SOURCE_DIR}/src/logic/LevelGeometry.cpp
    ${PROJECT_SOURCE_DIR}/src/logic/WallGridShapes.cpp
    ${PROJECT_SOURCE_DIR}/src/core/SaveData.cpp
    ${PROJECT_SOURCE_DIR}/src/logic/Round.cpp
    ${PROJECT_SOURCE_DIR}/src/logic/Turret.cpp
    ${PROJECT_SOURCE_DIR}/src/logic/Level.cpp
    ${PROJECT_SOURCE_DIR}/src/logic/LevelIO.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/DeviceLayout.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/ShellState.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/RmlInput.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/ScreenInput.cpp)

target_compile_features(horde_tests PRIVATE cxx_std_20)
target_include_directories(horde_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(horde_tests PRIVATE doctest::doctest glm::glm SDL3::SDL3 RmlUi::RmlUi
    nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(horde_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(horde_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME horde_tests COMMAND horde_tests)
