#!/usr/bin/env python3
import gzip,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from compress_badge_rasters import compress_rasters
with tempfile.TemporaryDirectory() as td:
 p=Path(td);frame=p/'case.ppm';data=b'P6\n2 1\n255\n'+bytes([0,13,255,17,33,0]);frame.write_bytes(data)
 r=compress_rasters(p);assert len(r)==1 and not frame.exists();assert gzip.decompress(next(p.glob('*.gz')).read_bytes())==data
 assert compress_rasters(p)==r
 frame.write_bytes(data+b'x');r=compress_rasters(p);assert len(r)==2 and not frame.exists()
 frame.write_bytes(data);r=compress_rasters(p);assert len(r)==2 and not frame.exists()
 # Existing corrupt compressed evidence must never cause deletion of input.
 key=next(k for k,v in r.items() if v['bytes']==len(data));(p/key).write_bytes(b'corrupt');frame.write_bytes(data)
 try:compress_rasters(p)
 except (OSError,EOFError):pass
 else:raise AssertionError('Corrupt evidence accepted')
 assert frame.read_bytes()==data
 print('PASS lossless raster readback, idempotence, overwritten-name preservation, corrupt-output source retention')
