#!/usr/bin/env python3
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from build_badge_native import verify_time_sample_selection
class Selection(unittest.TestCase):
    def rows(self,*flags):return [{'file':'/runtime/src/ports/esp32s3/CpuPort.cpp','arguments':['cc',*flags]}]
    def test_enabled_and_disabled(self):
        verify_time_sample_selection(self.rows('-DRISC_ENABLE_TIME_SAMPLE=1'),'/runtime',True)
        verify_time_sample_selection(self.rows(),'/runtime',False)
        verify_time_sample_selection(self.rows('-DRISC_ENABLE_TIME_SAMPLE=0'),'/runtime',False)
    def test_conflicts_and_missing_rejected(self):
        for flags in [[],['-DRISC_ENABLE_TIME_SAMPLE'],['-DRISC_ENABLE_TIME_SAMPLE=0'],
                      ['-DRISC_ENABLE_TIME_SAMPLE=1','-URISC_ENABLE_TIME_SAMPLE'],
                      ['-DRISC_ENABLE_TIME_SAMPLE=1','-DRISC_ENABLE_TIME_SAMPLE=0'],
                      ['-DRISC_ENABLE_TIME_SAMPLE=1','-DRISC_ENABLE_TIME_SAMPLE=1']]:
            with self.assertRaises(RuntimeError):verify_time_sample_selection(self.rows(*flags),'/runtime',True)
        with self.assertRaises(RuntimeError):verify_time_sample_selection([], '/runtime', True)
        with self.assertRaises(RuntimeError):verify_time_sample_selection(self.rows('-DRISC_ENABLE_TIME_SAMPLE=1'),'/runtime',False)
if __name__=='__main__':unittest.main()
