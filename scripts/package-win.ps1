<#
.SYNOPSIS
    Assemble the portable Windows distribution from a finished build tree.

.DESCRIPTION
    Produces a self-contained directory that can be copied to any 64bit Windows
    machine. Nothing here depends on WSL, MSYS or Cygwin, and neither a compiler
    nor a Python interpreter is bundled -- the distribution instead states its
    version contract in MANIFEST.txt and checks it at run time in klee-env.cmd.

    Layout:

        <OutDir>\
          bin\                 klee.exe, kleaver.exe, klee-exec-tree.exe,
                               ktest-gen.exe, libz3.dll and the MSVC runtime
                               DLLs libz3 needs, plus the Python tools and
                               their .cmd shims
          lib\klee\runtime\    libklee*_{64,arm32}_Release.bca
          include\klee\        klee.h
          scripts\             klee-cm.ps1
          klee-env.cmd
          MANIFEST.txt
          README.md

.PARAMETER BuildDir
    A configured and built KLEE build tree.

.PARAMETER OutDir
    Directory to create. Removed first if it already exists.

.PARAMETER Z3Dll
    libz3.dll to ship. Defaults to the one next to the Z3 import library that
    the build linked against, recorded in CMakeCache.txt.

.PARAMETER SourceDir
    KLEE source tree. Defaults to the parent of this script's directory.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $BuildDir,
    [Parameter(Mandatory = $true)][string] $OutDir,
    [string] $Z3Dll,
    [string] $SourceDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $SourceDir) {
    $SourceDir = Split-Path -Parent $PSScriptRoot
}

$BuildDir  = (Resolve-Path -LiteralPath $BuildDir).Path
$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path

function Read-CMakeCacheEntry {
    param([string] $Name)

    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache)) {
        throw "No CMakeCache.txt in $BuildDir -- is that a build tree?"
    }
    # Cache lines are NAME:TYPE=VALUE. Match the name exactly so that, say,
    # LLVM_DIR does not also match LLVM_DIR_SOMETHING.
    $line = Select-String -LiteralPath $cache -Pattern "^$([regex]::Escape($Name)):[^=]*=(.*)$" |
            Select-Object -First 1
    if (-not $line) { return $null }
    return $line.Matches[0].Groups[1].Value
}

# ---------------------------------------------------------------------------
# Locate what goes in
# ---------------------------------------------------------------------------

$binDir = Join-Path $BuildDir 'bin'
if (-not (Test-Path -LiteralPath $binDir)) {
    throw "No bin\ directory in $BuildDir -- has the build run?"
}

$executables = @('klee.exe', 'kleaver.exe', 'klee-exec-tree.exe', 'ktest-gen.exe')
foreach ($exe in $executables) {
    if (-not (Test-Path -LiteralPath (Join-Path $binDir $exe))) {
        throw "Missing $exe in $binDir -- the build is incomplete."
    }
}

# Only the Release bitcode is shipped. The other build types are useful when
# debugging KLEE itself, and quadruple the size of the distribution.
$runtimeSrc = Join-Path $BuildDir 'runtime\lib'
if (-not (Test-Path -LiteralPath $runtimeSrc)) {
    throw "No runtime\lib in $BuildDir -- the runtime bitcode was not built."
}
$bcaFiles = @(Get-ChildItem -LiteralPath $runtimeSrc -Filter '*_Release.bca' -File)
if ($bcaFiles.Count -eq 0) {
    throw "No *_Release.bca under $runtimeSrc. Configure with -DKLEE_RUNTIME_BUILD_TYPE=Release."
}

# The architectures actually present, derived from the file names rather than
# from the CMake cache, so MANIFEST.txt describes what is shipped rather than
# what was requested.
$architectures = $bcaFiles |
    ForEach-Object { if ($_.BaseName -match '_([^_]+)_Release$') { $Matches[1] } } |
    Sort-Object -Unique
foreach ($required in @('64', 'arm32')) {
    if ($architectures -notcontains $required) {
        throw "No $required runtime bitcode was built; found: $($architectures -join ', ')"
    }
}

if (-not $Z3Dll) {
    $z3Lib = Read-CMakeCacheEntry 'Z3_LIBRARIES'
    if (-not $z3Lib) {
        throw 'Z3_LIBRARIES is not in the CMake cache and -Z3Dll was not given.'
    }
    # The import library sits in either lib\ or bin\ of the Z3 distribution;
    # the DLL is always in bin\.
    $z3Root = Split-Path -Parent (Split-Path -Parent $z3Lib)
    $Z3Dll  = Join-Path $z3Root 'bin\libz3.dll'
}
if (-not (Test-Path -LiteralPath $Z3Dll)) {
    throw "libz3.dll not found at $Z3Dll"
}

# ---------------------------------------------------------------------------
# Version contract inputs
# ---------------------------------------------------------------------------

$llvmDir = Read-CMakeCacheEntry 'LLVM_DIR'
$llvmVersion = 'unknown'
$llvmMajor = 0
if ($llvmDir) {
    # LLVM_DIR points at the directory holding LLVMConfig.cmake, which sets
    # PACKAGE_VERSION. Reading it avoids depending on llvm-config.exe, which the
    # Windows release does ship but which a relocated tree may not.
    $configFile = Join-Path $llvmDir 'LLVMConfig.cmake'
    if (Test-Path -LiteralPath $configFile) {
        $m = Select-String -LiteralPath $configFile -Pattern '^set\(LLVM_PACKAGE_VERSION\s+"?([0-9.]+)' |
             Select-Object -First 1
        if ($m) {
            $llvmVersion = $m.Matches[0].Groups[1].Value
            $llvmMajor = [int]($llvmVersion.Split('.')[0])
        }
    }
}
if ($llvmMajor -eq 0) {
    throw "Could not determine the LLVM version from $llvmDir; MANIFEST.txt would be wrong."
}

$z3Version = 'unknown'
$z3FileVersion = (Get-Item -LiteralPath $Z3Dll).VersionInfo.FileVersion
if ($z3FileVersion) { $z3Version = $z3FileVersion.Trim() }

Push-Location $SourceDir
try {
    $kleeCommit = (& git rev-parse HEAD 2>$null)
    $kleeDescribe = (& git describe --always --dirty 2>$null)
} finally {
    Pop-Location
}
if (-not $kleeCommit) { $kleeCommit = 'unknown' }
if (-not $kleeDescribe) { $kleeDescribe = 'unknown' }

$kleeVersion = Read-CMakeCacheEntry 'CMAKE_PROJECT_VERSION'
if (-not $kleeVersion) { $kleeVersion = '3.3-pre' }

# ---------------------------------------------------------------------------
# Build the tree
# ---------------------------------------------------------------------------

if (Test-Path -LiteralPath $OutDir) {
    Remove-Item -LiteralPath $OutDir -Recurse -Force
}
$null = New-Item -ItemType Directory -Path $OutDir -Force
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

$outBin     = Join-Path $OutDir 'bin'
$outRuntime = Join-Path $OutDir 'lib\klee\runtime'
$outInclude = Join-Path $OutDir 'include\klee'
$outScripts = Join-Path $OutDir 'scripts'
foreach ($d in @($outBin, $outRuntime, $outInclude, $outScripts)) {
    $null = New-Item -ItemType Directory -Path $d -Force
}

foreach ($exe in $executables) {
    Copy-Item -LiteralPath (Join-Path $binDir $exe) -Destination $outBin
}
Copy-Item -LiteralPath $Z3Dll -Destination $outBin

# KLEE itself is linked against the static CRT, matching the /MT prebuilt LLVM,
# so it needs no redistributable. libz3.dll is built /MD and does, so ship the
# MSVC runtime DLLs it imports.
#
# Prefer the copies shipped alongside libz3.dll: they are the exact versions it
# was built and tested against, whereas System32 holds whatever the build
# machine happens to have. Fall back to System32 for anything absent.
$crtDlls = @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
             'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll',
             'vcruntime140.dll', 'vcruntime140_1.dll', 'vcruntime140_threads.dll')
$z3BinDir = Split-Path -Parent $Z3Dll
$system32 = Join-Path $env:SystemRoot 'System32'
$missingCrt = @()
foreach ($dll in $crtDlls) {
    $source = Join-Path $z3BinDir $dll
    if (-not (Test-Path -LiteralPath $source)) {
        $source = Join-Path $system32 $dll
    }
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $outBin
    } else {
        $missingCrt += $dll
    }
}
# The first two are what libz3.dll actually imports; the rest are satellites
# that only some builds pull in, so their absence is not an error.
foreach ($required in @('msvcp140.dll', 'vcruntime140.dll')) {
    if ($missingCrt -contains $required) {
        throw "$required, which libz3.dll needs, is in neither $z3BinDir nor $system32."
    }
}

foreach ($bca in $bcaFiles) {
    Copy-Item -LiteralPath $bca.FullName -Destination $outRuntime
}

Copy-Item -LiteralPath (Join-Path $SourceDir 'include\klee\klee.h') -Destination $outInclude

# The Python tools are shipped as sources next to a .cmd shim. lit and cmd both
# refuse to execute an extension-less script no matter the file association, so
# the shim is what makes them usable.
foreach ($tool in @('ktest-tool', 'klee-stats')) {
    Copy-Item -LiteralPath (Join-Path $SourceDir "tools\$tool\$tool") `
              -Destination (Join-Path $outBin "$tool.py")
    $shim = @"
@echo off
rem Resolve a Python 3 interpreter: the py launcher first, then python on PATH.
setlocal
py -3 -c "" >nul 2>&1
if not errorlevel 1 (
    py -3 "%~dp0$tool.py" %*
    exit /b %errorlevel%
)
python -c "" >nul 2>&1
if not errorlevel 1 (
    python "%~dp0$tool.py" %*
    exit /b %errorlevel%
)
echo ERROR: $tool needs Python 3, which was not found. 1>&2
echo Install it from https://www.python.org/downloads/ or the Microsoft Store. 1>&2
exit /b 1
"@
    Set-Content -LiteralPath (Join-Path $outBin "$tool.cmd") -Value $shim -Encoding ASCII
}

# ---------------------------------------------------------------------------
# klee-env.cmd -- PATH setup and the version contract check
# ---------------------------------------------------------------------------

$maxClang = $llvmMajor
$minClang = 15

$kleeEnv = @"
@echo off
rem Set up a shell for the portable KLEE distribution and check that the system
rem toolchain can produce bitcode this klee.exe can read. See MANIFEST.txt.
set "KLEE_ROOT=%~dp0"
if "%KLEE_ROOT:~-1%"=="\" set "KLEE_ROOT=%KLEE_ROOT:~0,-1%"
set "PATH=%KLEE_ROOT%\bin;%PATH%"

rem Belt and braces: klee.exe finds ..\lib\klee\runtime relative to itself, so
rem this is only load-bearing if bin\klee.exe is moved out of the layout.
set "KLEE_RUNTIME_LIBRARY_PATH=%KLEE_ROOT%\lib\klee\runtime"

where clang >nul 2>&1
if errorlevel 1 (
    echo NOTE: no clang on PATH. You need one to compile bitcode for klee.
    echo       klee.exe reads bitcode from clang $minClang through $maxClang; see MANIFEST.txt.
    goto :eof
)

rem clang prints e.g. "clang version 22.1.8" or "Ubuntu clang version 14.0.0-1",
rem so scan the tokens for the first one starting with a digit rather than
rem assuming a fixed field position.
set "CLANG_MAJOR="
for /f "tokens=*" %%v in ('clang --version 2^>nul') do (
    if not defined CLANG_MAJOR call :parse_version %%v
)
if not defined CLANG_MAJOR (
    echo WARNING: could not parse the version of the clang on PATH.
    goto :eof
)

if %CLANG_MAJOR% GTR $maxClang (
    echo ERROR: clang %CLANG_MAJOR% is newer than the LLVM $llvmMajor this klee.exe links.
    echo        LLVM reads older bitcode but never newer, so its output will not load.
    echo        Use clang $maxClang or older. See MANIFEST.txt.
    exit /b 1
)
if %CLANG_MAJOR% LSS $minClang (
    echo WARNING: clang %CLANG_MAJOR% is older than the supported minimum $minClang.
    echo          Bitcode should still load, but this combination is untested.
)
goto :eof

:parse_version
for %%t in (%*) do (
    if not defined CLANG_MAJOR (
        for /f "delims=.0123456789" %%x in ("%%t") do set "NOT_A_VERSION=%%x"
        if not defined NOT_A_VERSION for /f "delims=." %%m in ("%%t") do set "CLANG_MAJOR=%%m"
        set "NOT_A_VERSION="
    )
)
goto :eof
"@
Set-Content -LiteralPath (Join-Path $OutDir 'klee-env.cmd') -Value $kleeEnv -Encoding ASCII

# ---------------------------------------------------------------------------
# scripts\klee-cm.ps1 -- compile firmware for Cortex-M, then run klee on it
# ---------------------------------------------------------------------------

$kleeCm = @'
<#
.SYNOPSIS
    Compile a C source for a Cortex-M target and symbolically execute one of its
    functions.

.DESCRIPTION
    Wraps the two-step flow of compiling to bitcode and running klee on it.

    --external-calls=none is not incidental: an x86-64 host cannot JIT-call a
    function with the ARM ABI, so any call leaving the bitcode has to be
    answered by a bitcode stub rather than by the real implementation.

.EXAMPLE
    .\klee-cm.ps1 -Source sensor.c -EntryPoint convert_reading

.EXAMPLE
    .\klee-cm.ps1 -Source sensor.c -EntryPoint convert_reading -Cpu m7
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Source,
    [Parameter(Mandatory = $true)][string] $EntryPoint,
    [ValidateSet('m0', 'm3', 'm4', 'm7')][string] $Cpu = 'm0',
    [string] $OutputDir,
    [string[]] $ClangArgs = @(),
    [Parameter(ValueFromRemainingArguments = $true)][string[]] $KleeArgs = @()
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# All Cortex-M profiles emit the same LLVM data layout, so one arm32 runtime
# serves every one of them; the triple only changes which instructions clang is
# willing to select.
$triples = @{
    'm0' = 'thumbv6m-none-eabi'
    'm3' = 'thumbv7m-none-eabi'
    'm4' = 'thumbv7em-none-eabi'
    'm7' = 'thumbv7em-none-eabihf'
}
$triple = $triples[$Cpu]

$Source = (Resolve-Path -LiteralPath $Source).Path
$bitcode = [System.IO.Path]::ChangeExtension($Source, '.bc')

$clang = Get-Command clang -ErrorAction SilentlyContinue
if (-not $clang) {
    throw 'No clang on PATH. Run klee-env.cmd first, or install a system clang.'
}

$compile = @(
    "--target=$triple"
    '-emit-llvm', '-c', '-g', '-O0'
    '-Xclang', '-disable-O0-optnone'
    '-ffreestanding'
    $ClangArgs
    $Source
    '-o', $bitcode
)
Write-Host "clang $($compile -join ' ')" -ForegroundColor DarkGray
& $clang.Path @compile
if ($LASTEXITCODE -ne 0) { throw "clang failed with exit code $LASTEXITCODE" }

$run = @('--libc=klee', '--external-calls=none', "--entry-point=$EntryPoint")
if ($OutputDir) { $run += "--output-dir=$OutputDir" }
$run += $KleeArgs
$run += $bitcode

Write-Host "klee $($run -join ' ')" -ForegroundColor DarkGray
& klee @run
exit $LASTEXITCODE
'@
Set-Content -LiteralPath (Join-Path $outScripts 'klee-cm.ps1') -Value $kleeCm -Encoding UTF8

# ---------------------------------------------------------------------------
# MANIFEST.txt and README.md
# ---------------------------------------------------------------------------

$manifest = @"
KLEE portable Windows distribution
==================================

KLEE version      $kleeVersion
KLEE commit       $kleeCommit
KLEE describe     $kleeDescribe
LLVM version      $llvmVersion
Z3 version        $z3Version
Bitcode arches    $($architectures -join ', ')
Built             $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm:ss')) UTC
Host              $env:COMPUTERNAME

Version contract
----------------

klee.exe links LLVM $llvmVersion. LLVM reads bitcode produced by its own version
or older, and never newer, so bitcode must come from clang $minClang..$maxClang.
klee-env.cmd checks the clang on PATH against that range: it errors above
$maxClang and warns below $minClang.

The .bca runtime bitcode shipped here was produced by the clang from the same
LLVM $llvmVersion release at build time. It is pinned to this klee.exe and does not
depend on whichever clang you have installed.

Not bundled
-----------

No C compiler and no Python interpreter. Install clang yourself for compiling
bitcode, and Python 3 if you want ktest-tool or klee-stats. klee-stats needs the
'tabulate' pip package for its table output; --to-csv and --grafana work without
it.
"@
Set-Content -LiteralPath (Join-Path $OutDir 'MANIFEST.txt') -Value $manifest -Encoding UTF8

$readme = @"
# KLEE for Windows -- portable distribution

Native Windows build. No WSL, MSYS or Cygwin involved, and nothing outside this
directory is written to. Copy it anywhere.

## Setup

``````
klee-env.cmd
``````

That puts ``bin`` on ``PATH`` and checks that the clang you have can produce
bitcode this ``klee.exe`` can read. See ``MANIFEST.txt`` for the exact range.

## Native x86-64

``````
clang -emit-llvm -c -g -O0 -Xclang -disable-O0-optnone example.c -o example.bc
klee example.bc
``````

## Cortex-M firmware

``````
scripts\klee-cm.ps1 -Source sensor.c -EntryPoint convert_reading -Cpu m4
``````

``-Cpu`` accepts ``m0``, ``m3``, ``m4`` and ``m7``. They all share one LLVM data
layout, so the single ``arm32`` runtime covers every profile; the triple only
decides which instructions clang selects.

Cross-architecture external calls are impossible by construction -- an x86-64
host cannot JIT-call an ARM-ABI function -- so the wrapper passes
``--external-calls=none``. Anything the firmware calls out to needs a bitcode
stub.

## Inspecting results

``````
ktest-tool klee-last\test000001.ktest
klee-stats klee-last
``````

Both need Python 3 on PATH; the ``.cmd`` shims find it via ``py -3`` or
``python``.

## klee-last

``klee-last`` is a directory symlink, and creating one on Windows needs
Developer Mode or an elevated shell. KLEE only warns if it cannot, and the
numbered ``klee-out-N`` directories are unaffected -- the warning is cosmetic.
"@
Set-Content -LiteralPath (Join-Path $OutDir 'README.md') -Value $readme -Encoding UTF8

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

$files = @(Get-ChildItem -LiteralPath $OutDir -Recurse -File)
$bytes = ($files | Measure-Object -Property Length -Sum).Sum

Write-Host ''
Write-Host "Packaged $OutDir" -ForegroundColor Green
Write-Host ("  {0} files, {1:N1} MB" -f $files.Count, ($bytes / 1MB))
Write-Host "  KLEE $kleeVersion ($kleeDescribe), LLVM $llvmVersion, Z3 $z3Version"
Write-Host "  runtime bitcode: $($architectures -join ', ')"
