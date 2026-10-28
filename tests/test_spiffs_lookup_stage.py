#!/usr/bin/env python3
"""Selection-proof tests; no device or external checkout required."""
import importlib.util,json,tempfile,unittest,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('stage',Path(__file__).parents[1]/'scripts/stage_spiffs_lookup.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
sys.path.insert(0,str(Path(__file__).parents[1]/'scripts'))
import package_badge_prototype as package
class Proof(unittest.TestCase):
 def test_native_builder_optional_lookup(self):
  self.assertFalse(package.spiffs_lookup_selected({'spiffs_lookup':None}))
  self.assertFalse(package.spiffs_lookup_selected({}))
  self.assertTrue(package.spiffs_lookup_selected({'spiffs_lookup':{'enabled':True}}))
  for value in [False,[],{},'selected',{'enabled':1},{'enabled':False}]:
   with self.subTest(value=value),self.assertRaisesRegex(ValueError,'Malformed selected SPIFFS'):
    package.spiffs_lookup_selected({'spiffs_lookup':value})
 def test_compile_selection(self):
  with tempfile.TemporaryDirectory() as td:
   b=Path(td);root=b/'component';root.mkdir()
   names=['esp_spiffs.c','spiffs_api.c','spiffs/src/spiffs_cache.c','spiffs/src/spiffs_check.c','spiffs/src/spiffs_gc.c','spiffs/src/spiffs_hydrogen.c','spiffs/src/spiffs_nucleus.c']
   receipt={'component_path':str(root),'staged_sha256':{}}
   rows=[{'file':str(root/n),'arguments':['cc','-DSPIFFS_NAME_HINT_ENTRIES=64']} for n in names]
   def check(r):
    (b/'compile_commands.json').write_text(json.dumps(r));return m.verify_compiled(b,receipt)
   self.assertEqual(check(rows),sorted(names))
   for bad in ['-DSPIFFS_NAME_HINT_ENTRIES=0','-USPIFFS_NAME_HINT_ENTRIES','-DSPIFFS_NAME_HINT_ENTRIES=64']:
    altered=json.loads(json.dumps(rows));altered[0]['arguments'].append(bad)
    with self.assertRaises(ValueError):check(altered)
   with self.assertRaises(ValueError):check(rows[:-1])
   with self.assertRaises(ValueError):check(rows+[{'file':str(b/'elsewhere/esp_spiffs.c'),'arguments':['cc']}])
   receipt['staged_sha256']={'source.c':'wrong'};(root/'source.c').write_text('changed')
   with self.assertRaises(ValueError):check(rows)
if __name__=='__main__':unittest.main()
