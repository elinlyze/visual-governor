param([string]$Gw2Dir = "C:\Program Files\Guild Wars 2")
$ErrorActionPreference = "Stop"
$Addons = Join-Path $Gw2Dir "addons"
$Paths = @(
    (Join-Path $Addons "visual_governor.dll"),
    (Join-Path $Addons "visual_governor"),
    (Join-Path $Addons "gw2_visual_governor.dll"),
    (Join-Path $Addons "gw2_visual_governor")
)
foreach ($Path in $Paths) {
    if (Test-Path $Path) { Remove-Item $Path -Recurse -Force }
}
Write-Host "Removed Visual Governor files from: $Gw2Dir"
