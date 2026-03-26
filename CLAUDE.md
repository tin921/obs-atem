# OBS ATEM Macro Panel — C++ Plugin

## What this is
A native C++ OBS Studio plugin that adds a dockable Qt panel for triggering
Blackmagic ATEM Mini macros directly via USB using the BMDSwitcherAPI COM SDK.
No middleware server, no browser dock, no external process.

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
┌─────────────────────────────────────────┐
│            OBS Studio                   │
│  ┌───────────────────────────────────┐  │
│  │   ATEM Macro Dock (Qt)            │  │
│  │  ┌─────┐ ┌─────┐ ┌─────┐        │  │
│  │  │ M1  │ │ M2  │ │ M3  │   ⚙    │  │
│  │  └─────┘ └─────┘ └─────┘        │  │
│  └───────────┬───────────────────────┘  │
│              │ COM / USB                │
│              ▼                          │
│     BMDSwitcherAPI.dll                  │
└──────────────┬──────────────────────────┘
               │ USB virtual ethernet
        ┌──────▼──────┐
        │  ATEM Mini  │
        │  (macros)   │
        └─────────────┘
```

## File structure

```
obs-atem-macros/
├── CMakeLists.txt              # Build config (OBS SDK + BMD SDK + Qt6)
├── CLAUDE.md                   # This file
├── README.md                   # User-facing setup/install docs
└── src/
    ├── plugin-main.cpp         # OBS plugin entry point, registers dock
    ├── atem-controller.h/cpp   # BMD COM SDK wrapper
    │                           #   - USB auto-detect and IP connection
    │                           #   - Macro enumeration (name, index, desc)
    │                           #   - Macro run/stop
    │                           #   - Callback for state changes
    ├── macro-dock.h/cpp        # Qt QDockWidget
    │                           #   - 2-column macro button grid
    │                           #   - OBS dark theme styling
    │                           #   - Running macro indicator (green highlight)
    │                           #   - Bottom player bar with stop button
    │                           #   - Auto-connects via USB on startup
    └── settings-dialog.h/cpp   # Gear icon (⚙) dialog
                                #   - Connection status (model, address, macro count)
                                #   - Connect via USB or manual IP
                                #   - Troubleshooting checklist and last error
```

## ATEM connection details

- The ATEM Mini connects via USB, which creates a virtual ethernet adapter
  on the host PC. The ATEM sits at its configured IP (default 192.168.10.240)
  over this virtual network.
- The BMDSwitcherAPI.dll is a COM library installed with ATEM Software Control
  at C:\Program Files (x86)\Blackmagic Design\Blackmagic ATEM Switchers\
- The SDK headers (BMDSwitcherAPI.h) must be downloaded separately from
  https://www.blackmagicdesign.com/developer/products/atem/sdk-and-software
- Multiple clients can connect to the ATEM simultaneously — having ATEM
  Software Control open alongside OBS is fine.
- Macros are stored on the ATEM hardware, not in software. Record/edit them
  in ATEM Software Control; this plugin reads and triggers them by index.
- The ATEM Software Control app does NOT need to be running for the SDK to work.

## Build requirements (Windows only)

- Visual Studio 2022 Community 17.14.28 (Desktop development with C++)
- CMake 3.28.1
- Git 2.43.0
- Qt 6.11.0 MSVC 2022 64-bit
- OBS Studio source (for plugin API headers)
- OBS Studio installed (for runtime DLLs, need to generate .lib import libraries)
- Blackmagic ATEM SDK 10.2.1 (for BMDSwitcherAPI.h)
- ATEM Software Control installed (provides the COM DLLs at runtime)

### Exact paths on this machine

```
Visual Studio:  D:\Program Filesx\Microsoft Visual Studio\2022\Community
Qt6 SDK:        D:\ProgramFiles\Qt\6.11.0\msvc2022_64
Qt6 CMake:      D:\ProgramFiles\Qt\6.11.0\msvc2022_64\lib\cmake\Qt6
OBS source:     D:\cemc-sr\obs-studio
OBS install:    C:\Program Files\obs-studio
OBS DLLs:       C:\Program Files\obs-studio\bin\64bit\
ATEM SDK:       D:\cemc-sr\Blackmagic_ATEM_Switchers_SDK_10.2.1\Blackmagic ATEM Switchers SDK 10.2.1\Windows
ATEM headers:   D:\cemc-sr\Blackmagic_ATEM_Switchers_SDK_10.2.1\Blackmagic ATEM Switchers SDK 10.2.1\Windows\include
ATEM COM DLL:   C:\Program Files (x86)\Blackmagic Design\Blackmagic ATEM Switchers\BMDSwitcherAPI64.dll
Plugin source:  D:\cemc-sr\obs-atem-macros
```

### VS Developer Tools

Regular PowerShell does NOT have VS tools (dumpbin, midl, lib, cl) on PATH.
To load them into the current session:

```powershell
Import-Module "D:\Program Filesx\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "D:\Program Filesx\Microsoft Visual Studio\2022\Community" -DevCmdArguments "-arch=amd64" -SkipAutomaticLocation
```

Or open "Developer PowerShell for VS 2022" from the Start Menu.

### ATEM SDK header note

The Windows ATEM SDK ships with `.idl` files, not `.h` headers.
A pre-generated BMDSwitcherAPI.h exists in the SDK samples and has been
copied to the include folder:

```
Source: ...\Windows\Samples\DeviceInfo\BMDSwitcherAPI.h
Copied to: ...\Windows\include\BMDSwitcherAPI.h
```

### OBS import libraries

OBS install only ships DLLs, not .lib files. Import libraries are generated
from the installed DLLs using a PowerShell script:

```powershell
# From Developer PowerShell (run once, outputs to build/ directory):
powershell -ExecutionPolicy Bypass -File build\gen-obs-libs.ps1
```

The script (build\gen-obs-libs.ps1) uses dumpbin + lib and handles the
dumpbin output format (entries look like: `1  0  ADDR  name = name`).
Generated files: build\obs.lib and build\obs-frontend-api.lib
CMakeLists.txt searches CMAKE_BINARY_DIR so they are found automatically.

### Qt version note

OBS ships Qt 6.6.3 runtime DLLs. We compile against Qt 6.11.0 MSVC SDK.
Minor version mismatch — Qt 6.x maintains forward ABI compatibility for
widget-level code, but if runtime crashes occur, install Qt 6.6.3 MSVC
via the Qt MaintenanceTool at D:\ProgramFiles\Qt\MaintenanceTool.exe.

### Build commands

Run all of the following from a Developer PowerShell (with VS tools loaded):

```powershell
# 1. Generate OBS import libraries (only needed once, or after OBS update)
cd D:\cemc-sr\obs-atem-macros
powershell -ExecutionPolicy Bypass -File build\gen-obs-libs.ps1

# 2. CMake configure (only needed when CMakeLists.txt changes)
cmake -B build -G "Visual Studio 17 2022" -A x64 `
    -DOBS_DIR="D:/cemc-sr/obs-studio" `
    -DATEM_SDK_DIR="D:/cemc-sr/Blackmagic_ATEM_Switchers_SDK_10.2.1/Blackmagic ATEM Switchers SDK 10.2.1/Windows" `
    -DQt6_DIR="D:/ProgramFiles/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6"

# 3. Build
cmake --build build --config Release
```

### Install

```powershell
# Run as Administrator:
copy build\Release\obs-atem-macros.dll "C:\Program Files\obs-studio\obs-plugins\64bit\"
```

## Runtime requirements (on the streaming PC)

- OBS Studio (28+ for Qt6, or 27.x for Qt5)
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

## Current status

- Plugin compiled and installed ✓ (build\Release\obs-atem-macros.dll)
- Installed to C:\Program Files\obs-studio\obs-plugins\64bit\ ✓
- NOT YET tested against a real ATEM device
- NEXT STEP: Launch OBS and verify the ATEM Macros dock appears;
  connect to the ATEM and test macro triggering
- COM threading model: OBS runs Qt on STA thread; ATEM callbacks
  are marshaled back via QMetaObject::invokeMethod with Qt::QueuedConnection

### Build fixes applied during initial compile

- Source files are in project root (not src/ subdirectory)
- obs-frontend-api.h moved to frontend/api/ in this OBS version (not UI/obs-frontend-api/)
- obsconfig.h is generated by OBS cmake; a stub was created at obs-studio/libobs/obsconfig.h
- BMD SDK 10.2.1 has no _i.c GUID file; GUIDs are defined in bmd-guids.cpp
- IBMDSwitcherMacroPoolCallback::Notify() signature: (eventType, index, transferMacro*)
- IBMDSwitcherMacroPool::IsValid() uses BOOL*, not a BMDSwitcherMacroValidity enum
- IBMDSwitcherMacroControl::GetRunStatus() takes 3 params: (status*, loop*, index*)

## Known considerations

- USB video capture (ATEM as UVC webcam) is separate from USB control
  (virtual ethernet). Only one app can capture the webcam feed at a time,
  but multiple apps can connect to the control protocol simultaneously.
- The plugin targets Windows only due to the COM-based BMD SDK.
  A cross-platform version would need PyATEMMax or atem-connection
  with a middleware layer.
- OBS 30+ has a new dock registration API (obs_frontend_add_dock_by_id);
  older OBS uses mainWindow->addDockWidget. Both are handled in plugin-main.cpp.

## User's hardware setup

- Blackmagic ATEM Mini (base model, not Pro/Extreme)
- Connected via USB to PC
- ATEM IP configured as 192.168.10.240 (subnet 255.255.255.0, gateway 192.168.10.1)
- Switching mode: Cut Bus
- Used for church sermon recording with OBS
- ATEM Software Control version 10.2.1 installed
- OBS Studio installed at C:\Program Files\obs-studio (Qt 6.6.3)
- Visual Studio 2022 installed on D: drive
- Development workspace: D:\cemc-sr\

## ATEM Simulator (for dev/testing without hardware)

A Python stub at `atem-simulator/` emulates the ATEM Mini on UDP port 9910.
No external dependencies — pure Python stdlib. Reads macros from a TSV file.

```bash
# Start simulator
python atem_simulator.py --macros macros.tsv

# OBS plugin connects to 127.0.0.1 instead of USB
```

TSV format (2 columns, no header): `Macro Name<TAB>Description`

The simulator handles the full ATEM UDP protocol handshake, dumps initial
state (product name, topology, macro properties), responds to macro
run/stop commands, and prints all received commands to the console.
This allows full plugin development and testing on a dev machine
without an ATEM Mini connected.
