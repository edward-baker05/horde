# Third-party dependencies.
#
# Policy: prefer a system install where one exists (fast, and what Linux distros
# ship), otherwise fetch the pinned tag. That keeps `dnf install` builds quick
# while preserving the "you don't need to install anything" experience on
# Windows and macOS, where these libraries are not packaged.

include(FetchContent)

set(HORDE_SDL3_TAG "release-3.4.12" CACHE STRING "SDL3 tag to fetch when no system SDL3 is found")
set(HORDE_SDL3_IMAGE_TAG "release-3.4.4" CACHE STRING "SDL3_image tag to fetch when no system SDL3_image is found")
set(HORDE_GLM_TAG "1.0.1" CACHE STRING "glm tag to fetch when no system glm is found")
set(HORDE_IMGUI_TAG "v1.92.9-docking" CACHE STRING "Dear ImGui tag to fetch")

# --- SDL3 --------------------------------------------------------------------

find_package(SDL3 3.4 CONFIG QUIET)

if(SDL3_FOUND)
    message(STATUS "horde: using system SDL3 ${SDL3_VERSION}")
else()
    message(STATUS "horde: no system SDL3, fetching ${HORDE_SDL3_TAG}")
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${HORDE_SDL3_TAG}
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_MakeAvailable(SDL3)
endif()

# --- SDL3_image --------------------------------------------------------------

find_package(SDL3_image 3.4 CONFIG QUIET)

if(SDL3_image_FOUND)
    message(STATUS "horde: using system SDL3_image ${SDL3_image_VERSION}")
else()
    message(STATUS "horde: no system SDL3_image, fetching ${HORDE_SDL3_IMAGE_TAG}")
    set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_TIF OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG ${HORDE_SDL3_IMAGE_TAG}
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_MakeAvailable(SDL3_image)
endif()

# --- glm ---------------------------------------------------------------------

find_package(glm CONFIG QUIET)

if(glm_FOUND)
    message(STATUS "horde: using system glm ${glm_VERSION}")
else()
    message(STATUS "horde: no system glm, fetching ${HORDE_GLM_TAG}")
    set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
    set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG ${HORDE_GLM_TAG}
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_MakeAvailable(glm)
endif()

# --- Dear ImGui --------------------------------------------------------------
#
# Always fetched: ImGui is not packaged by any distro and ships no CMakeLists,
# so we compile the core plus the two backends we need into a static library.
# The -docking tag is used for dockable tool windows.

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG ${HORDE_IMGUI_TAG}
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)

target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)

target_compile_features(imgui PUBLIC cxx_std_20)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_library(imgui::imgui ALIAS imgui)
