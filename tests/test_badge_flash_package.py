#!/usr/bin/env python3
"""Fail-closed tests on copies of the exact CI archive; no hardware operations."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import package_badge_flash as p

class PackageRejectionTests(unittest.TestCase):
    def reject(self, member, mutate, message, update_native=False):
        with tempfile.TemporaryDirectory() as td:
            td=Path(td); archive=td/'mutated.zip'
            with zipfile.ZipFile(ARGS.artifact) as z:
                entries={n:z.read(n) for n in z.namelist()}
            entries[member]=mutate(entries[member])
            if update_native:
                proof=json.loads(entries['evidence/build-proof.json'])
                proof['artifacts'][member.removeprefix('evidence/build/')]['sha256']=hashlib.sha256(entries[member]).hexdigest()
                entries['evidence/build-proof.json']=json.dumps(proof).encode()
            with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
                for n,data in entries.items(): z.writestr(n,data)
            original=p.ARCHIVE_SHA
            try:
                # Test-only re-sealing reaches inner validation; production pins archive.
                p.ARCHIVE_SHA=p.sha(archive)
                with self.assertRaisesRegex(ValueError,message):p.package(archive,ARGS.idf,td/'out')
                self.assertFalse((td/'out/badge-combined.bin').exists())
            finally:p.ARCHIVE_SHA=original
    def test_wrong_archive(self):
        with tempfile.TemporaryDirectory() as td:
            f=Path(td)/'bad.zip';f.write_bytes(b'wrong')
            with self.assertRaisesRegex(ValueError,'archive checksum'):p.package(f,ARGS.idf,Path(td)/'out')
    def test_wrong_head(self):self.reject('home-evidence/drivers-source.txt',lambda x:b'0'*40,'Drivers head')
    def test_native_tamper(self):self.reject('evidence/build/riscrte_native.bin',lambda x:x[:-1]+bytes([x[-1]^1]),'Native artifact mismatch')
    def test_store_tamper(self):self.reject('home-evidence/target/store/default.elf',lambda x:x[:-1],'Store inventory mismatch')
    def test_missing_file(self):self.reject('home-evidence/target/store/boot.json',lambda x:b'{}','Store inventory mismatch')
    def test_bad_partition(self):self.reject('evidence/build/partition_table/partition-table.bin',lambda x:x[:4]+bytes([x[4]^1])+x[5:],'Partition MD5 mismatch',True)
    def test_wrong_page_size(self):
        def change(b):d=json.loads(b);d['SPIFFS_PAGE_SIZE']=256;return json.dumps(d).encode()
        self.reject('evidence/build/config/sdkconfig.json',change,'Incompatible native flash')
    def test_missing_admission(self):
        def change(b):d=json.loads(b);d['real_runtime_graph_admission']=False;return json.dumps(d).encode()
        self.reject('home-evidence/admission-normal/admission.json',change,'Missing graph admission')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--artifact',required=True,type=Path);parser.add_argument('--idf',required=True,type=Path)
    ARGS,rest=parser.parse_known_args();ARGS.idf=ARGS.idf.resolve();unittest.main(argv=[sys.argv[0],*rest])
