"""Verify the actual imported dependency and preserved shared prefix, not truth."""
import hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 spec=json.loads((ROOT/'qualification/bq-source-inputs.json').read_text())
 for row in spec['files']:
  b=(ROOT/row['path']).read_bytes()
  assert hashlib.sha256(b).hexdigest()==row['sha256'],row['path']
  assert hashlib.sha1(b'blob '+str(len(b)).encode()+b'\0'+b).hexdigest()==row['blob'],row['path']
 old=(ROOT/spec['shared_header']['path']).read_bytes();assert hashlib.sha256(old).hexdigest()==spec['shared_header']['sha256']
 extended=(ROOT/'Drivers/bq25896/RiscUsbVbusV1.h').read_bytes();start=extended.index(b'\n/* An extension of the published monitor/external-host prefix');end=extended.index(b'#ifdef __cplusplus',start)
 assert extended[:start]+extended[end:]==old
 print('PASS exact BQ source/dependency pins and unchanged published shared prefix')
if __name__=='__main__':main()
