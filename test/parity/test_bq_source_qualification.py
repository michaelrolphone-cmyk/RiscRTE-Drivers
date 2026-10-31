"""Actual BQ evidence bindings reuse the shared source-qualification consumer."""
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import check_parity as parity

class BQQualificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for directory in ('Drivers', 'manifest', 'qualification', 'scripts', 'sdk'):
            shutil.copytree(ROOT/directory, self.root/directory, ignore=shutil.ignore_patterns('__pycache__'))
        # Read-only access to immutable objects; tests never change this gitdir.
        import subprocess
        gitdir = subprocess.check_output(['git','-C',str(ROOT),'rev-parse','--absolute-git-dir'],text=True).strip()
        (self.root/'.git').write_text('gitdir: '+gitdir+'\n')
        self.q = json.loads((self.root/'qualification/current-sources.json').read_text())
        # Focus on immutable producer-lock users; the frozen minimal release
        # fixture intentionally omits the clock artifact's accepted hash/size.
        self.q['sources'] = [e for e in self.q['sources'] if e['id'] in ('usb-ch34x-v2','board-power-t5s3-v2')]
        self.entry = next(e for e in self.q['sources'] if e['id']=='board-power-t5s3-v2')
        fixtures = Path(__file__).parent/'fixtures'
        self.source = json.loads((fixtures/'source.json').read_text())
        self.released = parity.unique(json.loads((fixtures/'release.json').read_text())['drivers'],'released')

    def audit(self, qualification=None):
        read = lambda p: json.loads((self.root/p).read_text())
        return parity.validate(self.root, read('manifest/released-drivers.json'), read('manifest/source-trees.json'),
            self.source['source'],self.released,self.source['pins']['master'],self.source['pins']['release-index'],
            parity.load_custody(self.root), self.q if qualification is None else qualification)

    def change_evidence(self, name, edit):
        reference = self.entry['evidence'][name]
        path = self.root/reference['path']
        data = json.loads(path.read_text());edit(data)
        raw = (json.dumps(data,indent=2)+'\n').encode();path.write_bytes(raw)
        reference['sha256'] = hashlib.sha256(raw).hexdigest()

    def assert_rejected(self, shared_sdk_changed=False):
        result = self.audit()
        self.assertTrue(any('board-power-t5s3-v2' in e for e in result['qualification_errors']))
        self.assertIn('source version mismatch: board-power-t5s3-v2', result['errors'])
        self.assertIn('board-power-t5s3-v2: recorded source tree needs refresh', result['errors'])
        self.assertFalse(any(r['id']=='board-power-t5s3-v2' for r in result['metadata_resolutions']))
        if shared_sdk_changed:
            self.assertTrue(any('usb-ch34x-v2' in e for e in result['qualification_errors']))
        else:
            self.assertTrue(any(r['id']=='usb-ch34x-v2' for r in result['metadata_resolutions']))

    def test_exact_delta_and_frozen_inputs(self):
        paths=['manifest/released-drivers.json','manifest/source-trees.json','manifest/local-source-custody.json']
        before_bytes={p:(self.root/p).read_bytes() for p in paths}
        old=copy.deepcopy(self.q);old['sources']=[e for e in old['sources'] if e['id']!='board-power-t5s3-v2']
        before=self.audit(old);after=self.audit()
        self.assertEqual(after['qualification_errors'],[])
        removed=['source version mismatch: board-power-t5s3-v2','board-power-t5s3-v2: recorded source tree needs refresh']
        self.assertEqual(Counter(after['errors']),Counter(before['errors'])-Counter(removed))
        self.assertEqual(before['drivers'],after['drivers'])
        self.assertIn('board-power-t5s3-v2: conflict source drift (preserve/reconcile local edits)',after['errors'])
        self.assertEqual(before_bytes,{p:(self.root/p).read_bytes() for p in paths})

    def test_changed_source_and_sdk_are_rejected(self):
        for path in ('Drivers/bq25896/driver.c','sdk/driver/RiscUsbVbusV1.h'):
            file=self.root/path;old=file.read_bytes()
            with self.subTest(path=path):
                file.write_bytes(old+b'\n/* changed */\n');self.assert_rejected(shared_sdk_changed=path.startswith('sdk/'));file.write_bytes(old)

    def test_changed_canonical_annotation_with_refreshed_evidence_hash_rejected(self):
        self.change_evidence('current_manifest',lambda d:d.update(canonical_sha256='0'*64))
        self.assert_rejected()

    def test_changed_historical_bytes_with_refreshed_evidence_hash_rejected(self):
        self.change_evidence('historical_replay',lambda d:d.update(actual_sha256='0'*64))
        self.assert_rejected()

    def test_missing_or_wrong_original_producer_is_rejected(self):
        original=copy.deepcopy(self.entry['historical_acceptance'])
        for key,value in [('commit','1'*40),('blob','0'*40),('sha256','0'*64)]:
            self.entry['historical_acceptance']=copy.deepcopy(original)
            self.entry['historical_acceptance'][key]=value
            with self.subTest(key=key):self.assert_rejected()

    def test_incomplete_historical_source_coverage_rejected(self):
        self.change_evidence('historical_replay',lambda d:d['source_files'].pop('driver.c'))
        self.assert_rejected()

    def test_malformed_or_missing_evidence_rejected(self):
        for name in ('current_build','current_manifest','historical_replay','historical_manifest'):
            ref=self.entry['evidence'][name];p=self.root/ref['path'];original=p.read_bytes();old_hash=ref['sha256']
            for value in ([],None,'invalid',42):
                with self.subTest(name=name,value=value):
                    raw=json.dumps(value).encode();p.write_bytes(raw);ref['sha256']=hashlib.sha256(raw).hexdigest();self.assert_rejected()
            p.unlink();ref['sha256']=old_hash;self.assert_rejected();p.write_bytes(original)

    def test_stale_upstream_or_local_binding_rejected(self):
        old=copy.deepcopy(self.entry)
        for path in ('tree_sha','upstream'):
            self.entry.clear();self.entry.update(copy.deepcopy(old))
            if path=='upstream':self.entry[path]['commit']='0'*40
            else:self.entry[path]='0'*40
            with self.subTest(path=path):self.assert_rejected()

    def test_wrong_current_source_coverage_and_overlay_rejected(self):
        self.change_evidence('current_build',lambda d:d['source_files'].pop('RiscUsbVbusV1.h'))
        self.assert_rejected()

if __name__=='__main__':unittest.main()
