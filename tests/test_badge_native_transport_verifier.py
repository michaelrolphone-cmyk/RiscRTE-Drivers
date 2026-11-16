import importlib.util,sys,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('badge_verifier',ROOT/'test/test_badge_native.py');verifier=importlib.util.module_from_spec(spec);spec.loader.exec_module(verifier)
sys.path.insert(0,str(ROOT/'scripts'));import build_badge_native as builder
class Transport(unittest.TestCase):
 def test_legacy_and_explicit(self):
  verify=verifier.verify_transport_definitions
  verify([]);verify(['-DRISC_CO5300_STRIPE_ROWS=4'],True)
  for queued in (False,True):
   for rows in (4,8,16,32):
    args=['-DRISC_CO5300_STRIPE_ROWS='+str(rows),'-DRISC_CO5300_QUEUED_PIXELS='+str(int(queued))]
    verify(args,True,queued,rows)
    bad=[args[:-1]+['-DRISC_CO5300_QUEUED_PIXELS='+str(int(not queued))],args[1:],args+args[:1],args+args[1:],args+['-URISC_CO5300_STRIPE_ROWS'],args+['-URISC_CO5300_QUEUED_PIXELS']]
    if queued:bad.append(args[:-1])
    for altered in bad:
     with self.assertRaises(AssertionError):verify(altered,True,queued,rows)
  for args in (['-DRISC_CO5300_QUEUED_PIXELS=1'],['-DRISC_CO5300_STRIPE_ROWS=4']):
   with self.assertRaises(AssertionError):verify(args)
 def test_columns(self):
  for enabled in (False,True):
   args=['-DRISC_CO5300_STRIPE_ROWS=4','-DRISC_CO5300_REUSE_COLUMNS='+str(int(enabled))]
   verifier.verify_transport_definitions(args,True,False,4,enabled)
   for bad in (args+args[1:],args+['-URISC_CO5300_REUSE_COLUMNS'],args+['-DRISC_CO5300_REUSE_COLUMNS='+str(int(not enabled))]):
    with self.assertRaises(AssertionError):verifier.verify_transport_definitions(bad,True,False,4,enabled)
   if enabled:
    with self.assertRaises(AssertionError):verifier.verify_transport_definitions(args[:1],True,False,4,enabled)
 def test_build_forwards_selection(self):
  class Stop(Exception):pass
  previous_run,previous_board=builder.run,builder.BOARD
  try:
   builder.BOARD=ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c/prototype/native'
   for queued in (False,True):
    for rows in (4,8,16,32):
     calls=[]
     def run(cmd,*args):
      calls.append(cmd)
      if len(calls)==2:raise Stop()
     builder.run=run
     with self.assertRaises(Stop):builder.qualify(Path('/runtime'),Path('/idf'),Path('/bin/cc'),Path('/python'),Path('/out'),{},'',{},True,None,True,True,queued,rows,True)
     cmd=calls[1];self.assertIn('--reuse-columns',cmd);self.assertIn('--prototype',cmd);self.assertIn('--quick-actions',cmd)
     self.assertEqual('--queued-pixels' in cmd,queued);self.assertEqual(cmd[cmd.index('--native-stripe-rows')+1],str(rows))
  finally:builder.run, builder.BOARD=previous_run,previous_board
if __name__=='__main__':unittest.main()
