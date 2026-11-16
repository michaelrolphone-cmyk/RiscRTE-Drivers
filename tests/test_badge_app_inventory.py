import importlib.util,json,tempfile,unittest
from pathlib import Path
P=Path(__file__).resolve().parents[1]/'scripts/badge_app_inventory.py';spec=importlib.util.spec_from_file_location('inventory',P);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Inventory(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name)
  for d in ['system/Apps/native','utilities/Apps','controllers/scripts','store']: (self.root/d).mkdir(parents=True)
  self.write('controllers/scripts/current_apps_overlay.py',"SYSTEM_APPS=('files',)\nUTILITY_APPS=('meter',)\nPRODUCTIVITY_APPS=('writer',)\nCLOCK_APPS=('default','clock')\n")
  self.json('store/boot.json',{'app_capabilities':[{'manifest':'default.json','grants':[{'capability':'display.output','api':1}]}],'drivers':[]})
  self.json('store/default.json',{'id':'shared-home','file_name':'default.elf','requires':[{'capability':'display.output','api':1}]})
  self.json('catalog.json',{'apps':[{'file_name':'default.elf'}]})
 def tearDown(self):self.tmp.cleanup()
 def write(self,p,t):(self.root/p).write_text(t)
 def json(self,p,j):self.write(p,json.dumps(j))
 def collect(self):return m.collect(*(self.root/d for d in ['system','utilities','controllers','store','catalog.json']))
 def test_roles_use_file_name_not_provider_identity(self):
  r=self.collect();self.assertEqual(r['installed_app_ids'],['default']);self.assertEqual(len(r['intended_watch_roles']),6);self.assertIn('writer',r['missing_intended_roles'])
 def test_capability_closure_not_compatibility_claim(self):
  self.json('system/Apps/native/files.json',{'id':'files','file_name':'files.elf','requires':[{'capability':'storage.installed-files','api':1}]})
  r=next(x for x in self.collect()['apps'] if x['id']=='files');self.assertFalse(r['installed']);self.assertEqual(r['profiles'][0]['missing_capabilities'],[]);self.assertIn('qualification required',r['profiles'][0]['assessment'])
 def test_missing_version_and_legacy_distinct(self):
  self.json('utilities/Apps/meter.json',{'file_name':'meter.elf','requires':[{'capability':'display.output','api':'>=2'}]})
  self.json('system/Apps/legacy.json',{'file_name':'legacy.elf'})
  rows={x['id']:x for x in self.collect()['apps']};self.assertEqual(len(rows['meter']['profiles'][0]['missing_capabilities']),1);self.assertIn('legacy/API',rows['legacy']['profiles'][0]['assessment'])
 def test_unpackaged_catalog_rejected(self):
  self.json('catalog.json',{'apps':[{'file_name':'missing.elf'}]})
  with self.assertRaisesRegex(ValueError,'unpackaged'):self.collect()
 def test_new_source_profile_automatically_included(self):
  self.json('utilities/Apps/new-tool.json',{'file_name':'new-tool.elf','requires':[]});self.assertIn('new-tool',[r['id'] for r in self.collect()['apps']])
if __name__=='__main__':unittest.main()
