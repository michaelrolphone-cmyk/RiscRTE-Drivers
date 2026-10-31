import ast,copy,importlib.util,pathlib,unittest,sys,os,subprocess,tempfile,hashlib
p=pathlib.Path(__file__).resolve().parents[1]/'scripts/timecard_evidence_gate.py'
# File-based module loading does not add its directory for sibling imports.
# Resolve from this checkout, never the caller's cwd or ambient PYTHONPATH.
sys.path.insert(0,str(p.parent))
spec=importlib.util.spec_from_file_location('timecard_evidence_gate',p);g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
class TimecardGateTests(unittest.TestCase):
 def good(self):
  r=dict(schema=1,qualification='badge-timecard',passed=True,sanitize=False,include_timecard=True,foreground=True,target_build_sha256='target',source_pins={'system':'pin'},store_files={'timecard.elf':'hash'},host_preference_store=1,host_atomic_key_value=0,native_alarms=True,quick_actions=True,physical_tested=False,target_instruction_execution=False,case_count=6,timecard_recipe={'recipe':'exact'},foreground_recipes={str(i):{} for i in range(9)},source_sha256={'source':'hash'},fixture_sha256={x:'hash' for x in g.FIXTURES})
  r['foreground_recipes']['timecard']={}
  r.update(launcher_catalog=[{'file_name':'settings.elf'}]+[{'file_name':str(i)+'.elf'} for i in range(8)]+[{'file_name':'timecard.elf'}],launcher_catalog_path='catalog.json',launcher_catalog_sha256='catalog-hash')
  r['source_sha256']['catalog.json']='catalog-hash'
  r['results']=[dict(target_index=0,scenario=k,returncode=0,stderr='',stdout='PASS actual Runtime Timecard scenario='+k+' launcher_item=10 catalog=10 actual_scroll=1 data_reads=1 data_writes='+str(int(k=='save'))+' light=1 restored=1 pending_frames=12 copied_preference_writes='+str(int(k.startswith('quick-')))+' retained='+str(int(k=='quick-retained'))+' no_after_retained=passed modeled_not_device') for k in g.EXPECTED]
  return r
 def run_gate(self,r):g.qualify_timecard({'source_pins':{'system':'pin'},'foreground_idle':{'apps':[str(i) for i in range(9)]+['timecard']}},r,False,{'timecard.elf':'hash'},'target',p.parents[1],False)
 def test_real_producer_fixture_inventory_matches_gate(self):
  # Inspect actual producer declaration rather than a second mock inventory.
  producer=p.with_name('test_badge_timecard_execution.py')
  tree=ast.parse(producer.read_text())
  inventories=[value for node in ast.walk(tree) if isinstance(node,ast.Dict)
   for key,value in zip(node.keys,node.values)
   if isinstance(key,ast.Constant) and key.value=='fixture_sha256']
  self.assertEqual(len(inventories),1)
  inv=inventories[0];self.assertIsInstance(inv,ast.DictComp)
  self.assertEqual(len(inv.generators),1)
  declared=inv.generators[0].iter;self.assertIsInstance(declared,ast.List)
  names=[]
  for item in declared.elts:
   if ast.dump(item)==ast.dump(ast.parse('Path(__file__).resolve()',mode='eval').body):
    names.append('scripts/'+producer.name)
   else:
    self.assertIsInstance(item,ast.BinOp);self.assertIsInstance(item.op,ast.Div)
    self.assertIsInstance(item.left,ast.Name);self.assertEqual(item.left.id,'ROOT')
    self.assertIsInstance(item.right,ast.Constant);self.assertIsInstance(item.right.value,str)
    names.append(item.right.value)
  self.assertEqual(len(names),len(set(names)))
  self.assertEqual(set(names),g.FIXTURES)
 def test_each_fixture_required_and_unexpected_fixture_refused(self):
  for name in g.FIXTURES:
   with self.subTest(missing=name):
    r=self.good();del r['fixture_sha256'][name]
    with self.assertRaisesRegex(ValueError,'Missing Timecard source provenance'):self.run_gate(r)
  r=self.good();r['fixture_sha256']['tests/unrelated.cpp']='hash'
  with self.assertRaisesRegex(ValueError,'Missing Timecard source provenance'):self.run_gate(r)
 def test_valid(self):self.run_gate(self.good())
 def test_standalone_entrypoint_without_ambient_imports(self):
  with tempfile.TemporaryDirectory() as cwd:
   for isolated in (False,True):
    command=[sys.executable,'-B']+(['-I'] if isolated else [])+[str(pathlib.Path(__file__).resolve()),'TimecardGateTests.test_valid']
    result=subprocess.run(command,cwd=cwd,env={k:v for k,v in os.environ.items() if k!='PYTHONPATH'},capture_output=True,text=True)
    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    self.assertIn('Ran 1 test',result.stderr)
 def test_real_source_verification_remains_enforced(self):
  with tempfile.TemporaryDirectory() as directory:
   root=pathlib.Path(directory);r=self.good()
   for name in g.FIXTURES:
    fixture=root/name;fixture.parent.mkdir(parents=True,exist_ok=True);fixture.write_text('fixture '+name)
   r['fixture_sha256']={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in g.FIXTURES}
   catalog=root/'catalog.json';catalog.write_text('catalog')
   source=root/'source.c';source.write_text('original input')
   r['launcher_catalog_path']=str(catalog);r['launcher_catalog_sha256']=hashlib.sha256(catalog.read_bytes()).hexdigest()
   r['source_sha256']={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in (catalog,source)}
   args=({'source_pins':{'system':'pin'},'foreground_idle':{'apps':list(r['foreground_recipes'])}},r,False,{'timecard.elf':'hash'},'target',root)
   g.qualify_timecard(*args)
   source.write_text('changed input')
   with self.assertRaisesRegex(ValueError,'Execution source changed'):
    g.qualify_timecard(*args)
 def test_manifest_position_is_not_assumed(self):
  r=self.good();r['launcher_catalog'].insert(1,r['launcher_catalog'].pop())
  for row in r['results']:row['stdout']=row['stdout'].replace('launcher_item=10','launcher_item=2')
  self.run_gate(r)
  r['results'][0]['stdout']=r['results'][0]['stdout'].replace('launcher_item=2','launcher_item=10')
  with self.assertRaises(ValueError):self.run_gate(r)
 def test_catalog_must_be_unique_and_source_bound(self):
  for key,value in [('launcher_catalog',[]),('launcher_catalog_path',None),('launcher_catalog_sha256','changed')]:
   r=self.good();r[key]=value
   with self.assertRaises(ValueError):self.run_gate(r)
  r=self.good();r['launcher_catalog'][1]={'file_name':'timecard.elf'}
  with self.assertRaises(ValueError):self.run_gate(r)
 def test_mutations(self):
  for key,value in [('sanitize',True),('include_timecard',False),('foreground',False),('target_build_sha256','other'),('source_pins',{}),('store_files',{}),('host_preference_store',0),('host_atomic_key_value',1),('physical_tested',True),('target_instruction_execution',True),('native_alarms',False),('quick_actions',False),('case_count',5),('timecard_recipe',None),('foreground_recipes',{}),('source_sha256',{}),('fixture_sha256',{})]:
   with self.subTest(key=key):
    r=self.good();r[key]=value
    with self.assertRaises(ValueError):self.run_gate(r)
 def test_results(self):
  for i in range(6):
   for key,value in [('returncode',1),('stderr','fault'),('stdout','PASS'),('target_index',9),('scenario','other')]:
    with self.subTest(i=i,key=key):
     r=self.good();r['results'][i][key]=value
     with self.assertRaises(ValueError):self.run_gate(r)
 def test_numeric_contradictions(self):
  for scenario,field,value in [('save','data_writes','0'),('cold-load','data_reads','0'),('light-draft','light','0'),('light-draft','restored','0'),('quick-shared','copied_preference_writes','0'),('quick-shared','pending_frames','0'),('quick-retained','copied_preference_writes','2'),('quick-retained','pending_frames','0')]+[(k,'actual_scroll','0') for k in g.EXPECTED]+[(k,'retained',str(int(k!='quick-retained'))) for k in g.EXPECTED]+[(k,'data_writes','1') for k in g.EXPECTED if k!='save']:
   with self.subTest(scenario=scenario,field=field):
    import re
    r=self.good();row=r['results'][g.EXPECTED.index(scenario)];row['stdout']=re.sub(r'\b'+field+r'=\d+',field+'='+value,row['stdout'])
    with self.assertRaises(ValueError):self.run_gate(r)
 def test_ambiguous_pass(self):
  r=self.good();r['results'][0]['stdout']+='\n'+r['results'][0]['stdout']
  with self.assertRaises(ValueError):self.run_gate(r)
if __name__=='__main__':unittest.main()
