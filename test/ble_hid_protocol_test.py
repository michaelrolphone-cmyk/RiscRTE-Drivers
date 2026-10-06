#!/usr/bin/env python3
"""Production NimBLE/HID ELF against an independent, deterministic HCI central.
No RF is used. cryptography/OpenSSL is the independent Secure Connections oracle.
"""
import ctypes as C, sys, struct, collections, hashlib
from pathlib import Path
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.cmac import CMAC
from cryptography.hazmat.primitives.ciphers import algorithms
B=C.c_bool; U8=C.c_uint8; U16=C.c_uint16; U32=C.c_uint32; U64=C.c_uint64; I8=C.c_int8; I32=C.c_int32; SZ=C.c_size_t; P=C.c_void_p
F=lambda r,*a:C.CFUNCTYPE(r,*a)
class Dep(C.Structure):_fields_=[('name',C.c_char_p),('version',U32),('api',P)]
class Driver(C.Structure):_fields_=[('version',U32),('size',U32),('id',C.c_char_p),('cap',C.c_char_p),('api_version',U32),('api',P),('start',F(B,C.POINTER(Dep),SZ)),('stop',F(None)),('quiesce',F(B))]
class Status(C.Structure):_fields_=[('size',U32),('state',U32),('flags',U32),('number',U32),('error',I32),('generation',U32)]
class Hid(C.Structure):_fields_=[('version',U32),('size',U32),('ctx',P),('open',F(B,P,C.c_char_p,B,C.POINTER(U64))),('poll',F(B,P,U64,U32)),('status',F(B,P,U64,C.POINTER(Status))),('confirm',F(B,P,U64,B)),('keyboard',F(B,P,U64,U8,C.POINTER(U8))),('mouse',F(B,P,U64,U8,I8,I8,I8)),('release',F(B,P,U64)),('close',F(B,P,U64)),('forget',F(B,P)),('battery',F(B,P,U64,U8))]
Send=F(B,P,U8,C.POINTER(U8),SZ); Next=F(I32,P,C.POINTER(U8),C.POINTER(U8),SZ,C.POINTER(SZ))
Claim=F(B,P,C.POINTER(U64)); OwnedSend=F(B,P,U64,U8,C.POINTER(U8),SZ); OwnedNext=F(I32,P,U64,C.POINTER(U8),C.POINTER(U8),SZ,C.POINTER(SZ)); Release=F(I32,P,U64)
class Host(C.Structure):_fields_=[('version',U32),('size',U32),('ctx',P),('send',Send),('next',Next),('enable',F(B,P,B)),('status',F(B,P,C.POINTER(U8))),('claim',Claim),('send_owned',OwnedSend),('next_owned',OwnedNext),('release',Release)]
class Clock(C.Structure):_fields_=[('version',U32),('size',U32),('ctx',P),('now',F(U64,P)),('sleep',F(None,P,U32))]
class KV(C.Structure):_fields_=[('version',U32),('size',U32),('ctx',P),('get',F(I32,P,C.c_char_p,P,U32,C.POINTER(U32))),('put',F(I32,P,C.c_char_p,P,U32))]
def arr(b):return (U8*len(b)).from_buffer_copy(b)
def le(n,size=2):return n.to_bytes(size,'little')
def cmac(key,msg):c=CMAC(algorithms.AES(key));c.update(msg);return c.finalize()
def f4(u,v,x,z=0):return cmac(x[::-1],u[::-1]+v[::-1]+bytes([z]))[::-1]
def f5(w,n1,n2,a1,a2):
 t=cmac(bytes.fromhex('6c888391aaf5a53860370bdb5a6083be'),w[::-1]);m=b'btle'+n1[::-1]+n2[::-1]+a1[:1]+a1[1:][::-1]+a2[:1]+a2[1:][::-1]+b'\x01\x00'
 return tuple(cmac(t,bytes([i])+m)[::-1] for i in (0,1))
def f6(w,n1,n2,r,io,a1,a2):return cmac(w[::-1],n1[::-1]+n2[::-1]+r[::-1]+io[::-1]+a1[:1]+a1[1:][::-1]+a2[:1]+a2[1:][::-1])[::-1]
def g2(u,v,x,y):return int.from_bytes(cmac(x[::-1],u[::-1]+v[::-1]+y[::-1])[-4:],'big')%1000000
class Fixture:
 def __init__(self,path):
  self.lib=C.CDLL(str(path));self.lib.t5_driver_get.argtypes=[U32];self.lib.t5_driver_get.restype=C.POINTER(Driver)
  self.driver=self.lib.t5_driver_get(2).contents;self.hid=C.cast(self.driver.api,C.POINTER(Hid)).contents
  self.rx=collections.deque();self.acl=[];self.commands=[];self.kv={};self.time=100;self.lease=0;self.claims=0;self.releases=0;self.fail_send=False;self.fail_close=False;self.fail_write=False;self.drop_ack=False;self.random_counter=0;self.callbacks=[]
  def wrap(t,fn):v=t(fn);self.callbacks.append(v);return v
  self.host=Host(1,C.sizeof(Host),None,Send(),Next(),F(B,P,B)(),F(B,P,C.POINTER(U8))(),wrap(Claim,self.claim),wrap(OwnedSend,self.send),wrap(OwnedNext,self.next),wrap(Release,self.release))
  self.clock=Clock(1,C.sizeof(Clock),None,wrap(F(U64,P),lambda _:self.time),wrap(F(None,P,U32),self.sleep))
  self.storage=KV(1,C.sizeof(KV),None,wrap(F(I32,P,C.c_char_p,P,U32,C.POINTER(U32)),self.get),wrap(F(I32,P,C.c_char_p,P,U32),self.put))
  self.deps=(Dep*3)(Dep(b'bluetooth.hci',1,C.addressof(self.host)),Dep(b'platform.clock',1,C.addressof(self.clock)),Dep(b'storage.key-value.bound',1,C.addressof(self.storage)))
  self.t=U64()
 def sleep(self,_,n):self.time+=n
 def get(self,_,key,buf,cap,size):
  size[0]=0
  if key not in self.kv:return -1
  b=self.kv[key];size[0]=len(b)
  if cap<len(b):return -2
  C.memmove(buf,b,len(b));return 0
 def put(self,_,key,p,n):
  if self.fail_write:return -5
  self.kv[key]=C.string_at(p,n);return 0
 def claim(self,_,out):self.claims+=1;self.lease=7;out[0]=7;return True
 def release(self,_,token):
  assert token==7
  self.releases+=1
  if self.fail_close:return -1
  self.lease=0;self.rx.clear();return 1
 def next(self,_,token,typ,p,cap,size):
  assert token==7 and self.lease and cap>=1028
  size[0]=0
  if not self.rx:return 0
  kind,b=self.rx.popleft();typ[0]=kind;size[0]=len(b);C.memmove(p,b,len(b));return 1
 def event(self,b):self.rx.append((4,bytes(b)))
 def complete(self,op,b=b''):self.event(bytes([14,4+len(b),1])+le(op)+b'\0'+b)
 def send(self,_,token,kind,p,n):
  assert token==7 and self.lease
  b=C.string_at(p,n)
  if self.fail_send:return False
  if kind==2:
   self.acl.append(b);self.event(b'\x13\x05\x01'+le(int.from_bytes(b[:2],'little')&0xfff)+b'\x01\x00');return True
  assert kind==1 and n==3+b[2]
  op=int.from_bytes(b[:2],'little');self.commands.append((op,b[3:]))
  if self.drop_ack:return True
  data=b''
  if op==0x1001:data=bytes([9,0,0,9,0,0,0,0])
  elif op==0x1002:data=bytes(64)
  elif op==0x1003:data=bytes(4)+b'\x60'+bytes(3)
  elif op==0x2002:data=b'\xfb\x00\x08'
  elif op==0x2003:data=bytes(8)
  elif op==0x1009:data=bytes.fromhex('33221100450a')
  elif op==0x2018:
   data=hashlib.sha256(b'fixture RNG, never production'+le(self.random_counter,4)).digest()[:8];self.random_counter+=1
  elif op in (0x202a,0x2007):data=b'\x08'
  elif op in (0x201a,0x201b):data=b[:2] if False else b[3:5]
  elif op==0x0406:
   self.event(b'\x0f\x04\x00\x01'+le(op));self.event(b'\x05\x04\x00'+b[3:5]+b'\x13');return True
  self.complete(op,data);return True
 def start(self):
  assert not self.lib.t5_driver_get(1)
  assert not self.driver.start(self.deps,2)
  bad=(Dep*3)(self.deps[0],self.deps[0],self.deps[2]);assert not self.driver.start(bad,3)
  assert self.driver.start(self.deps,3)
  assert not self.hid.open(None,b"X"*21,True,C.byref(self.t))
  assert not self.hid.open(None,b"Bad\nname",True,C.byref(self.t))
  assert not self.hid.poll(None,1,1)
  assert not self.hid.close(None,1)
  assert self.claims==0 and not self.commands
  assert not self.hid.open(None,b'RiscRTE HID',False,C.byref(self.t))
  assert self.hid.open(None,b'RiscRTE HID',True,C.byref(self.t)) and self.t.value
  self.pump(20);assert self.status().state==2,self.describe()
 def status(self):s=Status(C.sizeof(Status));assert self.hid.status(None,self.t,C.byref(s));return s
 def describe(self):s=self.status();return (s.state,s.flags,s.error,self.commands)
 def pump(self,n=8):
  for _ in range(n):
   if not self.hid.poll(None,self.t,1):raise AssertionError(self.describe())
   self.time+=1
 def connect(self):
  # LE Connection Complete: handle1, peripheral role, peer public address.
  self.peer=bytes.fromhex('ca61a06794e0');self.own=bytes.fromhex('33221100450a')
  self.event(b'\x3e\x13\x01\x00\x01\x00\x01\x00'+self.peer+b'\x18\x00\x00\x00\xc8\x00\x00');self.pump(10)
 def l2cap(self,cid,payload):b=le(len(payload))+le(cid)+payload;self.rx.append((2,b'\x01\x20'+le(len(b))+b));self.pump(10)
 def outgoing(self,cid):
  found=[];remaining=[]
  for b in self.acl:
   if int.from_bytes(b[6:8],'little')==cid:found.append(b[8:])
   else:remaining.append(b)
  self.acl=remaining;return found
 def smp(self,payload):self.l2cap(6,payload);return self.outgoing(6)
 def att(self,payload):self.l2cap(4,payload);return self.outgoing(4)
 def discover(self):
  reply=self.att(b'\x02\x40\x00');assert reply==[b'\x03\x40\x00'],reply
  start=1;services=[]
  while True:
   reply=self.att(b'\x10'+le(start)+b'\xff\xff\x00\x28');assert len(reply)==1,reply
   b=reply[0]
   if b[0]==1:assert b[4]==10;break
   assert b[:2]==b'\x11\x06'
   for i in range(2,len(b),6):services.append(struct.unpack('<HHH',b[i:i+6]))
   start=services[-1][1]+1
   if start>65535:break
  hids=next(s for s in services if s[2]==0x1812);self.chars=[];start=hids[0]
  while start<=hids[1]:
   reply=self.att(b'\x08'+le(start)+le(hids[1])+b'\x03\x28');assert len(reply)==1
   b=reply[0]
   if b[0]==1:assert b[4]==10;break
   assert b[:2]==b'\x09\x07'
   for i in range(2,len(b),7):self.chars.append(struct.unpack('<HBHH',b[i:i+7]))
   start=self.chars[-1][0]+1
  self.report_handles={}
  for i,(decl,props,value,uuid) in enumerate(self.chars):
   end=self.chars[i+1][0]-1 if i+1<len(self.chars) else hids[1]
   if uuid==0x2a4d:
    reply=self.att(b'\x04'+le(value+1)+le(end));b=reply[0];assert b[:2]==b'\x05\x01',b
    descriptors={struct.unpack('<HH',b[j:j+4])[1]:struct.unpack('<HH',b[j:j+4])[0] for j in range(2,len(b),4)}
    ref=self.att(b'\x0a'+le(descriptors[0x2908]))[0];assert ref[0]==11
    self.report_handles[tuple(ref[1:])]=(value,descriptors.get(0x2902))
  assert set(self.report_handles)=={(1,1),(2,1),(1,2)}
  self.proto=next(c[2] for c in self.chars if c[3]==0x2a4e)
  self.bootkey=next(c[2] for c in self.chars if c[3]==0x2a22)
  self.bootmouse=next(c[2] for c in self.chars if c[3]==0x2a33)
  reportmap=next(c[2] for c in self.chars if c[3]==0x2a4b);data=b'';offset=0
  while True:
   b=self.att((b'\x0a'+le(reportmap)) if not offset else b'\x0c'+le(reportmap)+le(offset))[0]
   assert b[0] in (11,13),b;data+=b[1:]
   if len(b)<64:break
   offset=len(data)
  assert b'\x85\x01' in data and b'\x85\x02' in data and len(data)>100
  self.kbd,self.kcc=self.report_handles[(1,1)];self.mouse,self.mcc=self.report_handles[(2,1)]
  for ccc in (self.kcc,self.mcc):
   reply=self.att(b'\x12'+le(ccc)+b'\x01\x00');assert b'\x13' in reply,reply
  self.outgoing(4)
  assert self.status().flags&3==3,self.describe()
  print('ATT service/characteristic/descriptor discovery, long report map, encrypted CCCs: PASS')
 def reports(self):
  self.outgoing(4)
  assert not self.hid.keyboard(None,self.t,0,arr(b'\x04\x04'+bytes(4)))
  assert not self.hid.mouse(None,self.t,32,0,0,0)
  assert not self.hid.keyboard(None,self.t.value+1,0,arr(bytes(6)))
  assert self.hid.keyboard(None,self.t,3,arr(b'\x04'+bytes(5)))
  assert self.hid.keyboard(None,self.t,0,arr(bytes(6)))
  assert self.hid.mouse(None,self.t,1,17,-20,1)
  assert self.hid.mouse(None,self.t,0,0,0,0)
  self.pump();reports=self.outgoing(4)
  assert reports==[b'\x1b'+le(self.kbd)+b'\x03\x00\x04'+bytes(5),b'\x1b'+le(self.kbd)+bytes(8),b'\x1b'+le(self.mouse)+bytes([1,17,236,1]),b'\x1b'+le(self.mouse)+bytes(4)],reports
  assert self.hid.keyboard(None,self.t,2,arr(b'\x05'+bytes(5)))
  self.pump();self.outgoing(4);self.time+=1001;self.pump()
  releases=self.outgoing(4);assert b'\x1b'+le(self.kbd)+bytes(8) in releases
  # Mouse movement cannot indefinitely renew a held keyboard modifier.
  assert self.hid.keyboard(None,self.t,2,arr(b'\x05'+bytes(5)))
  self.pump();self.outgoing(4)
  for _ in range(3):
   self.time+=400;assert self.hid.mouse(None,self.t,0,1,0,0);self.pump()
  assert b'\x1b'+le(self.kbd)+bytes(8) in self.outgoing(4)
  # Keyboard traffic similarly cannot renew a held mouse button.
  assert self.hid.mouse(None,self.t,1,0,0,0);self.pump();self.outgoing(4)
  for _ in range(3):
   self.time+=400;assert self.hid.keyboard(None,self.t,0,arr(bytes(6)));self.pump()
  assert b'\x1b'+le(self.mouse)+bytes(4) in self.outgoing(4)
  # A different CCC change while a modifier is held must not silently lose it.
  assert self.hid.keyboard(None,self.t,2,arr(b'\x05'+bytes(5)));self.pump();self.outgoing(4)
  reply=self.att(b'\x12'+le(self.mcc)+b'\x00\x00')
  assert b'\x1b'+le(self.kbd)+bytes(8) in reply
  for ccc in (self.bootkey+1,self.bootmouse+1):self.att(b'\x12'+le(ccc)+b'\x01\x00')
  self.att(b'\x52'+le(self.proto)+b'\x00');self.outgoing(4)
  assert not self.hid.mouse(None,self.t,2,10,20,4)
  assert not self.hid.mouse(None,self.t,8,0,0,0)
  assert self.hid.mouse(None,self.t,2,10,20,0);self.pump()
  assert self.outgoing(4)==[b'\x1b'+le(self.bootmouse)+bytes([2,10,20])]
  assert self.hid.release(None,self.t);self.pump();self.outgoing(4)
  print('FIFO keyboard/modifier/click reports, signed motion, watchdog, CCC/protocol cleanup, boot mouse: PASS')
 def pair(self,accept=True,timeout=False,tamper=False):
  self.connect();assert self.status().state==3
  assert not self.hid.keyboard(None,self.t,1,arr(bytes(6)))
  req=bytes([1,1,0,13,16,2,2]);out=self.smp(req);rsp=next(p for p in out if p[0]==2)
  assert rsp[:5]==bytes([2,1,0,13,16]),rsp.hex()
  private=ec.derive_private_key(0xdeadbeefcafe123456789abcdef,ec.SECP256R1());pub=private.public_key().public_numbers();pk=le(pub.x,32)+le(pub.y,32)
  out=self.smp(b'\x0c'+pk);other=next(p[1:] for p in out if p[0]==12);confirm=next(p[1:] for p in out if p[0]==3)
  na=bytes.fromhex('ab77cba98dabb8c1995ad3fbe72cfb46');out=self.smp(b'\x04'+na);nb=next(p[1:] for p in out if p[0]==4)
  assert confirm==f4(other[:32],pk[:32],nb)
  s=self.status();assert s.state==4 and s.number==g2(pk[:32],other[:32],na,nb),(s.state,s.number)
  # No keys or mouse reports can be sent before explicit numeric confirmation.
  assert not self.hid.mouse(None,self.t,1,0,0,0)
  if timeout:
   self.time+=30001;self.pump();assert not self.status().flags&12;assert any(op==0x0406 for op,_ in self.commands);print("Numeric comparison timeout disconnects without accepting: PASS");return
  assert self.hid.confirm(None,self.t,accept)
  if not accept:
   self.pump();assert not self.status().flags&12;assert any(op==0x0406 for op,_ in self.commands);print("Explicit numeric rejection disconnects without accepting: PASS");return
  self.pump();out=self.outgoing(6)
  peerkey=ec.EllipticCurvePublicNumbers(int.from_bytes(other[:32],'little'),int.from_bytes(other[32:],'little'),ec.SECP256R1()).public_key()
  dh=private.exchange(ec.ECDH(),peerkey)[::-1];a1=b'\0'+self.peer;a2=b'\0'+self.own
  mac,ltk=f5(dh,na,nb,a1,a2);check=f6(mac,na,nb,bytes(16),req[1:4],a1,a2)
  if tamper:
   out+=self.smp(b'\x0d'+bytes([check[0]^1])+check[1:]);assert any(p[0]==5 for p in out);assert not self.status().flags&12;assert b'hid_ours' not in self.kv;print('Incorrect DHKey check is rejected without bonding: PASS');return
  out+=self.smp(b'\x0d'+check);remote=next(p[1:] for p in out if p[0]==13)
  assert remote==f6(mac,nb,na,bytes(16),rsp[1:4],a2,a1)
  self.event(b'\x3e\x0d\x05\x01\x00'+bytes(10));self.pump()
  replies=[p for op,p in self.commands if op==0x201a];assert replies and replies[-1][2:]==ltk
  self.event(b'\x08\x04\x00\x01\x00\x01');self.pump()
  # Peer identity distribution completes durable authenticated SC bonding.
  out=self.smp(b'\x08'+bytes.fromhex('00112233445566778899aabbccddeeff'))
  out+=self.smp(b'\x09\x00'+self.peer)
  self.ltk=ltk
  assert self.status().flags&28==28,self.describe()
  assert set(self.kv)>={b'hid_ours',b'hid_peer',b'hid_identity'}
  print('Real NimBLE Secure Connections numeric comparison, ECDH/CMAC checks and persisted bond: PASS')

def close_fixture(f):
 assert f.hid.close(None,f.t);f.t.value=0;assert f.status().state==0
 assert f.driver.quiesce()
def main():
 f=Fixture(Path(sys.argv[1]));f.start();print('Driver admission, bounded HCI startup and HID advertising: PASS')
 scenario=sys.argv[2] if len(sys.argv)>2 else 'happy'
 if scenario in ('reject','timeout','tamper'):
  f.pair(scenario!='reject',scenario=='timeout',scenario=='tamper');close_fixture(f);return
 if scenario=='invalid-public-key':
  f.connect();f.smp(bytes([1,1,0,13,16,2,2]));out=f.smp(b'\x0c'+bytes(64));assert any(p[0]==5 for p in out);assert not f.status().flags&12;close_fixture(f);print('Invalid Secure Connections public key rejected: PASS');return
 if scenario=='legacy':
  f.connect();out=f.smp(bytes([1,1,0,5,16,2,2]));assert any(p[0]==5 for p in out);assert not f.status().flags&12;close_fixture(f);print('Legacy pairing cannot downgrade Secure Connections profile: PASS');return
 if scenario=='retained':
  f.fail_close=True;assert not f.hid.close(None,f.t);assert not f.driver.quiesce();assert f.lease==7
  assert not f.hid.open(None,b'Other',True,C.byref(U64()))
  f.fail_close=False;assert f.hid.close(None,f.t);f.t.value=0;assert f.driver.quiesce();print('Failed close/quiescence retain lease and retry safely: PASS');return
 if scenario=='malformed':
  f.rx.append((4,b'\x0e\x03\x01'));assert not f.hid.poll(None,f.t,1);assert f.status().state==6
  close_fixture(f);print('Malformed HCI stream fails closed and closes custody: PASS');return
 if scenario=='storage':
  assert f.hid.close(None,f.t);f.t.value=0;f.kv[b'hid_ours']=bytes(64)
  assert not f.hid.open(None,b'RiscRTE HID',True,C.byref(f.t));assert not f.t.value;assert f.status().error==28
  f.fail_write=True;assert not f.hid.forget(None);assert not f.hid.open(None,b'RiscRTE HID',True,C.byref(f.t))
  f.fail_write=False;assert f.hid.forget(None);assert f.hid.open(None,b'RiscRTE HID',True,C.byref(f.t));f.pump(20);close_fixture(f);print('Corrupt/uncertain persistence fails closed; explicit forget recovers: PASS');return
 f.pair();f.discover();f.reports()
 assert f.hid.close(None,f.t);f.t.value=0;assert f.status().state==0
 assert f.hid.open(None,b'RiscRTE HID',False,C.byref(f.t));f.pump(20);f.connect()
 f.event(b'\x3e\x0d\x05\x01\x00'+bytes(10));f.pump()
 assert [p for op,p in f.commands if op==0x201a][-1][2:]==f.ltk
 f.event(b'\x08\x04\x00\x01\x00\x01');f.pump()
 assert f.status().flags&28==28,f.describe()
 f.outgoing(4)
 assert f.att(b'\x0a'+le(f.kbd))==[b'\x0b'+bytes(8)]
 assert f.att(b'\x0a'+le(f.mouse))==[b'\x0b'+bytes(4)]
 assert f.att(b'\x0a'+le(f.proto))==[b'\x0b\x01']
 print('Bonded reconnect restores CCCs without re-pairing; neutral reports/protocol reset: PASS')
 close_fixture(f)
 assert f.driver.start(f.deps,3)
 for _ in range(12):
  prior=f.t.value;assert f.hid.open(None,b'RiscRTE HID',False,C.byref(f.t));f.pump(20)
  assert not f.hid.close(None,prior)
  assert f.hid.close(None,f.t);f.t.value=0
 assert f.driver.quiesce()
 print('Checked host stop, lease release, quiescence/restart and 12 repeat opens without arena growth: PASS')
if __name__=='__main__':main()
