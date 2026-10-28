#!/usr/bin/env python3
"""Verify exact sealed image bytes/regions/header/checksums without device IO."""
import argparse,hashlib,json,struct
from pathlib import Path
def verify(package):
 p=Path(package);j=json.loads((p/'package.json').read_text());raw=(p/'badge-prototype-combined.bin').read_bytes()
 def digest(b):return hashlib.sha256(b).hexdigest()
 assert len(raw)==33554432==j['flash_bytes'];assert digest(raw)==j['combined']['sha256']
 regions=sorted(j['regions'],key=lambda r:r['offset']);last=0
 for r in regions:
  off,capacity=r['offset'],r['capacity'];b=(p/r['file']).read_bytes()
  assert off>=last and len(b)<=capacity and off+capacity<=len(raw)
  assert len(b)==r['bytes'] and digest(b)==r['sha256'] and raw[off:off+len(b)]==b
  assert all(x==255 for x in raw[last:off]);assert all(x==255 for x in raw[off+len(b):off+capacity]);last=off+capacity
 assert all(x==255 for x in raw[last:]);assert raw[0x9000:0xf000]==b'\xff'*0x6000
 pt=(p/'partition-table.bin').read_bytes();rows=[]
 for at in range(0,len(pt),32):
  chunk=pt[at:at+32]
  if chunk[:2]==b'\xeb\xeb':assert chunk[16:]==hashlib.md5(pt[:at]).digest();break
  magic,kind,sub,off,size,name,flags=struct.unpack('<HBBII16sI',chunk);assert magic==0x50aa
  rows.append([name.rstrip(b'\0').decode(),kind,sub,off,size,flags])
 else:raise AssertionError('partition MD5 missing')
 assert rows==[['nvs',1,2,0x9000,0x6000,0],['factory',0,0,0x10000,0x300000,0],['bootfs',1,130,0x310000,0x1bf0000,0],['appdata',1,65,0x1f00000,0x100000,0]]
 for name in ['bootloader.bin','riscrte_native.bin']:
  b=(p/name).read_bytes();assert b[0]==0xe9 and b[2]==2 and b[3]==0x5f and b[23]==1
  at=24;checksum=0xef
  for _ in range(b[1]):
   _,length=struct.unpack_from('<II',b,at);at+=8
   for byte in b[at:at+length]:checksum^=byte
   at+=length
  end=(at+16)&~15;assert b[end-1]==checksum and b[end:end+32]==hashlib.sha256(b[:end]).digest()
  print(name,'SDK header, segments, XOR checksum and validation SHA passed')
 store={str(f.relative_to(p/'store')):{'sha256':digest(f.read_bytes()),'bytes':f.stat().st_size} for f in (p/'store').rglob('*') if f.is_file()}
 assert store==j['store_files'];assert digest((p/'store/default.elf').read_bytes())==json.loads((p/'store-proof.json').read_text())['home']['sha256']
 for line in (p/'SHA256SUMS').read_text().splitlines():
  h,name=line.split('  ',1);assert digest((p/name).read_bytes())==h,name
 print('PASS independent32MiB partition/region/blank-gap/store/hash validation;',len(store),'store files; physical untested')
 return {'passed':True,'physical_verified':False,'combined':j['combined'],'store_files':len(store),'blank_nvs':True,'package_sha256':digest((p/'package.json').read_bytes())}
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--package',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
 result=verify(args.package);args.output.write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
