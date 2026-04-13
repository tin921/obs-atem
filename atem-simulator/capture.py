"""
ATEM Mini — Protocol Capture & Probe Tool
==========================================
Connects to a real ATEM device and captures the full session lifecycle:

  1. Handshake  — SYN / SYN-ACK / ACK
  2. State dump — all fields including every MPrp macro slot
  3. Control channel — waits for and completes the secondary SYN (session 0x8000)
  4. Keepalive  — measures device-initiated keepalive interval passively
  5. Probes     — macro run/stop, program/preview input, cut, auto, unknown cmds
  6. Disconnect — probes clean teardown; observes device reaction to silence

All packets (sent and received) are hex-dumped with timestamps.
Output goes to stdout AND captured-output.log (overwritten each run).

Usage:
    python capture.py [ip]
    python capture.py 192.168.10.240

Default IP: 192.168.10.240 (ATEM Mini via USB virtual ethernet)
"""

import socket
import struct
import sys
import time
import os
from datetime import datetime

# ── Constants ─────────────────────────────────────────────────────────────────

ATEM_PORT   = 9910
HEADER_SIZE = 12

FLAG_RELIABLE    = 0x08
FLAG_SYN         = 0x10
FLAG_RETRANSMIT  = 0x20
FLAG_REQ_RETRANS = 0x40
FLAG_ACK         = 0x80

FLAG_NAMES = {
    FLAG_RELIABLE:    'RELIABLE',
    FLAG_SYN:         'SYN',
    FLAG_RETRANSMIT:  'RETRANSMIT',
    FLAG_REQ_RETRANS: 'REQ_RETRANS',
    FLAG_ACK:         'ACK',
}

LOG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'captured-output.log')

# ── Logging ───────────────────────────────────────────────────────────────────

log_file = None

def ts():
    return datetime.now().strftime('%H:%M:%S.%f')[:-3]

def log(msg=''):
    line = f'[{ts()}] {msg}' if msg else ''
    print(line)
    if log_file:
        log_file.write(line + '\n')
        log_file.flush()

def log_raw(direction, data, addr=None):
    addr_str = f' {addr[0]}:{addr[1]}' if addr else ''
    arrow    = '>>' if direction == 'TX' else '<<'
    log(f'  {arrow}{addr_str} [{len(data)} bytes]')
    for i in range(0, len(data), 16):
        chunk      = data[i:i+16]
        hex_part   = ' '.join(f'{b:02x}' for b in chunk)
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        log(f'    {i:04x}  {hex_part:<47}  {ascii_part}')

def log_section(title):
    bar = '─' * max(0, 62 - len(title))
    log()
    log(f'── {title} {bar}')

# ── Packet Helpers ────────────────────────────────────────────────────────────

def flag_str(flags):
    parts = [name for bit, name in FLAG_NAMES.items() if flags & bit]
    return '|'.join(parts) if parts else f'0x{flags:02x}'

def build_header(flags, length, session_id, ack_id=0, remote_seq=0, local_seq=0):
    byte0 = (flags & 0xF8) | ((length >> 8) & 0x07)
    byte1 = length & 0xFF
    return struct.pack('>BBHHHHH', byte0, byte1, session_id, ack_id, remote_seq, 0, local_seq)

def parse_header(data):
    if len(data) < HEADER_SIZE:
        return None
    b0, b1, session, ack_id, unk, remote_seq, local_seq = struct.unpack('>BBHHHHH', data[:12])
    flags  = b0 & 0xF8
    length = ((b0 & 0x07) << 8) | b1
    return {
        'flags':      flags,
        'length':     length,
        'session':    session,
        'ack_id':     ack_id,
        'unknown':    unk,
        'remote_seq': remote_seq,
        'local_seq':  local_seq,
        'payload':    data[HEADER_SIZE:],
        'raw':        data,
    }

def log_header(pkt, prefix=''):
    log(f'  {prefix}flags={flag_str(pkt["flags"])} len={pkt["length"]} '
        f'session=0x{pkt["session"]:04x} ack_id={pkt["ack_id"]} '
        f'remote_seq={pkt["remote_seq"]} local_seq={pkt["local_seq"]} '
        f'unknown=0x{pkt["unknown"]:04x}')

def parse_fields(payload):
    fields = []
    offset = 0
    while offset + 8 <= len(payload):
        flen = struct.unpack('>H', payload[offset:offset+2])[0]
        if flen < 8:
            log(f'  [!] field length {flen} < 8 at offset {offset}, stopping parse')
            break
        name  = payload[offset+4:offset+8].decode('ascii', errors='replace')
        fdata = payload[offset+8:offset+flen]
        fields.append((name, fdata))
        offset += flen
        if offset > len(payload):
            log(f'  [!] field overran payload boundary at offset {offset}')
            break
    return fields

def build_field(cmd_name, data):
    name_bytes = cmd_name.encode('ascii')[:4].ljust(4, b'\x00')
    total_len  = 8 + len(data)
    return struct.pack('>HH', total_len, 0) + name_bytes + data

# ── Field Decoders ────────────────────────────────────────────────────────────

VIDEO_MODES = {
    0x35323570: '525i59.94 NTSC',
    0x36323569: '625i50 PAL',
    0x37323050: '720p50',
    0x37323059: '720p59.94',
    0x31303869: '1080i50',
    0x31303859: '1080i59.94',
    0x31303870: '1080p23.98',
    0x31303824: '1080p24',
    0x31303825: '1080p25',
    0x31303830: '1080p30',
    0x31307073: '1080p50',
    0x31307059: '1080p59.94',
    0x31307060: '1080p60',
    0x32313625: '2160p25',
    0x32313630: '2160p30',
    0x32313659: '2160p59.94',
    0x32313660: '2160p60',
}

SOURCE_NAMES = {
    0: 'Black', 1: 'Input 1', 2: 'Input 2', 3: 'Input 3', 4: 'Input 4',
    5: 'Input 5', 6: 'Input 6', 7: 'Input 7', 8: 'Input 8',
    1000: 'Color Bars', 2001: 'Color 1', 2002: 'Color 2',
    3010: 'Media Player 1', 3011: 'Media Player 1 Key',
    3020: 'Media Player 2', 3021: 'Media Player 2 Key',
    10010: 'Clean Feed 1', 10011: 'Clean Feed 2',
    11001: 'Aux 1',
    13001: 'ME 1 Program', 13002: 'ME 1 Preview',
}

def src_name(src):
    return SOURCE_NAMES.get(src, f'Source {src}')

def decode_field(name, data):
    lines = [f'raw ({len(data)} bytes): {data.hex()}']

    if name == '_ver' and len(data) >= 4:
        major, minor = struct.unpack('>HH', data[:4])
        lines += [
            f'protocol version: {major}.{minor}',
            f'  -> struct.pack(">HH", {major}, {minor})',
            f'  note: major always 2; minor=28→fw8.0, 30→fw8.1, 31→fw9.4, 32→fw9.6',
        ]

    elif name == '_pin' and len(data) >= 44:
        prod  = data[:44].rstrip(b'\x00').decode('utf-8', errors='replace')
        model = data[44] if len(data) > 44 else 0
        extra = data[45:].hex() if len(data) > 45 else '(none)'
        lines += [
            f'product name:  "{prod}"',
            f'model byte:    0x{model:02x}',
            f'extra bytes:   {extra}',
            f'total length:  {len(data)} bytes',
            f'  -> name="{prod}", model=0x{model:02x}',
            f'  known models: 0x00=ATEM 1M/E, 0x01=ATEM Mini, 0x02=ATEM Mini Pro',
        ]

    elif name == '_top':
        labels = [
            'M/E units', 'sources', 'downstream keyers', 'AUX busses',
            'MixMinus outputs', 'media players', 'multiviewers', 'rs485',
            'hyperdecks', 'DVE', 'stingers', 'supersources',
            'unknown[12]', 'unknown[13]', 'unknown[14]', 'scalers',
            'unknown[16]', 'unknown[17]', 'camera control', 'unknown[19]',
            'unknown[20]', 'unknown[21]', 'adv chroma keyers',
            'configurable outputs', 'unknown[24]',
        ]
        for i in range(len(data)):
            label = labels[i] if i < len(labels) else f'unknown[{i}]'
            lines.append(f'  [{i:2d}] 0x{data[i]:02x} = {data[i]:3d}  ({label})')
        lines.append(f'  total: {len(data)} bytes')
        lines.append(f'  -> bytes={data.hex()}')

    elif name == '_MeC' and len(data) >= 2:
        me, keyers = struct.unpack('>BB', data[:2])
        lines += [f'M/E index={me} keyers={keyers}',
                  f'  -> struct.pack(">BBH", {me}, {keyers}, 0)']

    elif name == '_mpl' and len(data) >= 2:
        stills, clips = struct.unpack('>BB', data[:2])
        lines += [f'media player stills={stills} clips={clips}',
                  f'  -> struct.pack(">BBH", {stills}, {clips}, 0)']

    elif name == '_MAC' and len(data) >= 1:
        count = data[0]
        lines += [f'macro pool size={count}',
                  f'  -> struct.pack(">B3x", {count})']

    elif name == 'VidM' and len(data) >= 4:
        raw    = struct.unpack('>I', data[:4])[0]
        desc   = VIDEO_MODES.get(raw, 'unknown mode')
        as_str = data[:4].decode('ascii', errors='replace')
        lines += [f'video mode: 0x{raw:08x} = "{as_str}" ({desc})',
                  f'  -> struct.pack(">I", 0x{raw:08x})']

    elif name == 'PrgI' and len(data) >= 4:
        me  = data[0]
        src = struct.unpack('>H', data[2:4])[0]
        lines += [f'program input: M/E={me} source={src} ({src_name(src)})',
                  f'  -> struct.pack(">BxH", {me}, {src})']

    elif name == 'PrvI' and len(data) >= 4:
        me  = data[0]
        src = struct.unpack('>H', data[2:4])[0]
        lines += [f'preview input: M/E={me} source={src} ({src_name(src)})',
                  f'  -> struct.pack(">BxH", {me}, {src})']

    elif name == 'MPrp' and len(data) >= 8:
        idx, is_used, has_unsupported = struct.unpack('>HBB', data[:4])
        name_len, desc_len            = struct.unpack('>HH', data[4:8])
        offset     = 8
        macro_name = data[offset:offset+name_len].decode('utf-8', errors='replace')
        offset    += name_len
        macro_desc = data[offset:offset+desc_len].decode('utf-8', errors='replace')
        lines += [
            f'macro index={idx} isUsed={is_used} hasUnsupported={has_unsupported}',
            f'  name ({name_len}b): "{macro_name}"',
            f'  desc ({desc_len}b): "{macro_desc}"',
            f'  total field data: {len(data)} bytes',
        ]

    elif name == 'MRPr' and len(data) >= 6:
        running, waiting, loop, pad, index = struct.unpack('>BBBBH', data[:6])
        lines += [
            f'macro run status: running={running} waiting={waiting} loop={loop} '
            f'pad={pad} index={index} (0xffff=none)',
            f'  -> struct.pack(">BBBBHH", {running}, {waiting}, {loop}, 0, {index}, 0)',
        ]

    elif name == 'TlIn' and len(data) >= 2:
        count = struct.unpack('>H', data[:2])[0]
        lines.append(f'tally input count={count}')
        for i in range(min(count, len(data) - 2)):
            lines.append(f'  input {i+1}: tally=0x{data[2+i]:02x}')

    elif name == 'TlSr' and len(data) >= 2:
        count = struct.unpack('>H', data[:2])[0]
        lines.append(f'tally source count={count}')

    elif name == 'InCm':
        lines.append('*** INIT COMPLETE — end of state dump ***')

    return lines

# ── Capture Engine ────────────────────────────────────────────────────────────

class ATEMCapture:
    def __init__(self, ip):
        self.ip          = ip
        self.session     = 0x0000
        self.ctrl_session = None   # secondary control channel session id
        self.local_seq   = 0
        self.sock        = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(3.0)

        self.all_fields  = {}   # name -> last fdata (single-value fields)
        self.all_macros  = []   # list of (index, name, desc, is_used) from MPrp
        self.packet_num  = 0

    # ── Low-level I/O ─────────────────────────────────────────

    def send(self, data, label=''):
        if label:
            log(f'  # TX {label}')
        log_raw('TX', data, (self.ip, ATEM_PORT))
        self.sock.sendto(data, (self.ip, ATEM_PORT))

    def recv(self, timeout=3.0, label=''):
        self.sock.settimeout(timeout)
        try:
            data, addr = self.sock.recvfrom(4096)
        except socket.timeout:
            if label:
                log(f'  # [timeout waiting for {label}]')
            return None, None
        self.packet_num += 1
        if label:
            log(f'  # RX {label} (pkt #{self.packet_num})')
        log_raw('RX', data, addr)
        pkt = parse_header(data)
        if pkt:
            log_header(pkt, prefix='  hdr: ')
        return pkt, addr

    def send_ack(self, ack_num, session=None, label='ACK'):
        sid = session if session is not None else self.session
        hdr = build_header(FLAG_ACK, HEADER_SIZE, sid, ack_id=ack_num)
        self.send(hdr, label=label)

    def send_command(self, cmd_name, cmd_data, label=''):
        self.local_seq += 1
        field   = build_field(cmd_name, cmd_data)
        pkt_len = HEADER_SIZE + len(field)
        hdr     = build_header(FLAG_RELIABLE | FLAG_ACK, pkt_len,
                               self.session, local_seq=self.local_seq)
        self.send(hdr + field, label=label or f'CMD {cmd_name}')

    def drain(self, timeout=1.5):
        """Receive all packets until timeout. ACKs reliable packets. Returns list of pkts."""
        responses = []
        self.sock.settimeout(0.2)
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                data, addr = self.sock.recvfrom(4096)
                self.packet_num += 1
                log_raw('RX', data, addr)
                pkt = parse_header(data)
                if not pkt:
                    continue
                log_header(pkt, prefix='  hdr: ')
                if pkt['flags'] & FLAG_RELIABLE and pkt['local_seq']:
                    self.send_ack(pkt['local_seq'],
                                  label=f'ACK seq={pkt["local_seq"]}')
                if pkt['payload']:
                    fields = parse_fields(pkt['payload'])
                    for fname, fdata in fields:
                        log(f'  field [{fname}]:')
                        for line in decode_field(fname, fdata):
                            log(f'    {line}')
                responses.append(pkt)
            except socket.timeout:
                continue
        return responses

    # ── Step 1: Handshake ─────────────────────────────────────

    def do_handshake(self, my_session=0x1fed):
        log_section('STEP 1: HANDSHAKE')

        syn_payload = struct.pack('>B7x', 0x01)
        pkt_len     = HEADER_SIZE + len(syn_payload)
        syn         = build_header(FLAG_SYN, pkt_len, my_session) + syn_payload
        log(f'SYN payload byte[0]=0x01 (client hello); bytes[1-7]=0x00 (always zero per BMD SDK)')
        self.send(syn, label=f'SYN session=0x{my_session:04x}')

        pkt, _ = self.recv(label='SYN-ACK')
        if not pkt:
            log('ERROR: no SYN-ACK received — is the ATEM connected?')
            return False
        if not (pkt['flags'] & FLAG_SYN):
            log(f'ERROR: expected SYN flag, got flags={flag_str(pkt["flags"])}')
            return False

        self.session = pkt['session']
        log(f'SYN-ACK: server assigned session=0x{self.session:04x}')
        if pkt['payload']:
            log(f'  SYN-ACK payload: {pkt["payload"].hex()}')
            b0 = pkt['payload'][0] if pkt['payload'] else 0
            log(f'  payload byte[0]=0x{b0:02x} (0x02=server hello, always 2)')

        ack = build_header(FLAG_ACK, HEADER_SIZE, self.session)
        self.send(ack, label='ACK (handshake complete)')
        log('Handshake done — device will now send state dump')
        return True

    # ── Step 2: State Dump ────────────────────────────────────

    def receive_state_dump(self):
        log_section('STEP 2: STATE DUMP (macro list + device state)')
        log('Waiting for device to push all state fields...')
        log('Each MPrp field = one macro slot (used or empty)')

        pkt_count  = 0
        field_count = 0

        while True:
            pkt, _ = self.recv(timeout=5.0, label='state dump')
            if pkt is None:
                log('[!] Timeout during state dump — dump may be incomplete')
                break

            pkt_count += 1

            # Secondary SYN arrives mid-dump — handle it but don't start probes yet
            if pkt['flags'] & FLAG_SYN:
                self.ctrl_session = pkt['session']
                log(f'  [secondary SYN] session=0x{self.ctrl_session:04x} — control channel init')
                log(f'  Sending SYN-ACK for control channel (no state dump on this channel)')
                payload = struct.pack('>B7x', 0x02)
                pkt_len = HEADER_SIZE + len(payload)
                hdr     = build_header(FLAG_SYN, pkt_len, self.ctrl_session)
                self.send(hdr + payload, label=f'SYN-ACK control session=0x{self.ctrl_session:04x}')
                continue

            # ACK all reliable packets
            if pkt['flags'] & FLAG_RELIABLE and pkt['local_seq']:
                self.send_ack(pkt['local_seq'],
                              label=f'ACK seq={pkt["local_seq"]}')

            if not pkt['payload']:
                log(f'  [empty packet — keepalive or dump boundary marker]')
                continue

            fields = parse_fields(pkt['payload'])
            log(f'  packet #{pkt_count}: {len(fields)} field(s)')
            field_count += len(fields)

            done = False
            for fname, fdata in fields:
                log()
                log(f'  ┌─ [{fname}]')
                for line in decode_field(fname, fdata):
                    log(f'  │  {line}')
                log(f'  └─')

                # Store fields — MPrp collected as list, others as dict
                if fname == 'MPrp' and len(fdata) >= 8:
                    idx, is_used, _ = struct.unpack('>HBB', fdata[:4])
                    name_len, desc_len = struct.unpack('>HH', fdata[4:8])
                    offset     = 8
                    macro_name = fdata[offset:offset+name_len].decode('utf-8', errors='replace')
                    offset    += name_len
                    macro_desc = fdata[offset:offset+desc_len].decode('utf-8', errors='replace')
                    self.all_macros.append({
                        'index':    idx,
                        'is_used':  is_used,
                        'name':     macro_name,
                        'desc':     macro_desc,
                        'raw':      fdata.hex(),
                    })
                else:
                    self.all_fields[fname] = fdata

                if fname == 'InCm':
                    done = True

            if done:
                log()
                log(f'State dump complete — {pkt_count} packets received, '
                    f'{field_count} total fields, '
                    f'{len(self.all_macros)} macro slots')
                break

    # ── Step 3: Control Channel Handshake ─────────────────────

    def wait_for_control_channel(self):
        log_section('STEP 3: CONTROL CHANNEL (secondary SYN)')

        if self.ctrl_session is not None:
            log(f'Secondary SYN already received during state dump '
                f'(session=0x{self.ctrl_session:04x})')
            log('Waiting for ACK from device to complete control channel...')
            pkt, _ = self.recv(timeout=2.0, label='control channel ACK')
            if pkt and (pkt['flags'] & FLAG_ACK):
                log(f'Control channel ACK received — control channel ready')
            elif pkt is None:
                log('[!] No ACK for control channel — device may not require it')
            else:
                log(f'Unexpected packet on control channel: flags={flag_str(pkt["flags"])}')
        else:
            log('Secondary SYN not yet seen — waiting up to 3s...')
            pkt, _ = self.recv(timeout=3.0, label='secondary SYN')
            if pkt and (pkt['flags'] & FLAG_SYN):
                self.ctrl_session = pkt['session']
                log(f'Secondary SYN received (session=0x{self.ctrl_session:04x})')
                payload = struct.pack('>B7x', 0x02)
                pkt_len = HEADER_SIZE + len(payload)
                hdr     = build_header(FLAG_SYN, pkt_len, self.ctrl_session)
                self.send(hdr + payload, label='SYN-ACK control channel')
                # Wait for its ACK
                pkt2, _ = self.recv(timeout=2.0, label='control ACK')
                if pkt2 and (pkt2['flags'] & FLAG_ACK):
                    log('Control channel ACK received — fully connected')
                else:
                    log('[!] No ACK for control channel SYN-ACK')
            else:
                log('[!] No secondary SYN received — BMD SDK may not have sent one')

    # ── Step 4: Keepalive Timing ──────────────────────────────

    def measure_keepalive(self):
        log_section('STEP 4: KEEPALIVE TIMING MEASUREMENT')
        log('Listening passively for 12 seconds — measuring device-initiated keepalive interval')
        log('Not sending anything — observing raw device behaviour')

        intervals  = []
        last_time  = time.time()
        start_time = time.time()
        pkt_count  = 0
        deadline   = start_time + 12.0

        self.sock.settimeout(0.5)
        while time.time() < deadline:
            try:
                data, addr = self.sock.recvfrom(4096)
                self.packet_num += 1
                now = time.time()
                elapsed = now - last_time
                last_time = now

                pkt_count += 1
                log(f'  device packet #{pkt_count} after {elapsed:.3f}s gap')
                log_raw('RX', data, addr)
                pkt = parse_header(data)
                if pkt:
                    log_header(pkt, prefix='  hdr: ')
                    # ACK to stay connected
                    if pkt['flags'] & FLAG_RELIABLE and pkt['local_seq']:
                        self.send_ack(pkt['local_seq'],
                                      label=f'ACK seq={pkt["local_seq"]}')
                    if pkt['payload']:
                        fields = parse_fields(pkt['payload'])
                        for fname, fdata in fields:
                            log(f'  field [{fname}]: {fdata.hex()}')

                if pkt_count > 1:  # first gap is from end of state dump, not representative
                    intervals.append(elapsed)

            except socket.timeout:
                continue

        if intervals:
            avg = sum(intervals) / len(intervals)
            log()
            log(f'Keepalive results over 12s:')
            log(f'  packets received:  {pkt_count}')
            log(f'  intervals (s):     {[f"{i:.3f}" for i in intervals]}')
            log(f'  average interval:  {avg:.3f}s')
            log(f'  min interval:      {min(intervals):.3f}s')
            log(f'  max interval:      {max(intervals):.3f}s')
            log(f'  -> simulator keepalive_loop sleep should be ~{avg:.1f}s')
        else:
            log(f'[!] No device-initiated packets observed in 12s')
            log(f'    Device may only send keepalives in response to client keepalives')

    # ── Step 5: Probes ────────────────────────────────────────

    def probe(self, label, cmd_name, cmd_data, wait=2.0, notes=''):
        log_section(f'PROBE: {label}')
        if notes:
            log(f'  {notes}')
        self.send_command(cmd_name, cmd_data, label=f'{cmd_name} — {label}')
        log(f'  Draining responses for {wait}s...')
        responses = self.drain(timeout=wait)
        log(f'  -> {len(responses)} response packet(s)')
        return responses

    def run_probes(self):
        log_section('STEP 5: PROBES')

        used_macros = [m for m in self.all_macros if m['is_used']]
        log(f'{len(used_macros)} used macros available for probing')

        # ── Macro run / stop ──────────────────────────────────

        for macro in used_macros[:2]:  # probe first two used macros
            idx  = macro['index']
            name = macro['name']
            self.probe(
                label=f'MACRO RUN #{idx} "{name}"',
                cmd_name='MSRc',
                cmd_data=struct.pack('>HBB', idx, 0, 0),
                wait=2.5,
                notes='MSRc: bytes[0:2]=index(>H), byte[2]=action(0=run,1=stop,2=delete), byte[3]=pad\n'
                      '  Expect: MRPr with running=1, then MRPr with running=0 after completion',
            )
            time.sleep(0.3)

            self.probe(
                label=f'MACRO STOP (after #{idx})',
                cmd_name='MSRc',
                cmd_data=struct.pack('>HBB', 0, 1, 0),
                wait=1.0,
                notes='action=1 (stop). Expect: MRPr with running=0, index=0xffff',
            )
            time.sleep(0.3)

        if not used_macros:
            log('No used macros found — skipping macro run/stop probes')
            self.probe(
                label='MACRO RUN #0 (blind — may not exist)',
                cmd_name='MSRc',
                cmd_data=struct.pack('>HBB', 0, 0, 0),
                wait=2.5,
            )

        # ── Program input ─────────────────────────────────────

        self.probe(
            label='PROGRAM INPUT → Input 2',
            cmd_name='CPgI',
            cmd_data=struct.pack('>BxH', 0, 2),
            wait=1.0,
            notes='CPgI: byte[0]=M/E, byte[1]=pad, bytes[2:4]=source(>H)\n'
                  '  Expect: PrgI pushed back by device confirming the change',
        )
        time.sleep(0.3)
        self.probe(
            label='PROGRAM INPUT → Input 1 (restore)',
            cmd_name='CPgI',
            cmd_data=struct.pack('>BxH', 0, 1),
            wait=1.0,
        )
        time.sleep(0.3)

        # ── Preview input ─────────────────────────────────────

        self.probe(
            label='PREVIEW INPUT → Input 3',
            cmd_name='CPvI',
            cmd_data=struct.pack('>BxH', 0, 3),
            wait=1.0,
            notes='CPvI: same layout as CPgI\n'
                  '  Expect: PrvI pushed back by device',
        )
        time.sleep(0.3)
        self.probe(
            label='PREVIEW INPUT → Input 2 (restore)',
            cmd_name='CPvI',
            cmd_data=struct.pack('>BxH', 0, 2),
            wait=1.0,
        )
        time.sleep(0.3)

        # ── Cut / Auto ────────────────────────────────────────

        self.probe(
            label='CUT TRANSITION',
            cmd_name='DCut',
            cmd_data=struct.pack('>4x'),
            wait=1.0,
            notes='DCut: 4 zero bytes. Expect: PrgI/PrvI swapped in device push',
        )
        time.sleep(0.5)

        self.probe(
            label='AUTO TRANSITION',
            cmd_name='DAut',
            cmd_data=struct.pack('>4x'),
            wait=1.5,
            notes='DAut: 4 zero bytes. Expect: transition state updates during auto',
        )
        time.sleep(0.5)

        # ── Unknown commands ──────────────────────────────────

        self.probe(
            label='UNKNOWN COMMAND "????" (all zeros)',
            cmd_name='????',
            cmd_data=b'\x00\x00\x00\x00',
            wait=1.0,
            notes='Expect: device silently ignores unknown commands (no response)',
        )
        time.sleep(0.3)

        self.probe(
            label='UNKNOWN COMMAND "XXXX" (non-zero payload)',
            cmd_name='XXXX',
            cmd_data=b'\x01\x02\x03\x04',
            wait=1.0,
            notes='Expect: device silently ignores — same as above',
        )
        time.sleep(0.3)

        # ── Keepalive (empty reliable) ────────────────────────

        log_section('PROBE: KEEPALIVE (empty RELIABLE packet)')
        log('  Sending empty reliable packet — observing device response')
        self.local_seq += 1
        hdr = build_header(FLAG_RELIABLE | FLAG_ACK, HEADER_SIZE,
                           self.session, local_seq=self.local_seq)
        self.send(hdr, label='keepalive empty RELIABLE')
        responses = self.drain(timeout=1.5)
        log(f'  -> {len(responses)} response packet(s)')
        if responses:
            for r in responses:
                log(f'     flags={flag_str(r["flags"])} ack_id={r["ack_id"]} '
                    f'local_seq={r["local_seq"]}')

    # ── Step 6: Disconnect ────────────────────────────────────

    def probe_disconnect(self):
        log_section('STEP 6: DISCONNECT PROBES')

        # ── 6a: Observe silence timeout ───────────────────────
        log('6a. Stop all traffic for 15s — observe if device sends disconnect notification')
        log('    Expected: device keeps sending keepalives; eventually stops after timeout')
        silence_pkts = []
        self.sock.settimeout(0.5)
        deadline = time.time() + 15.0
        while time.time() < deadline:
            try:
                data, addr = self.sock.recvfrom(4096)
                self.packet_num += 1
                now = time.time()
                log(f'  [{now:.3f}] device sent packet during silence:')
                log_raw('RX', data, addr)
                pkt = parse_header(data)
                if pkt:
                    log_header(pkt, prefix='  hdr: ')
                    silence_pkts.append(pkt)
                    # Do NOT ACK — we are simulating silence
            except socket.timeout:
                continue

        log(f'  Device sent {len(silence_pkts)} packet(s) during 15s silence')
        if silence_pkts:
            for p in silence_pkts:
                log(f'    flags={flag_str(p["flags"])} len={p["length"]} '
                    f'session=0x{p["session"]:04x}')

        # ── 6b: SYN-only packet (possible FIN signal) ─────────
        log()
        log('6b. Probe: send SYN-only packet (no ACK, no payload) — possible disconnect signal?')
        log('    Some protocols reuse SYN for teardown; observing device reaction')
        hdr = build_header(FLAG_SYN, HEADER_SIZE, self.session)
        self.send(hdr, label='SYN-only (disconnect probe)')
        responses = self.drain(timeout=2.0)
        log(f'  -> {len(responses)} response(s) from SYN-only')
        for r in responses:
            log(f'     flags={flag_str(r["flags"])} session=0x{r["session"]:04x} '
                f'len={r["length"]}')

        # ── 6c: Zero-flags packet (no flags at all) ───────────
        log()
        log('6c. Probe: send packet with flags=0x00 (no flags set)')
        hdr = build_header(0x00, HEADER_SIZE, self.session)
        self.send(hdr, label='no-flags packet')
        responses = self.drain(timeout=1.5)
        log(f'  -> {len(responses)} response(s) from no-flags packet')

        # ── 6d: Reconnect to verify device accepts new session ─
        log()
        log('6d. Reconnect with a new session ID — verify device accepts it cleanly')
        log('    This simulates a client restart without explicit disconnect')
        new_session  = 0x2abc
        syn_payload  = struct.pack('>B7x', 0x01)
        pkt_len      = HEADER_SIZE + len(syn_payload)
        syn          = build_header(FLAG_SYN, pkt_len, new_session) + syn_payload
        self.send(syn, label=f'reconnect SYN session=0x{new_session:04x}')
        pkt, _ = self.recv(timeout=3.0, label='reconnect SYN-ACK')
        if pkt and (pkt['flags'] & FLAG_SYN):
            log(f'  Reconnect SYN-ACK received — session=0x{pkt["session"]:04x}')
            log(f'  Device accepted new session cleanly (no explicit disconnect needed)')
            # Send ACK to complete but don't receive full dump
            ack = build_header(FLAG_ACK, HEADER_SIZE, pkt['session'])
            self.send(ack, label='reconnect ACK')
            self.drain(timeout=1.0)  # absorb any state dump start
        elif pkt is None:
            log('  [!] No response to reconnect SYN — device may require explicit disconnect first')
        else:
            log(f'  Unexpected response: flags={flag_str(pkt["flags"])}')

    # ── Summary ───────────────────────────────────────────────

    def print_summary(self):
        log_section('SUMMARY — copy these values into run.py')

        log()
        log('── Single-value fields ─────────────────────────────────────────')
        ordered = ['_ver', '_pin', '_top', '_MeC', '_mpl', 'VidM',
                   '_MAC', 'PrgI', 'PrvI', 'MRPr']
        printed = set()
        for fname in ordered:
            if fname in self.all_fields:
                log()
                log(f'  [{fname}]')
                for line in decode_field(fname, self.all_fields[fname]):
                    log(f'    {line}')
                printed.add(fname)

        other = [k for k in self.all_fields if k not in printed and k != 'InCm']
        if other:
            log()
            log(f'  [other fields: {", ".join(sorted(other))}]')
            for fname in sorted(other):
                log()
                log(f'  [{fname}]')
                for line in decode_field(fname, self.all_fields[fname]):
                    log(f'    {line}')

        log()
        log('── Macros (MPrp) ───────────────────────────────────────────────')
        used = [m for m in self.all_macros if m['is_used']]
        log(f'  Total slots: {len(self.all_macros)}  |  Used: {len(used)}')
        log()
        for m in used:
            log(f'  #{m["index"]:3d}  "{m["name"]}"')
            if m['desc']:
                log(f'         desc: "{m["desc"]}"')
        log()
        log('  TSV for macros.tsv (paste into atem-simulator/macros.tsv):')
        for m in used:
            log(f'  {m["name"]}\t{m["desc"]}')

    # ── Main ──────────────────────────────────────────────────

    def run(self):
        if not self.do_handshake():
            return
        time.sleep(0.1)

        self.receive_state_dump()
        time.sleep(0.1)

        self.wait_for_control_channel()
        time.sleep(0.3)

        self.measure_keepalive()
        time.sleep(0.3)

        self.run_probes()
        time.sleep(0.3)

        self.probe_disconnect()

        self.print_summary()
        self.sock.close()


# ── Entry Point ───────────────────────────────────────────────────────────────

def main():
    global log_file

    ip = sys.argv[1] if len(sys.argv) > 1 else '192.168.10.240'

    log_file = open(LOG_PATH, 'w', encoding='utf-8')

    log('=' * 70)
    log('  ATEM Protocol Capture & Probe Tool')
    log('=' * 70)
    log(f'  Target:   {ip}:{ATEM_PORT}')
    log(f'  Log file: {LOG_PATH}')
    log('=' * 70)
    log()

    try:
        cap = ATEMCapture(ip)
        cap.run()
    except KeyboardInterrupt:
        log()
        log('[!] Interrupted by user')
    finally:
        log()
        log(f'Log saved to: {LOG_PATH}')
        log_file.close()


if __name__ == '__main__':
    main()
