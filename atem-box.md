# ATEM Mini Simulator

A Python stub that emulates a Blackmagic ATEM Mini on the network.
Speaks the ATEM UDP protocol on port 9910 so any ATEM client can connect
to it — including the BMDSwitcherAPI COM SDK, ATEM Software Control,
PyATEMMax, and the OBS plugin from this project.

No hardware needed. All macro executions are printed to the console.

## Usage

```bash
# Run with default macros
python atem_simulator.py

# Run with custom macro file
python atem_simulator.py --macros my_macros.tsv

# Run on a different port
python atem_simulator.py --port 9911
```

## TSV File Format

Two columns, tab-separated, no header row:

```
Wide Shot	Switch to Camera 1 wide angle
Close Up Pastor	Switch to Camera 2 close up
Lower Third On	Show lower third overlay
Lower Third Off	Hide lower third overlay
```

Column 1 = macro name, Column 2 = description.

## Connecting

### OBS Plugin (via settings gear)
Set connection to **Manual IP** → `127.0.0.1`

### ATEM Software Control
Open ATEM Software Control → connection dialog → enter `127.0.0.1`

### PyATEMMax (Python)
```python
import PyATEMMax
switcher = PyATEMMax.ATEMMax()
switcher.connect("127.0.0.1")
switcher.waitForConnection()
```

## Console Output

The simulator prints all received commands:

```
[14:32:01.234] SYN from 127.0.0.1:52341 (session 0x53ab)
[14:32:01.235] SYN-ACK sent to 127.0.0.1:52341 (new session 0x0002)
[14:32:01.240] Handshake complete with 127.0.0.1:52341
[14:32:01.280] State dump complete for 127.0.0.1:52341 — ready
[14:32:05.100] ▶ MACRO RUN #0: "Wide Shot"
[14:32:05.601] ■ MACRO COMPLETE #0: "Wide Shot"
[14:32:08.300] ▶ MACRO RUN #2: "Lower Third On"
[14:32:08.801] ■ MACRO COMPLETE #2: "Lower Third On"
[14:32:10.500] ■ MACRO STOP (was running #-1)
```

## Requirements

- Python 3.6+ (no external dependencies, stdlib only)
- Port 9910 available (or use --port to change)
- Run as admin if port 9910 is blocked by firewall

## Notes

- Macros auto-complete after 0.5 seconds (simulates execution)
- Multiple clients can connect simultaneously
- The simulator identifies itself as "ATEM Mini (Simulator)" firmware v10.2
- Clients that don't send packets for 10 seconds are disconnected