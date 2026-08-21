cmake_minimum_required(VERSION 3.28)
project(horde LANGUAGES CXX VERSION 0.1.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(Dependencies)
include(Shaders)

# Add new source files here.
add_executable(horde
    src/main.cpp
    src/app/App.cpp
    src/core/Paths.cpp
    src/gfx/Camera2D.cpp
    src/gfx/GpuContext.cpp
    src/gfx/LevelRenderer.cpp
    src/gfx/ShaderLoader.cpp
    src/gfx/SpriteBatch.cpp
    src/gfx/Texture.cpp
    src/editor/EditorUi.cpp
    src/editor/Handles.cpp
    src/editor/Markers.cpp
    src/editor/HitTest.cpp
    src/editor/Tools.cpp
    src/scene/EditorScene.cpp
    src/scene/LevelScene.cpp
    src/scene/MainMenuScene.cpp
    src/scene/SceneStack.cpp
    src/scene/TechTreeScene.cpp
    src/ui/ImGuiLayer.cpp
    src/logic/UnitData.cpp
    src/logic/UnitData.hpp
    src/logic/Level.cpp
    src/logic/LevelGeometry.cpp
    src/logic/LevelFiles.cpp
    src/logic/LevelIO.cpp
    src/logic/LevelValidation.cpp)

target_compile_features(horde PRIVATE cxx_std_20)
target_include_directories(horde PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(horde PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    glm::glm
    imgui::imgui
    nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(horde PRIVATE /W4 /permissive- /arch:AVX2)
else()
    target_compile_options(horde PRIVATE -Wall -Wextra -Wpedantic -mavx2 -mfma)
endif()

# Regenerates shaders/compiled/ when the shadercross CLI is available; a no-op
# otherwise, since the compiled artifacts are committed.
horde_add_shaders(horde)

# Runtime files live next to the executable so that SDL_GetBasePath() finds them
# regardless of the working directory the game is launched from.
add_custom_command(TARGET horde POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:horde>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/compiled
        $<TARGET_FILE_DIR:horde>/shaders
    COMMENT "Copying assets and compiled shaders next to horde binary")


set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT horde)
