#Requires -Version 5.1
<#
.SYNOPSIS
    Build script for Werfs (WAML + C++20 + MVVM).

.DESCRIPTION
    Drives cl.exe / link.exe / rc.exe directly. CMake and Ninja are deliberately
    not used here: both parse child-process stdout through pipes, and in a
    restricted sandbox those pipes cannot be created, so `configure` hangs
    instead of failing. Invoking the toolchain directly keeps stdio inherited.
    CMakeLists.txt is maintained in parallel for normal machines.

    Switching to PowerShell also removes two classic cmd.exe hazards that cost
    real debugging time in the previous build.bat:
      - "%~dp0" ends with a backslash, so /I"%~dp0" swallowed the next argument
        and produced "D8003: missing source filename";
      - /Fo"dir\" likewise let the trailing backslash escape the quote, dropping
        .obj files into the current directory instead of the object directory.
    Both disappear because arguments are passed as an array, not a parsed line.

.PARAMETER Target
    What to build. With no argument every sample is built (release), because
    running the samples is what the project is for; the harnesses and tools are
    reached through 'all'.

      samples   DEFAULT. Every sample below, optimised, WAML embedded in the exe
      basic     basic controls sample  (waml_basic.exe)
      list      collections + layout   (waml_list.exe)
      binding   the four binding modes (waml_binding.exe)
      theming   tokens + Light/Dark    (waml_theming.exe)
      usercontrol  nested data sources (waml_usercontrol.exe)
      editor    the multi-tab editor   (waml_editor.exe)
      all       samples plus every tool and test binary
      debug     one sample with symbols and WAML read from disk at runtime, so
                editing markup needs no rebuild (-Sample picks which)
      tests     unit/integration tests
      bench     performance baseline
      snapshot  D2D render + determinism check
      interact  headless input driver
      probe     resource gate (also runs the probe afterwards)
      probedbg  same gate, built in debug
      res       resource compilation only
      icon      regenerate the app icon
      dpishot   DPI screenshot tool

.PARAMETER Clean
    Delete the output directory first.

.PARAMETER OutDir
    Where to put artifacts. Defaults to $env:WUI_OUT, else <repo>/build.

    Pointing this outside the repository is the reliable fix for SmartScreen
    warnings on binaries produced by automated sessions: executables created by
    such a session can inherit an unresolved-SID entry (S-1-4-*) in their
    permissions, which makes Windows Defender show "Windows protected your PC"
    on every launch even though the binary is unsigned-but-fine and
    byte-identical to one that launches silently.

    Diagnose with:  icacls <file>.exe | findstr /i "S-1-4"

.EXAMPLE
    pwsh -File build.ps1
    pwsh -File build.ps1 all
    pwsh -File build.ps1 debug -Clean

.EXAMPLE
    # Keep artifacts out of the repository (recommended in automated sessions).
    pwsh -File build.ps1 all -OutDir D:\wui-dist
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('samples', 'all', 'debug', 'tests', 'bench', 'snapshot',
                 'interact', 'probe', 'probedbg', 'res', 'icon', 'dpishot',
                 'basic', 'list', 'binding', 'theming', 'usercontrol', 'editor')]
    [string]$Target = 'samples',

    [switch]$Clean,

    [string]$OutDir = $env:WUI_OUT
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Framework  = Join-Path $ProjectDir 'framework'
$SamplesRoot = Join-Path $ProjectDir 'samples'
$IconSourceDir = Join-Path $SamplesRoot 'basic\resources'
$SharedSample = Join-Path $SamplesRoot 'shared'
$SampleHostSource = Join-Path $SharedSample 'SampleHost.cpp'

if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $ProjectDir 'build'
}
$OutDir = [System.IO.Path]::GetFullPath($OutDir)

if ($Clean -and (Test-Path -LiteralPath $OutDir)) {
    Write-Host "Cleaning $OutDir ..." -ForegroundColor Yellow
    Remove-Item -LiteralPath $OutDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# ---------------------------------------------------------------------------
# MSVC environment
#
# A .bat cannot modify the parent PowerShell process's environment, so capture
# `set` output from cmd and import it. Without this, cl/link/rc are not on PATH.
# ---------------------------------------------------------------------------
function Find-VcVars {
    # vswhere is the documented way to locate a VS install and does not depend
    # on any environment variable. Note: $env:ProgramFiles and friends can be
    # empty in some shells/sandboxes, so never build a path from them blind --
    # Join-Path throws on a null root before any filtering can happen.
    $vswhereCandidates = @('C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe')
    foreach ($root in @(${env:ProgramFiles(x86)}, $env:ProgramW6432, 'C:\Program Files (x86)')) {
        if ([string]::IsNullOrWhiteSpace($root)) { continue }
        $vswhereCandidates += Join-Path $root 'Microsoft Visual Studio\Installer\vswhere.exe'
    }
    $vswhereCandidates = $vswhereCandidates |
        Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
        Select-Object -Unique

    foreach ($vswhere in $vswhereCandidates) {
        $installPath = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null |
            Where-Object { $_ -and $_.Trim() } |
            Select-Object -First 1
        if ($installPath) {
            $candidate = Join-Path $installPath.Trim() 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }

    # Fall back to the conventional locations. $env:ProgramFiles can be empty
    # in some shells/sandboxes, so the literal default is tried as well and
    # every root is null-checked before joining.
    $roots = @('C:\Program Files', $env:ProgramFiles, $env:ProgramW6432) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_) } |
        Select-Object -Unique
    foreach ($root in $roots) {
        foreach ($edition in @('Enterprise', 'Professional', 'Community', 'BuildTools')) {
            $candidate = Join-Path $root "Microsoft Visual Studio\2022\$edition\VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path -LiteralPath $candidate) { return $candidate }
        }
    }
    return $null
}

function Import-MsvcEnvironment {
    $vcvars = Find-VcVars
    if (-not $vcvars) {
        throw "Visual Studio 2022 with the C++ workload was not found. Install VS 2022 with 'Desktop development with C++', or run this script from a Developer PowerShell."
    }
    Write-Verbose "Using $vcvars"

    # VSLANG forces English diagnostics so CI logs and this script's own output
    # stay parseable regardless of the machine's UI language.
    $env:VSLANG = '1033'
    $lines = cmd /c "call `"$vcvars`" >nul 2>&1 && set"
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($matches[1])" -Value $matches[2]
        }
    }
}

Import-MsvcEnvironment

# ---------------------------------------------------------------------------
# Version stamping
#
# One source feeds VERSIONINFO, the file-properties dialog and the About text.
# CI overrides these with environment variables of the same name.
# ---------------------------------------------------------------------------
function Get-VersionPart {
    param([string]$Name, [string]$Default)
    $v = [Environment]::GetEnvironmentVariable("WUI_VERSION_$Name")
    if ([string]::IsNullOrWhiteSpace($v)) { return $Default }
    return $v
}

$VersionMajor = Get-VersionPart 'MAJOR' '0'
$VersionMinor = Get-VersionPart 'MINOR' '7'
$VersionPatch = Get-VersionPart 'PATCH' '3'
$VersionBuild = Get-VersionPart 'BUILD' '0'
$VersionString = "$VersionMajor.$VersionMinor.$VersionPatch.$VersionBuild"

# ---------------------------------------------------------------------------
# Compiler / linker settings
# ---------------------------------------------------------------------------
$VersionDefs = @(
    "/DWUI_VERSION_MAJOR=$VersionMajor"
    "/DWUI_VERSION_MINOR=$VersionMinor"
    "/DWUI_VERSION_PATCH=$VersionPatch"
    "/DWUI_VERSION_BUILD=$VersionBuild"
)

# Sample code includes framework headers by plain name ("Mvvm.h"), so these
# include paths are what make a sample depend on the framework rather than sit
# inside it.
$Includes = @(
    (Join-Path $Framework 'src')
    (Join-Path $Framework 'src\core')
    (Join-Path $Framework 'src\mvvm')
    (Join-Path $Framework 'src\waml')
    (Join-Path $Framework 'src\render')
    (Join-Path $Framework 'src\platform')
    (Join-Path $SamplesRoot 'shared')
    (Join-Path $ProjectDir 'tests')
) | ForEach-Object { "/I$_" }

$CommonDefs = @(
    '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', '/DUNICODE', '/D_UNICODE'
) + $VersionDefs + $Includes

$CommonCompile = @(
    '/nologo', '/c', '/std:c++20', '/EHsc', '/utf-8', '/W4',
    '/permissive-', '/MP', '/FS', '/Zc:__cplusplus', '/Zc:preprocessor'
) + $CommonDefs

# Debug: symbols, no optimisation, markup from disk.
# /MDd must pair with /D_DEBUG; mixing /MD with /D_DEBUG fails to link
# _CrtDbgReport.
$DebugFlags = @('/Od', '/Zi', '/DEBUG', '/MDd', '/D_DEBUG', '/DWUI_WAML_LOOSE_FILES=1')
# Release: optimised, markup from resources, assertions off.
$ReleaseFlags = @('/O2', '/Zi', '/MD', '/DNDEBUG')

# Framework sources: UI toolkit only, no knowledge of any sample.
$CoreSources = @(
    'src\core\Core.cpp'
    'src\core\Widget.cpp'
    'src\core\Types.cpp'
    'src\core\Controls.cpp'
    'src\core\Overlay.cpp'
    'src\mvvm\Binding.cpp'
    'src\mvvm\Converters.cpp'
    'src\waml\Diagnostics.cpp'
    'src\waml\Parser.cpp'
    'src\waml\StyleTriggers.cpp'
    'src\core\Animation.cpp'
    'src\core\VisualStates.cpp'
    'src\core\TextInput.cpp'
    'src\render\SoftwareRender.cpp'
    'src\platform\Resources.cpp'
    'src\platform\FileDialogWin32.cpp'
    'src\platform\Image.cpp'
    'src\platform\ImageWin32.cpp'
    'src\platform\TextInputWin32.cpp'
) | ForEach-Object { Join-Path $Framework $_ }

$RenderSources = @(Join-Path $Framework 'src\render\D2DRender.cpp')

$Libs = @(
    'd2d1.lib', 'dwrite.lib', 'comctl32.lib', 'ole32.lib', 'user32.lib',
    'gdi32.lib', 'shell32.lib', 'version.lib', 'advapi32.lib', 'uuid.lib', 'ole32.lib'
)

$Manifest = Join-Path $IconSourceDir 'app.manifest'
$IconFile = Join-Path $IconSourceDir 'app.ico'
# The shared .res used by tools (probe, snapshot). Samples compile their own via
# Build-SampleResource, because each embeds a different payload id.
$ResourceScript = Join-Path $IconSourceDir 'app.rc'
$ResObj = Join-Path $OutDir 'objres\app.res'

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Invoke-Tool {
    param(
        [Parameter(Mandatory)][string]$Exe,
        # Not Mandatory: some tools (the probe) take no arguments, and a
        # Mandatory binding rejects an empty array.
        [string[]]$Arguments = @()
    )
    # Out-Host matters: without it the toolchain's stdout enters the success
    # stream and becomes the caller's return value, so a function that returns
    # an exe path would instead return cl.exe's file list.
    & $Exe @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "$Exe failed with exit code $LASTEXITCODE"
    }
}

function Get-ConfigFlags {
    param([string]$Config)
    if ($Config -eq 'debug') { return $script:DebugFlags }
    return $script:ReleaseFlags
}

# Compiles and links one executable.
#
# Each configuration keeps its own object directory so switching between
# release and debug never reuses objects built with a different
# WUI_WAML_LOOSE_FILES or optimisation setting.
function Build-Executable {
    param(
        [Parameter(Mandatory)][string]$ExeName,   # waml_tests
        [Parameter(Mandatory)][string]$ObjName,   # tests
        [Parameter(Mandatory)][string]$Config,    # release | debug
        [Parameter(Mandatory)][string[]]$Sources,
        [string[]]$ExtraLink = @(),
        # Which .res to embed. Defaults to the gallery's, because the framework
        # tools (tests/bench/probe...) share it; a sample passes its own.
        [string]$ResourceObj = $ResObj,
        # Which manifest to embed. A sample has its own copy of the same file.
        [string]$ManifestPath = $Manifest,
        # Extra include directories. A sample needs its own directory and its
        # own resources/ so its Resource.h (with its own payload ids) wins.
        [string[]]$ExtraIncludes = @()
    )

    $flags = Get-ConfigFlags $Config
    $objDir = Join-Path $OutDir "$Config\$ObjName"
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null

    $includeFlags = $ExtraIncludes | ForEach-Object { "/I$_" }
    # Extra (sample-local) includes go FIRST: a sample's own Resource.h must win
    # over the gallery's, since both define IDR_WAML_* with different ids.
    $compileArgs = @('/nologo', '/c', '/std:c++20', '/EHsc', '/utf-8', '/W4',
                     '/permissive-', '/MP', '/FS', '/Zc:__cplusplus', '/Zc:preprocessor') +
                   $CommonDefs + $includeFlags + $flags + @("/Fo$objDir\") + $Sources
    Invoke-Tool 'cl' $compileArgs

    $objects = Get-ChildItem -LiteralPath $objDir -Filter '*.obj' |
        ForEach-Object { $_.FullName }
    if ($objects.Count -eq 0) {
        throw "No object files were produced in $objDir"
    }

    $exePath = Join-Path $OutDir "$Config\$ExeName.exe"
    $linkArgs = @(
        '/nologo', '/DEBUG', "/OUT:$exePath", '/INCREMENTAL:NO',
        '/MANIFEST:EMBED', "/MANIFESTINPUT:$ManifestPath"
    ) + $objects + @($ResourceObj) + $Libs + $ExtraLink
    Invoke-Tool 'link' $linkArgs

    return $exePath
}

# ---------------------------------------------------------------------------
# Resource compilation
#
# rc.exe resolves the .rc's relative paths against the .rc location, so this
# works from any working directory. A failure must abort: an exe linked without
# its .res still runs, but silently ships with no markup, no icon and no
# version information.
# ---------------------------------------------------------------------------
function Build-Resource {
    if (-not (Test-Path -LiteralPath $IconFile)) {
        Write-Host 'resources\app.ico is missing, generating it' -ForegroundColor Yellow
        Build-Icon
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ResObj) | Out-Null

    $rcArgs = @(
        '/nologo'
        "/fo$ResObj"
        "/dWUI_VERSION_MAJOR=$VersionMajor"
        "/dWUI_VERSION_MINOR=$VersionMinor"
        "/dWUI_VERSION_PATCH=$VersionPatch"
        "/dWUI_VERSION_BUILD=$VersionBuild"
        $ResourceScript
    )
    Invoke-Tool 'rc' $rcArgs
    Write-Host "  resources -> $ResObj" -ForegroundColor DarkGray
}

function Build-Icon {
    $objDir = Join-Path $OutDir 'objicon'
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null

    $compileArgs = $CommonCompile + $ReleaseFlags + @("/Fo$objDir\") +
        @(Join-Path $ProjectDir 'tools\MakeIcon.cpp')
    Invoke-Tool 'cl' $compileArgs

    $objects = Get-ChildItem -LiteralPath $objDir -Filter '*.obj' |
        ForEach-Object { $_.FullName }
    $exePath = Join-Path $OutDir 'waml_makeicon.exe'
    $linkArgs = @('/nologo', "/OUT:$exePath", '/INCREMENTAL:NO') + $objects +
        @('gdiplus.lib', 'ole32.lib', 'user32.lib', 'gdi32.lib')
    Invoke-Tool 'link' $linkArgs

    Invoke-Tool $exePath @($IconFile)
    Write-Host "  icon -> $IconFile" -ForegroundColor DarkGray
}

# ---------------------------------------------------------------------------
# Individual targets
# ---------------------------------------------------------------------------
function Build-Tests {
    $exe = Build-Executable -ExeName 'waml_tests' -ObjName 'tests' -Config 'release' `
        -Sources ($CoreSources + (Join-Path $ProjectDir 'tests\Tests.cpp'))
    Write-Host "  tests -> $exe" -ForegroundColor DarkGray
}

function Build-Bench {
    $exe = Build-Executable -ExeName 'waml_bench' -ObjName 'bench' -Config 'release' `
        -Sources ($CoreSources + (Join-Path $ProjectDir 'bench\Bench.cpp'))
    Write-Host "  bench -> $exe" -ForegroundColor DarkGray
}

# The harnesses bind a sample's ViewModel and read its markup ids, so they need
# The harnesses bind samples' ViewModels and read their markup ids, so they need
# those include paths. Kept out of the global list so a sample's own Resource.h
# still wins when building that sample.
$ListSampleDir = Join-Path $SamplesRoot 'list'
$BasicSampleDir = Join-Path $SamplesRoot 'basic'
$BindingSampleDir = Join-Path $SamplesRoot 'binding'
$ThemingSampleDir = Join-Path $SamplesRoot 'theming'
$UserControlSampleDir = Join-Path $SamplesRoot 'usercontrol'
$EditorSampleDir = Join-Path $SamplesRoot 'editor'
# Tools that touch several samples need every sample's headers.
$AllSampleIncludes = @($ListSampleDir, (Join-Path $ListSampleDir 'resources'),
                       $BasicSampleDir, (Join-Path $BasicSampleDir 'resources'),
                       $BindingSampleDir, (Join-Path $BindingSampleDir 'resources'),
                       $ThemingSampleDir, (Join-Path $ThemingSampleDir 'resources'),
                       $UserControlSampleDir, (Join-Path $UserControlSampleDir 'resources'),
                       $EditorSampleDir, (Join-Path $EditorSampleDir 'resources'))

# A sample's ViewModel implementation has to be compiled into the tools that drive
# it. The editor is the first sample with real logic in a .cpp; the others keep
# everything in headers, which is why this list used to be empty.
$SampleVmSources = @(Join-Path $EditorSampleDir 'EditorVm.cpp')

function Build-Snapshot {
    $exe = Build-Executable -ExeName 'waml_snapshot' -ObjName 'snapshot' -Config 'release' `
        -Sources ($CoreSources + $RenderSources + $SampleVmSources +
                  (Join-Path $ProjectDir 'tools\Snapshot.cpp')) `
        -ExtraIncludes $AllSampleIncludes
    Write-Host "  snapshot -> $exe" -ForegroundColor DarkGray
}

function Build-Interact {
    $exe = Build-Executable -ExeName 'waml_interact' -ObjName 'interact' -Config 'release' `
        -Sources ($CoreSources + $SampleVmSources +
                  (Join-Path $ProjectDir 'tools\Interact.cpp')) `
        -ExtraIncludes $AllSampleIncludes
    Write-Host "  interact -> $exe" -ForegroundColor DarkGray
}

function Build-DpiShot {
    $exe = Build-Executable -ExeName 'waml_dpishot' -ObjName 'dpishot' -Config 'release' `
        -Sources ($CoreSources + $RenderSources + (Join-Path $ProjectDir 'tools\DpiShot.cpp')) `
        -ExtraIncludes $AllSampleIncludes
    Write-Host "  dpishot -> $exe" -ForegroundColor DarkGray
}

function Build-Probe {
    param([string]$Config = 'release')
    $exe = Build-Executable -ExeName 'waml_probe' -ObjName 'probe' -Config $Config `
        -Sources ($CoreSources + (Join-Path $ProjectDir 'tools\ResourceProbe.cpp')) `
        -ExtraIncludes $AllSampleIncludes
    Write-Host "  probe -> $exe" -ForegroundColor DarkGray
    return $exe
}

function Build-Sample {
    param(
        [Parameter(Mandatory)][string]$ExeName,  # waml_list
        [Parameter(Mandatory)][string]$ObjName,  # list
        [Parameter(Mandatory)][string]$Dir,      # absolute sample directory
        [Parameter(Mandatory)][string]$ResName,  # resource base name: list
        [string]$Config = 'release'
    )
    $resObj = Build-SampleResource -Dir $Dir -Name $ResName
    # Every .cpp beside App.cpp is part of the sample. A sample whose ViewModel is
    # only a header needs nothing extra; one with real logic (the editor's close
    # flow) keeps it in its own translation unit instead of inlining everything
    # into a 600-line header.
    $sampleSources = @(Get-ChildItem -LiteralPath $Dir -Filter '*.cpp' |
        ForEach-Object { $_.FullName })
    $exe = Build-Executable -ExeName $ExeName -ObjName $ObjName -Config $Config `
        -Sources ($CoreSources + $RenderSources + $SampleHostSource + $sampleSources) `
        -ResourceObj $resObj `
        -ManifestPath (Join-Path $Dir 'resources\app.manifest') `
        -ExtraIncludes @($Dir, (Join-Path $Dir 'resources')) `
        -ExtraLink @('/SUBSYSTEM:WINDOWS')
    Write-Host "  $ObjName -> $exe" -ForegroundColor DarkGray
    return $exe
}
# Sample registry: the single place that knows which samples exist.
#
# Adding a sample means adding one line here. The dispatcher loops over this
# table, so no target needs a hand-written branch.
$SampleRegistry = @(
    @{ Name = 'basic';   Exe = 'waml_basic';   Res = 'basic' }
    @{ Name = 'list';    Exe = 'waml_list';    Res = 'list' }
    # The binding page exists to make a mode difference visible: four columns,
    # one property. It also exercises FallbackValue and UpdateSourceTrigger.
    @{ Name = 'binding'; Exe = 'waml_binding'; Res = 'binding' }
    # The theming page is the only sample that asks the host for a runtime theme
    # switch (HostOptions::themeProperty); everything else stays on Light.
    @{ Name = 'theming'; Exe = 'waml_theming'; Res = 'theming' }
    # The component-scope page: two cards, each with its own DataContext bound
    # from the page, and a badge one level deeper. It exists to prove that a
    # subtree can switch data sources without copying the page's ViewModel.
    @{ Name = 'usercontrol'; Exe = 'waml_usercontrol'; Res = 'usercontrol' }
    # The integration sample: menu bar, command bar, tabs, unsaved prompt and the
    # file dialogs, all on one page. It absorbed the planned `files` sample, whose
    # four blocks (open / save as / folder pick / read preview) were a subset of
    # what the editor needs -- two samples for one capability meant two places to
    # keep the dialog wiring correct.
    @{ Name = 'editor';  Exe = 'waml_editor';  Res = 'editor' }
)

function Build-RegisteredSample {
    param(
        [Parameter(Mandatory)][hashtable]$Entry,
        [string]$Config = 'release'
    )
    return Build-Sample -ExeName $Entry.Exe -ObjName $Entry.Name -ResName $Entry.Res `
        -Dir (Join-Path $SamplesRoot $Entry.Name) -Config $Config
}

function Get-SampleEntry {
    param([Parameter(Mandatory)][string]$Name)
    $entry = $SampleRegistry | Where-Object { $_.Name -eq $Name } | Select-Object -First 1
    if (-not $entry) {
        throw "Unknown sample '$Name'. Known: $(($SampleRegistry | ForEach-Object { $_.Name }) -join ', ')"
    }
    return $entry
}

function Build-AllRegisteredSamples {
    param([string]$Config = 'release')
    foreach ($entry in $SampleRegistry) {
        Build-RegisteredSample -Entry $entry -Config $Config | Out-Null
    }
}

# Builds the resource script of one sample into that sample's own .res file.
# Kept separate from the gallery's Build-Resource because each sample has its
# own app.rc with its own payload ids.
function Build-SampleResource {
    param(
        [Parameter(Mandatory)][string]$Dir,
        [Parameter(Mandatory)][string]$Name
    )
    $rc = Join-Path $Dir 'resources\app.rc'
    $ico = Join-Path $Dir 'resources\app.ico'
    if (-not (Test-Path -LiteralPath $ico)) {
        throw "Missing icon: $ico"
    }
    $resObj = Join-Path $OutDir "objres\$Name.res"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $resObj) | Out-Null
    $rcArgs = @(
        '/nologo'
        "/fo$resObj"
        "/dWUI_VERSION_MAJOR=$VersionMajor"
        "/dWUI_VERSION_MINOR=$VersionMinor"
        "/dWUI_VERSION_PATCH=$VersionPatch"
        "/dWUI_VERSION_BUILD=$VersionBuild"
        $rc
    )
    Invoke-Tool 'rc' $rcArgs
    Write-Host "  $Name resources -> $resObj" -ForegroundColor DarkGray
    return $resObj
}

# ---------------------------------------------------------------------------
# Dispatch
# ---------------------------------------------------------------------------
Write-Host "Werfs $VersionString  |  target=$Target" -ForegroundColor Cyan
Write-Host "output: $OutDir" -ForegroundColor DarkGray

try {
    switch ($Target) {
        # Default: run every sample. This is what people actually want when they
        # type "build" -- the tools and harnesses are for maintainers.
        'samples' {
            Build-AllRegisteredSamples -Config 'release'
        }
        # Everything: samples plus every harness and tool.
        'all' {
            Build-Resource
            Build-AllRegisteredSamples -Config 'release'
            Build-Tests
            Build-Bench
            Build-Snapshot
            Build-Interact
            Build-DpiShot
            Build-Probe -Config 'release' | Out-Null
        }
        # Symbols plus loose markup, so editing a .waml needs no rebuild. Builds
        # the debug variant of every sample silently: samples are only ever run
        # one at a time, so this stays cheap.
        'debug' {
            Build-AllRegisteredSamples -Config 'debug'
        }
        { $_ -in ($SampleRegistry | ForEach-Object { $_.Name }) } {
            Build-RegisteredSample -Entry (Get-SampleEntry $Target) -Config 'release' | Out-Null
        }
        'tests'   { Build-Resource; Build-Tests }
        'bench'   { Build-Resource; Build-Bench }
        'snapshot' { Build-Resource; Build-Snapshot }
        'interact' { Build-Resource; Build-Interact }
        'dpishot' { Build-Resource; Build-DpiShot }
        'res'     { Build-Resource }
        'icon'    { Build-Icon }
        'probe' {
            Build-Resource
            $exe = Build-Probe -Config 'release'
            Invoke-Tool $exe
        }
        'probedbg' {
            Build-Resource
            $exe = Build-Probe -Config 'debug'
            Invoke-Tool $exe
        }
    }
}
catch {
    Write-Host ''
    Write-Host "BUILD FAILED: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

Write-Host ''
Write-Host "BUILD OK ($Target)" -ForegroundColor Green
exit 0
