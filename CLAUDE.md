# OBS ATEM Panels — C++ Plugin

## What this is
A native C++ OBS Studio plugin that adds dockable Qt panels for a Blackmagic
ATEM Mini, talking to it directly (USB or Ethernet) through the BMDSwitcherAPI
COM SDK. No middleware server, no browser dock, no external process.

- **ATEM Macros** panel — trigger the macros stored on the ATEM
- **ATEM PiP** panel — main and PiP camera, position/size/crop of the PiP
  box, seven preset buttons; layout reference `mockups/index.html`

The plugin is developed and tested against the real ATEM device only.

## Why C++ was selected

We evaluated multiple approaches before landing on C++. The key requirements
were: (1) a custom dockable panel in OBS (like Audio Mixer or Scene Transitions),
and (2) direct communication with the ATEM hardware — no middleware process.

| Approach         | Custom Dock? | Direct to ATEM? | Why rejected / selected        |
|------------------|:---:|:---:|------------------------------------------------|
| **C++ plugin**   | ✓   | ✓   | **SELECTED** — only option meeting both reqs   |
| Python script    | ✗   | ✓   | OBS Python only gets a properties panel in     |
|                  |     |     | Tools → Scripts, cannot create Qt dock widgets |
| Lua script       | ✗   | ✗   | Same UI limitation as Python, plus no ATEM     |
|                  |     |     | protocol library exists for Lua                |
| JS browser dock  | ✓   | ✗   | Browser JS can't open UDP sockets; requires a  |
|                  |     |     | local Node.js server as middleware (rejected)  |
| Node.js server   | ✓*  | ✓   | Works via Custom Browser Dock, but user        |
|                  |     |     | explicitly rejected the middleware approach     |

OBS custom docks are Qt widgets and can only be created via C++ plugins.
This is a hard constraint from OBS, not a preference.

## Architecture

```
┌──────────────────────────────────────────────┐
│                 OBS Studio                   │
│  ┌──────────────────┐  ┌──────────────────┐  │
│  │  AtemMacroDock   │  │  AtemPipDock     │  │
│  │  (QWidget)       │  │  (QWidget)       │  │
│  └────────┬─────────┘  └─────────┬────────┘  │
│           └──────────┬───────────┘           │
│                      ▼                       │
│   AtemSession (QObject) — one shared         │
│   connection; SDK callbacks → Qt signals     │
│                      ▼                       │
│   AtemController (macros)  +  AtemPip (PiP)  │
│   Qt-free, also used by atem-cli             │
│                      │  COM                  │
│                      ▼                       │
│             BMDSwitcherAPI64.dll             │
└──────────────────────┬───────────────────────┘
                       │  USB or Ethernet
                       ▼
                ┌─────────────┐
                │  ATEM Mini  │
                └─────────────┘
```

- Panels are plain `QWidget`s. On OBS 30+ `obs_frontend_add_dock_by_id`
  wraps them in OBS's own dock (OBS owns and destroys them); older OBS and the
  harness wrap them in a `QDockWidget`. The version check uses
  `LIBOBS_API_MAJOR_VER` — do NOT use `OBS_VERSION`: the obsconfig.h stub does
  not define it, so it silently evaluated to 0 and forced the legacy path.
- `AtemSession` is created in `obs_module_load`, auto-connects on
  `OBS_FRONTEND_EVENT_FINISHED_LOADING`, calls `shutdown()` (releases all COM
  objects) on `OBS_FRONTEND_EVENT_EXIT`, and is deleted in `obs_module_unload`.
- The last connection mode (USB/IP) and IP are stored with QSettings under
  `HKCU\Software\obs-atem\obs-atem`.

### Threading rules

- BMD SDK callbacks arrive on SDK threads. `AtemSession` marshals every one to
  the UI thread with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`;
  panels only ever see Qt signals.
- PiP change notifications are coalesced (one `pipChanged` per event-loop
  pass) because preview drags produce bursts of fly/mask events.
- `ConnectTo` blocks the UI thread and the controller holds its (non-recursive)
  mutex meanwhile. The SDK may pump messages during that call, so Qt timers can
  fire re-entrantly: every timer/slot that calls into the controller must bail
  out while `AtemSession::isBusy()` is true.
- Connection loss: the `IBMDSwitcherCallback` "disconnected" event →
  `AtemController::handleConnectionLost()` on the UI thread → panels show
  "Connection to the ATEM was lost." with a reconnect button.

## File structure

```
obs-atem/
├── CMakeLists.txt              # Targets: obs-atem (plugin), atem-harness, atem-cli
├── CLAUDE.md / README.md / CONTRIBUTING.md
├── plugin-main.cpp             # OBS entry: creates AtemSession, registers both docks
├── atem-controller.h/cpp       # BMD SDK wrapper (Qt-free)
│                               #   - USB auto-detect / IP connect, failure reasons
│                               #   - Macro enumeration, run/stop, run status
│                               #   - Switcher "disconnected" callback
│                               #   - Owns AtemPip, attaches it on connect
├── atem-pip.h/cpp              # BMD SDK wrapper for PiP (Qt-free)
│                               #   - Input list (program- and key-fill-capable)
│                               #   - M/E 1 program input, upstream key 1 fill/on-air/type
│                               #   - Fly params (position/size), DVE mask (crop)
├── bmd-util.h                  # BmdCallback<Iface, Args...> COM sink template,
│                               #   bmdRelease, BSTR <-> UTF-8 helpers
├── atem-session.h/cpp          # Shared connection + Qt signals + saved settings
├── panel-common.h/cpp          # Shared stylesheet + PanelHeader (status dot, title)
├── macro-dock.h/cpp            # ATEM Macros panel (connect view, grid, player bar)
├── pip-dock.h/cpp              # ATEM PiP panel: camera rows, presets, fields, ⚙ page;
│                               #   local view + ~30 Hz sends + echo holds
├── pip-preview.h/cpp           # Program preview (drag/resize/nudge) + paintProgram()
│                               #   shared with preset thumbnails; PipGeometry
├── pip-widgets.h/cpp           # PictureButton (CamButton, PresetButton), PipSpinBox
├── pip-settings.h/cpp          # Camera names/colours/pictures + presets (QSettings "pip/...")
├── settings-dialog.h/cpp       # ⚙ dialog: status, USB/IP connect, connection log, troubleshooting
├── obs-log.h                   # blog(): libobs inside OBS, stderr elsewhere
├── harness/main.cpp            # atem-harness: both panels + "ATEM log" dock (SDK calls)
├── cli/main.cpp                # atem-cli: info | pip | run N | stop [--ip ADDR]
├── mockups/                    # index.html = PiP layout reference; drafts.html = earlier A/B/C
├── data/locale/en-US.ini       # Plugin text (dock titles via obs_module_get_string)
├── scripts/gen-obs-libs.ps1    # Generates obs.lib / obs-frontend-api.lib into build\
└── scripts/deploy.ps1          # Copies the plugin to %ProgramData%\obs-studio\plugins\obs-atem
```

## ATEM connection details

- USB: `ConnectTo("")` — the SDK auto-detects a USB-connected ATEM.
  Ethernet: `ConnectTo("<ip>")`. The user's ATEM is at 192.168.10.240.
- The BMDSwitcherAPI64.dll COM library is installed with ATEM Software Control
  at C:\Program Files (x86)\Blackmagic Design\Blackmagic ATEM Switchers\
- The SDK headers (BMDSwitcherAPI.h) must be downloaded separately from
  https://www.blackmagicdesign.com/developer/products/atem/sdk-and-software
  The SDK manual is `Blackmagic Switchers SDK.pdf` in the SDK root folder.
- Multiple clients can connect to the ATEM simultaneously — having ATEM
  Software Control open alongside OBS is fine.
- Macros are stored on the ATEM hardware, not in software. Record/edit them
  in ATEM Software Control; this plugin reads and triggers them by index.
- The ATEM Software Control app does NOT need to be running for the SDK to work.

## PiP on the ATEM Mini

The ATEM Mini has no SuperSource. PiP = upstream key 1 of M/E 1 as a DVE key:

| Panel control | SDK call |
|---|---|
| Main input | `IBMDSwitcherMixEffectBlock::SetProgramInput` (hard cut) |
| PiP input | `IBMDSwitcherKey::SetInputFill` |
| PiP on/off | `IBMDSwitcherKey::SetOnAir` |
| Key type must be DVE | `IBMDSwitcherKey::SetType(bmdSwitcherKeyTypeDVE)` |
| Position X/Y, size X/Y | `IBMDSwitcherKeyFlyParameters::SetPositionX/Y`, `SetSizeX/Y` |
| Size > 1.0 allowed? | `IBMDSwitcherKeyFlyParameters::GetCanScaleUp` |
| Crop T/B/L/R + enable | `IBMDSwitcherKeyDVEParameters::SetMaskTop/...`, `SetMasked` |
| Reset | `IBMDSwitcherKeyFlyParameters::ResetDVE`, `IBMDSwitcherKeyDVEParameters::ResetMask` |

In SDK 10.2.1 position/size live on the *fly* parameters interface, not the
DVE parameters interface (the manual: "most properties in this interface also
take effect when the key type is set to DVE").

The SDK manual gives **no numeric ranges**. The number-box limits in
`pip-dock.cpp` (X ±32, Y ±18, size 0–1, crop T/B 0–38, L/R 0–52) and the
preview's frame (X ±16, Y ±9, +Y up) are ATEM Software Control's 16:9 values
and MUST be checked against the real device (`atem-cli pip` prints the live
values).

### PiP panel behaviour (implemented from `mockups/index.html`)

- Camera rows are CAM 1–4 = ATEM inputs 1–4. Main row: one lit (program).
  PiP row: lit = on air with that fill; pressing the lit one → `SetOnAir(FALSE)`,
  pressing another → `SetInputFill` + `SetOnAir(TRUE)`. No separate on-air button.
- Size always keeps the aspect ratio (SizeX = SizeY). Crop has no checkbox:
  `SetMasked` follows "any crop edge > 0".
- The panel edits a local `AtemPipState` view immediately, sends continuous
  values at ~30 Hz, and holds each edited field until the device echoes it (or
  400 ms) so stale echoes don't make values jump back. Fields being typed or
  dragged are never overwritten.
- Presets (7) store cameras, on-air, position, size, crop and a 192×108
  thumbnail drawn by `PipPreview::paintProgram` from the camera pictures /
  colours (not real video). Recall sends only what differs, on-air last.
- Settings live in QSettings group `pip` (camera N name/color/picture path,
  showNames, presetN/...); shared by the plugin and the harness.
- Open question for the user: preset thumbnails from real ATEM video in OBS
  instead of the camera pictures?

## Build requirements (Windows only)

- Visual Studio 2022 (Desktop development with C++) or VS 2022 Build Tools
- CMake 3.16+ (bundled with VS)
- Qt 6 MSVC 2022 64-bit — same version as the Qt DLLs in the installed OBS
- OBS Studio source at the tag matching the installed OBS (for plugin API headers)
- OBS Studio installed (DLLs used to generate .lib import libraries)
- Blackmagic ATEM SDK 10.2.1 (for BMDSwitcherAPI.h)
- ATEM Software Control installed (provides the COM DLLs at runtime)

### OBS / Qt version matching (important)

- OBS refuses to load a plugin built against a newer libobs than the running
  OBS (log: `Module '...' compiled with newer libobs X.Y`). The OBS source at
  D:\cemc-sr\obs-studio is **32.1.0**; the installed OBS is 32.2.2, which
  loads it. The previously installed OBS shipped Qt 6.6.3 (OBS 30.x), so the
  April 2026 DLL was most likely rejected.
- Compile against the same Qt minor version that the installed OBS ships
  (check Qt6Core.dll in OBS's bin\64bit).

### Machine state (2026-09-26)

The original setup was done under a Windows account `Admin` that no longer
exists; the current account is `DELL`. Git reports "dubious ownership" on
D:\cemc-sr\obs-studio for that reason (use `git -c safe.directory=* ...`).

```
VS 2022 Build Tools:   C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
                       (MSVC 14.44, bundled CMake + Ninja); VS Community not installed
OBS Studio:            32.2.2 at C:\Program Files\obs-studio (ships Qt 6.11.1)
OBS source (headers):  D:\cemc-sr\obs-studio (32.1.0 — older than 32.2.2, so OK;
                       libobs\obsconfig.h is a hand-made stub)
Qt SDK:                D:\ProgramFiles\Qt\6.11.1\msvc2022_64 (qtbase + qttools)
                       Qt6_DIR = D:/ProgramFiles/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6
ATEM Software Control: 10.2.1 installed
ATEM SDK:              D:\cemc-sr\Blackmagic_ATEM_Switchers_SDK_10.2.1\Blackmagic ATEM Switchers SDK 10.2.1\Windows
ATEM 10.4.1 download:  in D:\cemc-sr, for a later upgrade (software + firmware + SDK together)
OBS import libs:       build\obs.lib, build\obs-frontend-api.lib are from the OLD OBS —
                       regenerate from 32.2.2 with scripts\gen-obs-libs.ps1
```

Qt 6.11.1 install note: aqtinstall 3.3.0 cannot install Qt 6.10+ (Qt moved
those to per-compiler folders on the server). It was fetched manually from
https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6111/qt6_6111_msvc2022_64/qt.qt6.6111.win64_msvc2022_64/
(qtbase and qttools .7z, SHA-1 checked) and extracted into the kit folder.

### VS Developer Tools

Regular PowerShell does NOT have VS tools (dumpbin, lib, cl) on PATH. Open
"Developer PowerShell for VS 2022" from the Start Menu, or from cmd run:

```
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
```

### ATEM SDK header note

The Windows ATEM SDK ships with `.idl` files, not `.h` headers.
A pre-generated BMDSwitcherAPI.h exists in the SDK samples and has been
copied to the include folder:

```
Source: ...\Windows\Samples\DeviceInfo\BMDSwitcherAPI.h
Copied to: ...\Windows\include\BMDSwitcherAPI.h
```

The SDK has no `_i.c` GUID file. Interface and class IDs come from
`__uuidof(IBMDSwitcherXxx)` / `__uuidof(CBMDSwitcherDiscovery)` — the header's
MIDL_INTERFACE / DECLSPEC_UUID attributes make that work in MSVC — so no GUIDs
are hand-copied anywhere.

### OBS import libraries

OBS install only ships DLLs, not .lib files. Generate import libraries from
the installed DLLs (Developer PowerShell, repo root):

```powershell
powershell -ExecutionPolicy Bypass -File scripts\gen-obs-libs.ps1
# other OBS location:  ... -File scripts\gen-obs-libs.ps1 -ObsDir "D:\OBS"
```

Generated files: build\obs.lib and build\obs-frontend-api.lib. CMakeLists.txt
searches CMAKE_BINARY_DIR so they are found automatically. If the OBS headers
or libs are not found, CMake skips the plugin target with a warning and still
builds atem-harness and atem-cli.

### Build commands

Run from a Developer PowerShell (with VS tools loaded):

```powershell
cd D:\cemc-sr\obs-atem
powershell -ExecutionPolicy Bypass -File scripts\gen-obs-libs.ps1   # once / after OBS update

cmake -B build -G "Visual Studio 17 2022" -A x64 `
    -DOBS_DIR="D:/cemc-sr/obs-studio" `
    -DATEM_SDK_DIR="D:/cemc-sr/Blackmagic_ATEM_Switchers_SDK_10.2.1/Blackmagic ATEM Switchers SDK 10.2.1/Windows" `
    -DQt6_DIR="<Qt>/msvc2022_64/lib/cmake/Qt6"

cmake --build build --config Release
```

`/utf-8` is set for MSVC in CMakeLists.txt: the sources contain UI glyphs
(⚙ ⟳ ▶ ● —). Without it MSVC reads them in the ANSI code page.

### Install

```powershell
# OBS closed; no Administrator needed:
powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1
```

Deploys to `C:\ProgramData\obs-studio\plugins\obs-atem\{bin\64bit,data}` — OBS
28+ on Windows loads per-machine plugins from there (OBSBasic.cpp,
GetProgramDataPath "obs-studio/plugins/%module%"). `data\locale\en-US.ini`
must exist and be non-empty or OBS logs "Failed to load 'en-US' text".

## Runtime requirements (on the streaming PC)

- OBS Studio 30+ (matching the libobs the plugin was built against, or newer)
- ATEM Software Control installed (registers BMDSwitcherAPI64.dll COM server)
- ATEM Mini connected via USB or Ethernet
- The plugin DLL placed in obs-plugins/64bit/
- No Python, Node.js, or other runtimes needed
- ATEM Software Control does NOT need to be running, just installed

### COM server registration

Installing ATEM Software Control automatically registers the COM server.
If you need to register it manually (e.g. copied DLL without full install):

```powershell
# Register (run as Administrator)
regsvr32 "C:\Program Files (x86)\Blackmagic Design\Blackmagic ATEM Switchers\BMDSwitcherAPI64.dll"

# Verify registration
reg query "HKCR\CLSID" /s /f "BMDSwitcherDiscovery"

# Unregister
regsvr32 /u "C:\Program Files (x86)\Blackmagic Design\Blackmagic ATEM Switchers\BMDSwitcherAPI64.dll"
```

If the plugin logs "Failed to create BMDSwitcherDiscovery", the COM server
is not registered. Either install ATEM Software Control or run the regsvr32
command above as Administrator.

## Testing against the real ATEM

1. `atem-cli` (USB) or `atem-cli --ip 192.168.10.240` — confirms the SDK
   connection, lists macros, inputs and the live PiP values.
2. `atem-cli run N` / `atem-cli stop` — macro control without any UI.
3. `atem-harness.exe` — both panels in a plain window, debuggable in VS,
   with an "ATEM log" dock showing every SDK call. Same panel sources as the
   plugin; only plugin-main.cpp is OBS-specific. Needs the Qt `bin` folder on
   PATH (or `windeployqt`).
4. The plugin in OBS — check the OBS log for `[ATEM]` lines.

## Current status

- Macro panel reworked; plugin, harness and CLI build (2026-09-25)
- PiP panel implemented from `mockups/index.html` and builds (2026-09-26);
  layout, preview drag/resize, number boxes and settings checked offscreen
- NEVER yet run against a real ATEM or loaded in OBS (no record of a
  successful run; the April 2026 DLL was likely rejected by OBS 30.x — see
  version matching above)
- OBS 32.2.2, Qt 6.11.1 and ATEM Software Control 10.2.1 installed
  (2026-09-26); plugin, harness and CLI build against Qt 6.11.1
- 2026-09-26: plugin loaded in OBS 32.2.2 (Qt 6.11.1 runtime = compiled),
  both docks shown; with the ATEM Mini on USB it connected ("ATEM Mini",
  PiP key/fly/dve found, 14 inputs) and the PiP panel read live values
  (Color 1 on program, CAM 1 PiP on air at X -8, Y -4.55, size 0.5 — a
  flush bottom-left box, consistent with edges at X ±16 / Y ±9, +Y up)
- NEXT STEPS: confirm on the ATEM output that the PiP really is bottom-left;
  exercise camera buttons, drag, numbers, presets and macros on hardware

### SDK signature notes

- IBMDSwitcherMacroPoolCallback::Notify(eventType, index, transferMacro*)
- IBMDSwitcherMacroPool::IsValid() uses BOOL*, not a BMDSwitcherMacroValidity enum
- IBMDSwitcherMacroControl::GetRunStatus(status*, loop*, index*); status can be
  Idle, Running or WaitingForUser (treated as running)
- IBMDSwitcherKeyFlyParametersCallback::Notify(eventType, keyFrame)
- BMDSwitcherInputId is `long long`
- ConnectTo failure reasons are FourCC codes (e.g. 0x63667373 'cfss' = StateSync)

## Known considerations

- USB video capture (ATEM as UVC webcam) is separate from the control
  connection. Only one app can capture the webcam feed at a time, but
  multiple apps can connect to the control protocol simultaneously.
- The plugin targets Windows only due to the COM-based BMD SDK.
- ConnectTo blocks the OBS UI thread for a few seconds when no ATEM answers
  (at startup and on manual connect).

## User's hardware setup

- Blackmagic ATEM Mini (base model, not Pro/Extreme)
- Connected via USB to PC
- ATEM IP configured as 192.168.10.240 (subnet 255.255.255.0, gateway 192.168.10.1)
- Switching mode: Cut Bus
- Used for church sermon recording with OBS
- ATEM Software Control version 10.2.1
- Development workspace: D:\cemc-sr\
