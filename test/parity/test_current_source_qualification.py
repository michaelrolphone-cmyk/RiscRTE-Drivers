"""Synthetic contract fixtures: not claims of hosted execution or hardware tests."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import check_parity as p

class QualificationTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.src=self.root/'Drivers/example';self.src.mkdir(parents=True)
        self.old_manifest={'id':'example','version':'1.0.0','type':'driver'}
        self.write(self.src/'manifest.json',self.old_manifest);(self.src/'program.c').write_text('old')
        subprocess.run(['git','init','-q',str(self.root)],check=True)
        subprocess.run(['git','-C',str(self.root),'add','Drivers'],check=True)
        tree=subprocess.check_output(['git','-C',str(self.root),'write-tree'],text=True).strip()
        self.old_tree=subprocess.check_output(['git','-C',str(self.root),'rev-parse',tree+':Drivers/example'],text=True).strip()
        old_files=self.hashes()
        self.current_manifest=dict(self.old_manifest,version='2.0.0')
        self.write(self.src/'manifest.json',self.current_manifest);(self.src/'program.c').write_text('new behavior; qualification is not equivalence')
        (self.root/'build.py').write_text('build settings')
        self.inventory={'drivers':[{'id':'example','version':'1.0.0','source_path':'Drivers/example'}],'parity_source':{'repository':'owner/upstream','source_master_sha':'a'*40}}
        self.trees={'source_repository':'owner/upstream','source_master_sha':'a'*40,'drivers':{'example':self.old_tree}}
        self.upstream={'example':{'id':'example','version':'2.0.0','source_path':'Drivers/example','tree_sha':'b'*40}}
        self.released={'example':{'id':'example','version':'1.0.0','sha256':'c'*64,'size':123}}
        common={'compiler_sha256':'e'*64,'compiler_version':'synthetic fixture','libgcc':{'sha256':'f'*64},'readelf':{'sha256':'d'*64},'command':['synthetic-only']}
        current=dict(common,mode='current-source',expected_version='2.0.0',source_files=self.hashes(),build_inputs={'build.py':hashlib.sha256(b'build settings').hexdigest()},actual_sha256='d'*64,actual_size=42)
        history=dict(common,mode='historical',expected_version='1.0.0',source_tree=self.old_tree,source_files=old_files,actual_sha256='c'*64,actual_size=123,accepted_historical_sha256='c'*64,accepted_historical_size=123,byte_parity=True)
        self.evidence={'current_build':current,'historical_replay':history,'current_manifest':dict(self.current_manifest,sha256='d'*64,size_bytes=42),'historical_manifest':dict(self.old_manifest,sha256='c'*64,size_bytes=123)}
        evidence={}
        for name,value in self.evidence.items():
            path='qualification/'+name+'.json';self.write(self.root/path,value)
            evidence[name]={'path':path,'sha256':hashlib.sha256((self.root/path).read_bytes()).hexdigest()}
        self.item={'id':'example','path':'Drivers/example','version':'2.0.0','tree_sha':p.local_tree(self.src),'release_index_commit':'9'*40,'upstream':{'repository':'owner/upstream','commit':'8'*40,'path':'Drivers/example','version':'2.0.0','tree_sha':'b'*40},'historical':{'version':'1.0.0','source_tree':self.old_tree,'sha256':'c'*64,'size':123},'evidence':evidence,'attribution':'synthetic test author','rationale':'test explicit current/historical separation only','limits':['not actual execution evidence']}
        self.q={'schema':1,'sources':[self.item]}
    def write(self,path,value):path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value))
    def hashes(self):return {str(f.relative_to(self.src)):hashlib.sha256(f.read_bytes()).hexdigest() for f in self.src.rglob('*') if f.is_file()}
    def audit(self,q=None):return p.validate(self.root,self.inventory,self.trees,self.upstream,self.released,'8'*40,'9'*40,qualifications=self.q if q is None else q)
    def reject(self):
        r=self.audit();self.assertTrue(r['qualification_errors']);self.assertEqual(len(r['metadata_resolutions']),0)
        self.assertIn('source version mismatch: example',r['errors']);return r
    def rewrite(self,name,value):
        path=self.root/self.item['evidence'][name]['path'];self.write(path,value)
        self.item['evidence'][name]['sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    def test_generic_nonclock_program_c_resolves_only_two_metadata_diagnostics(self):
        r=self.audit();self.assertEqual(r['qualification_errors'],[]);self.assertEqual(len(r['metadata_resolutions']),2)
        self.assertIn('example: conflict source drift (preserve/reconcile local edits)',r['errors'])
        self.assertFalse(r['current_source_observation']['rows'][0]['bytes_match_upstream'])
        self.assertEqual(r['legacy_diagnostics'],p.validate(self.root,self.inventory,self.trees,self.upstream,self.released,'8'*40,'9'*40)['errors'])
    def test_changed_current_source(self):(self.src/'program.c').write_text('changed');self.reject()
    def test_changed_sdk_or_build_input(self):(self.root/'build.py').write_text('changed');self.reject()
    def test_changed_upstream_pin(self):self.upstream['example']['tree_sha']='a'*40;self.reject()
    def test_changed_release_pin(self):self.item['release_index_commit']='a'*40;self.reject()
    def test_missing_receipt(self):(self.root/self.item['evidence']['current_build']['path']).unlink();self.reject()
    def test_changed_receipt_bytes(self):(self.root/self.item['evidence']['current_build']['path']).write_text('{}');self.reject()
    def test_self_consistent_receipt_without_source_coverage(self):
        x=copy.deepcopy(self.evidence['current_build']);del x['source_files']['program.c'];self.rewrite('current_build',x);self.reject()
    def test_self_consistent_receipt_wrong_artifact(self):
        x=copy.deepcopy(self.evidence['current_build']);x['actual_size']=99;self.rewrite('current_build',x);self.reject()
    def test_malformed_evidence_shapes_fail_without_escaping_validate(self):
        for name in self.evidence:
            for malformed in ([],None,'not an object',42):
                with self.subTest(document=name,value=malformed):
                    self.rewrite(name,malformed);result=self.reject()
                    self.assertTrue(any('must be JSON objects' in e for e in result['qualification_errors']))
            self.rewrite(name,self.evidence[name])

    def test_historical_lock_not_rewritten(self):self.item['historical']['sha256']='d'*64;self.reject()
    def test_historical_receipt_false_parity(self):
        x=copy.deepcopy(self.evidence['historical_replay']);x['byte_parity']=False;self.rewrite('historical_replay',x);self.reject()
    def test_historical_source_incomplete(self):
        x=copy.deepcopy(self.evidence['historical_replay']);del x['source_files']['program.c'];self.rewrite('historical_replay',x);self.reject()
    def test_historical_tree_for_wrong_package_not_qualified(self):
        wrong=dict(self.old_manifest,id='different-package')
        self.write(self.src/'manifest.json',wrong);(self.src/'program.c').write_text('old')
        files=self.hashes()
        subprocess.run(['git','-C',str(self.root),'add','Drivers'],check=True)
        tree=subprocess.check_output(['git','-C',str(self.root),'write-tree'],text=True).strip()
        old_tree=subprocess.check_output(['git','-C',str(self.root),'rev-parse',tree+':Drivers/example'],text=True).strip()
        self.write(self.src/'manifest.json',self.current_manifest);(self.src/'program.c').write_text('new behavior; qualification is not equivalence')
        self.trees['drivers']['example']=old_tree;self.item['historical']['source_tree']=old_tree
        history=copy.deepcopy(self.evidence['historical_replay']);history.update(source_tree=old_tree,source_files=files)
        self.rewrite('historical_replay',history);self.rewrite('historical_manifest',dict(wrong,sha256='c'*64,size_bytes=123))
        result=self.reject();self.assertTrue(any('historical source identity/version' in e for e in result['qualification_errors']))

    def test_no_compiler_pin(self):
        x=copy.deepcopy(self.evidence['current_build']);del x['compiler_sha256'];self.rewrite('current_build',x);self.reject()
    def test_duplicate_rows(self):self.q['sources'].append(copy.deepcopy(self.item));self.reject()
    def test_unsafe_evidence_path(self):self.item['evidence']['current_build']['path']='../escape';self.reject()
    def test_symlink_evidence(self):
        path=self.root/self.item['evidence']['current_build']['path'];other=self.root/'outside.json';path.rename(other);path.symlink_to(other);self.reject()
    def test_missing_attribution(self):self.item['attribution']='';self.reject()
    def test_missing_whole_qualification_restores_original_diagnostics(self):
        r=self.audit({'schema':1,'sources':[]});self.assertIn('source version mismatch: example',r['errors']);self.assertEqual(r['metadata_resolutions'],[])
    def test_cli_automatically_consumes_linkage_and_missing_file_restores_errors(self):
        from unittest.mock import patch
        from contextlib import redirect_stdout,redirect_stderr
        import io
        self.inventory['parity_source'].update(branch='release-index',path='index.json')
        self.write(self.root/'manifest/released-drivers.json',self.inventory)
        self.write(self.root/'manifest/source-trees.json',self.trees)
        self.write(self.root/'qualification/current-sources.json',self.q)
        out=self.root/'report.json'
        with patch.object(p,'ROOT',self.root),patch.object(p,'snapshot',return_value=('8'*40,'9'*40,self.upstream,self.released)),patch.object(sys,'argv',['parity','--output',str(out)]),redirect_stdout(io.StringIO()),redirect_stderr(io.StringIO()):
            self.assertEqual(p.main(),1) # conflict remains
            result=json.loads(out.read_text());self.assertEqual(len(result['metadata_resolutions']),2)
            (self.root/'qualification/current-sources.json').unlink()
            self.assertEqual(p.main(),1)
            self.assertIn('source version mismatch: example',json.loads(out.read_text())['errors'])
            self.write(self.root/'qualification/current-sources.json',None)
            self.assertEqual(p.main(),1) # explicit null is invalid, not absent

    def test_other_package_diagnostic_not_resolved(self):
        self.trees['drivers']['other']='1'*40;r=self.audit();self.assertIn('source inventory mismatch: other',r['errors']);self.assertEqual(len(r['metadata_resolutions']),2)

    def annotations(self):
        for name, parity in [('current_manifest',False),('historical_manifest',True)]:
            self.rewrite(name,dict(self.evidence[name],canonical_sha256='c'*64,canonical_size_bytes=123,byte_parity=parity))
        self.rewrite('current_build',dict(self.evidence['current_build'],accepted_historical_sha256='c'*64,accepted_historical_size=123,byte_parity=False))
    def test_generic_builder_annotations_bound_not_ignored(self):
        self.annotations();r=self.audit();self.assertEqual(r['qualification_errors'],[]);self.assertEqual(len(r['metadata_resolutions']),2)
    def test_invalid_annotation_values_and_extra_semantics_rejected(self):
        self.annotations()
        for name in ('current_manifest','historical_manifest'):
            valid=json.loads((self.root/self.item['evidence'][name]['path']).read_text())
            for key,value in [('canonical_sha256','f'*64),('canonical_size_bytes',124),('canonical_size_bytes',True),('byte_parity',1),('byte_parity',not valid['byte_parity']),('requires',['new semantic field'])]:
                with self.subTest(name=name,key=key,value=value):self.rewrite(name,{**valid,key:value});self.reject()
            for key in ('canonical_sha256','canonical_size_bytes','byte_parity'):
                bad=copy.deepcopy(valid);del bad[key]
                with self.subTest(name=name,missing=key):self.rewrite(name,bad);self.reject()
            self.rewrite(name,valid)
    def producer(self,content=None):
        # An independent immutable synthetic producer, not current builder text.
        source=self.src/'manifest.json';old=source.read_bytes();source.write_text(json.dumps(self.old_manifest));program=self.src/'program.c';current=program.read_bytes();program.write_text('old')
        pth=self.root/'historical-builder.py';pth.write_text(content if content is not None else "OLD_HASH="+repr('c'*64)+"\nOLD_SIZE=123\n")
        subprocess.run(['git','-C',str(self.root),'add','Drivers','historical-builder.py'],check=True)
        subprocess.run(['git','-C',str(self.root),'-c','gc.auto=0','-c','maintenance.auto=false','-c','user.name=fixture','-c','user.email=fixture@example.invalid','commit','-qm','synthetic historical producer'],check=True)
        commit=subprocess.check_output(['git','-C',str(self.root),'rev-parse','HEAD'],text=True).strip();blob=subprocess.check_output(['git','-C',str(self.root),'rev-parse',commit+':historical-builder.py'],text=True).strip()
        source.write_bytes(old);program.write_bytes(current)
        self.item['historical_acceptance']={'format':'python-literal-producer-lock-v1','commit':commit,'path':'historical-builder.py','blob':blob,'sha256':hashlib.sha256(pth.read_bytes()).hexdigest(),'constants':{'sha256':'OLD_HASH','size':'OLD_SIZE'}}
        self.rewrite('historical_replay',dict(self.evidence['historical_replay'],historical_build_commit=commit))
        self.released['example']={'id':'example','version':'2.0.0','sha256':'f'*64,'size':999}
        return pth
    def test_original_producer_lock_separate_from_new_release_index(self):
        self.producer();self.annotations()
        # Keep the producer pin when decorating current evidence.
        r=self.audit();self.assertFalse(r['qualification_errors']);self.assertEqual(len(r['metadata_resolutions']),2)
        self.assertIn('released version mismatch: example',r['errors']);self.assertIn('example: conflict source drift (preserve/reconcile local edits)',r['errors'])
    def test_mutable_builder_cannot_replace_immutable_acceptance(self):
        path=self.producer();path.write_text("OLD_HASH="+repr('f'*64)+"\nOLD_SIZE=999\n")
        r=self.audit();self.assertFalse(r['qualification_errors'])
        self.item['historical']['sha256']='f'*64;self.reject()
    def test_producer_missing_changed_wrong_pins_rejected(self):
        self.producer();ref=copy.deepcopy(self.item['historical_acceptance'])
        for key,value in [('commit','0'*40),('path','missing.py'),('path','../escape'),('blob','0'*40),('sha256','0'*64),('format','unknown')]:
            with self.subTest(key=key):self.item['historical_acceptance']={**ref,key:value};self.reject()
        self.item['historical_acceptance']=ref
        self.rewrite('historical_replay',dict(self.evidence['historical_replay'],historical_build_commit='1'*40));self.reject()
    def test_producer_literal_contract_rejects_ambiguous_assignments(self):
        variants=["OLD_HASH="+repr('c'*64)+"\n", "OLD_HASH='c'*64\nOLD_SIZE=123\n", "OLD_HASH="+repr('c'*64)+"\nOLD_SIZE=123\nOLD_SIZE=123\n", "OLD_HASH="+repr('c'*64)+"\nOLD_SIZE: int=123\n", "OLD_HASH="+repr('c'*64)+"\nOLD_SIZE=123\nif False: OLD_SIZE=9\n", "OLD_HASH="+repr('c'*64)+"\nOLD_SIZE=True\n", "this is not Python !"]
        for content in variants:
            with self.subTest(content=content):
                self.producer(content);self.reject()

if __name__=='__main__':unittest.main()
