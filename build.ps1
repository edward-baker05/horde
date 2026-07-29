# Configure, build, and run horde on Windows.
# Usage: .\build.ps1 [debug|release]
param(
    [ValidateSet('debug', 'release')]
    [string]$Mode = 'debug'
)

$ErrorActionPreference = 'Stop'

$Config = if ($Mode -eq 'release') { 'Release' } else { 'Debug' }

cmake --preset windows
cmake --build --preset "windows-$Mode"
Push-Location ".\build\windows\bin\$Config"
try {
    & ".\horde.exe"
} finally {
    Pop-Location
}
