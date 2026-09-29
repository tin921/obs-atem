# Generates obs.lib and obs-frontend-api.lib import libraries from the DLLs
# of an installed OBS Studio (OBS ships DLLs but no .lib files).
#
# Run from a Developer PowerShell for VS 2022 (needs dumpbin + lib). Output
# goes to the repo's build\, where CMakeLists.txt looks for it.
#
#   powershell -ExecutionPolicy Bypass -File scripts\gen-obs-libs.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\gen-obs-libs.ps1 -ObsDir "D:\OBS"

param(
    [string]$ObsDir = "C:\Program Files\obs-studio"
)

$ErrorActionPreference = "Stop"
foreach ($tool in "dumpbin", "lib") {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool not found: run this from a Developer PowerShell for VS 2022."
    }
}
$build = Join-Path (Split-Path -Parent $PSScriptRoot) "build"
New-Item -ItemType Directory -Force -Path $build | Out-Null

$dlls = @(
    "$ObsDir\bin\64bit\obs.dll",
    "$ObsDir\bin\64bit\obs-frontend-api.dll"
)
foreach ($dll in $dlls) {
    if (-not (Test-Path $dll)) {
        throw "Cannot find $dll (use -ObsDir for another OBS folder)."
    }
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($dll)
    $defFile = Join-Path $build "$baseName.def"
    $libFile = Join-Path $build "$baseName.lib"

    # dumpbin /exports lines look like: "  1    0 00012340 name"
    $exports = dumpbin /exports $dll
    if ($LASTEXITCODE -ne 0) { throw "dumpbin failed on $dll" }
    $defContent = @("EXPORTS")
    foreach ($line in $exports) {
        $match = [regex]::Match($line, '^\s*\d+\s+[A-Fa-f0-9]+\s+[A-Fa-f0-9]+\s+([^\s]+)')
        if ($match.Success) {
            $defContent += $match.Groups[1].Value
        }
    }
    if ($defContent.Count -le 1) { throw "No exports found in $dll" }
    $defContent | Set-Content -Path $defFile -Encoding Ascii

    lib /def:$defFile /machine:x64 /out:$libFile
    if ($LASTEXITCODE -ne 0) { throw "lib failed for $libFile" }
    "{0}: {1} exports -> {2}" -f $baseName, ($defContent.Count - 1), $libFile
}
