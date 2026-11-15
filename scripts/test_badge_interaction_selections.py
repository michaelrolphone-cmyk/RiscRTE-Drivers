#!/usr/bin/env python3
import copy,itertools,unittest
from build_badge_prototype import qualify_interaction_selections as qualify
class SelectionTests(unittest.TestCase):
 def fixture(self,t,s,n):
  return ({'interaction_trace':t,'artifacts':{'default':{'defines':['-DWATCH_INTERACTION_TRACE'] if t else []}}},{'build_defines':['-DPORTABLE_SETTINGS_NATIVE_GEOMETRY'] if s else [],'native_settings_geometry':{'native_font_rasters':True}},{'sliced_native_frames':n,'raster_work_budget_ms':8 if n else None,'build_defines':['-DPORTABLE_NOVA_SLICED_FRAMES','-DPORTABLE_FRAME_RECEIPTS','-DPORTABLE_RASTER_WORK_BUDGET_MS=8','-DPORTABLE_RASTER_SNAPSHOT','-DPORTABLE_RASTER_COOPERATIVE_INPUT','-DPORTABLE_INPUT_SAMPLE_INTERVAL_MS=2u','-DPORTABLE_RGB565_SPAN_FILL'] if n else []})
 def test_all_selections(self):
  for flags in itertools.product([False,True],repeat=3):qualify(*self.fixture(*flags),*flags)
 def test_each_required_receipt(self):
  for which in range(3):
   docs=list(self.fixture(True,True,True));docs[which]['build_defines' if which else 'interaction_trace']=[] if which else False
   with self.assertRaises(ValueError):qualify(*docs,True,True,True)
 def test_every_sliced_define(self):
  for i in range(7):
   docs=list(self.fixture(True,True,True));docs[2]['build_defines'].pop(i)
   with self.assertRaises(ValueError):qualify(*docs,True,True,True)
 def test_budget_required(self):
  for val in [None,2,True,'8']:
   docs=list(self.fixture(True,True,True));docs[2]['raster_work_budget_ms']=val
   with self.assertRaises(ValueError):qualify(*docs,True,True,True)
 def test_trace_define_required(self):
  docs=list(self.fixture(True,True,True));docs[0]['artifacts']['default']['defines']=[]
  with self.assertRaises(ValueError):qualify(*docs,True,True,True)
 def test_native_fonts_required(self):
  docs=list(self.fixture(True,True,True));docs[1]['native_settings_geometry']={}
  with self.assertRaises(ValueError):qualify(*docs,True,True,True)
 def test_no_truthy_metadata(self):
  for val in [1,'true',None]:
   docs=list(self.fixture(True,True,True));docs[0]['interaction_trace']=val
   with self.assertRaises(ValueError):qualify(*docs,True,True,True)
 def test_round_selection(self):
  docs=list(self.fixture(True,True,True));docs[1]['build_defines'].append('-DPORTABLE_SETTINGS_ROUND_GEOMETRY');docs[1]['round_settings_geometry']={'flag_value':64}
  qualify(*docs,True,True,True,True)
  with self.assertRaises(ValueError):qualify(*docs,True,True,True,False)
  for value in [None,0,1,True,32]:
   bad=copy.deepcopy(docs);bad[1]['round_settings_geometry']['flag_value']=value
   with self.assertRaises(ValueError):qualify(*bad,True,True,True,True)
  docs[1]['build_defines'].remove('-DPORTABLE_SETTINGS_ROUND_GEOMETRY')
  with self.assertRaises(ValueError):qualify(*docs,True,True,True,True)
if __name__=='__main__':unittest.main()
