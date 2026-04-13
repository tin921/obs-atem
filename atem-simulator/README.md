# atem-simulator

Tools for developing and testing the OBS ATEM plugin without real hardware.

## Table of Contents

- [Files](#files)
- [Workflow 1 — Developing without hardware](#workflow-1--developing-without-hardware)
- [Workflow 2 — Updating the simulator from a real device](#workflow-2--updating-the-simulator-from-a-real-device)
- [Further reading](#further-reading)

## Files

|File|Purpose|
|----|-------|
|`run.py`|Emulates an ATEM Mini on the network — connect the plugin to this instead of real hardware|
|`capture.py`|Connects to a real ATEM and captures the full session with verbose logging|
|`captured-output.log`|Output from the last `capture.py` run — used to update `run.py`|
|`macros.tsv`|Macro definitions loaded by `run.py`|

## Workflow 1 — Developing without hardware

```bash
# 1. Start the simulator
python run.py --macros macros.tsv

# 2. In OBS plugin settings (gear icon):
#    Connection → Manual IP → 127.0.0.1
```

The plugin connects, loads macros, and buttons work as they would against
real hardware.

## Workflow 2 — Updating the simulator from a real device

Run this whenever you get access to real hardware, or after a BMD firmware
update that might change protocol fields.

```bash
# 1. Connect real ATEM via USB, then capture the full session
python capture.py
# → writes captured-output.log
```

Then share the log with Claude:

> "Here is the capture log from my ATEM Mini — update `run.py` to match"

Claude reads the SUMMARY section at the end of the log and updates:

|Log section|What gets updated in `run.py`|
|-----------|------------------------------|
|`_ver` decode|`build_firmware_version()`|
|`_pin` decode|`build_product_name()` — exact name, model byte, length|
|`_top` decode|`build_topology()` — all bytes in order|
|`_MeC`, `_mpl`, `_MAC`|their respective build functions|
|`VidM` decode|`build_video_mode()`|
|`MPrp` entries|default macros list + `macros.tsv`|
|`MRPr` probe responses|`build_macro_run_status()` — field layout, idle index value|
|`PrgI`/`PrvI` probe responses|program/preview input field format|
|Unknown command probes|confirms device is silent → no change needed in simulator|
|Step 5 probe timing|`complete_macro()` sleep duration|
|Step 4 average keepalive interval|`keepalive_loop()` sleep|

## Further reading

- [run.py.md](run.py.md) — usage, macro file format, connecting clients
- [capture.py.md](capture.py.md) — the 6-step capture process and log format
