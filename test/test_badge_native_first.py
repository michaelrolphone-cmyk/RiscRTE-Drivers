#!/usr/bin/env python3
"""Native evidence survives later failures; no required gate is bypassed."""
import ast,importlib.util,json,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
spec=importlib.util.spec_from_file_location('cohort',ROOT/'scripts/run_badge_prototype_ci.py');cohort=importlib.util.module_from_spec(spec);spec.loader.exec_module(cohort)
class NativeFirst(unittest.TestCase):
 def invoke(self,flags=(),fail_native=False):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);out=root/'evidence';commands=[]
   def fake(command,log,env):
    commands.append(command)
    if Path(command[1]).name=='build_badge_native.py':
     if fail_native:raise subprocess.CalledProcessError(7,command)
     native=Path(command[command.index('--output')+1]);native.mkdir(parents=True);(native/'build-proof.json').write_text('{"fixture":true}')
    else:raise subprocess.CalledProcessError(11,command)
   argv=['runner','--workspace',str(root),'--idf',str(root/'idf'),'--cc','exact-gcc','--output',str(out),*flags]
   with patch.object(sys,'argv',argv),patch.object(cohort,'run_logged',fake):
    with self.assertRaises(subprocess.CalledProcessError) as caught:cohort.main()
   return commands,caught.exception.returncode,(out/'native/build-proof.json').is_file()
 def test_later_failure_keeps_native_proof_and_fails_cohort(self):
  commands,code,proof=self.invoke();self.assertEqual(code,11);self.assertTrue(proof)
  self.assertEqual([Path(c[1]).name for c in commands],['build_badge_native.py','test_badge_source_pack.py'])
 def test_native_failure_is_not_swallowed(self):
  commands,code,proof=self.invoke(fail_native=True);self.assertEqual(code,7);self.assertFalse(proof);self.assertEqual(len(commands),1)
 def test_selected_native_flags_preserved(self):
  flags=['--foreground-idle','--include-files','--native-daily','--native-alarms','--quick-actions','--time-server-sync','--native-performance-trace','--reuse-columns','--queued-pixels','--reuse-unchanged-stripes','--native-stripe-rows','16','--spiffs-source','/exact/spiffs']
  commands,code,proof=self.invoke(flags);command=commands[0];self.assertEqual(code,11);self.assertTrue(proof)
  for f in ['--quick-actions','--time-server-sync','--performance-trace','--reuse-columns','--queued-pixels','--reuse-unchanged-stripes']:self.assertEqual(command.count(f),1)
  self.assertEqual(command[command.index('--native-stripe-rows')+1],'16');self.assertEqual(command[command.index('--spiffs-source')+1],'/exact/spiffs')
 def test_owning_parity_and_failure_semantics_remain(self):
  source=(ROOT/'scripts/run_badge_prototype_ci.py').read_text();tree=ast.parse(source)
  native=[n for n in ast.walk(tree) if isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id=='run' and n.args and isinstance(n.args[0],ast.Constant) and n.args[0].value=='build_badge_native.py'];self.assertEqual(len(native),1)
  # Tool implementation, selected source and historical parity are separate roles.
  self.assertIn("external(w/'scene-qualification-tools/scripts/test_native_scene.py'",source)
  self.assertIn("'--system-under-test',w/'system-prototype'",source)
  self.assertIn("'--system-revision',json.loads",source)
  self.assertIn("'--parity-source',w/'timecard-system-baseline'",source)
  self.assertIn("'--baseline',w/'timecard-pre-scene-baseline'",source)
  self.assertIn('subprocess.run(command,check=True',source)
  workflow=(ROOT/'.github/workflows/badge-prototype.yml').read_text();self.assertIn('if: always()',workflow);self.assertIn('/native/build/riscrte_native.elf',workflow)
if __name__=='__main__':unittest.main()

