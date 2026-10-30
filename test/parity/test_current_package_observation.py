import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'scripts'))
import check_parity as p

class CurrentPackages(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.inv={'drivers':[]};self.source={}
    def put(self,path,identity='same',version='1.0.0',**fields):
        d=self.root/'Drivers'/path;d.mkdir(parents=True)
        (d/'manifest.json').write_text(json.dumps(dict(id=identity,version=version,**fields)))
        return d
    def observe(self,custody=None):
        return p.current_packages(self.root,self.inv,self.source,custody,[],[])
    def test_current_manifest_distinct_from_historical(self):
        self.put('sample',version='2.0.0',driver_abi=2,requires=[{'capability':'usb.host','api':1}])
        old={'id':'same','version':'1.0.0','source_path':'Drivers/sample'};self.inv['drivers']=[old]
        row=self.observe()['rows'][0]
        self.assertEqual(row['current_manifest']['version'],'2.0.0');self.assertEqual(row['current_manifest']['driver_abi'],2)
        self.assertEqual(old['version'],'1.0.0');self.assertEqual(len(row['manifest_sha256']),64)
    def test_prerelease_identity_is_observed_without_release_validation(self):
        self.put('sample',version='0.1.6-checked-cleanup.1')
        row=self.observe()['rows'][0]
        self.assertEqual(row['availability'],'observed')
        self.assertEqual(row['current_manifest']['version'],'0.1.6-checked-cleanup.1')
    def test_same_identity_variants_are_not_collapsed_or_equated(self):
        self.put('one');self.put('two',driver_abi=2)
        rows=self.observe()['rows'];self.assertEqual(len(rows),2)
        for row in rows:self.assertEqual(row['same_identity_paths'],['Drivers/one','Drivers/two']);self.assertFalse(row['errors'])
    def test_missing_upstream_and_original_and_custody_memberships(self):
        self.source={'absent':{'source_path':'Drivers/absent'}}
        self.inv['original_drivers']=[{'id':'original','source_path':'Drivers/original'}]
        self.put('candidate')
        rows={r['path']:r for r in self.observe({'sources':[{'path':'Drivers/candidate','kind':'candidate','id':'same'}]})['rows']}
        self.assertEqual(rows['Drivers/absent']['availability'],'missing_local')
        self.assertIsNone(rows['Drivers/absent']['current_manifest'])
        self.assertEqual(rows['Drivers/original']['availability'],'missing_local')
        self.assertIn({'collection':'local_custody_declarations','id':'same'},rows['Drivers/candidate']['memberships'])
    def test_duplicate_manifest_keys_rejected_not_last_write_wins(self):
        d=self.put('sample');(d/'manifest.json').write_text('{"id":"one","id":"two","version":"1.0.0"}')
        row=self.observe()['rows'][0];self.assertEqual(row['availability'],'unresolved');self.assertIn('duplicate manifest key',row['errors'][0])
    def test_malformed_identity_and_manifest_shape(self):
        for value in [[],{'id':[],'version':'1.0.0'},{'id':'x','version':1}]:
            d=self.root/'Drivers/sample';d.mkdir(parents=True,exist_ok=True);(d/'manifest.json').write_text(json.dumps(value))
            self.assertEqual(self.observe()['rows'][0]['availability'],'unresolved')
    def test_duplicate_declaration_and_identity_mismatch_explicit(self):
        self.put('sample');entry={'id':'different','source_path':'Drivers/sample'};self.inv['drivers']=[entry,entry]
        report=self.observe();self.assertTrue(report['membership_errors']);self.assertIn('differs',report['rows'][0]['errors'][0])
    def test_symlink_and_invalid_paths_do_not_read_outside(self):
        (self.root/'Drivers').mkdir();(self.root/'Drivers/link').symlink_to(self.root,target_is_directory=True)
        self.inv['drivers']=[{'id':'link','source_path':'Drivers/link'},{'id':'escape','source_path':'Drivers/../secret'}]
        report=self.observe();self.assertEqual(report['rows'][0]['availability'],'unresolved');self.assertTrue(report['membership_errors'])

if __name__=='__main__':unittest.main()
