#!/usr/bin/env python3
"""Production NimBLE session provider against the independent HCI/SMP central.
No RF/device actions. ECDH, f4/f5/f6/g2 use OpenSSL through cryptography.
"""
from ble_hid_protocol_test import *

class Descriptor(C.Structure):
 _fields_=[('size',U32),('transport',U32),('remaining',U32),('url',C.c_char*257),('username',C.c_char*65),('password',C.c_char*65),('session',C.c_char*65)]
class SetupStatus(C.Structure):
 _fields_=[('size',U32),('state',U32),('flags',U32),('number',U32),('pair_generation',U32),('generation',U32),('remaining',U32),('error',I32),('native_close',I32)]
class Setup(C.Structure):
 _fields_=[('version',U32),('size',U32),('ctx',P),('open',F(I32,P,C.c_char_p,C.POINTER(Descriptor),U32,C.POINTER(U64))),('poll',F(I32,P,U64,U32)),('status',F(I32,P,U64,C.POINTER(SetupStatus))),('confirm',F(I32,P,U64,U32,U32,B)),('close',F(I32,P,U64))]
class ClockWait(C.Structure):
 _fields_=Clock._fields_+[('wait_tag',U32),('wait_version',U32),('wait',F(B,P,U32))]
class Central(Fixture):
 def __init__(self,path):
  symbols={row.split()[-1]:int(row.split()[0],16) for row in subprocess.check_output(['nm','--defined-only',str(path)],text=True).splitlines() if len(row.split())==3}
  self.lib=C.CDLL(str(path));self.lib.t5_driver_get.argtypes=[U32];self.lib.t5_driver_get.restype=C.POINTER(Driver)
  self.driver=self.lib.t5_driver_get(2).contents;self.api=C.cast(self.driver.api,C.POINTER(Setup)).contents
  assert self.api.version==1 and self.api.size==C.sizeof(Setup)
  self.fail_receive=False;self.connection=1;self.rx=collections.deque();self.acl=[];self.commands=[];self.kv={};self.time=100;self.lease=0;self.claims=0;self.releases=0;self.fail_send=False;self.fail_close=False;self.fail_write=False;self.drop_ack=False;self.random_counter=0;self.callbacks=[];self.no_credits=False;self.claim_failure=0;self.resolving={}
  def wrap(t,fn):v=t(fn);self.callbacks.append(v);return v
  self.host=Host(1,C.sizeof(Host),None,Send(),Next(),F(B,P,B)(),F(B,P,C.POINTER(U8))(),wrap(Claim,self.claim),wrap(OwnedSend,self.send),wrap(OwnedNext,self.next),wrap(Release,self.release))
  self.clock=ClockWait(1,C.sizeof(ClockWait),None,wrap(F(U64,P),self.clock_now),wrap(F(None,P,U32),self.sleep),0x43575431,1,wrap(F(B,P,U32),self.scheduler_wait))
  self.deps=(Dep*2)(Dep(b'bluetooth.hci',1,C.addressof(self.host)),Dep(b'platform.clock',1,C.addressof(self.clock)))
  self.t=U64();self.allow_failure=False;self.cleanup_calls=[];self.wait_refuse=False
  base=C.cast(self.lib.t5_driver_get,P).value-symbols['t5_driver_get']
  self.wire_address=base+symbols['descriptor']
 def clock_now(self,context):
  self.cleanup_calls.append(('now',));return self.time
 def scheduler_wait(self,context,n):
  self.cleanup_calls.append(('wait',n))
  if self.wait_refuse or not 1<=n<=50:return False
  Fixture.sleep(self,context,n);return True
 def sleep(self,context,n):
  self.cleanup_calls.append(('sleep',n));Fixture.sleep(self,context,n)
 def next(self,*args):
  self.cleanup_calls.append(('next',));return Fixture.next(self,*args)
 def send(self,*args):
  self.cleanup_calls.append(('send',));return Fixture.send(self,*args)
 def release(self,*args):
  result=Fixture.release(self,*args);self.cleanup_calls.append(('release',result));return result
 def descriptor(self):
  return Descriptor(C.sizeof(Descriptor),1,900000,b'http://192.0.2.7:8080/dav/'+b'x'*200,b'temporary-user',b'ephemeral-password-DO-NOT-LOG',b'session-0001')
 def open(self,lifetime=300000):
  self.data=self.descriptor();self.expected=bytes(self.data.url),bytes(self.data.username),bytes(self.data.password),bytes(self.data.session)
  rc=self.api.open(None,b'RiscRTE Setup',C.byref(self.data),lifetime,C.byref(self.t));assert rc in (0,1),rc
  self.pump(20);assert self.status().state==2
 def start(self):
  assert not self.lib.t5_driver_get(1)
  assert not self.driver.start(self.deps,1)
  assert self.driver.start(self.deps,2)
  assert self.claims==0 and not self.commands
  self.open()
 def status(self):
  s=SetupStatus(C.sizeof(SetupStatus));assert self.api.status(None,self.t,C.byref(s))==0;return s
 def describe(self):
  s=self.status();return (s.state,s.flags,s.error)
 def pump(self,n=8):
  for _ in range(n):
   rc=self.api.poll(None,self.t,1)
   if not self.allow_failure:assert rc in (0,1),(rc,self.describe())
   self.time+=1
 def wiped(self):assert C.string_at(self.wire_address,464)==bytes(464)
 def close(self):
  assert self.api.close(None,self.t)==0;self.t.value=0
  self.wiped()
  assert self.status().state==0
 def discover(self):
  assert self.att(b'\x02\x40\x00')==[b'\x03\x40\x00']
  self.value=self.validity=None;start=1
  # Find by type value matches the complete 128-bit primary service UUID.
  uuid=bytes.fromhex('21752464481b2da7864c626b01f012cc')
  r=self.att(b'\x06\x01\x00\xff\xff\x00\x28'+uuid)[0]
  assert r[0]==7,r.hex();begin,end=struct.unpack('<HH',r[1:5])
  while begin<=end:
   r=self.att(b'\x08'+le(begin)+le(end)+b'\x03\x28')[0]
   if r[0]==1:break
   assert r[:2]==b'\x09\x15',r.hex()
   for off in range(2,len(r),21):
    declaration,properties,value=struct.unpack('<HBH',r[off:off+5]);u=r[off+5:off+21]
    assert properties==2
    if u[12]==2:self.value=value
    if u[12]==3:self.validity=value
    begin=declaration+1
  assert self.value and self.validity
 def deny(self,disconnected=False):
  for p in (b'\x0a'+le(self.value),b'\x0c'+le(self.value)+le(20),b'\x0a'+le(self.validity)):
   r=self.att(p)
   if disconnected and not r:continue
   assert len(r)==1 and r[0][0]==1 and r[0][-1] in (5,15),r
  assert not self.status().flags&8
 def pair(self,accept=True,timeout=False,tamper=False,stale=False,just_works=False):
  if self.status().state==2:self.connect()
  req=bytes([1,3 if just_works else 1,0,12,16,0,0]);out=self.smp(req);rsp=next(p for p in out if p[0]==2)
  assert rsp==bytes([2,1,0,12,16,0,0]),rsp.hex()
  private=ec.derive_private_key(0xdeadbeefcafe123456789abcdef,ec.SECP256R1());pub=private.public_key().public_numbers();pk=le(pub.x,32)+le(pub.y,32)
  out=self.smp(b'\x0c'+pk);other=next(p[1:] for p in out if p[0]==12);confirm=next(p[1:] for p in out if p[0]==3)
  na=bytes.fromhex('ab77cba98dabb8c1995ad3fbe72cfb46');out=self.smp(b'\x04'+na);nb=next(p[1:] for p in out if p[0]==4)
  assert confirm==f4(other[:32],pk[:32],nb)
  s=self.status()
  if just_works:assert s.state==3 and not s.flags
  else:assert s.state==4 and s.number==g2(pk[:32],other[:32],na,nb)
  if self.value:self.deny()
  if stale:
   assert self.api.confirm(None,self.t,s.pair_generation-1,s.number,True)==-1
   assert self.api.confirm(None,self.t,s.pair_generation,(s.number+1)%1000000,True)==-1
   assert self.status().state==4
  if timeout:
   self.time+=30001;self.allow_failure=True
   assert self.api.confirm(None,self.t,s.pair_generation,s.number,True)==-5
   self.pump();assert self.status().flags==0;return
  rc=0 if just_works else self.api.confirm(None,self.t,s.pair_generation,s.number,accept);assert rc==0,rc
  if not accept:
   self.allow_failure=True;self.pump();assert self.status().flags==0;return
  assert self.status().flags==(0 if just_works else 4)
  self.pump();out=self.outgoing(6)
  peerkey=ec.EllipticCurvePublicNumbers(int.from_bytes(other[:32],'little'),int.from_bytes(other[32:],'little'),ec.SECP256R1()).public_key()
  dh=private.exchange(ec.ECDH(),peerkey)[::-1];a1=b'\0'+self.peer;a2=b'\0'+self.own
  mac,ltk=f5(dh,na,nb,a1,a2);check=f6(mac,na,nb,bytes(16),req[1:4],a1,a2)
  if tamper:
   self.allow_failure=True;out+=self.smp(b'\x0d'+bytes([check[0]^1])+check[1:]);assert any(p[0]==5 for p in out);assert not self.status().flags&8;return
  out+=self.smp(b'\x0d'+check);remote=next(p[1:] for p in out if p[0]==13)
  assert remote==f6(mac,nb,na,bytes(16),rsp[1:4],a2,a1)
  self.event(b'\x3e\x0d\x05'+le(self.connection)+bytes(10));self.pump()
  replies=[p for op,p in self.commands if op==0x201a];assert replies[-1][2:]==ltk
  self.event(b'\x08\x04\x00'+le(self.connection)+b'\x01')
  if just_works:self.allow_failure=True
  self.pump()
  if just_works:
   assert not self.status().flags and self.status().state==7;return
  self.ltk=ltk;assert self.status().flags==15,self.describe();assert not self.kv
 def read_descriptor(self):
  value=b''
  while True:
   r=self.att((b'\x0a'+le(self.value)) if not value else b'\x0c'+le(self.value)+le(len(value)))[0]
   assert r[0] in (11,13),r.hex();value+=r[1:]
   if len(r)<64:break
  assert value[:2]==b'\x01\x01' and int.from_bytes(value[2:4],'little')==len(value)
  assert int.from_bytes(value[4:8],'little')==self.data.remaining
  sizes=struct.unpack('<4H',value[8:16]);parts=[];off=16
  for size in sizes:parts.append(value[off:off+size]);off+=size
  assert tuple(parts)==self.expected and off==len(value)
  return value

def main():
 f=Central(Path(sys.argv[1]));scenario=sys.argv[2] if len(sys.argv)>2 else 'happy'
 if scenario=='clock-admission':
  original=(f.clock.size,f.clock.wait_tag,f.clock.wait_version,f.callbacks[-1])
  for size,tag,version,callback in ((C.sizeof(Clock),0x43575431,1,original[3]),(C.sizeof(ClockWait)-1,0x43575431,1,original[3]),(C.sizeof(ClockWait),0,1,original[3]),(C.sizeof(ClockWait),0x43575431,2,original[3]),(C.sizeof(ClockWait),0x43575431,1,F(B,P,U32)())):
   f.clock.size=size;f.clock.wait_tag=tag;f.clock.wait_version=version;f.clock.wait=callback
   assert not f.driver.start(f.deps,2)
   d=f.descriptor();assert f.api.open(None,b'Setup',C.byref(d),1000,C.byref(f.t))==-2 and not f.t.value
   assert not f.claims and not f.cleanup_calls
  f.clock.size,f.clock.wait_tag,f.clock.wait_version,f.clock.wait=original
  f.start();f.close();assert f.driver.quiesce()
  print('Prefix-only, truncated, invalid tag/version/null scheduler suffix reject before any native or clock call: PASS');return
 if scenario=='validation':
  assert f.driver.start(f.deps,2)
  cases=[('transport',2),('url',b'http://user:pass@example.org/'),('url',b'http://example.org/#fragment'),('url',b'http://example.org/a\nb'),('url',b'http:///dav'),('url',b'http://example.org\\@evil/'),('url',b'http://example.org/a b'),('username',b''),('password',b'a\x01'),('remaining',0)]
  for field,value in cases:
   d=f.descriptor();setattr(d,field,value)
   assert f.api.open(None,b'Setup',C.byref(d),1000,C.byref(f.t))==-1 and not f.t.value
  assert not f.claims and not f.commands
  f.open();f.close();assert f.driver.quiesce();print('Descriptor validation rejects unsafe data before radio claim: PASS');return
 if scenario=='clock-wrap':f.time=0xfffffff0
 f.start()
 if scenario=='maximum':
  f.close();f.descriptor=lambda:Descriptor(C.sizeof(Descriptor),1,900000,b'http://192.0.2.7/'+b'x'*239,b'u'*64,b'p'*64,b's'*64)
  f.open()
 if scenario=='claim':
  f.close()
  for fail in (1,2):
   f.claim_failure=fail;d=f.descriptor();assert f.api.open(None,b'Setup',C.byref(d),1000,C.byref(f.t))==-4 and f.t.value
   if fail==2:
    f.fail_close=True;assert f.api.close(None,f.t)==3;f.fail_close=False
   f.close();f.claim_failure=0;f.open();f.close()
  assert f.driver.quiesce();print('Failed and retained native claims preserve cleanup custody: PASS');return
 if scenario=='retained-terminal':
  f.connect();f.discover();f.pair();f.fail_close=True
  assert f.api.close(None,f.t)==3;f.wait_refuse=True;f.cleanup_calls.clear()
  assert f.api.close(None,f.t)==-3 and f.cleanup_calls==[('release',-1),('wait',1)]
  f.wiped();f.cleanup_calls.clear();token=f.t.value;f.fail_close=False;f.wait_refuse=False
  s=SetupStatus(C.sizeof(SetupStatus));d=f.descriptor()
  for _ in range(32):
   assert f.api.close(None,f.t)==-3 and f.api.poll(None,f.t,16)==-3
   assert f.api.status(None,f.t,C.byref(s))==-3 and f.api.confirm(None,f.t,1,0,True)==-3
   assert f.api.open(None,b'Setup',C.byref(d),1000,C.byref(U64()))==-3
   assert not f.driver.quiesce() and not f.driver.start(f.deps,2)
   f.driver.stop()
  assert f.cleanup_calls==[] and f.t.value==token and f.lease==7
  print('Explicit owner-checked wait refusal is terminal: all later APIs/fini inert and custody retained: PASS');return
 if scenario=='retained-incomplete':
  f.connect();f.discover();f.pair();f.fail_close=True;f.drop_disconnect=True;f.sleep_jump=500
  f.cleanup_calls.clear();assert f.api.close(None,f.t)==3
  assert sum(call==('wait',1) for call in f.cleanup_calls)>=2
  assert not any(call[0]=='sleep' for call in f.cleanup_calls)
  f.cleanup_calls.clear()
  for _ in range(8):assert f.api.close(None,f.t)==3
  assert f.cleanup_calls==[('release',-1),('wait',1)]*8
  f.fail_close=False;f.close();assert f.driver.quiesce()
  print('Native release attempt fences further NimBLE work even after incomplete host stop: PASS');return
 if scenario=='retained-cooperate':
  f.connect();f.discover();f.pair();f.read_descriptor();f.fail_close=True
  assert f.api.close(None,f.t)==3;f.wiped()
  token=f.t.value;before=(len(f.commands),len(f.rx));f.cleanup_calls.clear()
  for _ in range(64):
   assert f.api.close(None,f.t)==3 and f.t.value==token and f.lease==7
   f.wiped()
  assert f.cleanup_calls==[('release',-1),('wait',1)]*64,f.cleanup_calls
  assert before==(len(f.commands),len(f.rx))
  f.cleanup_calls.clear();assert not f.driver.quiesce()
  assert f.cleanup_calls==[('release',-1),('wait',1)] and f.lease==7
  f.cleanup_calls.clear();f.fail_close=False;assert f.api.close(None,f.t)==0;f.t.value=0;f.wiped()
  assert f.cleanup_calls==[('release',1)] and not f.lease
  assert f.api.close(None,token)==-2 and f.driver.quiesce()
  print('64 retained close retries and quiesce yield once each, never poll transport, then release custody: PASS');return
 if scenario=='retained':
  f.connect();f.discover();f.pair();f.read_descriptor();f.fail_close=True
  assert f.api.close(None,f.t)==3
  f.wiped()
  before=(len(f.commands),len(f.rx));s=SetupStatus(C.sizeof(SetupStatus))
  for _ in range(10):
   assert f.api.poll(None,f.t,16)==3 and f.api.status(None,f.t,C.byref(s))==3
   assert f.api.confirm(None,f.t,1,0,True)==3
  assert before==(len(f.commands),len(f.rx)) and not f.driver.quiesce() and f.lease
  f.fail_close=False;f.close();assert f.driver.quiesce();print('Retained native release permits cleanup only, with no ordinary host work: PASS');return
 if scenario=='immediate-close':
  f.close()
  for _ in range(20):
   d=f.descriptor();assert f.api.open(None,b'Setup',C.byref(d),1000,C.byref(f.t)) in (0,1);f.close()
  assert f.driver.quiesce();print('Repeated immediate close and reopen: PASS');return
 if scenario=='short-lifetime':
  f.close();f.descriptor=lambda:Descriptor(C.sizeof(Descriptor),1,1000,b'http://192.0.2.7/dav/',b'user',b'pass',b'session')
  f.open();assert 0<f.status().remaining<=1000
 f.connect();f.discover();f.deny()
 if scenario=='just-works':
  f.pair(just_works=True);f.close();assert f.driver.quiesce();print('Completed unauthenticated SC Just Works never exposes credentials: PASS');return
 if scenario in ('legacy','short-key','invalid-key'):
  f.allow_failure=True
  req=bytes([1,3 if scenario=='just-works' else 1,0,4 if scenario=='legacy' else 12,7 if scenario=='short-key' else 16,0,0])
  out=f.smp(req)
  if scenario=='invalid-key':out+=f.smp(b'\x0c'+bytes(64))
  if scenario!='just-works':assert any(p[0]==5 for p in out),out
  assert not f.status().flags&8;f.close();assert f.driver.quiesce();print('Rejected insecure pairing profile '+scenario+': PASS');return
 if scenario in ('reject','timeout','tamper'):
  f.pair(accept=scenario!='reject',timeout=scenario=='timeout',tamper=scenario=='tamper');f.wiped();f.close();assert f.driver.quiesce();print('SMP '+scenario+' fails closed: PASS');return
 f.pair(stale=True)
 if scenario=='expiry-mid-read':
  first=f.att(b'\x0a'+le(f.value))[0];assert first[0]==11 and len(first)==64
  f.time+=300000;f.allow_failure=True
  rest=f.att(b'\x0c'+le(f.value)+le(63))
  assert not rest or rest[0][0]==1
  assert not f.status().flags;f.wiped();f.close();assert f.driver.quiesce()
  print('Expiry interrupts ATT long read and destroys the copied secret: PASS');return
 if scenario=='copy':
  f.data.url=b'http://changed.invalid/';f.data.password=b'changed'
 f.read_descriptor()
 # ATT rejects invalid long-read offsets and writes.
 assert f.att(b'\x0c'+le(f.value)+le(511))[0][-1]==7
 assert f.att(b'\x12'+le(f.value)+b'changed')[0][-1]==3
 validity=f.att(b'\x0a'+le(f.validity))[0]
 assert validity[0]==11 and len(validity)==17 and int.from_bytes(validity[1:5],'little')<=300000
 if scenario in ('expiry','short-lifetime'):
  f.time+=1000 if scenario=='short-lifetime' else 300000;f.allow_failure=True
  # Status checks expiry even without a preceding poll; ATT never serves another byte.
  s=f.status();assert s.state==6 and not s.flags and s.remaining==0;f.wiped()
  f.deny(disconnected=True);assert f.api.poll(None,f.t,1)==-5
 elif scenario=='disconnect':
  old=f.t.value;f.allow_failure=True
  f.event(b'\x05\x04\x00'+le(f.connection)+b'\x13');f.pump();assert f.status().state==6 and not f.status().flags
  f.close();f.allow_failure=False;f.open();assert f.t.value!=old
  assert f.api.close(None,old)==-2
  f.connect(handle=2);f.discover();f.deny()
  # Prior ephemeral LTK is absent and cannot resume encryption in a new session.
  f.event(b'\x3e\x0d\x05'+le(f.connection)+bytes(10));f.pump()
  assert any(op==0x201b for op,_ in f.commands)
  f.pair();f.read_descriptor()
 elif scenario=='stale-events':
  before=f.describe()
  f.event(b'\x08\x04\x05\x02\x00\x00');f.event(b'\x05\x04\x00\x02\x00\x13');f.pump()
  assert f.describe()==before;f.read_descriptor()
 elif scenario=='fault':
  f.fail_receive=True;assert f.api.poll(None,f.t,1)==-4;assert not f.status().flags
  f.fail_receive=False
 f.close();assert f.driver.quiesce();print('Real SC numeric comparison, immutable authenticated ATT long reads, '+scenario+': PASS')
if __name__=='__main__':main()
