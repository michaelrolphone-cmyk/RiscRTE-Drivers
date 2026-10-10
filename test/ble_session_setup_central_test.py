#!/usr/bin/env python3
"""Reference central rejects malformed/unsafe profiles without any BLE adapter."""
import importlib.util
import struct
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'examples/ble_session_setup_central.py'
s=importlib.util.spec_from_file_location('central',p);c=importlib.util.module_from_spec(s);s.loader.exec_module(c)
def wire(url=b'http://192.0.2.7/dav/'):
 parts=(url,b'user',b'pass',b'id');return bytes([1,1])+struct.pack('<HI4H',16+sum(map(len,parts)),900000,*map(len,parts))+b''.join(parts)
w=wire();d=c.decode_descriptor(w);assert d['url']=='http://192.0.2.7/dav/' and d['password']=='pass'
assert c.decode_validity(struct.pack('<IIQ',1000,1,90000))==(1000,1,90000)
for invalid in (w[:-1],w+b'x',bytes([2])+w[1:],wire(b'http://user:pass@host/'),wire(b'https://host/'),wire(b'http://host/a b'),wire(b'http://host/#x'),wire(b'http://host/\x01')):
 try:c.decode_descriptor(invalid)
 except ValueError:pass
 else:raise AssertionError('accepted unsafe descriptor')
for invalid in (bytes(16),struct.pack('<IIQ',300001,1,900000),struct.pack('<IIQ',1000,1,999)):
 try:c.decode_validity(invalid)
 except ValueError:pass
 else:raise AssertionError('accepted unsafe validity')
print('Reference central wire decoder and unsafe/expired inputs: PASS')
