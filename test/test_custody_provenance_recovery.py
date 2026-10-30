import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

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

if __name__=='__main__':unittest.main()
