import unittest,sys,subprocess,tempfile,os,importlib.util
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from build_badge_native import verify_column_reuse_selection
class Selection(unittest.TestCase):
 def test_exact(self):
  for enabled in (False,True):
   args=['-DRISC_CO5300_REUSE_COLUMNS='+str(int(enabled))]
   row={'file':'/components/co5300/native/Co5300Display.cpp','arguments':args}
   self.assertEqual(verify_column_reuse_selection([row],enabled)['enabled'],enabled)
   for bad in ([],args+args,args+['-URISC_CO5300_REUSE_COLUMNS'],args+['-DRISC_CO5300_REUSE_COLUMNS='+str(int(not enabled))]):
    with self.assertRaises(RuntimeError):verify_column_reuse_selection([dict(row,arguments=bad)],enabled)
   for rows in ([],[row,row]):
    with self.assertRaises(RuntimeError):verify_column_reuse_selection(rows,enabled)
 def test_cmake(self):
  profile=ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype/native/product-profile.cmake'
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'check.cmake';p.write_text(f'include("{profile}")\nmessage("selected=${{RISC_NATIVE_DEFINITIONS}}")\n')
   for on in (False,True):
    cmd=[os.environ.get('CMAKE','cmake')]+(['-DRISC_BADGE_REUSE_COLUMNS=ON'] if on else [])+['-P',str(p)]
    r=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr);self.assertIn('RISC_CO5300_REUSE_COLUMNS='+str(int(on)),r.stderr)
 def test_builder_cli(self):
  r=subprocess.run([sys.executable,str(ROOT/'scripts/build_badge_native.py'),'--help'],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr);self.assertIn('--reuse-columns',r.stdout)
if __name__=='__main__':unittest.main()
