#!/usr/bin/env python3
"""Packaging invariants complement, but never replace, real Runtime admission."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from build_badge_home import home_manifest


class Contract(unittest.TestCase):
    def setUp(self):
        self.path=ROOT/'boards/waveshare-esp32-s3-touch-amoled-1.75c'
        self.prep=json.loads((self.path/'preparation.json').read_text())
        self.board=json.loads((self.path/'home/board.json').read_text())
        self.boot=json.loads((self.path/'home/boot.json').read_text())

    def test_graph_selects_only_four_real_providers(self):
        self.assertEqual({d['instance_id'] for d in self.boot['drivers']},{2,5,6,13})
        self.assertEqual({d['instance_id'] for d in self.board['devices']},{2,4,5,6,13})
        self.assertEqual(self.boot['provider_activation'],'demand-retained')
        self.assertNotIn('external-rtc',str(self.board))
        self.assertNotIn('sd-memory',str(self.board))
        pmic=next(d for d in self.board['devices'] if d['instance_id']==4)
        self.assertEqual(pmic['config']['address'],self.prep['power']['address'])
        self.assertEqual(pmic['config']['irq'],-1)

    def test_board_uses_prepared_wiring(self):
        prep=self.prep
        devices={d['instance_id']:d for d in self.board['devices']}
        bus=next(b for b in self.board['buses'] if b['kind']=='i2c')
        for field in ('controller','frequency_hz'):
            self.assertEqual(bus[field],prep['i2c'][field])
        self.assertEqual(bus['pins'],{k:prep['i2c'][k] for k in ('sda','scl')})
        display,touch=devices[5]['config'],devices[6]['config']
        for config,key in ((display,'display'),(touch,'touch')):
            for field in ('width','height'):
                self.assertEqual(config[field],prep[key][field])
            self.assertEqual(config['reset'],prep[key]['reset']['pin'])
            self.assertEqual(config['reset_assert_ms'],prep[key]['reset']['assert_ms'])
            self.assertEqual(config['reset_recovery_ms'],prep[key]['reset']['recovery_ms'])
        self.assertNotEqual(display['reset'],touch['reset'])
        self.assertEqual(touch['address'],prep['touch']['address'])
        self.assertEqual(touch['irq'],prep['touch']['irq'])
        self.assertEqual(devices[6]['bindings'],{'i2c.bus':2,'power.rail':13})
        self.assertEqual(devices[13]['config'],{'parent_instance_id':4,'function_id':1})

    def test_packaging_keeps_every_declaration(self):
        grants=self.boot['app_capabilities'][0]
        required=[{'capability':g['capability'],'api':g['api']} for g in grants['grants']]
        optional=[{'capability':n,'api':v} for n,v in [('board.battery',1),('rtc.clock',2),
                   ('input.power-key',1),('power.sleep',1),('storage.files',1)]]
        original={'requires':required+optional[:-1],'optional':optional[-1:],
                  'type':'application','file_name':'default.elf'}
        expected=copy.deepcopy(original)
        packaged=home_manifest(original,grants)
        self.assertEqual(original,expected)
        self.assertEqual(packaged['requires'],required)
        self.assertEqual(packaged['optional'],optional)
        self.assertEqual(packaged['file_name'],'default.elf')
        for index in range(len(required)):
            bad=copy.deepcopy(grants);bad['grants'].pop(index)
            with self.assertRaises(ValueError):home_manifest(original,bad)
        for name in ('board.battery','rtc.clock','platform.gpio','invented.capability'):
            bad=copy.deepcopy(grants);bad['grants'].append({'capability':name,'api':1,'instance_id':0})
            with self.assertRaises(ValueError):home_manifest(original,bad)
        bad=copy.deepcopy(original);bad['optional'].append(required[0])
        with self.assertRaises(ValueError):home_manifest(bad,grants)


if __name__=='__main__':unittest.main()
