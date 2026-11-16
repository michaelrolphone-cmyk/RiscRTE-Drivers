#!/usr/bin/env python3
"""Actual shared receipt/define contract; no compiler or hardware needed."""
import unittest
from build_badge_prototype import qualify_alarm_ready_drain

class Selection(unittest.TestCase):
    def test_default_and_selected(self):
        qualify_alarm_ready_drain({}, False)
        qualify_alarm_ready_drain(self.valid(), True)

    @staticmethod
    def valid():
        return {'foreground_idle': {'alarm_ready_drain': {'enabled': True, 'budget_ms': 8}},
                'build_defines': ['-DPORTABLE_ALARM_READY_DRAIN',
                                  '-DPORTABLE_ALARM_READY_BUDGET_MS=8u',
                                  '-DALARM_SERVICE_TAGGED_V2']}

    def test_rejects_mismatch_and_conflicting_defines(self):
        cases = [{}, self.valid(), self.valid(), self.valid(), self.valid(), self.valid()]
        cases[1]['build_defines'].pop()
        cases[2]['build_defines'].append('-DPORTABLE_ALARM_READY_DRAIN=0')
        cases[3]['build_defines'].append('-DPORTABLE_ALARM_READY_BUDGET_MS=1u')
        cases[4]['foreground_idle']['alarm_ready_drain']['budget_ms'] = 1
        cases[5]['foreground_idle']['alarm_ready_drain']['enabled'] = False
        for case in cases:
            with self.subTest(case=case), self.assertRaises(ValueError):
                qualify_alarm_ready_drain(case, True)
        with self.assertRaises(ValueError):
            qualify_alarm_ready_drain(self.valid(), False)

if __name__ == '__main__':
    unittest.main()
