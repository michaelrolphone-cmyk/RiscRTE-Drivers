import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import shutil
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import check_parity as parity

SPEC = importlib.util.spec_from_file_location('custody_recovery', Path(__file__).resolve().parents[1]/'scripts/recover_custody_provenance.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
REPO = 'michaelrolphone-cmyk/RiscRTE-Drivers'

class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.origin, self.target = root/'origin', root/'target'
        for p in [self.origin,self.target]:
            subprocess.run(['git','init','-q',str(p)],check=True)
            self.git(p,'config','user.email','fixture@example.invalid')
            self.git(p,'config','user.name','Fixture')
        p=self.origin/'Drivers/example';p.mkdir(parents=True);(p/'source.c').write_text('original\n')
        self.git(self.origin,'add','.');self.git(self.origin,'commit','-qm','provenance')
        self.commit=self.git(self.origin,'rev-parse','HEAD');self.tree=self.git(self.origin,'rev-parse','HEAD:Drivers/example')
        (self.target/'sentinel').write_text('selected working tree\n');self.git(self.target,'add','.');self.git(self.target,'commit','-qm','selected')
        self.head=self.git(self.target,'rev-parse','HEAD');self.git(self.target,'remote','add','origin','https://github.com/'+REPO+'.git')
        (self.target/'manifest').mkdir()
        self.entry={'path':'Drivers/example','tree_sha':self.tree,'provenance':{'repository':REPO,'commit':self.commit,'path':'Drivers/example'}}
        self.write([self.entry]);self.calls=[]
    def git(self,p,*a):return subprocess.check_output(['git','-C',str(p),*a],text=True).strip()
    def write(self,entries): (self.target/'manifest/local-source-custody.json').write_text(json.dumps({'schema':1,'sources':entries}))
    def run_git(self,args,**kw):
        self.calls.append(args)
        if 'fetch' in args:
            args=args.copy();args[args.index('origin')]=str(self.origin)
        return subprocess.run(args,**kw)
    def recover(self):return MODULE.recover(self.target,REPO,run=self.run_git)
    def test_missing_hydrated_then_offline_reuse_preserves_selected_head(self):
        result=self.recover();self.assertEqual(result['fetched_commits'],[self.commit]);self.assertEqual(self.git(self.target,'rev-parse','HEAD'),self.head)
        self.assertEqual((self.target/'sentinel').read_text(),'selected working tree\n')
        self.assertEqual(self.git(self.target,'rev-parse',self.commit+':Drivers/example'),self.tree)
        self.calls=[];self.assertEqual(self.recover()['fetched_commits'],[]);self.assertFalse(any('fetch' in a for a in self.calls))
    def test_tree_mismatch_rejected(self):
        self.entry['tree_sha']='0'*40;self.write([self.entry])
        with self.assertRaisesRegex(ValueError,'tree mismatch'):self.recover()
    def test_missing_unavailable_commit_fails_no_fallback(self):
        self.entry['provenance']['commit']='1'*40;self.write([self.entry])
        with self.assertRaises(subprocess.CalledProcessError):self.recover()
        self.assertEqual(len([a for a in self.calls if 'fetch' in a]),1)
        self.assertEqual(self.git(self.target,'rev-parse','HEAD'),self.head)
    def test_foreign_malformed_duplicate_rejected_before_fetch(self):
        for entries in [[{**self.entry,'provenance':None}], [self.entry,self.entry], [{**self.entry,'provenance':{**self.entry['provenance'],'repository':'other/repo'}}], [{**self.entry,'path':'../outside'}]]:
            self.write(entries);self.calls=[]
            with self.assertRaises(ValueError):self.recover()
            self.assertFalse(any('fetch' in a for a in self.calls))
    def test_wrong_origin_rejected(self):
        self.git(self.target,'remote','set-url','origin','https://github.com/other/repo.git')
        with self.assertRaisesRegex(ValueError,'Origin'):self.recover()
    def test_blob_is_not_provenance_tree(self):
        blob=self.git(self.origin,'rev-parse',self.commit+':Drivers/example/source.c')
        self.entry['tree_sha']=blob;self.write([self.entry])
        with self.assertRaisesRegex(ValueError,'tree mismatch'):self.recover()

    def later(self):
        path = self.origin/'Drivers/later_provider'
        path.mkdir(parents=True)
        (path/'source.c').write_text('later source\n')
        self.git(self.origin,'add','.')
        self.git(self.origin,'commit','-qm','later provenance')
        commit = self.git(self.origin,'rev-parse','HEAD')
        tree = self.git(self.origin,'rev-parse','HEAD:Drivers/later_provider')
        entry = {'path':'Drivers/later_provider', 'kind':'support', 'tree_sha':tree,
                 'provenance':{'repository':REPO,'commit':commit,'path':'Drivers/later_provider'}}
        (self.target/'qualification').mkdir(exist_ok=True)
        (self.target/'qualification/local-source-custody.json').write_text(json.dumps({'schema':1,'sources':[entry]}))
        return entry

    def test_later_declaration_real_git_hydration_and_offline_custody(self):
        later = self.later()
        frozen = (self.target/'manifest/local-source-custody.json').read_bytes()
        # The ordinary validator remains strictly offline and refuses missing
        # provenance before the explicit, exact-commit hydrator has run.
        shutil.copytree(self.origin/later['path'], self.target/later['path'])
        with self.assertRaises(subprocess.CalledProcessError):
            parity.validate_custody(self.target, {'schema':1,'sources':[later]}, set(), set())
        receipt = self.recover()
        self.assertEqual({r['commit'] for r in receipt['provenance']}, {self.commit,later['provenance']['commit']})
        fetched = [a for a in self.calls if 'fetch' in a]
        self.assertEqual({a[-1] for a in fetched}, {self.commit,later['provenance']['commit']})
        self.assertTrue(all(a[3:7] == ['fetch','--no-tags','--depth=1','origin'] for a in fetched))
        self.assertEqual(parity.validate_custody(self.target, {'schema':1,'sources':[later]}, set(), set())[1], [])
        self.calls=[]
        self.assertEqual(self.recover()['fetched_commits'], [])
        self.assertFalse(any('fetch' in a for a in self.calls))
        self.assertEqual(self.git(self.target,'rev-parse','HEAD'), self.head)
        self.assertEqual((self.target/'sentinel').read_text(),'selected working tree\n')
        self.assertEqual((self.target/'manifest/local-source-custody.json').read_bytes(), frozen)

    def test_cross_manifest_conflicts_rejected_before_fetch(self):
        self.later()
        path=self.target/'qualification/local-source-custody.json'
        for entry in [self.entry, {**self.entry,'tree_sha':'0'*40}]:
            path.write_text(json.dumps({'schema':1,'sources':[entry]}))
            self.calls=[]
            with self.assertRaisesRegex(ValueError,'provenance'): self.recover()
            self.assertFalse(any('fetch' in a for a in self.calls))

    def test_unavailable_later_commit_has_no_fallback(self):
        later=self.later()
        later['provenance']['commit']='1'*40
        (self.target/'qualification/local-source-custody.json').write_text(json.dumps({'schema':1,'sources':[later]}))
        with self.assertRaises(subprocess.CalledProcessError): self.recover()
        self.assertTrue(all(a[-1] in [self.commit,'1'*40] for a in self.calls if 'fetch' in a))
        self.assertEqual(self.git(self.target,'rev-parse','HEAD'),self.head)

    def test_malformed_or_symlinked_later_declaration_refuses_fetch(self):
        self.later()
        path=self.target/'qualification/local-source-custody.json'
        for document in [None, [], {'schema':2,'sources':[]}, {'schema':1,'sources':{}}]:
            path.write_text(json.dumps(document));self.calls=[]
            with self.assertRaises(ValueError): self.recover()
            self.assertFalse(any('fetch' in a for a in self.calls))
        path.unlink();path.symlink_to(self.target/'manifest/local-source-custody.json');self.calls=[]
        with self.assertRaisesRegex(ValueError,'symlink'): self.recover()
        self.assertFalse(any('fetch' in a for a in self.calls))

if __name__=='__main__':unittest.main()
