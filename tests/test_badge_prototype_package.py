#!/usr/bin/env python3
"""Mutation checks on copies of an exact local native/store/admission cohort."""
import argparse,copy,json,pathlib,shutil,sys,tempfile,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
import package_badge_prototype as p
class Rejections(unittest.TestCase):
 def trial(self,edit,expected):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d);native=root/'native';target=root/'target';native.mkdir();target.mkdir()
   shutil.copyfile(A.native/'build-proof.json',native/'build-proof.json')
   for name in [*p.read(A.native/'build-proof.json')['artifacts'],'config/sdkconfig.json']:
    dest=native/'build'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(A.native/'build'/name,dest)
   # The selected SPIFFS override is qualified from its exact compile commands
   # as well as the native artifact. Mutation copies retain that required input.
   if p.spiffs_lookup_selected(p.read(A.native/'build-proof.json')):
    shutil.copyfile(A.native/'build/compile_commands.json',native/'build/compile_commands.json')
   shutil.copyfile(A.target/'build.json',target/'build.json');shutil.copytree(A.target/'store',target/'store');(target/'shared-home').mkdir(parents=True);shutil.copyfile(A.target/'shared-home/default.elf',target/'shared-home/default.elf');shutil.copyfile(A.target/'shared-home/build.json',target/'shared-home/build.json')
   for name in ('wifi/wifi_settings-build-record.json','springboard/springboard-build-record.json'):
    source=A.target/name
    if source.exists():
     dest=target/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
   normal=root/'normal.json';sanitized=root/'sanitized.json';shutil.copyfile(A.normal,normal);shutil.copyfile(A.sanitized,sanitized)
   edit(native,target,normal,sanitized)
   with self.assertRaisesRegex(ValueError,expected):p.qualify(native,target,normal,sanitized,A.revision)
 def test_positive(self):p.qualify(A.native,A.target,A.normal,A.sanitized,A.revision)
 def test_native_tamper(self):self.trial(lambda n,t,a,b:(n/'build/riscrte_native.bin').write_bytes(b'tampered'),'Native artifact mismatch')
 def test_truncated_store(self):self.trial(lambda n,t,a,b:(t/'store/default.elf').write_bytes(b'truncated'),'Store inventory mismatch')
 def test_wrong_configuration(self):
  def edit(n,t,a,b):
   f=n/'build/config/sdkconfig.json';j=p.read(f);j['SPIFFS_PAGE_SIZE']=256;p.write(f,j)
  self.trial(edit,'Wrong native geometry')
 def test_mixed_native_store(self):
  def edit(n,t,a,b):j=p.read(a);j['native_elf_sha256']='0'*64;p.write(a,j)
  self.trial(edit,'Mixed native/store')
 def test_fake_complete(self):
  def edit(n,t,a,b):j=p.read(a);j['positive']['elf_count']=5;p.write(a,j)
  self.trial(edit,'Incomplete image')
 def test_missing_sanitized(self):
  def edit(n,t,a,b):j=p.read(b);j['sanitize']=False;p.write(b,j)
  self.trial(edit,'Missing complete admission')
 def test_wrong_source_pin(self):
  def edit(n,t,a,b):j=p.read(t/'build.json');j['source_pins']['runtime']['commit']='0'*40;p.write(t/'build.json',j)
  self.trial(edit,'Store source cohort')
 def test_partition_md5(self):
  def edit(n,t,a,b):
   f=n/'build/partition_table/partition-table.bin';raw=bytearray(f.read_bytes());raw[4]^=1;f.write_bytes(raw);j=p.read(n/'build-proof.json');j['artifacts']['partition_table/partition-table.bin']=p.record(f);p.write(n/'build-proof.json',j)
  self.trial(edit,'Partition MD5')
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--native',type=pathlib.Path,required=True);parser.add_argument('--target',type=pathlib.Path,required=True);parser.add_argument('--normal',type=pathlib.Path,required=True);parser.add_argument('--sanitized',type=pathlib.Path,required=True);parser.add_argument('--revision',required=True);A,rest=parser.parse_known_args();unittest.main(argv=[sys.argv[0],*rest])
