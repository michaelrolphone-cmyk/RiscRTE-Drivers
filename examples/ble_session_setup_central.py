#!/usr/bin/env python3
"""Read the RiscRTE session-setup profile after OS numeric-comparison pairing.
Install Bleak in your own environment. This helper never starts Wi-Fi or WebDAV.
"""
import argparse
import asyncio
import json
import os
import struct
import time
from urllib.parse import urlsplit
SERVICE = 'cc12f001-6b62-4c86-a72d-1b4864247521'
DESCRIPTOR = 'cc12f002-6b62-4c86-a72d-1b4864247521'
VALIDITY = 'cc12f003-6b62-4c86-a72d-1b4864247521'

def decode_descriptor(value):
    if len(value) < 16 or value[0] != 1 or value[1] not in (1, 2):
        raise ValueError('Unsupported descriptor')
    size, lifetime, *lengths = struct.unpack_from('<HI4H', value, 2)
    if not lifetime or size != len(value) or sum(lengths) + 16 != size:
        raise ValueError('Truncated or inconsistent descriptor')
    if not 1 <= lengths[0] <= 256 or any(not 1 <= n <= 64 for n in lengths[1:]):
        raise ValueError('Invalid descriptor lengths')
    fields = []; offset = 16
    for n in lengths:
        part = bytes(value[offset:offset + n])
        if any(c < 32 or c > 126 for c in part):
            raise ValueError('Invalid descriptor text')
        fields.append(part.decode('ascii')); offset += n
    url, username, password, session_id = fields
    parsed = urlsplit(url)
    scheme = 'http' if value[1] == 1 else 'https'
    if parsed.scheme != scheme or not parsed.netloc or parsed.username is not None or parsed.fragment:
        raise ValueError('Unsafe endpoint URL')
    if any(c.isspace() for c in url) or '\\' in url:
        raise ValueError('Unsafe endpoint URL')
    return {'schema': 1, 'transport': scheme, 'url': url, 'username': username,
            'password': password, 'session_id': session_id,
            'remaining_lifetime_ms_at_open': lifetime}

def decode_validity(value):
    if len(value) != 16: raise ValueError('Invalid validity snapshot')
    setup_ms, generation, webdav_ms = struct.unpack('<IIQ', value)
    if not 0 < setup_ms <= 300000 or webdav_ms < setup_ms or not generation:
        raise ValueError('Expired or inconsistent session')
    return setup_ms, generation, webdav_ms

async def read_session(address):
    from bleak import BleakClient, BleakScanner
    if not address:
        devices = await BleakScanner.discover(timeout=10, return_adv=True)
        matches = [d for d, a in devices.values() if SERVICE in [u.lower() for u in a.service_uuids]]
        if len(matches) != 1:
            raise RuntimeError('Expected one setup device; use --address to choose explicitly')
        address = matches[0]
    print('Compare the six-digit number in the OS pairing dialog with the watch. Approve both only if identical.')
    async with BleakClient(address, pair=True, timeout=90) as client:
        begin = time.monotonic()
        first = decode_validity(await client.read_gatt_char(VALIDITY))
        session = decode_descriptor(await client.read_gatt_char(DESCRIPTOR))
        last = decode_validity(await client.read_gatt_char(VALIDITY))
        elapsed = int((time.monotonic() - begin) * 1000)
        if first[1] != last[1] or elapsed >= first[0] or last[2] > session['remaining_lifetime_ms_at_open']:
            raise RuntimeError('Setup expired or connection changed while reading')
        session['expires_in_ms'] = min(first[2] - elapsed, last[2])
        return session

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', help='Explicit OS BLE address/identifier; otherwise scan service UUID')
    parser.add_argument('--out', required=True, help='New private JSON file for the temporary credentials')
    args = parser.parse_args()
    # Exclusive creation prevents overwriting a credential file or following a symlink.
    fd = os.open(args.out, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    try:
        session = asyncio.run(read_session(args.address))
        with os.fdopen(fd, 'w') as output:
            fd = -1
            json.dump(session, output, indent=2); output.write('\n')
        print('Session descriptor saved. Remove this temporary credential file after use.')
    except BaseException:
        if fd >= 0: os.close(fd)
        os.unlink(args.out)
        raise
if __name__ == '__main__': main()
