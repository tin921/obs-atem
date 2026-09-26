# Generates obs.lib and obs-frontend-api.lib import libraries from the DLLs
# of an installed OBS Studio (OBS ships DLLs but no .lib files).
#
# Run from a Developer PowerShell for VS 2022 (needs dumpbin + lib), in the
# repo root. Output goes to build\, where CMakeLists.txt looks for it.
#
#   powershell -ExecutionPolicy Bypass -File scripts\gen-obs-libs.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\gen-obs-libs.ps1 -ObsDir "D:\OBS"

param(
    [string]$ObsDir = "C:\Program Files\obs-studio"
)

New-Item -ItemType Directory -Force -Path build | Out-Null

$dlls = @(
    "$ObsDir\bin\64bit\obs.dll",
    "$ObsDir\bin\64bit\obs-frontend-api.dll"
)
foreach ($dll in $dlls) {
    if (-not (Test-Path $dll)) {
        Write-Warning "Cannot find $dll"
        continue
    }
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($dll)
    $defFile = "build\$baseName.def"
    $libFile = "build\$baseName.lib"

    # dumpbin /exports lines look like: "  1    0 00012340 name"
    $exports = dumpbin /exports $dll
    $defContent = @("EXPORTS")
    foreach ($line in $exports) {
        $match = [regex]::Match($line, '^\s*\d+\s+[A-Fa-f0-9]+\s+[A-Fa-f0-9]+\s+([^\s]+)')
        if ($match.Success) {
            $defContent += $match.Groups[1].Value
        }
    }
    $defContent | Set-Content -Path $defFile -Encoding Ascii

    lib /def:$defFile /machine:x64 /out:$libFile
}
