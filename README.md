# OBS ATEM Panels

A native C++ OBS Studio plugin that adds dockable panels for a Blackmagic
ATEM Mini. It talks to the switcher directly through Blackmagic's
BMDSwitcherAPI COM SDK — no middleware server, no external process.

- **ATEM Macros** — one-click buttons for the macros stored on the ATEM
- **ATEM PiP** *(in development)* — pick the main input and the
  picture-in-picture source, then move, scale and crop the PiP box

---

## Features

**ATEM Macros**

- **Auto-connect** — reconnects on startup the way you last connected (USB or IP)
- **Macro grid** — 2-column grid showing all macros by name, one click to run
- **Running indicator** — green highlight + bottom bar shows the active macro, with STOP
- **Connection-loss detection** — shows the error and a reconnect button if the ATEM goes away
- **Settings (⚙)** — connection status, manual IP connect, troubleshooting info

**ATEM PiP** *(skeleton — layout still being designed)*

- Main (program) input and PiP (upstream key fill) input
- PiP on/off air
- Position X/Y, size and crop with sliders plus exact number entry
- Loads the current values from the ATEM on connect and follows changes made
  elsewhere (ATEM Software Control, hardware buttons, macros)

Both panels are normal OBS docks: show them from **Docks**, then drag them anywhere.

---

## Architecture

```text
┌──────────────────────────────────────────────┐
│                 OBS Studio                   │
│  ┌──────────────────┐  ┌──────────────────┐  │
│  │  ATEM Macros     │  │  ATEM PiP        │  │
│  │  (Qt panel)      │  │  (Qt panel)      │  │
│  └────────┬─────────┘  └─────────┬────────┘  │
│           └──────────┬───────────┘           │
│                      ▼                       │
│        AtemSession (one shared connection)   │
│                      │  COM calls            │
│                      ▼                       │
│             BMDSwitcherAPI64.dll             │
│        (installed by ATEM Software Control)  │
└──────────────────────┬───────────────────────┘
                       │  USB or Ethernet
                       ▼
                ┌─────────────┐
                │  ATEM Mini  │
                └─────────────┘
```

---

## Requirements

### To run (the streaming PC)

- **Windows**, 64-bit
- **OBS Studio 30 or newer** — the same major.minor version as the OBS source
  the plugin was built against, or newer (see [Version matching](#version-matching))
- **ATEM Software Control** installed — it installs and registers
  `BMDSwitcherAPI64.dll`. It does not need to be running.
- ATEM Mini connected by USB or Ethernet

### To build

1. **ATEM Software Control** (see above)
2. **Blackmagic ATEM SDK** — [Blackmagic Developer](https://www.blackmagicdesign.com/developer/products/atem/sdk-and-software).
   The Windows SDK ships `.idl` files; copy the pre-generated
   `Windows/Samples/DeviceInfo/BMDSwitcherAPI.h` into `Windows/include/`.
3. **Visual Studio 2022** (Desktop development with C++) or the VS 2022 Build Tools
4. **CMake 3.16+** (bundled with Visual Studio)
5. **Qt 6, MSVC 2022 64-bit** — use the same Qt version your OBS ships
   (check `Qt6Core.dll` in `C:\Program Files\obs-studio\bin\64bit\` → Properties → Details)
6. **OBS Studio source** for the plugin API headers — clone
   [obs-studio](https://github.com/obsproject/obs-studio) and check out the tag
   matching your installed OBS (e.g. `git checkout 32.1.0`)
7. **OBS import libraries** — OBS ships DLLs but no `.lib` files; generate them
   with `scripts\gen-obs-libs.ps1`

### Version matching

OBS refuses to load a plugin built against a *newer* libobs than the running
OBS (the OBS log says `compiled with newer libobs X.Y`). Build against the OBS
source tag that matches the OBS you install, or older.

---

## Build

Open **Developer PowerShell for VS 2022**, then:

```powershell
cd D:\cemc-sr\obs-atem

# 1. Generate OBS import libraries (once, and after every OBS update)
powershell -ExecutionPolicy Bypass -File scripts\gen-obs-libs.ps1

# 2. Configure
cmake -B build -G "Visual Studio 17 2022" -A x64 `
    -DOBS_DIR="D:/cemc-sr/obs-studio" `
    -DATEM_SDK_DIR="D:/cemc-sr/Blackmagic_ATEM_Switchers_SDK_10.2.1/Blackmagic ATEM Switchers SDK 10.2.1/Windows" `
    -DQt6_DIR="<Qt install>/msvc2022_64/lib/cmake/Qt6"

# 3. Build
cmake --build build --config Release
```

Outputs in `build\Release\`:

| File | What it is |
|---|---|
| `obs-atem.dll` | The OBS plugin |
| `atem-harness.exe` | The same panels in a plain window, for debugging without OBS |
| `atem-cli.exe` | Command-line diagnostics for the ATEM connection (no Qt, no OBS) |

---

## Install

```powershell
# Run as Administrator
copy build\Release\obs-atem.dll "C:\Program Files\obs-studio\obs-plugins\64bit\"
```

The DLL is the only file needed. Qt and the Visual C++ runtime come with OBS;
the ATEM SDK comes with ATEM Software Control.

---

## Usage

1. Launch OBS Studio
2. Open **Docks → ATEM Macros** and **Docks → ATEM PiP**
3. After OBS finishes loading, the plugin connects to the ATEM (USB the first
   time, then however you last connected)
4. Click a macro to run it; click **STOP** in the bottom bar to stop it
5. Click **⚙** in the macro panel for connection settings and troubleshooting

---

## Development tools

**atem-harness.exe** hosts the exact panel sources in a bare Qt window, so you
can run them under the Visual Studio debugger without OBS. Put your Qt `bin`
folder on `PATH` (or run `windeployqt atem-harness.exe`) before starting it.

**atem-cli.exe** checks the SDK layer against the real switcher:

```powershell
atem-cli                        # USB: model, macros, inputs and PiP state
atem-cli --ip 192.168.10.240    # same over Ethernet
atem-cli pip                    # inputs and PiP state only
atem-cli run 3                  # run macro #3
atem-cli stop                   # stop the running macro
```

Output is also written to `atem-cli.log` in the current folder.

---

## Troubleshooting

**"BMD SDK not available" / "Failed to create BMDSwitcherDiscovery"**
→ Install ATEM Software Control. If the DLL was copied manually, register it
as Administrator:
`regsvr32 "C:\Program Files (x86)\Blackmagic Design\Blackmagic ATEM Switchers\BMDSwitcherAPI64.dll"`

**"No response from ATEM"**
→ Open ATEM Software Control. If it cannot see the ATEM either, the problem is
the cable, driver or network, not the plugin. Over Ethernet, the PC must be on
the ATEM's subnet (default ATEM IP `192.168.10.240`).

**The panels don't appear under Docks**
→ Check the OBS log (**Help → Log Files**) for `[ATEM]` lines, or for
`compiled with newer libobs` (see [Version matching](#version-matching)).

**No macros appear**
→ Macros are stored on the ATEM. Record them in ATEM Software Control first;
this plugin reads and triggers them by index.

**PiP position/size do nothing**
→ Upstream key 1 must be a DVE key. The PiP panel shows a warning and a
**Set key type to DVE** button when it isn't.

---

## File structure

```text
obs-atem/
├── CMakeLists.txt          Build config (OBS SDK + BMD SDK + Qt)
├── plugin-main.cpp         OBS entry point: creates the session, registers both docks
├── atem-controller.h/cpp   BMD SDK wrapper: connection, macros, connection-loss detection
├── atem-pip.h/cpp          BMD SDK wrapper: inputs, upstream key DVE (PiP) state and control
├── bmd-util.h              COM callback template, BSTR/UTF-8 helpers
├── atem-session.h/cpp      Shared connection for all panels; SDK callbacks → Qt signals
├── panel-common.h/cpp      Shared panel stylesheet and header bar
├── macro-dock.h/cpp        ATEM Macros panel
├── pip-dock.h/cpp          ATEM PiP panel (skeleton)
├── settings-dialog.h/cpp   Connection settings + troubleshooting dialog
├── obs-log.h               blog() shim so panel code also builds outside OBS
├── harness/main.cpp        Standalone Qt host for the panels
├── cli/main.cpp            Command-line diagnostics
├── mockups/                HTML layout prototypes for the PiP panel
└── scripts/gen-obs-libs.ps1  Generates OBS import libraries
```

---

## Notes

- Several clients can connect to the ATEM at once — ATEM Software Control can
  stay open alongside OBS.
- Using the ATEM as an OBS webcam source is separate from this control
  connection; both work together.
- Windows only — BMDSwitcherAPI is a COM library.

---

## License

See [LICENSE](LICENSE).
