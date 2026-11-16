#!/usr/bin/env python3
"""Lossless nested/repeated-name frame preservation at completed-case boundaries."""
import gzip, hashlib, json, tempfile
from pathlib import Path
from compress_badge_rasters import compress_rasters
with tempfile.TemporaryDirectory(prefix='raster-compression-') as t:
 root=Path(t); expected={}
 for parent in ['','bus','transfer','bus/nested']:
  p=root/parent/'frame.ppm';p.parent.mkdir(parents=True,exist_ok=True)
  data=b'P6\n2 1\n255\n'+bytes([len(parent)])*6;p.write_bytes(data);expected[str(p.relative_to(root))]=data
 records=compress_rasters(root);assert len(records)==4 and not list(root.rglob('*.ppm'))
 for name,r in records.items():
  data=gzip.decompress((root/name).read_bytes());assert data==expected[r['original_name']]
  assert r['sha256']==hashlib.sha256(data).hexdigest() and r['bytes']==len(data)
 assert compress_rasters(root)==records
 # A later completed frame with the same path retains both exact generations.
 p=root/'bus/frame.ppm';p.write_bytes(b'P6\n2 1\n255\n'+b'\xff'*6)
 later=compress_rasters(root);assert len(later)==5
 assert sum(r['original_name']=='bus/frame.ppm' for r in later.values())==2
 assert json.loads((root/'compressed-frames.json').read_text())==later
 assert all(gzip.decompress((root/n).read_bytes()) for n in later)
 print('PASS nested directories, repeated basenames, exact hashes/roundtrip, idempotence and preserved frame generations')
