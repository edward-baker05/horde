# Builds the SDL_shadercross CLI into tools\bin\, which is where
# cmake\Shaders.cmake looks for it.
#
# You only need this if you are EDITING a shader. Building the game itself uses
# the compiled artifacts committed under shaders\compiled\ and needs no shader
# toolchain.
#
# This is slow the first time - SDL_shadercross has no releases and no packaged
# dependencies, so it vendors and compiles DirectXShaderCompiler, which is an
# LLVM fork. Expect a large clone and a long build. It is a one-time cost.
#
# Windows is also the only platform that can produce SIGNED DXIL, because the
# signing library dxil.dll ships only on Windows. Run this here to regenerate
# shaders\compiled\dxil\.

param([string]$Ref = 'main')

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$Work = Join-Path $Root 'tools\.shadercross'
$Prefix = Join-Path $Root 'tools'

foreach ($tool in @('cmake', 'git')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool not found on PATH"
    }
}

$Src = Join-Path $Work 'src'
$Build = Join-Path $Work 'build'

if (-not (Test-Path $Src)) {
    Write-Host '==> Cloning SDL_shadercross and its vendored dependencies'
    Write-Host '    This clones DirectXShaderCompiler and will take a while.'
    git clone --recurse-submodules --shallow-submodules --depth 1 `
        --branch $Ref https://github.com/libsdl-org/SDL_shadercross.git $Src
    if ($LASTEXITCODE -ne 0) { throw 'clone failed' }
} else {
    Write-Host "==> Reusing existing clone at $Src"
    git -C $Src submodule update --init --recursive --depth 1
}

Write-Host '==> Configuring'
cmake -S $Src -B $Build `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_INSTALL_PREFIX="$Prefix" `
    -DSDLSHADERCROSS_VENDORED=ON `
    -DSDLSHADERCROSS_CLI=ON `
    -DSDLSHADERCROSS_SHARED=OFF `
    -DSDLSHADERCROSS_STATIC=ON `
    -DSDLSHADERCROSS_TESTS=OFF `
    -DSDLSHADERCROSS_INSTALL=ON
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }

Write-Host '==> Building (this is the long part)'
cmake --build $Build --config Release
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

Write-Host "==> Installing into $Prefix\bin"
cmake --install $Build --config Release
if ($LASTEXITCODE -ne 0) { throw 'install failed' }

Write-Host ''
Write-Host "Done. shadercross is at $Prefix\bin\shadercross.exe"
Write-Host 'Reconfigure the project to pick it up.'
