import copy,json,sys,tempfile,unittest,importlib.util
from pathlib import Path
from unittest.mock import Mock,patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import check_parity as p
class Support(unittest.TestCase):
 def setUp(self):
  self.t=tempfile.TemporaryDirectory();self.addCleanup(self.t.cleanup);self.root=Path(self.t.name);d=self.root/'Drivers/helpers';d.mkdir(parents=True);(d/'x.inc').write_bytes(b'body')
  self.pin={'mode':'100644','sha':p.object_hash('blob',b'body')};self.source=Mock();self.source.support_tree.return_value=('a'*40,{'x.inc':self.pin,'omitted.h':{'mode':'100644','sha':'b'*40}})
  self.doc={'schema':1,'sources':[{'path':'Drivers/helpers','tree_sha':p.local_tree(d),'upstream_tree':'a'*40,'files':[{'path':'x.inc',**self.pin}],'provenance':{'repository':'owner/source','commit':'c'*40,'path':'Drivers/helpers'}}]}
 def run_doc(self,doc=None,occupied=()):return p.validate_imported_support(self.root,self.doc if doc is None else doc,occupied,'owner/source',self.source,p.local_tree,p.object_hash)
 def test_subset(self):
  rows,errors=self.run_doc();self.assertFalse(errors);self.assertEqual(rows[0]['omitted_upstream_files'],['omitted.h']);self.assertFalse(rows[0]['whole_directory_equal']);self.assertFalse(rows[0]['release_qualified'])
 def test_changed_missing_extra_modes(self):
  d=self.root/'Drivers/helpers';f=d/'x.inc'
  for operation in ('changed','missing','extra','mode'):
   f.write_bytes(b'body');f.chmod(0o644)
   if operation=='changed':f.write_bytes(b'changed')
   if operation=='missing':f.unlink()
   if operation=='extra':(d/'other').write_bytes(b'')
   if operation=='mode':f.chmod(0o755)
   rows,errors=self.run_doc();self.assertFalse(rows);self.assertTrue(errors)
   (d/'other').unlink(missing_ok=True)
 def test_wrong_origin_or_tree(self):
  for key in ('tree_sha','upstream_tree'):
   doc=copy.deepcopy(self.doc);doc['sources'][0][key]='d'*40;self.assertTrue(self.run_doc(doc)[1])
  doc=copy.deepcopy(self.doc);doc['sources'][0]['provenance']['repository']='other/source';self.assertTrue(self.run_doc(doc)[1])
 def test_package_and_occupied(self):
  self.assertTrue(self.run_doc(occupied=['Drivers/helpers'])[1]);self.source.support_tree.return_value[1]['manifest.json']=self.pin;self.assertTrue(self.run_doc()[1])
 def test_malformed(self):
  for value in ([],None,'bad',2):
   doc=copy.deepcopy(self.doc);doc['sources'][0]['files']=value;self.assertTrue(self.run_doc(doc)[1])
  for name in ('../escape','/root','x//y','x/./y','x/../y',''):
   doc=copy.deepcopy(self.doc);doc['sources'][0]['files'][0]['path']=name;self.assertTrue(self.run_doc(doc)[1])
 def test_duplicates(self):
  doc=copy.deepcopy(self.doc);doc['sources']*=2;rows,errors=self.run_doc(doc);self.assertEqual(len(rows),1);self.assertTrue(errors)
 def test_symlink(self):
  f=self.root/'Drivers/helpers/x.inc';f.unlink();f.symlink_to('/dev/null');self.assertTrue(self.run_doc()[1])
 def test_full_validator_keeps_untracked_on_rejection(self):
  inventory={'parity_source':{'repository':'owner/source','source_master_sha':'c'*40},'drivers':[]}
  trees={'source_repository':'owner/source','source_master_sha':'c'*40,'drivers':{}}
  def audit(doc):return p.validate(self.root,inventory,trees,{}, {},'c'*40,'d'*40,imported_support=doc,support_source=self.source)
  before=audit(None);self.assertIn('untracked local source directory: helpers',before['errors'])
  accepted=audit(self.doc);self.assertFalse(accepted['errors']);self.assertEqual(accepted['drivers'],before['drivers'])
  self.assertEqual(accepted['current_source_observation']['imported_support'],accepted['imported_support'])
  (self.root/'Drivers/helpers/x.inc').write_bytes(b'changed')
  rejected=audit(self.doc);self.assertIn('untracked local source directory: helpers',rejected['errors']);self.assertTrue(any('local subset mismatch' in e for e in rejected['errors']))
  self.assertTrue(p.verify_observation(accepted['current_source_observation'],rejected['current_source_observation']))
 def test_api_reader_exact_commit_path_and_tree(self):
  source=p.Source('owner/source');commit='c'*40
  def api(path,ref=None):
   if path=='commits/'+commit:return {'sha':commit}
   if path=='contents/Drivers/helpers':
    self.assertEqual(ref,commit);return []
   if path=='contents/Drivers':
    self.assertEqual(ref,commit);return [{'path':'Drivers/helpers','type':'dir','sha':'a'*40}]
   if path=='git/trees/'+'a'*40+'?recursive=1':return {'truncated':False,'tree':[{'path':'x.inc','mode':'100644','type':'blob','sha':self.pin['sha']}]}
   raise AssertionError((path,ref))
  with patch.object(source,'api',side_effect=api):self.assertEqual(source.support_tree(commit,'Drivers/helpers'),('a'*40,{'x.inc':self.pin}))
 def test_api_reader_rejects_truncation_nonregular_and_drift(self):
  for change in ('truncated','symlink','commit'):
   source=p.Source('owner/source');commit='c'*40
   responses=[{'sha':'d'*40 if change=='commit' else commit},[],[{'path':'Drivers/helpers','type':'dir','sha':'a'*40}],{'truncated':change=='truncated','tree':[{'path':'x.inc','mode':'120000' if change=='symlink' else '100644','type':'blob','sha':self.pin['sha']}]}]
   with patch.object(source,'api',side_effect=responses):
    with self.assertRaises(ValueError):source.support_tree(commit,'Drivers/helpers')
 def test_git_reader_preserves_nested_modes(self):
  source=p.Source('owner/source',self.root)
  with patch.object(source,'git',side_effect=['c'*40,'a'*40,'100755 blob '+'b'*40+'\tnested/tool\n']):
   self.assertEqual(source.support_tree('c'*40,'Drivers/helpers'),('a'*40,{'nested/tool':{'mode':'100755','sha':'b'*40}}))
 def test_historical_standalone_loader_needs_no_optional_module(self):
  script=self.root/'check_parity.py';script.write_bytes(Path(p.__file__).read_bytes())
  spec=importlib.util.spec_from_file_location('historical_standalone',script);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  self.assertEqual(module.validate_imported_support(self.root,None,(),'owner/source',None,module.local_tree,module.object_hash),([],[]))
  with self.assertRaises(FileNotFoundError):module.validate_imported_support(self.root,self.doc,(),'owner/source',self.source,module.local_tree,module.object_hash)
 def test_absent(self):self.assertEqual(p.validate_imported_support(self.root,None,(),'owner/source',None,p.local_tree,p.object_hash),([],[]))
if __name__=='__main__':unittest.main()
