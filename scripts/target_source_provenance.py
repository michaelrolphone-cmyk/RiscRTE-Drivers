"""Verify an explicitly pinned, clean owning tree for reused store build inputs.

This verifies source provenance only. Store byte equality, native ABI admission,
execution receipts and changed-native qualification remain separate gates.
"""
import hashlib,json,subprocess
from pathlib import Path

def git(root,*args):
    return subprocess.check_output(['git','-C',str(root),*args],text=True).strip()

def verify(root,revision,built,profile):
    root=Path(root).resolve()
    if not isinstance(revision,str) or len(revision)!=40 or git(root,'rev-parse','HEAD')!=revision:
        raise ValueError('Exact target owning source revision required')
    if git(root,'status','--porcelain'):
        raise ValueError('Target owning source must be clean')
    if json.loads((root/profile/'sources.lock.json').read_text())!=built['source_pins']:
        raise ValueError('Target owning source lock mismatch')
    checked={}
    for name,digest in built['source_sha256'].items():
        relative=Path(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Target source input escapes owning tree')
        actual=hashlib.sha256((root/relative).read_bytes()).hexdigest()
        if actual!=digest:raise ValueError('Target owning source changed: '+name)
        checked[name]=actual
    if not checked:raise ValueError('Missing target source inputs')
    return {'repository_commit':revision,'repository_tree':git(root,'rev-parse','HEAD^{tree}'),
            'source_input_count':len(checked),'source_sha256':checked,
            'scope':'Exact store-build owning inputs; not a claim that host tests reran or native behavior is unchanged'}
