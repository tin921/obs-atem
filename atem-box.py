"""
ATEM Mini Simulator
===================
Emulates the Blackmagic ATEM Mini UDP protocol on port 9910.
Reads macros from a TSV file and serves them to any connecting client
(ATEM Software Control, BMDSwitcherAPI COM SDK, PyATEMMax, etc.)

Usage:
    python atem_simulator.py [--port 9910] [--macros macros.tsv]

TSV file format (2 columns, no header):
    Macro Name<TAB>Description

The simulator:
  - Handles the 3-way UDP handshake
  - Dumps initial state (product name, topology, macro properties)
  - Responds to macro run/stop commands and prints them to console
  - Maintains keepalive heartbeat
"""

import socket
import struct
import argparse
import csv
import time
import threading
import sys
from datetime import datetime

# ── Protocol Constants ────────────────────────────────────────

ATEM_PORT = 9910

# Header flags (bits in high nibble of first byte)
FLAG_RELIABLE    = 0x08  # bit 0 of flags → 0x08 in byte
FLAG_SYN         = 0x10  # bit 1
FLAG_RETRANSMIT  = 0x20  # bit 2
FLAG_REQ_RETRANS = 0x40  # bit 3
FLAG_ACK         = 0x80  # bit 4

HEADER_SIZE = 12

# ── Macro Data ────────────────────────────────────────────────

def load_macros(tsv_path):
    """Load macros from a TSV file. Returns list of (name, description)."""
    macros = []
    try:
        with open(tsv_path, 'r', encoding='utf-8') as f:
            reader = csv.reader(f, delimiter='\t')
            for row in reader:
                if len(row) >= 2:
                    macros.append((row[0].strip(), row[1].strip()))
                elif len(row) == 1:
                    macros.append((row[0].strip(), ""))
    except FileNotFoundError:
        print(f"[!] Macro file '{tsv_path}' not found, using defaults")
        macros = [
            ("Wide Shot", "Switch to Camera 1"),
            ("Close Up", "Switch to Camera 2"),
            ("Lower Third On", "Show overlay"),
            ("Lower Third Off", "Hide overlay"),
        ]
    return macros

# ── Packet Builder ────────────────────────────────────────────

def build_header(flags, length, session_id, ack_id=0, remote_seq=0, local_seq=0):
    """Build a 12-byte ATEM protocol header."""
    byte0 = (flags & 0xF8) | ((length >> 8) & 0x07)
    byte1 = length & 0xFF
    return struct.pack('>BBHHHHH',
        byte0, byte1,
        session_id,
        ack_id,
        0,  # unknown
        remote_seq,
        local_seq
    )


def build_field(cmd_name, data):
    """Build a single protocol field: length(2) + pad(2) + name(4) + data."""
    name_bytes = cmd_name.encode('ascii')[:4].ljust(4, b'\x00')
    total_len = 8 + len(data)
    return struct.pack('>HH', total_len, 0) + name_bytes + data


def build_string_field(s, length):
    """Encode a string as a fixed-length null-padded byte array."""
    return s.encode('utf-8')[:length].ljust(length, b'\x00')

# ── State Dump Fields (sent after handshake) ──────────────────

def build_firmware_version():
    """_ver field: PROTOCOL version (not firmware/software version).
    Major is always 2. Minor increments with each firmware generation:
      2.28 = firmware 8.0,  2.30 = firmware 8.1.1,
      2.31 = firmware 9.4,  2.32 = firmware 9.6 (ATEM Mini 9.5.1 ≈ 2.31)
    ATEM Software Control 10.x still uses the same hardware protocol.
    NOTE: major=10 is completely invalid — the SDK rejects it immediately."""
    data = struct.pack('>HH', 2, 32)  # protocol v2.31 (firmware 9.4–9.5)
    return build_field('_ver', data)


def build_product_name():
    """_pin field: product name.
    Protocol spec: 44-byte null-padded name + 4 bytes (model type + padding) = 48 bytes total data.
    Model byte 0x01 = ATEM Mini (base model)."""
    name = build_string_field("ATEM Mini", 44)   # must be 44 bytes, not 40
    data = name + struct.pack('>B3x', 0x01)       # model=0x01 + 3 padding bytes
    return build_field('_pin', data)


def build_topology():
    """_top field: hardware topology for ATEM Mini.
    Must be padded to a 4-byte boundary or field stream parsing breaks.
    Values matched to ATEM Mini (base model, 4 HDMI inputs)."""
    data = bytes([
        1,    # M/E units
        4,    # sources (4 HDMI inputs on ATEM Mini)
        2,    # downstream keyers
        1,    # AUX busses
        0,    # MixMinus outputs
        1,    # media players
        1,    # multiviewers
        0,    # rs485
        0,    # hyperdecks (ATEM Mini has none)
        1,    # DVE
        1,    # stingers
        0,    # supersources
        0, 0, 0,  # unknowns
        1,    # scalers
        0, 0,
        1,    # camera control
        0, 0, 0,
        1,    # advanced chroma keyers
        1,    # configurable outputs only
        1,    # unknown
        0x20, 3, 0xe8,  # unknown bytes matching ATEM Mini
    ])  # 28 bytes total — 4-byte aligned
    assert len(data) % 4 == 0, f"_top data must be 4-byte aligned, got {len(data)}"
    return build_field('_top', data)


def build_me_config():
    """_MeC field: M/E configuration."""
    data = struct.pack('>BBH', 0, 1, 0)  # M/E 0, 1 keyer
    return build_field('_MeC', data)


def build_mediaplayer_slots():
    """_mpl field: mediaplayer slots."""
    data = struct.pack('>BBH', 20, 0, 0)  # 20 stills, 0 clips
    return build_field('_mpl', data)


def build_program_input():
    """PrgI field: current program input."""
    data = struct.pack('>BxH', 0, 1)  # M/E 0, input 1
    return build_field('PrgI', data)


def build_preview_input():
    """PrvI field: current preview input."""
    data = struct.pack('>BxH', 0, 2)  # M/E 0, input 2
    return build_field('PrvI', data)


def build_macro_properties(index, name, description):
    """MPrp field: macro properties for a single slot."""
    name_bytes = name.encode('utf-8')[:63]
    desc_bytes = description.encode('utf-8')[:255]

    data = struct.pack('>HBB',
        index,               # macro index
        1,                   # isUsed = 1
        0,                   # hasUnsupportedOps = 0
    )
    # Name length + description length
    data += struct.pack('>HH', len(name_bytes), len(desc_bytes))
    data += name_bytes
    data += desc_bytes

    # Pad to 4-byte boundary
    while len(data) % 4 != 0:
        data += b'\x00'

    return build_field('MPrp', data)


def build_macro_pool_config(max_macros):
    """_MAC field: macro pool size."""
    data = struct.pack('>B3x', max_macros)
    return build_field('_MAC', data)


def build_macro_run_status(is_running=False, is_waiting=False, loop=False, index=0xFFFF):
    """MRPr field: macro run player status."""
    data = struct.pack('>BBBHH',
        1 if is_running else 0,
        1 if is_waiting else 0,
        1 if loop else 0,
        index,
        0
    )
    # Pad to 4 bytes
    while len(data) % 4 != 0:
        data += b'\x00'
    return build_field('MRPr', data)


def build_init_complete():
    """InCm field: signals end of initial state dump."""
    data = struct.pack('>4x')  # 4 bytes of zeros
    return build_field('InCm', data)


# ── ATEM Simulator ────────────────────────────────────────────

class ATEMSimulator:
    def __init__(self, port, macros):
        self.port = port
        self.macros = macros
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(('0.0.0.0', port))
        self.sock.settimeout(0.5)

        # Per-client state
        self.clients = {}  # addr -> ClientState
        self.running = True
        self.session_counter = 0x0001

        # Macro run state
        self.running_macro = -1
        self.macro_running = False

    def log(self, msg):
        ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
        print(f"[{ts}] {msg}")

    def get_session_id(self):
        self.session_counter += 1
        if self.session_counter > 0xFFFE:
            self.session_counter = 0x0001
        return self.session_counter

    def parse_header(self, data):
        """Parse 12-byte ATEM header."""
        if len(data) < HEADER_SIZE:
            return None
        b0, b1, session, ack_id, unk, remote_seq, local_seq = struct.unpack('>BBHHHHH', data[:12])
        flags = b0 & 0xF8
        length = ((b0 & 0x07) << 8) | b1
        return {
            'flags': flags,
            'length': length,
            'session': session,
            'ack_id': ack_id,
            'remote_seq': remote_seq,
            'local_seq': local_seq,
            'payload': data[HEADER_SIZE:]
        }

    def parse_commands(self, payload):
        """Parse commands/fields from payload."""
        commands = []
        offset = 0
        while offset < len(payload):
            if offset + 8 > len(payload):
                break
            cmd_len = struct.unpack('>H', payload[offset:offset+2])[0]
            if cmd_len < 8:
                break
            cmd_name = payload[offset+4:offset+8].decode('ascii', errors='replace')
            cmd_data = payload[offset+8:offset+cmd_len]
            commands.append((cmd_name, cmd_data))
            offset += cmd_len
        return commands

    def build_state_dump(self, session_id):
        """Build the full initial state dump as a list of packets."""
        fields = []

        # Product info
        fields.append(build_firmware_version())
        fields.append(build_product_name())

        # Topology
        fields.append(build_topology())
        fields.append(build_me_config())
        fields.append(build_mediaplayer_slots())

        # Video state
        fields.append(build_program_input())
        fields.append(build_preview_input())

        # Macro pool config
        max_macros = max(100, len(self.macros))
        fields.append(build_macro_pool_config(max_macros))

        # Macro properties for each defined macro
        for i, (name, desc) in enumerate(self.macros):
            fields.append(build_macro_properties(i, name, desc))

        # Macro run status (idle)
        fields.append(build_macro_run_status())

        # Init complete marker
        fields.append(build_init_complete())

        # Pack fields into packets (max ~1400 bytes per packet for UDP)
        packets = []
        current_payload = b''
        seq = 1

        for field in fields:
            if len(current_payload) + len(field) > 1400:
                # Flush current packet
                pkt_len = HEADER_SIZE + len(current_payload)
                header = build_header(FLAG_RELIABLE, pkt_len, session_id,
                                     local_seq=seq)
                packets.append(header + current_payload)
                seq += 1
                current_payload = field
            else:
                current_payload += field

        # Final packet with remaining fields
        if current_payload:
            pkt_len = HEADER_SIZE + len(current_payload)
            header = build_header(FLAG_RELIABLE, pkt_len, session_id,
                                 local_seq=seq)
            packets.append(header + current_payload)
            seq += 1

        # Empty packet to signal end of dump
        header = build_header(FLAG_RELIABLE, HEADER_SIZE, session_id,
                             local_seq=seq)
        packets.append(header)

        return packets, seq

    def send_ack(self, addr, session_id, ack_num):
        """Send an ACK packet."""
        header = build_header(FLAG_ACK, HEADER_SIZE, session_id, ack_id=ack_num)
        self.sock.sendto(header, addr)

    def handle_handshake(self, pkt, addr):
        """Handle the 3-way handshake."""
        client_session = pkt['session']
        self.log(f"SYN from {addr[0]}:{addr[1]} (session 0x{client_session:04x})")

        # Echo the client's session ID back in the SYN-ACK.
        # The BMDSwitcherAPI SDK expects the same session ID throughout the
        # connection (SYN-ACK + all state dump packets must match).
        payload = struct.pack('>B7x', 0x02)
        pkt_len = HEADER_SIZE + len(payload)
        header = build_header(FLAG_SYN, pkt_len, client_session)
        self.sock.sendto(header + payload, addr)

        self.clients[addr] = {
            'session': client_session,
            'state': 'handshake',
            'remote_seq': 0,
            'local_seq': 0,
            'last_contact': time.time(),
            'dump_packets': [],
            'dump_index': 0,
            'dump_seq': 0,
        }

        self.log(f"SYN-ACK sent to {addr[0]}:{addr[1]} (session 0x{client_session:04x})")

    def handle_packet(self, data, addr):
        """Handle an incoming packet."""
        hex_dump = ' '.join(f'{b:02x}' for b in data[:32])
        self.log(f"  << {addr[0]}:{addr[1]} [{len(data)}b] {hex_dump}{'...' if len(data) > 32 else ''}")

        pkt = self.parse_header(data)
        if pkt is None:
            return

        flags = pkt['flags']

        # SYN — new connection or secondary "control" SYN from SDK
        if flags & FLAG_SYN:
            existing = self.clients.get(addr)
            # The BMDSwitcherAPI sends a second SYN with session 0x8000 after
            # receiving the state dump. This is a "control channel" SYN —
            # complete the handshake but skip the state dump (go straight to
            # 'connected'). Sending a second dump causes StateSync failure.
            if existing and existing['state'] == 'connected' or pkt['session'] == 0x8000:
                client_session = pkt['session']
                self.log(f"SYN from {addr[0]}:{addr[1]} (session 0x{client_session:04x}) [secondary — no dump]")
                payload = struct.pack('>B7x', 0x02)
                pkt_len = HEADER_SIZE + len(payload)
                header = build_header(FLAG_SYN, pkt_len, client_session)
                self.sock.sendto(header + payload, addr)
                self.log(f"SYN-ACK sent to {addr[0]}:{addr[1]} (session 0x{client_session:04x})")
                # Mark as 'handshake2' — ACK will move directly to 'connected'
                self.clients[addr] = {
                    'session': client_session,
                    'state': 'handshake2',
                    'remote_seq': 0,
                    'local_seq': 0,
                    'last_contact': time.time(),
                    'dump_packets': [],
                    'dump_index': 0,
                    'dump_seq': 0,
                }
            else:
                self.handle_handshake(pkt, addr)
            return

        client = self.clients.get(addr)
        if not client:
            return

        client['last_contact'] = time.time()

        # ACK from secondary handshake → go directly to connected (no dump)
        if flags & FLAG_ACK and client['state'] == 'handshake2':
            self.log(f"Secondary handshake complete with {addr[0]}:{addr[1]} — control channel ready")
            client['state'] = 'connected'
            return

        # ACK from client during handshake → start state dump
        if flags & FLAG_ACK and client['state'] == 'handshake':
            self.log(f"Handshake complete with {addr[0]}:{addr[1]}")
            client['state'] = 'dumping'

            # Build and send initial state dump
            packets, last_seq = self.build_state_dump(client['session'])
            client['dump_packets'] = packets
            client['dump_index'] = 0
            client['dump_seq'] = last_seq

            # Send first batch of dump packets
            self.send_dump_packets(addr, client)
            return

        # ACK from client during dump → send more
        if flags & FLAG_ACK and client['state'] == 'dumping':
            self.log(f"  ACK from client (ack_id={pkt['ack_id']}, idx={client['dump_index']}/{len(client['dump_packets'])})")
            self.send_dump_packets(addr, client)
            if client['dump_index'] >= len(client['dump_packets']):
                client['state'] = 'connected'
                self.log(f"State dump complete for {addr[0]}:{addr[1]} — ready")
            return

        # ACK during connected state (keepalive response)
        if flags & FLAG_ACK and client['state'] == 'connected':
            self.log(f"  [connected] ACK ack_id={pkt['ack_id']} local_seq={pkt['local_seq']}")
            return

        # Unhandled packet during dump (not ACK) — log it
        if client['state'] == 'dumping':
            self.log(f"  [dump] unexpected flags=0x{flags:02x} local_seq={pkt['local_seq']} ack_id={pkt['ack_id']}")
            if pkt['payload']:
                cmds = self.parse_commands(pkt['payload'])
                for name, data in cmds:
                    self.log(f"    cmd '{name}' data={data.hex()}")
            return

        # Reliable packet with commands
        if flags & FLAG_RELIABLE and client['state'] == 'connected':
            # ACK the packet
            self.send_ack(addr, client['session'], pkt['local_seq'])

            # Parse commands
            if pkt['payload']:
                commands = self.parse_commands(pkt['payload'])
                for cmd_name, cmd_data in commands:
                    self.log(f"  [cmd] '{cmd_name}' ({len(cmd_data)}b) {cmd_data[:16].hex()}")
                    self.handle_command(cmd_name, cmd_data, addr, client)
        else:
            self.log(f"  [??] unhandled flags=0x{flags:02x} state={client['state']} seq={pkt['local_seq']}")

    def send_dump_packets(self, addr, client):
        """Send pending state dump packets."""
        count = 0
        while client['dump_index'] < len(client['dump_packets']) and count < 5:
            pkt = client['dump_packets'][client['dump_index']]
            self.sock.sendto(pkt, addr)
            client['dump_index'] += 1
            count += 1

    def handle_command(self, cmd_name, cmd_data, addr, client):
        """Handle a command from the client."""
        if cmd_name == 'MSRc':
            # Macro action command
            if len(cmd_data) >= 4:
                index = struct.unpack('>H', cmd_data[0:2])[0]
                action = cmd_data[2]  # 0=run, 1=stop, 2=delete

                if action == 0:
                    # Run macro
                    if index < len(self.macros):
                        name = self.macros[index][0]
                        self.log(f"▶ MACRO RUN #{index}: \"{name}\"")
                        self.macro_running = True
                        self.running_macro = index

                        # Send updated run status
                        field = build_macro_run_status(True, False, False, index)
                        pkt_len = HEADER_SIZE + len(field)
                        client['remote_seq'] = (client.get('remote_seq', 0) or 0) + 1
                        header = build_header(FLAG_RELIABLE, pkt_len,
                                            client['session'],
                                            local_seq=client['remote_seq'])
                        self.sock.sendto(header + field, addr)

                        # Auto-complete after a short delay (simulate macro execution)
                        def complete_macro():
                            time.sleep(0.5)
                            self.macro_running = False
                            self.running_macro = -1
                            self.log(f"■ MACRO COMPLETE #{index}: \"{name}\"")
                            field2 = build_macro_run_status(False, False, False, 0xFFFF)
                            pkt_len2 = HEADER_SIZE + len(field2)
                            client['remote_seq'] = (client.get('remote_seq', 0) or 0) + 1
                            header2 = build_header(FLAG_RELIABLE, pkt_len2,
                                                  client['session'],
                                                  local_seq=client['remote_seq'])
                            try:
                                self.sock.sendto(header2 + field2, addr)
                            except Exception:
                                pass
                        threading.Thread(target=complete_macro, daemon=True).start()
                    else:
                        self.log(f"[!] MACRO RUN #{index}: index out of range")

                elif action == 1:
                    # Stop macro
                    self.log(f"■ MACRO STOP (was running #{self.running_macro})")
                    self.macro_running = False
                    self.running_macro = -1

                    field = build_macro_run_status(False, False, False, 0xFFFF)
                    pkt_len = HEADER_SIZE + len(field)
                    client['remote_seq'] = (client.get('remote_seq', 0) or 0) + 1
                    header = build_header(FLAG_RELIABLE, pkt_len,
                                        client['session'],
                                        local_seq=client['remote_seq'])
                    self.sock.sendto(header + field, addr)
                else:
                    self.log(f"[?] MACRO ACTION {action} on #{index}")

        elif cmd_name == 'CPgI':
            # Change program input
            if len(cmd_data) >= 4:
                me = cmd_data[0]
                src = struct.unpack('>H', cmd_data[2:4])[0]
                self.log(f"📹 PROGRAM INPUT: M/E {me} → Source {src}")

        elif cmd_name == 'CPvI':
            # Change preview input
            if len(cmd_data) >= 4:
                me = cmd_data[0]
                src = struct.unpack('>H', cmd_data[2:4])[0]
                self.log(f"👁 PREVIEW INPUT: M/E {me} → Source {src}")

        elif cmd_name == 'DCut':
            self.log("✂ CUT transition")

        elif cmd_name == 'DAut':
            self.log("🔄 AUTO transition")

        else:
            self.log(f"📨 CMD: {cmd_name} ({len(cmd_data)} bytes)")

    def keepalive_loop(self):
        """Send periodic keepalive to connected clients."""
        while self.running:
            time.sleep(1.0)
            now = time.time()
            disconnected = []
            for addr, client in list(self.clients.items()):
                if client['state'] != 'connected':
                    continue
                if now - client['last_contact'] > 10:
                    self.log(f"Client {addr[0]}:{addr[1]} timed out")
                    disconnected.append(addr)
                    continue
                # Send keepalive (empty reliable packet)
                client['remote_seq'] = (client.get('remote_seq', 0) or 0) + 1
                header = build_header(FLAG_RELIABLE, HEADER_SIZE,
                                    client['session'],
                                    local_seq=client['remote_seq'])
                try:
                    self.sock.sendto(header, addr)
                except Exception:
                    disconnected.append(addr)

            for addr in disconnected:
                del self.clients[addr]

    def run(self):
        """Main server loop."""
        print("=" * 60)
        print("  ATEM Mini Simulator")
        print("=" * 60)
        print(f"  Listening on UDP port {self.port}")
        print(f"  Loaded {len(self.macros)} macros:")
        for i, (name, desc) in enumerate(self.macros):
            print(f"    #{i}: {name}")
            if desc:
                print(f"        {desc}")
        print()
        print("  Connect with:")
        print(f"    - ATEM Software Control → IP: 127.0.0.1")
        print(f"    - OBS Plugin → Settings → IP: 127.0.0.1")
        print(f"    - PyATEMMax → switcher.connect('127.0.0.1')")
        print()
        print("  Press Ctrl+C to stop")
        print("=" * 60)
        print()

        # Start keepalive thread
        ka_thread = threading.Thread(target=self.keepalive_loop, daemon=True)
        ka_thread.start()

        try:
            while self.running:
                try:
                    data, addr = self.sock.recvfrom(2048)
                    self.handle_packet(data, addr)
                except socket.timeout:
                    continue
                except OSError:
                    break
        except KeyboardInterrupt:
            print("\n[!] Shutting down simulator...")
        finally:
            self.running = False
            self.sock.close()


# ── Main ──────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='ATEM Mini Simulator')
    parser.add_argument('--port', type=int, default=ATEM_PORT,
                        help=f'UDP port (default: {ATEM_PORT})')
    parser.add_argument('--macros', type=str, default='macros.tsv',
                        help='Path to TSV file with macro definitions')
    args = parser.parse_args()

    macros = load_macros(args.macros)
    sim = ATEMSimulator(args.port, macros)
    sim.run()


if __name__ == '__main__':
    main()