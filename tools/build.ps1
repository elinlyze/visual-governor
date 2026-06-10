param(
    [switch]$Clean,
    [switch]$Install,
    [string]$ImguiDir = "C:\dev\imgui-v180",
    [string]$Gw2Dir = "C:\Program Files\Guild Wars 2"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "build"

if ($Clean -and (Test-Path $Build)) { Remove-Item $Build -Recurse -Force }

cmake -S $Root -B $Build -A x64 -DCMAKE_BUILD_TYPE=Release -DIMGUI_DIR="$ImguiDir"
cmake --build $Build --config Release --target visual_governor -- /m

$Dll = Join-Path $Build "bin\Release\visual_governor.dll"
if (!(Test-Path $Dll)) { $Dll = Join-Path $Build "bin\visual_governor.dll" }
if (!(Test-Path $Dll)) { throw "Build finished but DLL not found: $Dll" }
Write-Host "Built addon: $Dll"

if ($Install) {
    $Addons = Join-Path $Gw2Dir "addons"
    New-Item -ItemType Directory -Force -Path $Addons | Out-Null
    $OldDll = Join-Path $Addons "gw2_visual_governor.dll"
    if (Test-Path $OldDll) { Remove-Item $OldDll -Force }
    Copy-Item $Dll (Join-Path $Addons "visual_governor.dll") -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $Addons "visual_governor") | Out-Null
    Write-Host "Installed addon to: $(Join-Path $Addons 'visual_governor.dll')"
}
