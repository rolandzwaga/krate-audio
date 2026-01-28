<#
.SYNOPSIS
    Runs clang-tidy on the Krate Audio codebase.

.DESCRIPTION
    This script runs clang-tidy static analysis on the project source files.
    It uses the .clang-tidy configuration file in the project root.

    If compile_commands.json is available, it will be used for accurate flags.
    Otherwise, explicit include paths are used as a fallback.

.PARAMETER BuildDir
    Path to the CMake build directory containing compile_commands.json.
    Default: build/windows-x64-release

.PARAMETER Fix
    Apply suggested fixes automatically. Use with caution and review changes.

.PARAMETER Target
    Limit analysis to a specific target: 'dsp', 'iterum', 'disrumpo', or 'all'.
    Default: all

.PARAMETER Quiet
    Suppress progress output, only show warnings and errors.

.EXAMPLE
    .\tools\run-clang-tidy.ps1
    # Run clang-tidy on all source files

.EXAMPLE
    .\tools\run-clang-tidy.ps1 -Target dsp -Fix
    # Run on DSP library only and apply fixes

.EXAMPLE
    .\tools\run-clang-tidy.ps1 -BuildDir build/windows-x64-debug -Quiet
    # Use debug build and suppress progress output
#>

param(
    [string]$BuildDir = "build/windows-x64-release",
    [switch]$Fix,
    [ValidateSet("all", "dsp", "iterum", "disrumpo")]
    [string]$Target = "all",
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $ProjectRoot

# Colors for output
function Write-Info { param([string]$Message) Write-Host "[INFO] $Message" -ForegroundColor Cyan }
function Write-Success { param([string]$Message) Write-Host "[OK] $Message" -ForegroundColor Green }
function Write-Warn { param([string]$Message) Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Err { param([string]$Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }

# Find clang-tidy
$ClangTidyPath = $null
$ClangTidy = Get-Command clang-tidy -ErrorAction SilentlyContinue
if ($ClangTidy) {
    $ClangTidyPath = $ClangTidy.Source
} else {
    # Try common installation paths
    $PossiblePaths = @(
        "C:\Program Files\LLVM\bin\clang-tidy.exe",
        "C:\Program Files (x86)\LLVM\bin\clang-tidy.exe",
        "$env:LOCALAPPDATA\Programs\LLVM\bin\clang-tidy.exe"
    )
    foreach ($Path in $PossiblePaths) {
        if (Test-Path $Path) {
            $ClangTidyPath = $Path
            break
        }
    }
}

if (-not $ClangTidyPath) {
    Write-Err "clang-tidy not found. Please install LLVM/Clang tools."
    Write-Info "Install with: winget install LLVM.LLVM"
    Write-Info "Or download from: https://releases.llvm.org/"
    exit 1
}

if (-not $Quiet) {
    Write-Info "Using clang-tidy: $ClangTidyPath"
}

# Check if compile_commands.json exists
$CompileCommands = Join-Path $BuildDir "compile_commands.json"
$UseCompileCommands = Test-Path $CompileCommands

if ($UseCompileCommands) {
    if (-not $Quiet) {
        Write-Info "Using compile_commands.json from: $BuildDir"
    }
} else {
    if (-not $Quiet) {
        Write-Warn "compile_commands.json not found. Using fallback include paths."
        Write-Info "For best results, configure with Ninja: cmake -G Ninja -B build/ninja ."
    }
}

# Determine source directories based on target
$SourceDirs = @()
$IncludeDirs = @()

switch ($Target) {
    "dsp" {
        $SourceDirs += "dsp/include"
        $IncludeDirs += "dsp/include"
    }
    "iterum" {
        $SourceDirs += "plugins/iterum/src"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/iterum/src"
        $IncludeDirs += "extern/vst3sdk"
    }
    "disrumpo" {
        $SourceDirs += "plugins/disrumpo/src"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/disrumpo/src"
        $IncludeDirs += "extern/vst3sdk"
    }
    "all" {
        $SourceDirs += "dsp/include"
        $SourceDirs += "plugins/iterum/src"
        $SourceDirs += "plugins/disrumpo/src"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/iterum/src"
        $IncludeDirs += "plugins/disrumpo/src"
        $IncludeDirs += "extern/vst3sdk"
    }
}

# Find all source files (only .cpp files for analysis, headers are checked via includes)
$SourceFiles = @()
foreach ($Dir in $SourceDirs) {
    $FullPath = Join-Path $ProjectRoot $Dir
    if (Test-Path $FullPath) {
        # Only analyze .cpp files (headers are analyzed through includes)
        $Files = Get-ChildItem -Path $FullPath -Recurse -Include "*.cpp" |
            Where-Object { $_.FullName -notmatch "extern|build|vst3sdk" }
        $SourceFiles += $Files
    }
}

if ($SourceFiles.Count -eq 0) {
    Write-Warn "No source files found for target: $Target"
    exit 0
}

if (-not $Quiet) {
    Write-Info "Found $($SourceFiles.Count) source files to analyze"
}

# Build base clang-tidy arguments
$BaseArgs = @("--config-file=.clang-tidy")

if ($UseCompileCommands) {
    $BaseArgs += "-p"
    $BaseArgs += $BuildDir
}

if ($Fix) {
    $BaseArgs += "-fix"
    Write-Warn "Fix mode enabled - files will be modified!"
}

# Run clang-tidy on each file
$ErrorCount = 0
$WarningCount = 0
$FileCount = 0

foreach ($File in $SourceFiles) {
    $RelativePath = $File.FullName.Replace($ProjectRoot + "\", "").Replace("\", "/")

    if (-not $Quiet) {
        Write-Host "Analyzing: $RelativePath" -ForegroundColor Gray
    }

    # Build arguments for this file
    $TidyArgs = $BaseArgs.Clone()
    $TidyArgs += $File.FullName

    # Add fallback compiler flags if no compile_commands.json
    if (-not $UseCompileCommands) {
        $TidyArgs += "--"
        $TidyArgs += "-std=c++20"
        $TidyArgs += "-DWIN32"
        $TidyArgs += "-D_WIN32"
        $TidyArgs += "-DNOMINMAX"
        foreach ($Inc in $IncludeDirs) {
            $TidyArgs += "-I$Inc"
        }
    }

    # Capture output, suppressing stderr noise from clang-tidy
    $ErrorActionPreference = "SilentlyContinue"
    $Output = & $ClangTidyPath @TidyArgs 2>&1 | Out-String
    $ErrorActionPreference = "Stop"

    if ($Output) {
        # Filter to only our code paths (plugins/ and dsp/)
        $OurLines = $Output -split "`n" | Where-Object {
            ($_ -match "plugins[/\\]" -or $_ -match "dsp[/\\]") -and
            $_ -notmatch "Processing file" -and
            $_ -notmatch "Error while processing"
        }

        # Count warnings and errors from our code only
        $OurWarnings = ($OurLines | Where-Object { $_ -match "\bwarning:" }).Count
        $OurErrors = ($OurLines | Where-Object { $_ -match "\berror:" -and $_ -notmatch "clang-diagnostic-error" }).Count

        $WarningCount += $OurWarnings
        $ErrorCount += $OurErrors

        if ($OurWarnings -gt 0 -or $OurErrors -gt 0) {
            # Print warnings from our code
            foreach ($line in $OurLines) {
                if ($line -match "warning:|error:|note:") {
                    Write-Host $line
                }
            }
            Write-Host ""
        }
    }

    $FileCount++
}

# Summary
Write-Host ""
Write-Host ("=" * 60)
Write-Info "Clang-Tidy Analysis Complete"
Write-Host "  Files analyzed: $FileCount"

if ($ErrorCount -gt 0) {
    Write-Err "  Errors: $ErrorCount"
} else {
    Write-Success "  Errors: 0"
}

if ($WarningCount -gt 0) {
    Write-Warn "  Warnings: $WarningCount"
} else {
    Write-Success "  Warnings: 0"
}

Write-Host ("=" * 60)

# Exit with error code if issues found
if ($ErrorCount -gt 0) {
    exit 1
}

exit 0
