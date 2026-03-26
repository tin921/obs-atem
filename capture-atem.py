"""
Connects to a real ATEM device and dumps all state fields it sends.
Run this while the ATEM is connected via USB.

Usage:  python capture-atem.py [ip]
Default IP: 192.168.10.240
"""
import socket, struct, sys, time

ATEM_PORT = 9910
HEADER_SIZE = 12
FLAG_RELIABLE = 0x08
FLAG_SYN      = 0x10
FLAG_ACK      = 0x80

def build_header(flags, length, session_id, ack_id=0, local_seq=0):
    byte0 = (flags & 0xF8) | ((length >> 8) & 0x07)
    byte1 = length & 0xFF
    return struct.pack('>BBHHHHH', byte0, byte1, session_id, ack_id, 0, 0, local_seq)

def parse_header(data):
    if len(data) < HEADER_SIZE:
        return None
    b0, b1, session, ack_id, unk, remote_seq, local_seq = struct.unpack('>BBHHHHH', data[:12])
    flags = b0 & 0xF8
    length = ((b0 & 0x07) << 8) | b1
    return {'flags': flags, 'length': length, 'session': session,
            'ack_id': ack_id, 'remote_seq': remote_seq, 'local_seq': local_seq,
            'payload': data[HEADER_SIZE:]}

def parse_fields(payload):
    fields = []
    offset = 0
    while offset + 8 <= len(payload):
        flen = struct.unpack('>H', payload[offset:offset+2])[0]
        if flen < 8: break
        name = payload[offset+4:offset+8].decode('ascii', errors='replace')
        fdata = payload[offset+8:offset+flen]
        fields.append((name, fdata))
        offset += flen
    return fields

def decode_field(name, data):
    if name == '_ver' and len(data) >= 4:
        major, minor = struct.unpack('>HH', data[:4])
        return f'protocol version major={major} minor={minor}  -> struct.pack(">HH", {major}, {minor})'
    if name == '_pin' and len(data) >= 4:
        s = data[:44].rstrip(b'\x00').decode('utf-8', errors='replace')
        model = data[44] if len(data) > 44 else '?'
        return f'product="{s}" model_byte=0x{model:02x}' if isinstance(model, int) else f'product="{s}"'
    if name == '_top' and len(data) >= 1:
        return f'topology bytes={data.hex()}'
    if name == '_MAC' and len(data) >= 1:
        count = struct.unpack('>B', data[:1])[0] if len(data) >= 1 else '?'
        return f'macro pool size={count}'
    return f'({len(data)} bytes) {data[:16].hex()}'

ip = sys.argv[1] if len(sys.argv) > 1 else '192.168.10.240'
print(f'Connecting to ATEM at {ip}:{ATEM_PORT}')

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(5.0)

# Send SYN
my_session = 0x1fed
payload = struct.pack('>B7x', 0x01)
pkt_len = HEADER_SIZE + len(payload)
syn = build_header(FLAG_SYN, pkt_len, my_session) + payload
sock.sendto(syn, (ip, ATEM_PORT))
print(f'SYN sent (session=0x{my_session:04x})')

try:
    data, addr = sock.recvfrom(2048)
    pkt = parse_header(data)
    if not pkt or not (pkt['flags'] & FLAG_SYN):
        print('ERROR: expected SYN-ACK, got:', data.hex())
        sys.exit(1)
    server_session = pkt['session']
    print(f'SYN-ACK from {addr} (server session=0x{server_session:04x})')
except socket.timeout:
    print('ERROR: no response (ATEM not connected?)')
    sys.exit(1)

# Send ACK
ack = build_header(FLAG_ACK, HEADER_SIZE, server_session)
sock.sendto(ack, (ip, ATEM_PORT))
print('ACK sent, waiting for state dump...\n')

# Receive state dump
all_fields = {}
try:
    for _ in range(50):  # receive up to 50 packets
        try:
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            break
        pkt = parse_header(data)
        if not pkt:
            continue
        # ACK it
        if pkt['flags'] & FLAG_RELIABLE and pkt['local_seq']:
            a = build_header(FLAG_ACK, HEADER_SIZE, server_session, ack_id=pkt['local_seq'])
            sock.sendto(a, (ip, ATEM_PORT))
        if pkt['payload']:
            fields = parse_fields(pkt['payload'])
            for name, fdata in fields:
                all_fields[name] = fdata
                decoded = decode_field(name, fdata)
                print(f'  [{name}] {decoded}')
            if 'InCm' in [n for n, _ in fields]:
                print('\n[InCm received — state dump complete]')
                break
except Exception as e:
    print(f'Error: {e}')

sock.close()
print('\nDone. Key values for simulator:')
if '_ver' in all_fields:
    d = all_fields['_ver']
    if len(d) >= 4:
        major, minor = struct.unpack('>HH', d[:4])
        print(f'  _ver: struct.pack(">HH", {major}, {minor})  # protocol v{major}.{minor}')
if '_pin' in all_fields:
    d = all_fields['_pin']
    name = d[:44].rstrip(b'\x00').decode('utf-8', 'replace') if len(d) >= 44 else d.decode('utf-8','replace')
    model = d[44] if len(d) > 44 else 0
    print(f'  _pin: name="{name}" model=0x{model:02x}')
