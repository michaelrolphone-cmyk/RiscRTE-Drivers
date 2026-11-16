"""Narrow, explicit historical host-runner debug/pipe equivalence; not a rerun."""
import hashlib
from pathlib import Path
from package_badge_flash import require,sha
from execution_source_inputs import validated_inputs

def verify(receipt_path,receipt,equivalence=None):
 validated_inputs(receipt, allow_changed=(equivalence.get("source_path"),) if equivalence else ())
 used=False
 for path,digest in receipt['source_sha256'].items():
  if Path(path).parts[-2:]==("host-mapper","esp_dlfcn.h") and receipt.get("host_mapper_staging") is not None:continue
  current=Path(path).read_bytes()
  if hashlib.sha256(current).hexdigest()==digest:continue
  e=equivalence or {}
  require(not used and e.get('schema')==1 and e.get('qualification')=='host-runner-debug-pipe-only' and e.get('execution_receipt_sha256')==sha(receipt_path) and e.get('source_path')==path and path.endswith('/scripts/test_badge_prototype_execution.py'),'Execution source changed without exact equivalence: '+path)
  old=e.get('historical_source','').encode()
  require(hashlib.sha256(old).hexdigest()==digest and e.get('historical_sha256')==digest and e.get('current_sha256')==hashlib.sha256(current).hexdigest(),'Runner equivalence hash mismatch')
  before=b"common=['-O1','-g','-Wall'"
  after=b"common=['-pipe','-O1','-g1','-Wall'"
  require(old.count(before)==1 and old.replace(before,after,1)==current,'Runner changed beyond host pipe/debug metadata')
  used=True
 if equivalence is not None:require(used,'Unused runner equivalence must not relabel exact receipt')
 return used
