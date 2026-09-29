# Installs the built plugin where OBS Studio (Windows, 28+) loads per-machine
# plugins from, without needing Administrator:
#
#   %ProgramData%\obs-studio\plugins\obs-atem\bin\64bit\obs-atem.dll
#
# The DLL is the whole plugin: no data files.
#
# Run after building (from any folder):
#   powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1 -Config Debug

param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $repo "build\$Config\obs-atem.dll"
if (-not (Test-Path $dll)) {
    throw "$dll not found - build first (cmake --build build --config $Config)."
}
if (Get-Process obs64 -ErrorAction SilentlyContinue) {
    throw "OBS is running and has the plugin loaded. Close OBS, then deploy again."
}

$target = Join-Path $env:ProgramData "obs-studio\plugins\obs-atem"
New-Item -ItemType Directory -Force -Path "$target\bin\64bit" | Out-Null
Copy-Item $dll "$target\bin\64bit\" -Force
$pdb = Join-Path $repo "build\$Config\obs-atem.pdb"
if (Test-Path $pdb) { Copy-Item $pdb "$target\bin\64bit\" -Force }
# Earlier versions shipped data\locale\en-US.ini; no longer used. Only that
# file is removed (and its folders if they are then empty), nothing else.
$oldLocale = "$target\data\locale\en-US.ini"
if (Test-Path $oldLocale) {
    Remove-Item $oldLocale -Force
    foreach ($dir in "$target\data\locale", "$target\data") {
        if ((Test-Path $dir) -and -not (Get-ChildItem $dir -Force)) { Remove-Item $dir -Force }
    }
}

# An older copy in Program Files would load too and clash (same dock ids).
$legacy = "C:\Program Files\obs-studio\obs-plugins\64bit\obs-atem.dll"
if (Test-Path $legacy) {
    Write-Warning "Also found $legacy - remove it (as Administrator) so only one copy loads."
}

Get-ChildItem $target -Recurse -File | ForEach-Object { "{0,-70} {1:yyyy-MM-dd HH:mm}" -f $_.FullName, $_.LastWriteTime }
