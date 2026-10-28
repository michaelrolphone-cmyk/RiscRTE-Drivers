import pathlib,subprocess,sys,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class Selection(unittest.TestCase):
 def run_bad(self,extra,message):
  args=[sys.executable,str(ROOT/'scripts/build_badge_prototype.py')]
  for n in ['runtime','runtime-baseline','watch','watch-bringup','controllers','system','system-baseline','utilities','utilities-baseline','output']:args+=['--'+n,'/does-not-exist']
  r=subprocess.run(args+['--cc','not-a-compiler']+extra,capture_output=True,text=True)
  self.assertEqual(r.returncode,2);self.assertIn(message,r.stderr);self.assertNotIn('Traceback',r.stderr)
 def test_requires_exact_productivity_source(self):self.run_bad(['--include-timecard','--native-alarms'],'include-timecard requires native-alarms and exact Productivity source')
 def test_requires_actual_sleep_alarm_dependency(self):self.run_bad(['--include-timecard','--productivity','/does-not-exist'],'include-timecard requires native-alarms and exact Productivity source')
 def test_no_implicit_selection_from_source_path(self):self.run_bad(['--productivity','/does-not-exist'],'productivity requires explicit include-timecard selection')
 def test_preserves_existing_dependency_closure(self):self.run_bad(['--include-timecard','--productivity','/does-not-exist','--native-alarms','--quick-actions'],'native-alarms requires native-daily, include-files and foreground-idle')
 def test_requires_actual_preference_broker(self):self.run_bad(['--include-timecard','--productivity','/does-not-exist','--native-alarms'],'include-timecard requires the copied-preference QuickActions product cohort')
if __name__=='__main__':unittest.main()
