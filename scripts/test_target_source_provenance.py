#!/usr/bin/env python3
import copy,hashlib,json,pathlib,subprocess,tempfile
from target_source_provenance import verify
with tempfile.TemporaryDirectory(prefix='target-provenance-') as tmp:
 root=pathlib.Path(tmp);profile=pathlib.Path('profile');(root/profile).mkdir();(root/'input.c').write_text('int exact_input;\n')
 lock={'runtime':{'commit':'1'*40}};(root/profile/'sources.lock.json').write_text(json.dumps(lock))
 def git(*args):return subprocess.check_output(['git','-C',str(root),*args],text=True,stderr=subprocess.DEVNULL).strip()
 git('init');git('add','.');git('-c','user.name=Fixture','-c','user.email=fixture@example.invalid','commit','-m','fixture');pin=git('rev-parse','HEAD')
 built={'source_pins':lock,'source_sha256':{'input.c':hashlib.sha256((root/'input.c').read_bytes()).hexdigest()}}
 assert verify(root,pin,built,profile)['source_input_count']==1
 def rejects(revision,value,expected=ValueError):
  try:verify(root,revision,value,profile)
  except expected:return
  raise AssertionError('Expected rejection')
 rejects('0'*40,built)
 changed=copy.deepcopy(built);changed['source_sha256']['input.c']='0'*64;rejects(pin,changed)
 changed=copy.deepcopy(built);changed['source_pins']={};rejects(pin,changed)
 for name in ['../input.c','/input.c']:
  changed=copy.deepcopy(built);changed['source_sha256']={name:'0'*64};rejects(pin,changed)
 changed=copy.deepcopy(built);changed['source_sha256']={};rejects(pin,changed)
 changed=copy.deepcopy(built);changed['source_sha256']={'missing.c':'0'*64};rejects(pin,changed,FileNotFoundError)
 (root/'dirty').write_text('not qualified');rejects(pin,built);(root/'dirty').unlink()
 assert verify(root,pin,built,profile)['repository_commit']==pin
 print('Exact target source provenance: positive,8 refusals,clean recovery PASS')
