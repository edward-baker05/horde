# Shader pipeline.
#
# Shaders are authored once, in HLSL, under shaders/src/. SDL_GPU documents its
# resource binding model in HLSL register/space terms, so HLSL is the path of
# least friction even on Linux.
#
# SDL_GPU needs a different binary format per backend: SPIR-V for Vulkan, MSL
# for Metal, DXIL for D3D12. Those are produced by the `shadercross` CLI and
# COMMITTED under shaders/compiled/. They are small, deterministic artifacts, so
# committing them means a normal build on any platform needs no shader toolchain
# at all — which is what actually makes this repo build everywhere. Building
# shadercross requires compiling DirectXShaderCompiler (an LLVM fork) from
# source, and nobody should pay that cost just to build the game.
#
# Run tools/build_shadercross.sh (or .ps1) once if you need to EDIT a shader.
# Once the CLI is on PATH or in tools/bin/, the artifacts regenerate as part of
# the build and the regenerated files should be committed alongside the source.

set(HORDE_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/src")
set(HORDE_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/compiled")

find_program(HORDE_SHADERCROSS
    NAMES shadercross
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/bin"
    NO_CACHE
    DOC "SDL_shadercross CLI, used to recompile HLSL shaders")

# DXIL must be signed by dxil.dll, which only exists on Windows. DXIL produced
# anywhere else is unsigned and D3D12 rejects it outside developer mode, so we
# only emit it on Windows. Everywhere else SDL_GPU falls back to its Vulkan
# backend, which consumes the SPIR-V we do emit.
set(HORDE_SHADER_FORMATS spirv msl)
set(HORDE_SHADER_EXTENSION_spirv "spv")
set(HORDE_SHADER_EXTENSION_msl "msl")

if(WIN32)
    list(APPEND HORDE_SHADER_FORMATS dxil)
    set(HORDE_SHADER_EXTENSION_dxil "dxil")
endif()

# horde_add_shaders(<target>)
#
# Adds a `horde_shaders` target that recompiles shaders/src/*.hlsl into every
# format in HORDE_SHADER_FORMATS, and makes <target> depend on it. Does nothing
# if the shadercross CLI is unavailable.
function(horde_add_shaders TARGET)
    if(NOT HORDE_SHADERCROSS)
        message(STATUS
            "horde: shadercross not found - using the committed shaders in shaders/compiled/. "
            "Run tools/build_shadercross.sh if you need to edit a shader.")
        return()
    endif()

    message(STATUS "horde: shadercross found at ${HORDE_SHADERCROSS} - shaders will be recompiled")

    file(GLOB shader_sources CONFIGURE_DEPENDS "${HORDE_SHADER_SOURCE_DIR}/*.hlsl")

    if(NOT shader_sources)
        message(WARNING "horde: no shaders found in ${HORDE_SHADER_SOURCE_DIR}")
        return()
    endif()

    set(compiled_shaders "")

    foreach(source IN LISTS shader_sources)
        # sprite.vert.hlsl -> sprite.vert. shadercross infers both the source
        # language (.hlsl) and the stage (.vert/.frag/.comp) from the filename.
        get_filename_component(name "${source}" NAME)
        string(REGEX REPLACE "\\.hlsl$" "" stem "${name}")

        foreach(format IN LISTS HORDE_SHADER_FORMATS)
            set(output "${HORDE_SHADER_OUTPUT_DIR}/${format}/${stem}.${HORDE_SHADER_EXTENSION_${format}}")

            add_custom_command(
                OUTPUT "${output}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${HORDE_SHADER_OUTPUT_DIR}/${format}"
                COMMAND "${HORDE_SHADERCROSS}" "${source}" -o "${output}"
                DEPENDS "${source}" # This absolutely has the potential to break, edits to .hlsli files won't trigger recompilation - TODO 
                COMMENT "shadercross ${stem} -> ${format}"
                VERBATIM)

            list(APPEND compiled_shaders "${output}")
        endforeach()
    endforeach()

    add_custom_target(horde_shaders DEPENDS ${compiled_shaders})
    add_dependencies(${TARGET} horde_shaders)
endfunction()
