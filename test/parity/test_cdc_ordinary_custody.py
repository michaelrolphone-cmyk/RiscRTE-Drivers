"""Actual path custody is not a selected or release-qualified CDC replacement."""
import copy
from collections import Counter
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import check_parity as p
PATH='Drivers/usb_cdc_stream'

class OrdinaryCustody(unittest.TestCase):
    def entry(self):return next(e for e in p.load_custody(ROOT)['sources'] if e['path']==PATH)
    def test_actual_producer_and_shared_canonical_identity_remain_path_distinct(self):
        entry=self.entry()
        self.assertEqual(entry['kind'],'candidate');self.assertEqual(entry['candidate_for'],'usb-cdc-acm')
        self.assertEqual(entry['provenance']['commit'],'dbf925cf85f62cb0027dabe9f2b4f2359ff01160')
        self.assertEqual(entry['tree_sha'],'e3f0169dc0717566faecb82c147c811bf55df6a6')
        rows,errors=p.validate_custody(ROOT,{'schema':1,'sources':[entry]},set(),{'usb-cdc-acm'})
        self.assertEqual(errors,[]);self.assertFalse(rows[0]['release_qualified']);self.assertFalse(rows[0]['upstream_parity'])
        manifest=json.loads((ROOT/PATH/'manifest.json').read_text());self.assertEqual(manifest['version'],'0.1.8')
        self.assertEqual(json.loads((ROOT/'Drivers/usb_cdc_native/manifest.json').read_text())['version'],'0.1.9')
        self.assertEqual(json.loads((ROOT/'Drivers/usb_cdc_v2/manifest.json').read_text())['id'],'usb-cdc-acm-v2')
    def test_only_new_path_custody_diagnostic_is_removed(self):
        fixture=Path(__file__).parent/'fixtures';source=json.loads((fixture/'source.json').read_text())
        released=p.unique(json.loads((fixture/'release.json').read_text())['drivers'],'released')
        names=['manifest/released-drivers.json','manifest/source-trees.json','manifest/local-source-custody.json']
        frozen={n:(ROOT/n).read_bytes() for n in names}
        args=[ROOT,json.loads(frozen[names[0]]),json.loads(frozen[names[1]]),source['source'],released,source['pins']['master'],source['pins']['release-index']]
        custody=p.load_custody(ROOT);old=copy.deepcopy(custody);old['sources']=[e for e in old['sources'] if e['path']!=PATH]
        before=p.validate(*args,old);after=p.validate(*args,custody)
        label='untracked local source directory: usb_cdc_stream'
        self.assertEqual(Counter(before['errors'])[label],1)
        self.assertEqual(Counter(after['errors']),Counter(before['errors'])-Counter([label]))
        self.assertEqual(before['drivers'],after['drivers']);self.assertEqual(frozen,{n:(ROOT/n).read_bytes() for n in names})
    def test_source_drift_and_false_lineage_refuse(self):
        entry=self.entry();bad=copy.deepcopy(entry);bad['candidate_for']='usb-cdc-acm-v2'
        with self.assertRaisesRegex(ValueError,'candidate lineage'):p.validate_custody(ROOT,{'schema':1,'sources':[bad]},set(),{'usb-cdc-acm','usb-cdc-acm-v2'})
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);subprocess.run(['git','clone','-q','--shared','--no-checkout',str(ROOT),str(root)],check=True)
            shutil.copytree(ROOT/PATH,root/PATH)
            (root/PATH/'driver.c').write_bytes((root/PATH/'driver.c').read_bytes()+b'\n/* drift */\n')
            rows,errors=p.validate_custody(root,{'schema':1,'sources':[entry]},set(),{'usb-cdc-acm'})
            self.assertEqual(errors,['local source custody tree mismatch: '+PATH])
            bad=copy.deepcopy(entry);bad['tree_sha']=p.local_tree(root/PATH)
            with self.assertRaisesRegex(ValueError,'provenance tree mismatch'):p.validate_custody(root,{'schema':1,'sources':[bad]},set(),{'usb-cdc-acm'})

if __name__=='__main__':unittest.main()
