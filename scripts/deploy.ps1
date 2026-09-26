# Installs the built plugin where OBS Studio (Windows, 28+) loads per-machine
# plugins from, without needing Administrator:
#
#   %ProgramData%\obs-studio\plugins\obs-atem\bin\64bit\obs-atem.dll
#
# The DLL is the whole plugin: no data files.
#
# Run from the repo root after building:
#   powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1 -Config Debug

param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$dll = "build\$Config\obs-atem.dll"
if (-not (Test-Path $dll)) {
    throw "$dll not found - build first (cmake --build build --config $Config)."
}
if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    throw "OBS is running and has the plugin loaded. Close OBS, then deploy again."
}

$target = Join-Path $env:ProgramData "obs-studio\plugins\obs-atem"
New-Item -ItemType Directory -Force -Path "$target\bin\64bit" | Out-Null
Copy-Item $dll "$target\bin\64bit\" -Force
if (Test-Path "build\$Config\obs-atem.pdb") {
    Copy-Item "build\$Config\obs-atem.pdb" "$target\bin\64bit\" -Force
}
# Earlier versions shipped data\locale\en-US.ini; no longer used.
if (Test-Path "$target\data") { Remove-Item "$target\data" -Recurse -Force }

# An older copy in Program Files would load too and clash (same dock ids).
$legacy = "C:\Program Files\obs-studio\obs-plugins\64bit\obs-atem.dll"
if (Test-Path $legacy) {
    Write-Warning "Also found $legacy - remove it (as Administrator) so only one copy loads."
}

Get-ChildItem $target -Recurse -File | ForEach-Object { "{0,-70} {1:yyyy-MM-dd HH:mm}" -f $_.FullName, $_.LastWriteTime }
