import copy, hashlib, pathlib, sys, tempfile, unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from battery_evidence_gate import qualify

class Gate(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        source=pathlib.Path(self.tmp.name)/'source';source.write_text('retained source')
        self.r={'passed':True,'sanitize':False,'alarm_foreground_app':'battery',
                'selected_cases':[25,29],'case_count':2,'source_pins':{'runtime':'pin'},
                'store_files':{'battery.elf':'digest'},'source_sha256':{str(source):hashlib.sha256(source.read_bytes()).hexdigest()},
                'target_instruction_execution':False,'physical_tested':False,
                'results':[{'target_index':i,'stdout':f'APP_ENTER battery 3\nPASS actual Runtime alarm case={i} good','stderr':''} for i in [25,29]]}
        self.control=copy.deepcopy(self.r);self.control['alarm_foreground_app']='calculator'
    def verify(self):qualify(self.r,self.control,False)
    def test_positive(self):self.verify()
    def test_sanitized_positive(self):
        self.r['sanitize']=self.control['sanitize']=True
        qualify(self.r,self.control,True)
    def test_reject_nonempty_mixed_cohort(self):
        for key in ('source_pins','store_files'):
            with self.subTest(key=key):
                original=self.r[key];self.r[key]={'wrong':'digest'}
                with self.assertRaises(ValueError):self.verify()
                self.r[key]=original
    def test_reject_stderr(self):
        self.r['results'][0]['stderr']='runtime error'
        with self.assertRaises(ValueError):self.verify()
    def test_reject_bad_claims(self):
        for key,value in [('passed',False),('sanitize',True),('alarm_foreground_app','calculator'),('selected_cases',[25]),('case_count',1),('source_pins',{}),('store_files',{}),('source_sha256',{}),('physical_tested',True),('target_instruction_execution',True),('results',[])]:
            with self.subTest(key=key):
                old=self.r[key];self.r[key]=value
                with self.assertRaises(ValueError):self.verify()
                self.r[key]=old
    def test_reject_wrong_control(self):
        self.control['alarm_foreground_app']='battery'
        with self.assertRaises(ValueError):self.verify()
    def test_reject_wrong_actual_app(self):
        self.r['results'][0]['stdout']=self.r['results'][0]['stdout'].replace('battery','calculator')
        with self.assertRaises(ValueError):self.verify()
    def test_reject_duplicate(self):
        self.r['results'][1]=copy.deepcopy(self.r['results'][0])
        with self.assertRaises(ValueError):self.verify()
    def test_reject_changed_source(self):
        pathlib.Path(next(iter(self.r['source_sha256']))).write_text('changed')
        with self.assertRaises(ValueError):self.verify()

if __name__=='__main__':unittest.main()
