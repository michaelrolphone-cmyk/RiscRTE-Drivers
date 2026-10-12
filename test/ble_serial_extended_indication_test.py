#!/usr/bin/env python3
"""New real-NimBLE regression: extended-properties TX + indication-only RX."""
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'test'))
from ble_serial_protocol_test import *
class ExtendedIndication(Central):
 def __init__(self,path):
  super().__init__(path,'nus');self.subscription=None
 def queue_att(self,b):
  if len(b)==23 and b[:2]==b'\x09\x15' and b[2:4]==le(0x11):
   b=b[:4]+bytes([b[4]|0x80])+b[5:]
  return super().queue_att(b)
 def send(self,ctx,token,kind,p,n):
  b=C.string_at(p,n)
  if kind==2 and len(b)>=13 and b[8]==0x12 and b[9:11]==le(self.ccc_handle):
   self.subscription=b[11:13]
   # The independent peripheral rejects unsupported notification enablement.
   self.queue_att(b'\x13' if self.subscription==b'\x02\x00' else b'\x01\x12'+le(self.ccc_handle)+b'\x13')
   return True
  return super().send(ctx,token,kind,p,n)
f=ExtendedIndication(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'build/ble-serial-host/driver.so')
f.start();f.advertise();p=Peer(C.sizeof(Peer));assert f.hid.peer(None,f.t,0,C.byref(p));assert f.hid.connect(None,f.t,C.byref(p));f.pump(160)
assert f.status().state==6,f.describe()
assert f.hid.select(None,f.t,0)
for _ in range(80):f.hid.poll(None,f.t,1)
s=f.status();actual=f.subscription;f.close()
print('CCC write:',actual.hex() if actual else None,'expected: 0200; state:',s.state,'message:',s.message.decode())
assert actual==b'\x02\x00', 'TX extended-properties bit incorrectly selects notifications for indication-only RX'
assert s.state==8, 'Valid open indication channel must reach READY'
print('Extended-properties transmitter with indication-only receiver PASS')
