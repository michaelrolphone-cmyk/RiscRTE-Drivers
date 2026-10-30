"""Strict zero-change handling for an import-free Xtensa provider."""
import importlib.util
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('normalizer',Path(__file__).resolve().parents[2]/'scripts/normalize_xtensa_provider_relocations.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def fixture(plt=False,padding=False):
    data=bytearray(1024);data[:7]=b'\x7fELF\x01\x01\x01'
    for off,value in [(16,3),(18,94),(46,40),(48,6 if plt else 5),(50,1)]:struct.pack_into('<H',data,off,value)
    struct.pack_into('<I',data,32,64)
    names=b'\0.shstrtab\0.rela.dyn\0.dynamic\0.text\0.rela.plt\0';data[320:320+len(names)]=names
    def section(i,name,kind,addr,off,size,entry=0):struct.pack_into('<10I',data,64+i*40,names.index(name.encode()+b'\0'),kind,0,addr,off,size,0,0,4,entry)
    section(1,'.shstrtab',3,0,320,len(names));size=24 if padding else 12;section(2,'.rela.dyn',4,0x1000,400,size,12)
    section(3,'.dynamic',6,0x2000,512,32,8);section(4,'.text',1,0x3000,600,16)
    if plt:section(5,'.rela.plt',4,0x1000+size,400+size,12,12)
    struct.pack_into('<III',data,400,0x3000,5,0)
    struct.pack_into('<IIIIIIII',data,512,7,0x1000,8,size,9,12,0,0)
    return data
class NoPlt(unittest.TestCase):
    def run_image(self,data,expected=None):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'a.elf';p.write_bytes(data)
            if expected:
                with self.assertRaisesRegex(ValueError,expected):m.normalize(p)
                self.assertEqual(p.read_bytes(),data)
            else:self.assertEqual(m.normalize(p),0);self.assertEqual(p.read_bytes(),data)
    def test_import_free_relative_image_unchanged_and_idempotent(self):self.run_image(fixture())
    def test_wrong_architecture_class_and_endianness(self):
        for offset,value in [(4,2),(5,2),(18,3),(16,2)]:
            with self.subTest(offset=offset):
                d=fixture();d[offset]=value;self.run_image(d,'ELF|Xtensa')
    def test_ranges_sizes_and_entry_size(self):
        for offset,value in [(64+2*40+16,1020),(64+2*40+20,13),(64+2*40+36,8),(64+3*40+16,1020),(64+3*40+20,31)]:
            with self.subTest(offset=offset):
                d=fixture();struct.pack_into('<I',d,offset,value);self.run_image(d,'layout')
    def test_dynamic_consistency_and_contradictory_plt_tags(self):
        for offset,value in [(516,0x1004),(524,24),(532,8),(512,2),(512,20),(512,23),(528,7),(536,1)]:
            with self.subTest(offset=offset,value=value):
                d=fixture();struct.pack_into('<I',d,offset,value);self.run_image(d,'dynamic')
    def test_invalid_relocations_and_padding(self):
        for offset,value in [(400,0),(400,0x3001),(404,0),(404,3),(404,6),(404,0x105)]:
            with self.subTest(offset=offset,value=value):
                d=fixture();struct.pack_into('<I',d,offset,value);self.run_image(d,'Unsupported')
        self.run_image(fixture(padding=True),'Unsupported')
    def test_overlap_refuses(self):
        d=fixture();struct.pack_into('<I',d,64+4*40+16,404);self.run_image(d,'Overlapping')
    def test_existing_plt_padding_removal_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'plt.elf';before=fixture(plt=True,padding=True);p.write_bytes(before);self.assertEqual(m.normalize(p),1)
            expected=bytearray(before);struct.pack_into('<I',expected,64+2*40+20,12);struct.pack_into('<I',expected,524,12);self.assertEqual(p.read_bytes(),expected)
if __name__=='__main__':unittest.main()
