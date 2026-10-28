#!/usr/bin/env python3
"""Losslessly retain completed modeled PPM frames without loose-frame accumulation."""
import gzip, hashlib, json, re
from pathlib import Path

def compress_rasters(directory):
    directory=Path(directory)
    receipt=directory/'compressed-frames.json'
    records=json.loads(receipt.read_text()) if receipt.exists() else {}
    for raw in sorted(directory.rglob('*.ppm')):
        data=raw.read_bytes()
        header=re.match(rb'P6\n([1-9][0-9]*) ([1-9][0-9]*)\n255\n',data)
        if not header or len(data)!=header.end()+int(header[1])*int(header[2])*3:
            raise ValueError('Incomplete or malformed PPM frame: '+str(raw))
        digest=hashlib.sha256(data).hexdigest()
        packed=raw.with_name(raw.name+'.'+digest[:16]+'.gz')
        if not packed.exists():
            with packed.open('xb') as stream:
                with gzip.GzipFile(filename='',mode='wb',fileobj=stream,mtime=0) as output:
                    output.write(data)
        restored=gzip.decompress(packed.read_bytes())
        if restored!=data:raise ValueError('Compressed frame readback mismatch: '+str(raw))
        records[str(packed.relative_to(directory))]={'original_name':str(raw.relative_to(directory)),'bytes':len(data),'sha256':digest,'compressed_bytes':packed.stat().st_size}
        temporary=receipt.with_suffix('.json.tmp')
        temporary.write_text(json.dumps(records,indent=2,sort_keys=True)+'\n')
        temporary.replace(receipt)
        if raw.read_bytes()!=data:raise ValueError('Frame changed during preservation: '+str(raw))
        raw.unlink()
    return records

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path)
    print(json.dumps(compress_rasters(p.parse_args().directory),indent=2))
