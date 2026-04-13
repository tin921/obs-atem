# run.py — ATEM Mini Simulator

Emulates a Blackmagic ATEM Mini on UDP port 9910. Any ATEM client can connect
to it — including the BMDSwitcherAPI COM SDK (used by the OBS plugin), ATEM
Software Control, and PyATEMMax. No hardware required.

Protocol field values (topology, video mode, product name, etc.) are sourced from a real ATEM Mini capture — see [README.md](README.md) for how to refresh them


## Table of Contents

- [Usage](#usage)
- [Macro file format (TSV)](#macro-file-format-tsv)
- [Connecting clients](#connecting-clients)
  - [OBS plugin](#obs-plugin)
  - [ATEM Software Control](#atem-software-control)
  - [PyATEMMax (Python)](#pyatemmax-python)
- [Console output](#console-output)
- [Notes](#notes)

## Usage

```bash
python run.py                          # default macros, port 9910
python run.py --macros macros.tsv      # load macros from TSV file
python run.py --port 9911              # run on a different port
```

## Macro file format (TSV)

Two columns, tab-separated, no header row:

```
Wide Shot	Switch to Camera 1 wide angle
Close Up Pastor	Switch to Camera 2 close up
Lower Third On	Show lower third overlay
Lower Third Off	Hide lower third overlay
```

Column 1 = macro name, column 2 = description. If no file is provided,
built-in defaults are used. The macro list in `macros.tsv` can be generated
from a real device capture — see [README.md](README.md).

## Connecting clients

### OBS plugin

Open the settings gear in the ATEM Macros dock, set connection to
**Manual IP** and enter `127.0.0.1`.

### ATEM Software Control

Open ATEM Software Control → connection dialog → enter `127.0.0.1`.

### PyATEMMax (Python)

```python
import PyATEMMax
switcher = PyATEMMax.ATEMMax()
switcher.connect("127.0.0.1")
switcher.waitForConnection()
```

## Console output

```
[14:32:01.234] SYN from 127.0.0.1:52341 (session 0x53ab)
[14:32:01.235] SYN-ACK sent to 127.0.0.1:52341 (session 0x0002)
[14:32:01.240] Handshake complete with 127.0.0.1:52341
[14:32:01.280] State dump complete for 127.0.0.1:52341 — ready
[14:32:05.100] ▶ MACRO RUN #0: "Wide Shot"
[14:32:05.601] ■ MACRO COMPLETE #0: "Wide Shot"
[14:32:08.300] ▶ MACRO RUN #2: "Lower Third On"
[14:32:08.801] ■ MACRO COMPLETE #2: "Lower Third On"
[14:32:10.500] ■ MACRO STOP (was running #-1)
```

## Notes

- Macros auto-complete after 0.5 seconds
- Multiple clients can connect simultaneously
- Clients silent for 10 seconds are disconnected
