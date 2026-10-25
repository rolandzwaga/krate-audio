<#
.SYNOPSIS
    Builds test targets with clang-cl and the SDK's /fp:fast: a local proxy for the macOS leg's -ffast-math, for
    targets tools/macos-fastmath-repro.sh cannot build (plugin tests with the VST3 SDK and VSTGUI).

.DESCRIPTION
    A separate Ninja build tree (default F:\tmp\clangfast) configured with clang-cl and lld-link. clang-cl receives the
    SDK's /fp:fast (SMTG_PlatformToolset.cmake:80), which in clang means -ffast-math; /arch:AVX2 lets contraction emit
    FMA as on arm64. Then the given targets are built; run their executables from <BuildDir>\bin.

    Measured 2026-10-25: it reproduced Profundum's ceiling-endpoint red (fma(db, 1 / 12, 1) leaving 5.6e-17) and, on
    the 196bd138 tree, the three Profundum held-load preset reds to four digits of the macOS log.

    BLIND SPOT: a TU listed with the per-file "-fno-fast-math -fno-finite-math-only" (the tests/CMakeLists.txt blocks)
    is NOT fully IEEE here: clang-cl keeps the reciprocal transform that /fp:fast enabled, so x / 24 still becomes
    x * (1 / 24). Xcode's clang does turn it off. A red that needs an IEEE test TU against a fast-math plugin TU (the
    Profundum Pitch Bend Range one-ulp split) therefore does NOT show here.

    lld-link has no m.lib (a Unix -lm that a dependency adds); the script drops an empty stub into the build tree.

    node tools/clean-test-output.js --apply deletes F:\tmp trees, this one included; the next run configures it again
    and builds from scratch.

.PARAMETER Targets
    CMake targets to build, e.g. profundum_tests.
.PARAMETER BuildDir
    The proxy build tree. Configured on first use.
.PARAMETER Source
    The source tree (default: this repository). Point it at a git worktree to reproduce an older commit (a worktree
    has no submodules: junction its extern\vst3sdk to this repository's, and delete the junction before removing it).

.EXAMPLE
    .\tools\clangcl-fastmath-build.ps1 -Targets profundum_tests
    F:\tmp\clangfast\bin\profundum_tests.exe Profundum_StateRoundTrip
#>
param(
    [Parameter(Mandatory = $true)][string[]]$Targets,
    [string]$BuildDir = 'F:\tmp\clangfast',
    [string]$Source = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
)
$ErrorActionPreference = 'Stop'
$vs = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1'
& $vs -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$llvm = 'C:\Program Files\LLVM\bin'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'

if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
    & $cmake -S $Source -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_C_COMPILER=$llvm/clang-cl.exe" "-DCMAKE_CXX_COMPILER=$llvm/clang-cl.exe" "-DCMAKE_LINKER=$llvm/lld-link.exe" `
        '-DCMAKE_C_FLAGS=/arch:AVX2' '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /arch:AVX2'
    if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
}

$stub = Join-Path $BuildDir 'm.lib'
if (-not (Test-Path $stub)) {
    $c = Join-Path $BuildDir 'mstub.c'
    Set-Content -Path $c -Value 'int krate_m_stub;'
    & "$llvm\clang-cl.exe" /nologo /c $c "/Fo$(Join-Path $BuildDir 'mstub.obj')"
    & "$llvm\llvm-lib.exe" "/out:$stub" (Join-Path $BuildDir 'mstub.obj')
}

& $cmake --build $BuildDir --target @Targets
exit $LASTEXITCODE
