[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build-release',
    [ValidateSet('x64', 'ARM64')]
    [string]$Architecture = 'x64'
)

$ErrorActionPreference = 'Stop'
$wixDefaultBin = Get-ChildItem 'C:\Program Files (x86)\WiX Toolset v*\bin' -ErrorAction SilentlyContinue |
    Where-Object { (Test-Path (Join-Path $_.FullName 'candle.exe')) -and
                   (Test-Path (Join-Path $_.FullName 'light.exe')) } |
    Select-Object -First 1 -ExpandProperty FullName
if ($wixDefaultBin) {
    if ($env:PATH -notlike "*$wixDefaultBin*") { $env:PATH = "$env:PATH;$wixDefaultBin" }
    $env:WIX = Split-Path -Parent $wixDefaultBin
}
$sourceDirectory = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $sourceDirectory $BuildDirectory
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)

$cmakeVersion = (& cmake --version | Select-Object -First 1) -replace '^cmake version ', ''
if ([version]$cmakeVersion -lt [version]'4.3') {
    throw 'CMake 4.3 or newer is required to produce a per-user MSI.'
}

& cmake -S $sourceDirectory -B $BuildDirectory -G 'Visual Studio 17 2022' -A $Architecture
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $BuildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
$wixCompiler = Get-Command candle.exe -ErrorAction SilentlyContinue
$wixLinker = Get-Command light.exe -ErrorAction SilentlyContinue
if (-not $wixCompiler -or -not $wixLinker) {
    $install = Start-Process winget.exe -Verb RunAs -Wait -PassThru -ArgumentList @(
        'install', '--id', 'WiXToolset.WiXToolset', '-e',
        '--accept-source-agreements', '--accept-package-agreements'
    )
    if ($install.ExitCode -ne 0) { throw "WiX installation failed with exit code $($install.ExitCode)." }
    $env:PATH = "$env:PATH;$([Environment]::GetEnvironmentVariable('PATH', 'Machine'));$([Environment]::GetEnvironmentVariable('PATH', 'User'))"
    $wixCompiler = Get-Command candle.exe -ErrorAction SilentlyContinue
    $wixLinker = Get-Command light.exe -ErrorAction SilentlyContinue
    if (-not $wixCompiler -or -not $wixLinker) {
        throw 'WiX installation completed, but candle.exe/light.exe are not available. Add the WiX bin directory to PATH and rerun.'
    }
}
& cpack --config (Join-Path $BuildDirectory 'CPackConfig.cmake') -C Release -G ZIP -B $BuildDirectory
if ($LASTEXITCODE -ne 0) { throw 'Portable ZIP packaging failed.' }
& cpack --config (Join-Path $BuildDirectory 'CPackConfig.cmake') -C Release -G WIX -B $BuildDirectory
if ($LASTEXITCODE -ne 0) { throw 'MSI packaging failed.' }

# SHA256：优先用 cmdlet，某些受限 shell（PSModulePath 被改过）里拿不到 Get-FileHash，退回 certutil
function Get-Sha256([string]$Path) {
    $hash = Get-Command Get-FileHash -ErrorAction SilentlyContinue
    if ($hash) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
    $line = certutil -hashfile $Path SHA256 | Select-Object -Skip 1 -First 1
    return ($line -replace '\s', '')
}

$archive = Get-ChildItem -LiteralPath $BuildDirectory -Filter 'Tilefold-*-windows-*.zip' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $archive) { throw 'Portable ZIP was not generated.' }
$installer = Get-ChildItem -LiteralPath $BuildDirectory -Filter 'Tilefold-*-windows-*.msi' |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $installer) { throw 'MSI installer was not generated.' }

Write-Host ''
Write-Host "Portable ZIP: $($archive.FullName)"
Write-Host "  SHA256    : $(Get-Sha256 $archive.FullName)"
Write-Host "Installer MSI: $($installer.FullName)"
Write-Host "  SHA256    : $(Get-Sha256 $installer.FullName)"
