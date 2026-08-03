cmake_minimum_required(VERSION 3.16)
project(testing)

# Always emit compile_commands.json and keep a symlink to it at the repo
# root, so clangd (and any other tool) finds an up-to-date database without
# needing -DCMAKE_EXPORT_COMPILE_COMMANDS or manual steps after every
# reconfigure.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(CREATE_LINK
    "${CMAKE_BINARY_DIR}/compile_commands.json"
    "${CMAKE_SOURCE_DIR}/compile_commands.json"
    SYMBOLIC
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
set(PROJECT_SOURCE_DIR src)

# This assumes the SDL source is available in vendored/SDL
add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)

# Create your game executable target as usual
add_executable(testing WIN32 ${PROJECT_SOURCE_DIR}/main.cpp)
target_include_directories(testing PRIVATE include)

# Link to the actual SDL3 library.
target_link_libraries(testing PRIVATE SDL3::SDL3)
