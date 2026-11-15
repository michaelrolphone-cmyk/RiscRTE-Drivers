import copy,itertools,unittest
from types import SimpleNamespace
import raster_traffic_profile as p

class Tests(unittest.TestCase):
    def fixture(self,a,c,e):
        return {'build_defines':[('-D'+m) for m,v in [('PORTABLE_SETTINGS_ALPHA_GEOMETRY',a),('PORTABLE_RASTER_OVERWRITE_CLEAR',c),('PORTABLE_RASTER_UNDERLAY_EXCHANGE',e)] if v],
                'sliced_native_frames':True,'settings_replay_geometry':{'alpha_geometry':a,'pixel_budget_and_512_checkpoints_preserved':True,'completed_frame_hit_maps_unchanged':True},
                'raster_memory':{'overwrite_clear':c,'underlay_exchange':e}}
    def test_combinations(self):
        for flags in itertools.product([False,True],repeat=3):p.qualify(self.fixture(*flags),*flags)
        p.qualify({})
    def test_defines(self):
        r=self.fixture(True,True,True)
        for i,m in enumerate(r['build_defines']):
            for change in ['missing','duplicate','zero','undef']:
                q=copy.deepcopy(r)
                if change=='missing':q['build_defines'].pop(i)
                if change=='duplicate':q['build_defines'].append(m)
                if change=='zero':q['build_defines'][i]=m+'=0'
                if change=='undef':q['build_defines'].append('-U'+m[2:])
                with self.assertRaises(ValueError):p.qualify(q,True,True,True)
    def test_receipts(self):
        for group,field in [('settings_replay_geometry','alpha_geometry'),('settings_replay_geometry','pixel_budget_and_512_checkpoints_preserved'),('settings_replay_geometry','completed_frame_hit_maps_unchanged'),('raster_memory','overwrite_clear'),('raster_memory','underlay_exchange')]:
            for value in [False,None,1,'true']:
                q=self.fixture(True,True,True);q[group][field]=value
                with self.assertRaises(ValueError):p.qualify(q,True,True,True)
        q=self.fixture(True,True,True);q['sliced_native_frames']=False
        with self.assertRaises(ValueError):p.qualify(q,True,True,True)
    def test_routing(self):
        a=SimpleNamespace(settings_alpha_geometry=True,raster_overwrite_clear=True,raster_underlay_exchange=True)
        self.assertEqual(p.arguments(a,'settings'),['--settings-alpha-geometry','--raster-overwrite-clear','--raster-underlay-exchange'])
        self.assertEqual(p.arguments(a,'springboard'),['--raster-underlay-exchange'])
        self.assertEqual(p.arguments(a,'wifi_settings'),[])
    def test_mask_and_unselected_fitting(self):
        q=self.fixture(False,False,True);q['nova_mask_bounds']=True;q['build_defines'].append('-DPORTABLE_NOVA_MASK_BOUNDS')
        p.qualify(q,False,False,True,True)
        with self.assertRaises(ValueError):p.qualify(q,False,False,True,False)
        for change in ['flag','receipt']:
            r=self.fixture(True,True,True)
            if change=='flag':r['build_defines'].append('-DPORTABLE_SETTINGS_PREPARED_TEXT')
            else:r['settings_replay_geometry']['prepared_text']=True
            with self.assertRaises(ValueError):p.qualify(r,True,True,True)
    def test_prerequisites(self):
        class Parser:
            def error(self,message):raise ValueError(message)
        valid=dict(nova_mask_bounds=True,settings_alpha_geometry=True,raster_overwrite_clear=True,raster_underlay_exchange=True,native_settings_ui=True,sliced_native_frames=True,foreground_idle=True,native_alarms=True)
        p.validate(SimpleNamespace(**valid),Parser())
        for key in ['native_settings_ui','sliced_native_frames','foreground_idle','native_alarms']:
            bad=dict(valid);bad[key]=False
            with self.assertRaises(ValueError):p.validate(SimpleNamespace(**bad),Parser())
        p.validate(SimpleNamespace(**{key:False for key in valid}),Parser())

if __name__=='__main__':unittest.main()
