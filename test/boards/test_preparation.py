import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import board_preparation as bp
PROFILE=ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c/preparation.json'

class Preparation(unittest.TestCase):
    def setUp(self): self.p=json.loads(PROFILE.read_text())
    def test_pinned_board_identity_and_channels(self):
        p=bp.admit_badge175(self.p)
        self.assertEqual(p['board_id'],'waveshare-esp32-s3-touch-amoled-1.75c')
        self.assertEqual(p['physical_memory'],{'flash_bytes':32*1024*1024,'psram_bytes':8*1024*1024})
        self.assertEqual(p['i2c'],{'instance_id':101,'controller':0,'sda':15,'scl':14,'frequency_hz':400000})
        self.assertEqual(p['display']['qspi'],{'physical_controller':2,'sclk':38,'data':[4,5,6,7],'cs':12,'frequency_hz':40000000})
        self.assertEqual((p['display']['width'],p['display']['height'],p['display']['offset_x'],p['display']['offset_y']),(466,466,6,0))
        self.assertEqual((p['touch']['address'],p['touch']['reset']['pin'],p['touch']['irq']),(0x5a,2,11))
        self.assertEqual(p['display']['reset'],{'pin':1,'active_high':False,'assert_ms':10,'recovery_ms':150})
        self.assertEqual(p['touch']['reset'],{'pin':2,'active_high':False,'assert_ms':10,'recovery_ms':50})
    def test_power_is_not_206(self):
        p=self.p['power']
        self.assertEqual(p['display_touch_supply'],'DCDC1')
        self.assertTrue(p['cpu_shares_display_touch_supply'])
        self.assertEqual((p['enable_register'],p['enable_mask'],p['enable_value']),(0x80,1,1))
        self.assertEqual(p['policy'],'read-only-preserve')
        self.assertEqual(p['boot_key'],{'source':'gpio','pin':0,'active_high':False})
        self.assertEqual(p['power_key'],{'source':'pmic-pwron','gpio':None})
        self.assertNotIn('aldo2',json.dumps(self.p).lower())
    def test_absence_and_no_fork(self):
        self.assertIn('external-rtc',self.p['absent_hardware'])
        self.assertIn('sd-card',self.p['absent_hardware'])
        self.assertEqual(self.p['app'],{'repository':'michaelrolphone-cmyk/RiscRTE-Watch','role':'default.elf','board_specific_fork':False})
        self.assertFalse(self.p['integration']['installable'])
        self.assertFalse(self.p['integration']['hardware_verified'])
    def test_conservative_content_corners(self):
        d=self.p['display']; s=d['safe_insets']; n=d['width']
        self.assertEqual(set(s.values()),{69})
        self.assertEqual(n-s['left']-s['right'],328)
        for x in (s['left'],n-s['right']):
            for y in (s['top'],n-s['bottom']):
                self.assertLessEqual((x-n/2)**2+(y-n/2)**2,(n/2)**2)
    def test_duplicate_pin_rejection(self):
        groups=[('i2c','sda'),('i2c','scl'),('display','qspi','sclk'),('display','qspi','cs'),('display','reset','pin'),('touch','reset','pin'),('touch','irq'),('power','boot_key','pin')]
        for path in groups:
            p=copy.deepcopy(self.p); parent=p
            for k in path[:-1]:parent=parent[k]
            parent[path[-1]]=4 # data0 is already GPIO4
            with self.subTest(path=path), self.assertRaisesRegex(ValueError,'overlapping'):bp.validate(p)
    def test_invalid_fields(self):
        cases=[(('i2c','sda'),True),(('i2c','frequency_hz'),400001),(('i2c','instance_id'),0),
          (('display','width'),0),(('touch','width'),410),(('display','offset_x'),65535),
          (('display','rotation'),1),(('display','qspi','data'),[4,5,6]),
          (('display','reset','active_high'),0),(('touch','reset','assert_ms'),0),
          (('touch','reset','recovery_ms'),501),(('touch','irq'),49),
          (('power','identity_mask'),0),(('power','enable_value'),2),
          (('power','policy'),'rewrite'),(('power','display_power_off_supported'),True),
          (('power','power_key','gpio'),0),(('power','transaction_timeout_ms'),0),
          (('display','safe_insets','left'),466),(('display','backlight'),18),
          (('integration','installable'),True),(('integration','hardware_verified'),True),
          (('app','board_specific_fork'),True),(('physical_memory','flash_bytes'),0)]
        for path,v in cases:
            p=copy.deepcopy(self.p); parent=p
            for k in path[:-1]:parent=parent[k]
            parent[path[-1]]=v
            with self.subTest(path=path),self.assertRaises(ValueError):bp.validate(p)
    def test_generated_config_reproducible(self):
        self.assertEqual(bp.header(self.p),(PROFILE.parent/'startup-config.h').read_text())
    def test_exact_board_admission(self):
        for path, expected in bp.BADGE175.items():
            p=copy.deepcopy(self.p); parent=p
            for k in path[:-1]:parent=parent[k]
            parent[path[-1]]=None
            with self.subTest(path=path), self.assertRaises((ValueError, TypeError, KeyError)):
                bp.admit_badge175(p)
        for path, value in [(('board_id',),'waveshare-esp32-s3-touch-amoled-2.06'),
             (('display','reset','pin'),17),(('i2c','sda'),48),
             (('touch','controller'),'cst816'),(('power','controller'),'axp202'),
             (('power','enable_register'),0x90)]:
            p=copy.deepcopy(self.p); parent=p
            for k in path[:-1]:parent=parent[k]
            parent[path[-1]]=value
            bp.validate(p) # Structurally valid does not establish board identity.
            with self.subTest(path=path),self.assertRaisesRegex(ValueError,'1.75C source mismatch'):
                bp.admit_badge175(p)
    def test_generalized_valid_profile(self):
        p=copy.deepcopy(self.p)
        p['board_id']='independent-rectangular-example'
        p['display']['width']=p['touch']['width']=320
        p['display']['height']=p['touch']['height']=240
        p['display']['safe_insets']={k:0 for k in ('top','right','bottom','left')}
        p['display']['reset']['pin']=17
        p['display']['reset']['active_high']=True
        p['touch']['reset']['pin']=18
        bp.validate(p)
        with self.assertRaises(ValueError):bp.admit_badge175(p)
        self.assertIn('{17u, true,',bp.header(p))
    def test_sources_are_immutable(self):
        self.assertEqual(len(self.p['sources']),5)
        for s in self.p['sources']:
            self.assertRegex(s['url'],r'^https://github.com/[^/]+/[^/]+/blob/[0-9a-f]{40}/')
            self.assertRegex(s['sha256'],r'^[0-9a-f]{64}$')

if __name__=='__main__':unittest.main()
