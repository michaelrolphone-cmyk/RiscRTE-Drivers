import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'scripts'))
from build_badge_native import verify_performance_selection as verify
class Selection(unittest.TestCase):
 def test_exact_selection(self):
  def rows(args):return [{'file':'/runtime/src/main.cpp','arguments':['cc']+args}]
  verify(rows(['-DRISC_PERFORMANCE_TRACE=1']),'/runtime',True)
  verify(rows([]),'/runtime',False)
  verify(rows(['-DRISC_PERFORMANCE_TRACE=0']),'/runtime',False)
  for args in [[],['-DRISC_PERFORMANCE_TRACE=0'],['-DRISC_PERFORMANCE_TRACE=1','-DRISC_PERFORMANCE_TRACE=0'],['-URISC_PERFORMANCE_TRACE']]:
   with self.assertRaises(RuntimeError):verify(rows(args),'/runtime',True)
  for args in [['-DRISC_PERFORMANCE_TRACE=1'],['-DRISC_PERFORMANCE_TRACE=2'],['-URISC_PERFORMANCE_TRACE']]:
   with self.assertRaises(RuntimeError):verify(rows(args),'/runtime',False)
  with self.assertRaises(RuntimeError):verify([],'/runtime',True)
if __name__=='__main__':unittest.main()
