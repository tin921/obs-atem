# OBS ATEM Macro Panel

A native C++ OBS Studio plugin that adds a dockable panel for triggering Blackmagic
ATEM Mini macros. Connects directly via USB — no middleware server needed.

## Architecture

```
┌─────────────────────────────────┐
│          OBS Studio             │
│  ┌───────────────────────────┐  │
│  │   ATEM Macro Dock (Qt)    │  │
│  │  ┌─────┐ ┌─────┐ ┌─────┐ │  │
│  │  │ M1  │ │ M2  │ │ M3  │ │  │
│  │  └─────┘ └─────┘ └─────┘ │  │
│  │  ┌─────┐ ┌─────┐         │  │
│  │  │ M4  │ │ M5  │   ⚙     │  │
│  │  └─────┘ └─────┘         │  │
│  └───────────┬───────────────┘  │
│              │ COM / USB        │
│              ▼                  │
│     BMDSwitcherAPI.dll          │
└──────────────┬──────────────────┘
               │ USB
        ┌──────▼──────┐
        │  ATEM Mini  │
        │  (macros)   │
        └─────────────┘
```

## Features

- **Direct USB connection** — auto-detects ATEM on startup, no IP needed
- **Macro grid** — 2-column grid showing all macros by name, one click to run
- **Running indicator** — green highlight + bottom bar shows active macro
- **Settings panel (⚙)** — connection status, manual IP connect, troubleshooting info
- **OBS-native dock** — drag and position anywhere in OBS like Audio Mixer

## Prerequisites

1. **ATEM Software Control** installed
   - Download from [Blackmagic Support](https://www.blackmagicdesign.com/support/family/atem-live-production-switchers)
   - This installs `BMDSwitcherAPI64.dll` and the COM type libraries

2. **ATEM SDK** downloaded
   - Get from [Blackmagic Developer](https://www.blackmagicdesign.com/developer/products/atem/sdk-and-software)
   - Extract — you need the `include/` folder with the SDK headers

3. **Build tools**
   - Visual Studio 2019 or 2022 (with C++ workload)
   - CMake 3.16+
   - Qt 6 (matching OBS Studio's Qt version) or Qt 5.15

4. **OBS Studio source** (for headers/libs)
   - Clone from https://github.com/obsproject/obs-studio
   - Or use an OBS SDK/development package

## Build

```powershell
# Clone this repo
cd obs-atem-macros

# Create build directory
mkdir build && cd build

# Configure — adjust paths to match your system
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DOBS_DIR="C:/obs-studio" ^
    -DATEM_SDK_DIR="C:/ATEM SDK/ATEM Switchers SDK" ^
    -DQt6_DIR="C:/Qt/6.5.3/msvc2019_64/lib/cmake/Qt6"

# Build
cmake --build . --config Release

# The output DLL will be in build/Release/obs-atem-macros.dll
```

## Install

Copy the built DLL to your OBS plugins folder:

```powershell
copy Release\obs-atem-macros.dll ^
    "C:\Program Files\obs-studio\obs-plugins\64bit\"
```

Or for portable installs:
```powershell
copy Release\obs-atem-macros.dll ^
    "<obs-portable>\obs-plugins\64bit\"
```

## Usage

1. Launch OBS Studio
2. Go to **View → Docks → ATEM Macros** to show the panel
3. The plugin auto-connects to your ATEM via USB on startup
4. Click any macro button to trigger it
5. Click **⚙** (gear icon) for connection settings and troubleshooting

## Troubleshooting

### "BMD SDK not available"
→ Install ATEM Software Control. The plugin needs `BMDSwitcherAPI64.dll`.

### "No response from ATEM"
→ Check USB cable. Run `ipconfig` in a terminal and look for a
  "Blackmagic Design" network adapter with a `169.254.x.x` address.

### No macros appear
→ Record macros using ATEM Software Control first. Macros are stored
  on the ATEM hardware and the plugin reads them from the device.

### Plugin doesn't appear in OBS
→ Check that the DLL is in the correct `obs-plugins/64bit/` folder.
  Check the OBS log file for `[ATEM Macros]` entries.

## File Structure

```
obs-atem-macros/
├── CMakeLists.txt              # Build configuration
├── README.md
└── src/
    ├── plugin-main.cpp         # OBS plugin entry point
    ├── atem-controller.h/cpp   # BMD SDK COM wrapper
    ├── macro-dock.h/cpp        # Qt dock widget (macro grid UI)
    └── settings-dialog.h/cpp   # Connection settings + troubleshooting
```

## Notes

- Multiple clients can connect to the ATEM simultaneously — having ATEM
  Software Control open alongside OBS is fine.
- Macros are stored on the ATEM hardware. Create/edit them in ATEM
  Software Control, then this plugin loads and triggers them by index.
- The plugin uses COM `STA`/`MTA` threading. OBS runs Qt on the main
  STA thread; ATEM callbacks are marshaled back via `QMetaObject::invokeMethod`.

## License

MIT
