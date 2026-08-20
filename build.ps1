# Configure, build, and run horde on Windows.
# Usage: .\build.ps1 [debug|release]
param(
    [ValidateSet('debug', 'release')]
    [string]$Mode = 'debug'
)

$ErrorActionPreference = 'Stop'

$Config = if ($Mode -eq 'release') { 'Release' } else { 'Debug' }

cmake --preset windows
python tools/generate_compile_commands.py
cmake --build --preset "windows-$Mode"

# No need to change directory first: assets and shaders are resolved relative to
# the executable via SDL_GetBasePath(), not the working directory.
& ".\build\windows\bin\$Config\horde.exe"
