# capture.py — ATEM Protocol Capture & Probe Tool

Connects to a real ATEM Mini and captures the complete session lifecycle with
verbose hex logging. The output (`captured-output.log`) is the ground truth
used to keep `run.py` accurate.

## Table of Contents

- [Usage](#usage)
- [What it captures](#what-it-captures-6-steps)
  - [Step 1 — Connection (handshake)](#step-1--connection-handshake)
  - [Step 2 — Fetch macro list (state dump)](#step-2--fetch-macro-list-state-dump)
  - [Step 3 — Control channel (secondary SYN)](#step-3--control-channel-secondary-syn)
  - [Step 4 — Keepalive timing](#step-4--keepalive-timing)
  - [Step 5 — Play macros (probes)](#step-5--play-macros-probes)
  - [Step 6 — Disconnect](#step-6--disconnect)
- [Log format](#log-format)
- [Requirements](#requirements)

## Usage

```bash
python capture.py              # connects to 192.168.10.240 (default)
python capture.py 192.168.10.1 # specify IP
```

Output goes to both stdout and `captured-output.log` (overwritten each run).

## What it captures (6 steps)

### Step 1 — Connection (handshake)

Performs the 3-way UDP handshake the BMD SDK uses to establish a session.

- Sends SYN with client hello payload (`0x01` + 7 zero bytes)
- Receives SYN-ACK from device (server assigns session ID)
- Sends ACK to complete handshake

**Logged:** raw bytes both directions, full header decode (flags, session ID,
sequence numbers), meaning of each payload byte.

### Step 2 — Fetch macro list (state dump)

After the handshake, the device automatically pushes its complete current state
as a series of UDP packets. This is how the plugin gets the macro list.

Each packet contains named fields. Every field is logged with raw hex bytes,
a human-readable decode, and a `struct.pack(...)` call ready to paste into
`run.py`.

Key fields captured:

|Field|Contains|
|-----|--------|
|`_ver`|Protocol version (e.g. 2.28)|
|`_pin`|Product name ("ATEM Mini") + model byte|
|`_top`|Hardware topology — M/E units, sources, keyers, AUX busses, etc.|
|`_MeC`|M/E configuration (keyer count)|
|`_mpl`|Media player slot count|
|`_MAC`|Macro pool size (e.g. 100 slots)|
|`VidM`|Current video mode (1080p50, 720p59, etc.)|
|`PrgI`|Current program input (what's on air)|
|`PrvI`|Current preview input|
|`MPrp`|**One per macro slot** — index, name, description, isUsed flag|
|`MRPr`|Current macro run status (idle / running / waiting)|
|`TlIn`|Tally state per input|
|`InCm`|Init complete — end of state dump marker|

All `MPrp` fields are collected as a list so every macro slot is captured.
The `InCm` field signals dump completion.

### Step 3 — Control channel (secondary SYN)

The BMD SDK sends a second SYN packet (session `0x8000`) after receiving the
state dump to open a dedicated control channel. The capture explicitly handles
this and waits for the exchange to complete before running probes — ensuring
commands are sent only when the device is fully ready.

**Logged:** secondary SYN session ID, SYN-ACK sent, ACK received, timing
relative to state dump completion.

### Step 4 — Keepalive timing

Listens passively for 12 seconds without sending anything — measures how
frequently the device sends its own keepalive packets.

**Logged:** timestamp and interval of every device-initiated packet, plus
min/max/average interval.

### Step 5 — Play macros (probes)

Sends a series of commands to the real device and captures its full response:

|Probe|Command|Expected response|
|-----|-------|-----------------|
|Macro run #0, #1|`MSRc` action=0|`MRPr` running=1, then `MRPr` running=0 on complete|
|Macro stop|`MSRc` action=1|`MRPr` running=0, index=0xffff|
|Program input → Input 2|`CPgI`|`PrgI` pushed back confirming change|
|Program input → Input 1 (restore)|`CPgI`|`PrgI` restore|
|Preview input → Input 3|`CPvI`|`PrvI` pushed back|
|Preview input → Input 2 (restore)|`CPvI`|`PrvI` restore|
|Cut transition|`DCut`|`PrgI`/`PrvI` swapped|
|Auto transition|`DAut`|transition state updates|
|Unknown command `????`|—|device silent (no response)|
|Unknown command `XXXX`|—|device silent (no response)|
|Empty reliable packet|—|ACK from device|

**Logged:** every command sent (raw bytes + label), every response packet
(raw bytes + decoded fields), response count per probe, macro completion timing.

### Step 6 — Disconnect

Probes how the device handles session teardown:

|Sub-step|What happens|Purpose|
|--------|-----------|-------|
|6a — silence|Stop all traffic for 15s, do not ACK|Observe if device sends a disconnect notification or just stops|
|6b — SYN-only|Send SYN with no payload|Test if SYN-only acts as a reset/FIN signal|
|6c — no-flags|Send packet with flags=0x00|Test unrecognised flag combination|
|6d — reconnect|New SYN with fresh session ID|Confirm device accepts reconnect without explicit disconnect|

**Logged:** all device packets during silence (with timestamps), device
response to each probe packet, whether reconnect SYN-ACK is received.

## Log format

```
[HH:MM:SS.mmm]   # TX <label>             ← comment: what is being sent
[HH:MM:SS.mmm]   >> ip:port [N bytes]     ← raw bytes sent to device
[HH:MM:SS.mmm]     0000  aa bb cc ...     ← hex dump (16 bytes/row) + ASCII

[HH:MM:SS.mmm]   # RX <label> (pkt #N)   ← comment: what was received
[HH:MM:SS.mmm]   << ip:port [N bytes]     ← raw bytes received from device
[HH:MM:SS.mmm]     0000  aa bb cc ...
[HH:MM:SS.mmm]   hdr: flags=... session=0x... ack_id=... local_seq=...
[HH:MM:SS.mmm]   ┌─ [FieldName]
[HH:MM:SS.mmm]   │  raw (N bytes): aabbcc...
[HH:MM:SS.mmm]   │  decoded meaning
[HH:MM:SS.mmm]   │  -> struct.pack(...)
[HH:MM:SS.mmm]   └─
```

At the end, a **SUMMARY** section prints all `struct.pack` values and the macro
list as a TSV block.

## Requirements

- Python 3.6+ (no external dependencies, stdlib only)
- ATEM Mini connected via USB or Ethernet
- Port 9910 reachable (check Windows Firewall if no response)
