<#
.SYNOPSIS
Builds the test harness and checks that the mod compiles, using the compiler
bundled with Windhawk.

.DESCRIPTION
- TestDialogs: builds tools\test-dialogs into build\test-dialogs-x64.exe and
  build\test-dialogs-x86.exe.
- Mod: compiles src\file-dialog-favorites.wh.cpp for x86 and x64 with the same
  flags the Windhawk editor uses, plus -Wall -Wextra. The DLLs go to build\
  and are only a compile check; install the mod through the Windhawk editor.

.EXAMPLE
pwsh tools\build.ps1            # everything
pwsh tools\build.ps1 -Target Mod
#>
param(
    [ValidateSet('All', 'TestDialogs', 'Mod')]
    [string]$Target = 'All',
    [string]$WindhawkPath = 'C:\Program Files\Windhawk'
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo 'build'
$compiler = Join-Path $WindhawkPath 'Compiler'
$clang = Join-Path $compiler 'bin\clang++.exe'
$modSource = Join-Path $repo 'src\file-dialog-favorites.wh.cpp'

if (-not (Test-Path $clang)) {
    throw "Windhawk compiler not found at $clang"
}
New-Item -ItemType Directory -Force $build | Out-Null

$architectures = @(
    @{ Name = 'x64'; Triple = 'x86_64-w64-mingw32'; EngineDir = '64' },
    @{ Name = 'x86'; Triple = 'i686-w64-mingw32'; EngineDir = '32' }
)

function Invoke-Clang([string[]]$Arguments, [string]$StdinFile) {
    Push-Location $compiler
    try {
        if ($StdinFile) {
            Get-Content -Raw -Encoding utf8 $StdinFile | & $clang @Arguments
        } else {
            & $clang @Arguments
        }
        if ($LASTEXITCODE -ne 0) {
            throw "clang++ exited with code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

function Build-TestDialogs {
    $source = Join-Path $repo 'tools\test-dialogs\test-dialogs.cpp'
    foreach ($arch in $architectures) {
        $output = Join-Path $build "test-dialogs-$($arch.Name).exe"
        Write-Host "Building $output"
        Invoke-Clang @(
            '-std=c++23', '-O2', '-Wall', '-Wextra', '-Wno-missing-field-initializers',
            '-DUNICODE', '-D_UNICODE',
            '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
            '-target', $arch.Triple,
            '-municode', '-mwindows', '-static',
            $source,
            '-o', $output,
            '-lole32', '-lshell32', '-lcomdlg32', '-luuid'
        )
    }
}

function Get-ModMetadata([string]$Path) {
    $text = Get-Content -Raw -Encoding utf8 $Path
    $id = [regex]::Match($text, '(?m)^//\s*@id\s+(\S+)').Groups[1].Value
    $version = [regex]::Match($text, '(?m)^//\s*@version\s+(\S+)').Groups[1].Value
    $options = [regex]::Match($text, '(?m)^//\s*@compilerOptions\s+(.+)$').Groups[1].Value
    if (-not $id -or -not $version) {
        throw "Could not read @id/@version from $Path"
    }
    return @{
        Id = $id
        Version = $version
        CompilerOptions = @($options.Trim() -split '\s+' | Where-Object { $_ })
    }
}

function Build-Mod {
    $meta = Get-ModMetadata $modSource
    $engine = Get-ChildItem (Join-Path $WindhawkPath 'Engine') -Directory |
        Sort-Object { [version]$_.Name } | Select-Object -Last 1
    foreach ($arch in $architectures) {
        $output = Join-Path $build "$($meta.Id)-$($arch.Name).dll"
        Write-Host "Compiling $($meta.Id) $($meta.Version) for $($arch.Name)"
        $engineLib = Join-Path $engine.FullName "$($arch.EngineDir)\windhawk.lib"
        Invoke-Clang -StdinFile $modSource -Arguments (@(
            '-std=c++23', '-O2', '-shared', '-Wall', '-Wextra', '-Wno-missing-field-initializers',
            '-DUNICODE', '-D_UNICODE',
            '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-D_WIN32_IE=0x0A00',
            '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0',
            '-DWH_MOD', "-DWH_MOD_ID=L`"$($meta.Id)`"",
            "-DWH_MOD_VERSION=L`"$($meta.Version)`"",
            $engineLib,
            '-x', 'c++', '-',
            '-include', 'windhawk_api.h',
            '-target', $arch.Triple,
            '-Wl,--export-all-symbols',
            '-o', $output
        ) + $meta.CompilerOptions)
    }
}

if ($Target -in 'All', 'TestDialogs') { Build-TestDialogs }
if ($Target -in 'All', 'Mod') { Build-Mod }
Write-Host 'Done.'
