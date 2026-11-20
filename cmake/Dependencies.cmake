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
set(HORDE_JSON_TAG "v3.11.3" CACHE STRING "nlohmann/json tag to fetch when no system copy is found")
set(HORDE_DOCTEST_TAG "v2.4.11" CACHE STRING "doctest tag to fetch when no system copy is found")

# --- SDL3 --------------------------------------------------------------------

find_package(SDL3 3.4 CONFIG QUIET)

if(SDL3_FOUND)
    message(STATUS "horde: using system SDL3 ${SDL3_VERSION}")
else()
    message(STATUS "horde: no system SDL3, fetching ${HORDE_SDL3_TAG}")
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
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

# --- nlohmann/json -----------------------------------------------------------
#
# Header-only, and the only parsing dependency in the project. Used solely by
# src/logic/LevelIO.cpp to read and write level files.

find_package(nlohmann_json 3.11 CONFIG QUIET)

if(nlohmann_json_FOUND)
    message(STATUS "horde: using system nlohmann/json ${nlohmann_json_VERSION}")
else()
    message(STATUS "horde: no system nlohmann/json, fetching ${HORDE_JSON_TAG}")
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG ${HORDE_JSON_TAG}
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_MakeAvailable(nlohmann_json)
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

# --- doctest -----------------------------------------------------------------
#
# Header-only test framework, used only by tests/.

if(HORDE_BUILD_TESTS)
    find_package(doctest CONFIG QUIET)

    if(doctest_FOUND)
        message(STATUS "horde: using system doctest ${doctest_VERSION}")
    else()
        message(STATUS "horde: no system doctest, fetching ${HORDE_DOCTEST_TAG}")
        set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
        set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(doctest
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG ${HORDE_DOCTEST_TAG}
            GIT_SHALLOW ON
            EXCLUDE_FROM_ALL
            SYSTEM)
        # v2.4.11 declares cmake_minimum_required below 3.5, which CMake 4 rejects.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        FetchContent_MakeAvailable(doctest)
        unset(CMAKE_POLICY_VERSION_MINIMUM)
    endif()
endif()

# --- FreeType + RmlUi --------------------------------------------------------
#
# RmlUi lays out and styles the device screens. Its FreeType font engine needs
# FreeType: use the system one if present, otherwise build a minimal copy with
# every optional codec off (we only load TTF/OTF files).

# Release tarballs rather than git clones: far faster and reproducible. The
# SHA256 variables pin the default tags; set them together with the tag variables.
set(HORDE_FREETYPE_TAG "VER-2-13-3" CACHE STRING "FreeType tag to fetch when no system copy is found")
set(HORDE_FREETYPE_SHA256 "bc5c898e4756d373e0d991bab053036c5eb2aa7c0d5c67e8662ddc6da40c4103" CACHE STRING "SHA256 of the FreeType tarball for HORDE_FREETYPE_TAG")
set(HORDE_RMLUI_TAG "6.3" CACHE STRING "RmlUi tag to fetch")
set(HORDE_RMLUI_SHA256 "d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede" CACHE STRING "SHA256 of the RmlUi tarball for HORDE_RMLUI_TAG")

find_package(Freetype QUIET)

if(Freetype_FOUND OR FREETYPE_FOUND)
    message(STATUS "horde: using system FreeType ${FREETYPE_VERSION_STRING}")
else()
    message(STATUS "horde: no system FreeType, fetching ${HORDE_FREETYPE_TAG}")
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype
        URL https://github.com/freetype/freetype/archive/refs/tags/${HORDE_FREETYPE_TAG}.tar.gz
        URL_HASH SHA256=${HORDE_FREETYPE_SHA256}
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL
        SYSTEM
        OVERRIDE_FIND_PACKAGE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(freetype)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
endif()

set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
FetchContent_Declare(RmlUi
    URL https://github.com/mikke89/RmlUi/archive/refs/tags/${HORDE_RMLUI_TAG}.tar.gz
    URL_HASH SHA256=${HORDE_RMLUI_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
    SYSTEM)
# Static, so we ship no extra .so/.dll and a fetched (non-PIC) FreeType links in.
set(_horde_saved_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(RmlUi)
unset(CMAKE_POLICY_VERSION_MINIMUM)
set(BUILD_SHARED_LIBS ${_horde_saved_shared})

# --- stb_vorbis --------------------------------------------------------------
#
# Public-domain Ogg Vorbis decoder for the menu music. stb has no releases, so a
# commit is pinned; set HORDE_STB_SHA256 together with HORDE_STB_COMMIT.

set(HORDE_STB_COMMIT "0e9206afd9752c8c0826b671b16b702ec7d175cd" CACHE STRING "nothings/stb commit to fetch")
set(HORDE_STB_SHA256 "bfa11113029788800e1bbecdf6de107f9922d382c73ad20d3441526e4634c96e" CACHE STRING "SHA256 of the stb tarball for HORDE_STB_COMMIT")

FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/${HORDE_STB_COMMIT}.tar.gz
    URL_HASH SHA256=${HORDE_STB_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(stb)

add_library(stb_vorbis STATIC ${stb_SOURCE_DIR}/stb_vorbis.c)
target_include_directories(stb_vorbis SYSTEM PUBLIC ${stb_SOURCE_DIR})
# Decoding from memory and files only; no stdio-based pushdata API needed.
target_compile_definitions(stb_vorbis PRIVATE STB_VORBIS_NO_PUSHDATA_API)
if(NOT MSVC)
    target_compile_options(stb_vorbis PRIVATE -w)
endif()
