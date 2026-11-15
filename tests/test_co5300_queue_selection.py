import unittest,sys,subprocess,tempfile,os
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'scripts'))
from build_badge_native import verify_queued_selection
class Selection(unittest.TestCase):
 def test_exact(self):
  for enabled in (False,True):
   for stripe in (4,8,16,32):
    args=['-DRISC_CO5300_QUEUED_PIXELS='+str(int(enabled)),'-DRISC_CO5300_STRIPE_ROWS='+str(stripe)]
    row={'file':'/components/co5300/native/Co5300Display.cpp','arguments':args}
    self.assertEqual(verify_queued_selection([row],enabled,stripe)['internal_dma_bytes'],466*stripe*2*(2 if enabled else 1))
    for bad in (args[:-1],args+args[:1],args+['-URISC_CO5300_QUEUED_PIXELS'],args+['-DRISC_CO5300_STRIPE_ROWS=2']):
     with self.assertRaises(RuntimeError):verify_queued_selection([dict(row,arguments=bad)],enabled,stripe)
 def test_cmake(self):
  profile=Path(__file__).parents[1]/'boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype/native/product-profile.cmake'
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'check.cmake';p.write_text(f'include("{profile}")\nmessage("selected=${{RISC_NATIVE_DEFINITIONS}}")\n')
   for rows in (4,8,16,32):
    r=subprocess.run([os.environ.get('CMAKE','cmake'),'-DRISC_BADGE_QUEUED_PIXELS=ON','-DRISC_BADGE_PERFORMANCE_TRACE=ON','-DRISC_BADGE_STRIPE_ROWS='+str(rows),'-P',str(p)],capture_output=True,text=True)
    self.assertEqual(r.returncode,0,r.stderr);self.assertIn('RISC_CO5300_QUEUED_PIXELS=1',r.stderr);self.assertIn('RISC_CO5300_STRIPE_ROWS='+str(rows),r.stderr);self.assertIn('RISC_PERFORMANCE_TRACE=1',r.stderr)
   r=subprocess.run([os.environ.get('CMAKE','cmake'),'-P',str(p)],capture_output=True,text=True);self.assertEqual(r.returncode,0);self.assertIn('RISC_CO5300_QUEUED_PIXELS=0',r.stderr);self.assertIn('RISC_CO5300_STRIPE_ROWS=4',r.stderr)
   r=subprocess.run([os.environ.get('CMAKE','cmake'),'-DRISC_BADGE_STRIPE_ROWS=3','-P',str(p)],capture_output=True,text=True);self.assertNotEqual(r.returncode,0)
if __name__=='__main__':unittest.main()
