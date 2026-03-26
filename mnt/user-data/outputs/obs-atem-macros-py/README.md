# ATEM Macro Panel — OBS Python Script

A single-file OBS Python script that connects to your Blackmagic ATEM Mini
and provides buttons to trigger macros. Uses PyATEMMax (pure Python, zero
BMD SDK dependencies).

## Architecture

```
┌─────────────────────────────────────────┐
│            OBS Studio                   │
│  ┌───────────────────────────────────┐  │
│  │  Tools → Scripts → ATEM Panel     │  │
│  │                                   │  │
│  │  ⚙ Connection                     │  │
│  │  [USB ▼] [Connect] [Disconnect]   │  │
│  │  Status: Connected — ATEM Mini    │  │
│  │                                   │  │
│  │  🎛 Macros                         │  │
│  │  [#1  Wide Shot          ]        │  │
│  │  [#2  Close Up           ]        │  │
│  │  [#3  Lower Third On    ]        │  │
│  │  [#4  Lower Third Off   ]        │  │
│  │  [■  STOP MACRO          ]        │  │
│  │                                   │  │
│  │  🔧 Troubleshooting               │  │
│  └───────────────┬───────────────────┘  │
│                  │                      │
│           PyATEMMax (pure Python)       │
│           implements ATEM protocol      │
└──────────────────┬──────────────────────┘
                   │ USB / Ethernet (UDP port 9910)
            ┌──────▼──────┐
            │  ATEM Mini  │
            │  (macros)   │
            └─────────────┘
```

## Setup

### 1. Install PyATEMMax

Use the same Python installation that OBS is configured to use:

```bash
pip install PyATEMMax
```

### 2. Add the script to OBS

1. Open OBS → **Tools → Scripts**
2. Go to **Python Settings** tab → set your Python install path
3. Go to **Scripts** tab → click **+**
4. Select `atem-macro-panel.py`

### 3. Connect

1. Select connection mode: **USB (auto-detect)** or **Manual IP**
2. Click **Connect to ATEM**
3. Macro buttons appear automatically

## Usage

- Click any macro button to trigger it
- Click **■ STOP MACRO** to halt a running macro
- Click **↻ Refresh Macros** if you've added new macros via ATEM Software Control
- Check **🔧 Troubleshooting** section for connection diagnostics

## UI Location

The panel lives in **Tools → Scripts → (select atem-macro-panel.py)**.

OBS Python scripts cannot create custom dock widgets (that requires C++).
The properties panel in the Scripts dialog is the available UI surface.
If you need a floating dock, use the C++ plugin version instead.

## Comparison: Python vs C++ Plugin

| Feature           | Python Script        | C++ Plugin           |
|-------------------|----------------------|----------------------|
| UI Location       | Tools → Scripts      | Dockable panel       |
| Install           | Copy 1 file          | Compile DLL          |
| Dependencies      | pip install PyATEMMax| BMD SDK + Qt + OBS SDK|
| Edit & iterate    | Edit .py, reload     | Recompile            |
| Custom dock       | ✗ Not possible       | ✓ Full Qt dock       |
| BMD SDK needed    | ✗ No                 | ✓ Yes                |
| Cross-platform    | ✓ Win/Mac/Linux      | Windows only (COM)   |

## Troubleshooting

### "PyATEMMax not installed"
Run `pip install PyATEMMax` using the same Python OBS is pointed at.
Check: Tools → Scripts → Python Settings → Python Install Path.

### USB not detecting
The ATEM creates a virtual ethernet adapter over USB. The script tries
the default IP (192.168.10.240). If your ATEM has a different IP, use
Manual IP mode.

Run `ipconfig` (Windows) or `ifconfig` (Mac/Linux) and look for a
Blackmagic Design network adapter.

### No macros appear
Record macros in ATEM Software Control first. They're stored on the
ATEM hardware. Click "↻ Refresh Macros" after adding new ones.

## License

MIT
