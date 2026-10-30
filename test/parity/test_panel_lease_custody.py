"""Panel source custody is neither upstream parity nor deployed qualification."""
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
PRODUCER='05896b376a610b1c8788005726bb8a4cb06cfbda'
EXPECTED={'Drivers/tps65185_power':('candidate','tps65185-power','aec2ad4b852ae2a252fccb91cf2926473205f647'), 'Drivers/t5s3_panel_profile':('adapter','t5s3-panel-profile','d7feaf80a513b1498914e8821ea5f8b37d7ce3e1'), 'Drivers/panel_lease_lifecycle':('adapter','panel-lease-lifecycle','c67198fbeaba460a84ca89aa9a7cb96201898ba7')}
class PanelLeaseCustody(unittest.TestCase):
    def entries(self):return [e for e in p.load_custody(ROOT)['sources'] if e['path'] in EXPECTED]
    def test_exact_producer_identity_and_unqualified_state(self):
        self.assertEqual(len(self.entries()),3)
        for e in self.entries():
            with self.subTest(path=e['path']):
                kind,identity,tree=EXPECTED[e['path']]
                self.assertEqual((e['kind'],e['id'],e['tree_sha']),(kind,identity,tree))
                self.assertEqual(e['provenance']['commit'],PRODUCER)
                if kind=='candidate':self.assertEqual(e['candidate_for'],identity)
                else:self.assertNotIn('candidate_for',e)
                rows,errors=p.validate_custody(ROOT,{'schema':1,'sources':[e]},set(),{'tps65185-power'})
                self.assertEqual(errors,[]);self.assertFalse(rows[0]['release_qualified']);self.assertFalse(rows[0]['upstream_parity'])
                self.assertEqual(json.loads((ROOT/e['path']/'manifest.json').read_text())['driver_abi'],2)
    def test_only_three_path_diagnostics_removed(self):
        f=Path(__file__).parent/'fixtures';source=json.loads((f/'source.json').read_text());released=p.unique(json.loads((f/'release.json').read_text())['drivers'],'released')
        names=['manifest/released-drivers.json','manifest/source-trees.json','manifest/local-source-custody.json'];frozen={n:(ROOT/n).read_bytes() for n in names}
        args=[ROOT,json.loads(frozen[names[0]]),json.loads(frozen[names[1]]),source['source'],released,source['pins']['master'],source['pins']['release-index']]
        custody=p.load_custody(ROOT);old=copy.deepcopy(custody);old['sources']=[e for e in old['sources'] if e['path'] not in EXPECTED]
        before=p.validate(*args,old);after=p.validate(*args,custody)
        labels=['untracked local source directory: '+path.split('/')[1] for path in EXPECTED]
        for label in labels:self.assertEqual(Counter(before['errors'])[label],1)
        self.assertEqual(Counter(after['errors']),Counter(before['errors'])-Counter(labels))
        self.assertEqual(before['drivers'],after['drivers']);self.assertEqual(frozen,{n:(ROOT/n).read_bytes() for n in names})
    def test_each_source_drift_and_false_origin_refused(self):
        for e in self.entries():
            with self.subTest(path=e['path']),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);subprocess.run(['git','clone','-q','--shared','--no-checkout',str(ROOT),str(root)],check=True)
                shutil.copytree(ROOT/e['path'],root/e['path']);target=root/e['path']/'driver.c';target.write_bytes(target.read_bytes()+b'\n/* drift */\n')
                rows,errors=p.validate_custody(root,{'schema':1,'sources':[e]},set(),{'tps65185-power'})
                self.assertEqual(errors,['local source custody tree mismatch: '+e['path']])
                bad=copy.deepcopy(e);bad['tree_sha']=p.local_tree(root/e['path'])
                with self.assertRaisesRegex(ValueError,'provenance tree mismatch'):p.validate_custody(root,{'schema':1,'sources':[bad]},set(),{'tps65185-power'})
    def test_candidate_lineage_and_adapter_collision_refuse(self):
        entries=self.entries();candidate=next(e for e in entries if e['kind']=='candidate');bad=copy.deepcopy(candidate);bad['candidate_for']='wrong'
        with self.assertRaisesRegex(ValueError,'candidate lineage'):p.validate_custody(ROOT,{'schema':1,'sources':[bad]},set(),{'tps65185-power'})
        for e in entries:
            if e['kind']=='adapter':
                with self.assertRaisesRegex(ValueError,'duplicate local adapter identity'):p.validate_custody(ROOT,{'schema':1,'sources':[e]},set(),{e['id']})
if __name__=='__main__':unittest.main()
