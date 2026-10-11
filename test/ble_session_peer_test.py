#!/usr/bin/env python3
"""Two production NimBLE brokers connected by a deterministic HCI wire.
No sockets, adapters or RF. The controller model checks matching ephemeral LTKs.
"""
from ble_session_setup_protocol_test import *
import shutil
class Peer(C.Structure):
 _fields_=[('size',U32),('id',U32),('rssi',I32),('name',C.c_char*21)]
class PeerApi(C.Structure):
 _fields_=Setup._fields_+[('peer_tag',U32),('discover_peer',F(I32,P,U32,C.POINTER(U64))),('peer',F(I32,P,U64,U32,C.POINTER(Peer))),('select',F(I32,P,U64,U32)),('descriptor',F(I32,P,U64,C.POINTER(Descriptor)))]
class NetworkOffer(C.Structure):
 _fields_=[('size',U32),('endpoint',Descriptor),('ssid',C.c_char*33),('password',C.c_char*64),('address',U8*4)]
class NetworkApi(C.Structure):
 _fields_=PeerApi._fields_+[('network_tag',U32),('open_network',F(I32,P,C.c_char_p,C.POINTER(NetworkOffer),U32,C.POINTER(U64))),('peer_network',F(I32,P,U64,C.POINTER(NetworkOffer)))]
class Node(Central):
 def __init__(self,path,index):
  self.index=index;self.other=None;self.address=bytes([0x30+index,0x22,0x11,0,0x45,0x0a]);self.ltk=None;self.trace=[]
  super().__init__(path);self.client=C.cast(self.driver.api,C.POINTER(PeerApi)).contents
  assert self.api.size>=C.sizeof(PeerApi) and self.client.peer_tag==0x42504331
 def descriptor(self):return Descriptor(C.sizeof(Descriptor),1,300000,b'http://192.168.2.7:8080/',b'files',b'test-password',b'peer-session')
 def send(self,ctx,token,kind,p,n):
  b=C.string_at(p,n)
  if kind==2:
   assert self.other and self.lease;self.trace.append((int.from_bytes(b[:2],"little"),int.from_bytes(b[6:8],"little"),(b[8:].hex() if int.from_bytes(b[6:8],"little")==4 and b[8] in (1,6,7,8,9) else b[8]) if len(b)>8 else -1));
   # Controller-to-host first ACL fragments use PB=2; host output uses PB=0.
   flags=int.from_bytes(b[:2],"little");routed=le((flags&0xcfff)|0x2000)+b[2:] if flags&0x3000==0 else b
   self.other.rx.append((2,routed));self.event(b'\x13\x05\x01'+le(int.from_bytes(b[:2],'little')&0xfff)+b'\x01\x00');return True
  op=int.from_bytes(b[:2],'little');args=b[3:]
  if op==0x1009:self.complete(op,self.address);return True
  if op==0x2018:
   data=hashlib.sha256(bytes([self.index])+le(self.random_counter,4)).digest()[:8];self.random_counter+=1;self.complete(op,data);return True
  if op==0x200d:
   self.commands.append((op,args));self.event(b'\x0f\x04\x00\x01'+le(op))
   for node,role in ((self,0),(self.other,1)):
    node.peer=node.other.address;node.own=node.address;node.connection=1
    node.event(b'\x3e\x13\x01\x00'+le(1)+bytes([role,0])+node.peer+b'\x18\x00\x00\x00\xc8\x00\x00')
   return True
  if op==0x2019:
   self.ltk=args[12:28];assert len(self.ltk)==16
   self.event(b'\x0f\x04\x00\x01'+le(op));self.other.event(b'\x3e\x0d\x05'+le(1)+bytes(10));return True
  if op==0x201a:
   assert args[2:18]==self.other.ltk,'controller rejected mismatched ephemeral LTK'
   self.complete(op,le(1))
   for node in (self,self.other):node.event(b'\x08\x04\x00'+le(1)+b'\x01')
   return True
  if op==0x0406:
   self.event(b'\x0f\x04\x00\x01'+le(op))
   for node in (self,self.other):node.event(b'\x05\x04\x00'+le(1)+b'\x13')
   return True
  return super().send(ctx,token,kind,p,n)
 def advertise_to(self,scanner):
  service=bytes.fromhex('21752464481b2da7864c626b01f012cc')
  for kind,data in ((0,b'\x02\x01\x06'+bytes([17,7])+service),(4,bytes([14,9])+b'RiscRTE Files')):
   event=b'\x02\x01'+bytes([kind,0])+self.address+bytes([len(data)])+data+b'\xc0'
   scanner.event(b'\x3e'+bytes([len(event)])+event)
def main():
 source=Path(sys.argv[1]).resolve();scenario=sys.argv[2] if len(sys.argv)>2 else 'happy'
 with tempfile.TemporaryDirectory(prefix='ble-peer-') as directory:
  files=[Path(directory)/f'peer-{i}.so' for i in range(2)]
  for path in files:shutil.copyfile(source,path)
  server=Node(files[0],0);client=Node(files[1],1);server.other=client;client.other=server
  assert C.cast(server.lib.t5_driver_get,P).value!=C.cast(client.lib.t5_driver_get,P).value
  server.start();assert client.driver.start(client.deps,2)
  direct=scenario.startswith('network')
  if direct:
   server.close();server.net=C.cast(server.driver.api,C.POINTER(NetworkApi)).contents;client.net=C.cast(client.driver.api,C.POINTER(NetworkApi)).contents
   offer=NetworkOffer();offer.size=C.sizeof(offer);offer.endpoint=server.descriptor();offer.endpoint.url=b'http://192.168.4.1:8080/';offer.ssid=b'Files-ephemeral';offer.password=b'128-bit-random-fixture-password';offer.address[:]=[192,168,4,1]
   assert server.net.network_tag==client.net.network_tag==0x42504e31
   assert server.net.open_network(None,b'RiscRTE Files',C.byref(offer),300000,C.byref(server.t)) in (0,1)
   offer.ssid=b'caller-wiped';offer.password=b'caller-wiped';network=NetworkOffer();network.size=C.sizeof(network)
   assert client.net.peer_network(None,client.t,C.byref(network))<0
  assert client.client.discover_peer(None,300000,C.byref(client.t)) in (0,1)
  def pump(count=1):
   for _ in range(count):
    for node in (server,client):
     try:node.pump(1)
     except AssertionError:raise AssertionError((node.index,server.describe(),client.describe(),server.trace,client.trace))
  pump(30);assert client.status().state==9,client.describe();server.advertise_to(client);pump(10)
  peer=Peer(C.sizeof(Peer));assert client.client.peer(None,client.t,0,C.byref(peer))==0
  assert peer.id and bytes(peer.name)==b'RiscRTE Files' and peer.rssi==-64
  before=client.claims;assert client.client.select(None,client.t,peer.id+1)==-1 and client.claims==before
  out=Descriptor(C.sizeof(Descriptor));assert client.client.descriptor(None,client.t,C.byref(out))<0
  assert client.client.select(None,client.t,peer.id)==1
  for _ in range(200):
   pump();a=server.status();b=client.status()
   if a.state==4 and b.state==4:break
  assert a.state==b.state==4,(server.describe(),client.describe(),server.trace,client.trace)
  assert a.number==b.number
  assert client.client.descriptor(None,client.t,C.byref(out))<0
  assert client.api.confirm(None,client.t,b.pair_generation+1,b.number,True)==-1
  if scenario in ('reject','network-reject'):
   server.allow_failure=client.allow_failure=True
   assert client.api.confirm(None,client.t,b.pair_generation,b.number,False)==0;pump(20)
   assert client.client.descriptor(None,client.t,C.byref(out))<0
  else:
   assert server.api.confirm(None,server.t,a.pair_generation,a.number,True) in (0,1)
   assert client.api.confirm(None,client.t,b.pair_generation,b.number,True) in (0,1)
   for _ in range(300):
    pump()
    if client.status().state==5:break
   assert client.status().state==5,(server.describe(),client.describe())
   assert client.client.descriptor(None,client.t,C.byref(out))==0
   assert bytes(out.url)==(b'http://192.168.4.1:8080/' if direct else b'http://192.168.2.7:8080/') and bytes(out.password)==b'test-password' and 0<out.remaining<300000
   if direct:
    assert client.net.peer_network(None,client.t,C.byref(network))==0
    assert bytes(network.ssid)==b'Files-ephemeral' and bytes(network.password)==b'128-bit-random-fixture-password' and list(network.address)==[192,168,4,1]
    assert server.status().flags&16
   if scenario=='happy' and len(sys.argv)>3:Path(sys.argv[3]).write_bytes(bytes(out))
   if scenario in ('expiry','network-expiry'):
    server.allow_failure=client.allow_failure=True;client.time+=300000;pump(2)
    assert client.client.descriptor(None,client.t,C.byref(out))<0
  client.allow_failure=server.allow_failure=True
  old=client.t.value;client.close();server.pump(5);server.close()
  assert client.client.peer(None,old,0,C.byref(peer))==-2
  assert client.driver.quiesce() and server.driver.quiesce() and not client.kv and not server.kv
  print('Two production BLE brokers: advertised discovery, explicit peer selection, mutual SC numeric comparison, checked GATT descriptor/validity, '+scenario+', ephemeral cleanup PASS')
if __name__=='__main__':main()
