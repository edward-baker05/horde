import json
import glob
import os
from pathlib import Path

def main():
    root = Path(__file__).resolve().parent.parent
    build_dir = root / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    src_dir = root / "src"
    cpp_files = sorted(src_dir.rglob("*.cpp"))

    include_dirs = [
        "src",
        "build/windows/_deps/sdl3-src/include",
        "build/windows/_deps/sdl3_image-src/include",
        "build/windows/_deps/glm-src",
        "build/windows/_deps/imgui-src",
        "build/windows/_deps/imgui-src/backends",
        "build/_deps/sdl3-src/include",
        "build/_deps/sdl3_image-src/include",
        "build/_deps/glm-src",
        "build/_deps/imgui-src",
        "build/_deps/imgui-src/backends",
    ]

    base_args = ["clang++", "-std=c++20"]
    for inc in include_dirs:
        base_args.append(f"-I{inc}")

    # Auto-detect MSVC standard library and Windows SDK on Windows
    # Keeps .clangd completely clean and free of machine-specific absolute paths.
    if os.name == "nt":
        vs_candidates = sorted(glob.glob(r"C:\Program Files*\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*"))
        if vs_candidates:
            preferred = [c for c in vs_candidates if "2022\\Community" in c]
            chosen = preferred[-1] if preferred else vs_candidates[-1]
            msvc_inc = Path(chosen) / "include"
            if msvc_inc.exists():
                base_args.append(f"-isystem{msvc_inc.as_posix()}")

        sdk_candidates = sorted(glob.glob(r"C:\Program Files*\Windows Kits\10\Include\*"))
        if sdk_candidates:
            latest_sdk = Path(sdk_candidates[-1])
            for sub in ["ucrt", "shared", "um"]:
                p = latest_sdk / sub
                if p.exists():
                    base_args.append(f"-isystem{p.as_posix()}")

    base_args.extend(["-DWIN32", "-D_WINDOWS"])

    entries = []
    root_str = root.as_posix()

    for cpp_file in cpp_files:
        rel_path = cpp_file.relative_to(root).as_posix()
        entries.append({
            "directory": root_str,
            "arguments": base_args + ["-c", rel_path],
            "file": rel_path
        })

    out_file = build_dir / "compile_commands.json"
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(entries, f, indent=2)

    print(f"Generated {out_file} with {len(entries)} entries.")

if __name__ == "__main__":
    main()
