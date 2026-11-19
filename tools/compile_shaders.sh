#!/usr/bin/env bash
# Compiles shaders/src/*.hlsl (or just the ones named) into every committed
# format with an existing shadercross binary, without a CMake reconfigure.
#
#   tools/compile_shaders.sh                       # all shaders
#   tools/compile_shaders.sh unit_collide.comp     # one, by stem
#   SHADERCROSS=/path/to/shadercross tools/compile_shaders.sh
#
# The CMake build does the same automatically once shadercross is in tools/bin
# (see cmake/Shaders.cmake). DXIL is only produced on Windows, where it can be
# signed.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
sc="${SHADERCROSS:-}"
if [[ -z "$sc" ]]; then
    for candidate in "$root/tools/bin/shadercross" "$root/tools/bin/shadercross.exe"; do
        [[ -x "$candidate" ]] && sc="$candidate" && break
    done
fi
if [[ -z "$sc" ]] && command -v shadercross > /dev/null; then
    sc="$(command -v shadercross)"
fi
if [[ -z "$sc" ]]; then
    echo "shadercross not found: build it with tools/build_shadercross.sh or set SHADERCROSS" >&2
    exit 1
fi

formats=("spirv:spv" "msl:msl")
case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN* | Windows_NT) formats+=("dxil:dxil") ;;
esac

if [[ $# -gt 0 ]]; then
    stems=("$@")
else
    stems=()
    for f in "$root"/shaders/src/*.hlsl; do
        name="$(basename "$f")"
        stems+=("${name%.hlsl}")
    done
fi

failed=0
for stem in "${stems[@]}"; do
    for pair in "${formats[@]}"; do
        dir="${pair%%:*}"
        ext="${pair##*:}"
        mkdir -p "$root/shaders/compiled/$dir"
        if ! "$sc" "$root/shaders/src/$stem.hlsl" -I "$root/shaders/src" -o "$root/shaders/compiled/$dir/$stem.$ext"; then
            echo "FAILED: $stem -> $dir" >&2
            failed=1
        fi
    done
    echo "$stem"
done
exit $failed
