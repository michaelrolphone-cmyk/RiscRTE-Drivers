import pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from private_storage_profile import private_bindings,apply_private_bindings,qualify_private_grants
class PrivateStorage(unittest.TestCase):
 def manifest(self):return {'requires':[{'capability':'storage.app-data','api':1}],'optional':[{'capability':'storage.key-value','api':1}]}
 def test_distinct_domains_and_diagnostic_replacement(self):
  rows=apply_private_bindings(self.manifest(),{'grant_bindings':{'storage.key-value':[1],'storage.app-data':[1]}},[{'capability':'storage.app-data','api':1,'instance_id':61},{'capability':'display.output','api':1,'instance_id':5}])
  self.assertEqual(rows,[{'capability':'display.output','api':1,'instance_id':5},{'capability':'storage.key-value','api':1,'instance_id':1},{'capability':'storage.app-data','api':1,'instance_id':1}])
 def test_installed_receipt_match(self):
  m=self.manifest();r={'grant_bindings':{'storage.key-value':[1],'storage.app-data':[1]}}
  good=apply_private_bindings(m,r,[]);self.assertEqual(qualify_private_grants(m,r,good),r['grant_bindings'])
  for bad in [good[:-1],good+[good[-1]],good[:1]+[{'capability':'storage.app-data','api':1,'instance_id':61}],good[:1]+[{'capability':'storage.app-data','api':2,'instance_id':1}]]:
   with self.assertRaises(ValueError):qualify_private_grants(m,r,bad)
 def test_valid_namespaces_generalized(self):
  for n in [1,2,3,6,61,65535,2147483647]:
   self.assertEqual(private_bindings(self.manifest(),{'grant_bindings':{'storage.app-data':[n]}}),{'storage.app-data':[n]})
 def test_bad_namespace_shapes(self):
  for values in [None,1,[],[0],[-1],[True],[1.0],['1'],[2147483648],[1,1]]:
   for cap in ['storage.app-data','storage.key-value']:
    with self.assertRaises(ValueError):private_bindings(self.manifest(),{'grant_bindings':{cap:values}})
 def test_multiple_kv_not_multiple_appdata(self):
  self.assertEqual(private_bindings(self.manifest(),{'grant_bindings':{'storage.key-value':[3,1]}}),{'storage.key-value':[3,1]})
  with self.assertRaises(ValueError):private_bindings(self.manifest(),{'grant_bindings':{'storage.app-data':[3,1]}})
 def test_declaration_fences(self):
  for manifest in [{},{'requires':[{'capability':'storage.app-data','api':2}]},{'requires':[{'capability':'storage.app-data','api':True}]},{'requires':[{'capability':'storage.app-data','api':1}]*2}]:
   with self.assertRaises(ValueError):private_bindings(manifest,{'grant_bindings':{'storage.app-data':[1]}})
 def test_unrelated_bindings_do_not_add_authority(self):
  row={'capability':'display.output','api':1,'instance_id':5}
  self.assertEqual(apply_private_bindings({}, {'grant_bindings':{'net.wifi':15}},[row]),[row])
if __name__=='__main__':unittest.main()
