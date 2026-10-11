#!/usr/bin/env python3
"""Real NimBLE central + GATT over deterministic HCI, without RF or hardware.
Uses the established controller fixture, but an independent UART ATT server.
"""
from ble_hid_protocol_test import *
class Peer(C.Structure):_fields_=[('size',U32),('address',U8*6),('type',U8),('connectable',U8),('rssi',I8),('name',C.c_char*32)]
class Channel(C.Structure):_fields_=[('size',U32),('flags',U32),('tx',U16),('rx',U16),('service',C.c_char*37),('tx_uuid',C.c_char*37),('rx_uuid',C.c_char*37)]
class SerialStatus(C.Structure):_fields_=[('size',U32),('state',U32),('flags',U32),('peers',U32),('channels',U32),('rx',U32),('tx',U32),('dropped',U32),('generation',U32),('error',I32),('message',C.c_char*72)]
class Serial(C.Structure):_fields_=[('version',U32),('size',U32),('ctx',P),('open',F(B,P,C.POINTER(U64))),('poll',F(B,P,U64,U32)),('status',F(B,P,U64,C.POINTER(SerialStatus))),('peer',F(B,P,U64,U32,C.POINTER(Peer))),('connect',F(B,P,U64,C.POINTER(Peer))),('channel',F(B,P,U64,U32,C.POINTER(Channel))),('select',F(B,P,U64,U32)),('write',F(I32,P,U64,C.POINTER(U8),U32)),('read',F(I32,P,U64,C.POINTER(U8),U32)),('close',F(B,P,U64))]
class Central(Fixture):
 def __init__(self,path,profile="fff0"):
  self.profile=profile;self.tx_handle=0x12;self.rx_handle=0x12 if profile=="ffe0" else 0x15;self.ccc_handle=self.rx_handle+1;self.indicate=profile=="nus";self.no_response=profile in ("ffe0","nus");self.drop_ack=False
  super().__init__(path);self.hid=C.cast(self.driver.api,C.POINTER(Serial)).contents;self.deps=(Dep*2)(self.deps[0],self.deps[1]);self.tx=[];self.att_requests=[];self.reject_subscribe=False;self.peer_address=bytes.fromhex('ca61a06794e0');self.connection=1
 def status(self):
  s=SerialStatus(C.sizeof(SerialStatus));assert self.hid.status(None,self.t,C.byref(s));return s
 def describe(self):
  s=self.status();return (s.state,s.flags,s.error,s.message,self.commands[-4:],self.att_requests[-4:])
 def queue_att(self,b):
  b=le(len(b))+le(4)+b;self.rx.append((2,le(self.connection|0x2000)+le(len(b))+b))
 def send(self,ctx,token,kind,p,n):
  b=C.string_at(p,n)
  if self.fail_send:return False
  if kind==1 and int.from_bytes(b[:2],'little') in (0x200d,0x2016):
   op=int.from_bytes(b[:2],'little');self.commands.append((op,b[3:]));self.event(b'\x0f\x04\x00\x01'+le(op))
   if op==0x200d:self.event(b'\x3e\x13\x01\x00'+le(1)+b'\x00\x00'+self.peer_address+b'\x18\x00\x00\x00\xc8\x00\x00')
   else:self.event(b'\x3e\x0c\x04\x00'+le(1)+bytes(8))
   return True
  ok=super().send(ctx,token,kind,p,n)
  if kind!=2 or not ok:return ok
  assert len(b)>=8 and int.from_bytes(b[6:8],'little')==4,b.hex()
  q=b[8:];self.att_requests.append(q);op=q[0];reply=None
  if op==2:reply=b'\x03\x17\x00'
  elif op==0x10: # primary services, GAP and UART
   start=int.from_bytes(q[1:3],'little')
   su=0xffe0 if self.profile=='ffe0' else 0xabcd if self.profile=='unknown' else 0xfff0
   if self.profile=='nus':
    reply=(b'\x11\x06'+struct.pack('<HHH',1,5,0x1800)) if start<=1 else (b'\x11\x14'+struct.pack('<HH',0x10,0x17)+bytes.fromhex('6e400001b5a3f393e0a9e50e24dcca9e')[::-1]) if start<=0x10 else b'\x01'+q[:1]+q[1:3]+b'\x0a'
   else:
    entries=[v for v in [(1,5,0x1800),(0x10,0x17,su)] if v[0]>=start]
    reply=b'\x11\x06'+b''.join(struct.pack('<HHH',*v) for v in entries) if entries else b'\x01'+q[:1]+q[1:3]+b'\x0a'
  elif op==8:
   start=int.from_bytes(q[1:3],'little')
   if self.profile=='nus':
    entries=[v for v in [(0x11,0x04,0x12,'6e400002b5a3f393e0a9e50e24dcca9e'),(0x14,0x20,0x15,'6e400003b5a3f393e0a9e50e24dcca9e')] if v[0]>=start]
    reply=b'\x09\x15'+struct.pack('<HBH',*entries[0][:3])+bytes.fromhex(entries[0][3])[::-1] if entries else b'\x01'+q[:1]+q[1:3]+b'\x0a'
   else:
    entries=[(0x11,0x14,0x12,0xffe1)] if self.profile=='ffe0' else [(0x11,0x0c,0x12,0xfff2),(0x14,0x10,0x15,0xfff1)]
    entries=[v for v in entries if v[0]>=start]
    reply=b'\x09\x07'+b''.join(struct.pack('<HBHH',*v) for v in entries) if entries else b'\x01'+q[:1]+q[1:3]+b'\x0a'
  elif op==4:
   start=int.from_bytes(q[1:3],'little');reply=b'\x05\x01'+struct.pack('<HH',self.ccc_handle,0x2902) if start<=self.ccc_handle else b'\x01'+q[:1]+q[1:3]+b'\x0a'
  elif op in (0x12,0x52):
   h=int.from_bytes(q[1:3],'little')
   if h==self.ccc_handle:
    assert q[3:]==bytes([2 if self.indicate else 1,0]);reply=b'\x01\x12\x16\x00\x05' if self.reject_subscribe else b'\x13'
   else:
    assert h==0x12;self.tx.append(q[3:]);reply=b'\x13' if op==0x12 and not self.drop_ack else None
  elif op==0x1e:pass # indication confirmation
  else:raise AssertionError(q.hex())
  if reply:self.queue_att(reply)
  return True
 def start(self):
  assert not self.driver.start(self.deps,1);assert self.driver.start(self.deps,2)
  assert not self.hid.poll(None,1,1);assert not self.hid.close(None,1)
  assert self.claims==0 and not self.commands
  assert self.hid.open(None,C.byref(self.t)),self.describe();self.pump(80)
  assert self.status().state==2,self.describe()
 def advertise(self,index=0,name=b'OBDII'):
  a=bytes([index])+self.peer_address[1:] if index else self.peer_address
  data=b'\x02\x01\x06'+bytes([len(name)+1,9])+name
  self.event(bytes([0x3e,12+len(data),2,1,0,0])+a+bytes([len(data)])+data+b'\xc2');self.pump(10)
 def ready(self):
  self.advertise();self.advertise();assert self.status().peers==1,self.describe()
  p=Peer(C.sizeof(Peer));assert self.hid.peer(None,self.t,0,C.byref(p)) and p.name==b'OBDII' and p.connectable
  bad=Peer.from_buffer_copy(p);bad.address[0]^=0xff;assert not self.hid.connect(None,self.t,C.byref(bad))
  assert self.hid.connect(None,self.t,C.byref(p));self.pump(160)
  assert self.status().state==6,self.describe()
  c=Channel(C.sizeof(Channel));assert self.hid.channel(None,self.t,0,C.byref(c));assert c.tx==self.tx_handle and c.rx==self.rx_handle and c.flags&1
  assert self.hid.select(None,self.t,0);self.pump(80)
  assert self.status().state==8,self.describe()
 def close(self):
  assert self.hid.close(None,self.t),self.describe();self.t.value=0;assert self.status().state==0 and self.lease==0
  self.driver.stop()
 def notify(self,b,h=None):
  self.queue_att(bytes([0x1d if self.indicate else 0x1b])+le(self.rx_handle if h is None else h)+b);self.pump(10)
if __name__=='__main__':
 path=Path(sys.argv[1]);case=sys.argv[2] if len(sys.argv)>2 else 'normal'
 f=Central(path,case if case in ('ffe0','nus','unknown') else 'fff0');f.start()
 if case in ('ffe0','nus'):
  f.ready();c=Channel(C.sizeof(Channel));assert f.hid.channel(None,f.t,0,C.byref(c))
  assert c.service==(b'ffe0' if case=='ffe0' else b'6e400001-b5a3-f393-e0a9-e50e24dcca9e')
  assert f.hid.write(None,f.t,arr(b'ATI\r'),4)==4;f.pump(30);assert f.tx==[b'ATI\r']
  f.notify(b'ELM327\r>');b=arr(bytes(32));n=f.hid.read(None,f.t,b,32);assert bytes(b[:n])==b'ELM327\r>'
  if case=='nus':assert b'\x1e' in f.att_requests
  f.close();print(case+' discovered UART / write-without-response / RX / close PASS')
 elif case=='unknown':
  f.advertise();p=Peer(C.sizeof(Peer));assert f.hid.peer(None,f.t,0,C.byref(p));assert f.hid.connect(None,f.t,C.byref(p))
  for _ in range(160):f.hid.poll(None,f.t,1)
  assert f.status().state==10 # 0xabcd is not a vendor service; do not write unrelated standard characteristics
  f.close();print('non-vendor service is not probed as serial PASS')
 elif case=='write-timeout':
  f.ready();f.drop_ack=True;assert f.hid.write(None,f.t,arr(b'ATI\r'),4)==4;f.pump(5);f.time+=6000
  for _ in range(20):f.hid.poll(None,f.t,1)
  assert f.status().state==10 and f.hid.write(None,f.t,arr(b'X'),1)<0;f.close();print('missing write acknowledgement fails closed PASS')
 elif case=='normal':
  f.ready();assert f.hid.write(None,f.t,arr(b'ATZ\r'),4)==4;assert f.hid.write(None,f.t,arr(b'X'),1)==0
  f.pump(40);assert f.tx==[b'ATZ\r'];f.notify(b'ELM327 v1.5\r');f.notify(b'>');f.notify(b'bad',h=0x12)
  b=arr(bytes(128));n=f.hid.read(None,f.t,b,128);assert bytes(b[:n])==b'ELM327 v1.5\r>'
  assert f.hid.read(None,f.t,b,128)==0;assert f.hid.write(None,f.t,arr(bytes(21)),21)<0
  f.fail_close=True;assert not f.hid.close(None,f.t);assert f.lease;f.driver.stop();assert not f.hid.open(None,C.byref(U64()))
  f.fail_close=False;f.close();print('central scan/discovery/CCC/credits/fragmented RX/writes/retained close PASS')
 elif case=='overflow':
  f.ready()
  for i in range(102):f.notify(b'x'*20)
  f.queue_att(b'\x1b'+le(0x15)+b'y'*20)
  for i in range(20):f.hid.poll(None,f.t,1)
  assert f.status().state==10 and f.status().error;f.close();print('RX overflow fails closed PASS')
 elif case=='disconnect':
  f.ready();f.notify(b'old');f.event(b'\x05\x04\x00\x01\x00\x13');f.pump(20)
  assert f.status().state==9;assert f.hid.read(None,f.t,arr(bytes(20)),20)<0;f.close();print('disconnect stale data excluded PASS')
 elif case=='timeout':
  f.time+=20000
  for i in range(20):f.hid.poll(None,f.t,1)
  assert f.status().state in (3,10);f.close();print('scan bounded timeout PASS')
 elif case=='subscribe-auth':
  f.reject_subscribe=True;f.advertise();p=Peer(C.sizeof(Peer));assert f.hid.peer(None,f.t,0,C.byref(p));assert f.hid.connect(None,f.t,C.byref(p));f.pump(160);assert f.hid.select(None,f.t,0)
  for i in range(80):f.hid.poll(None,f.t,1)
  assert f.status().state==10 and b'pairing' in f.status().message;f.close();print('authentication required is explicit, not a false connected state PASS')
 elif case=='repeat':
  for i in range(10):
   f.ready();assert f.hid.close(None,f.t);f.t.value=0;assert f.hid.open(None,C.byref(f.t));f.pump(80)
  f.close();print('ten close/reopen host cycles PASS')
 else:raise AssertionError(case)
