"""Native compiler headers and installed licensing are separate exact inputs."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from test_badge_prototype_execution import verify_native_sdk

class NativeSdkRecipe(unittest.TestCase):
 def test_each_changed_or_missing_input_rejects(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);include=root/'include';include.mkdir();notice=root/'licenses/native-time';notice.mkdir(parents=True)
   header=include/'Runtime.h';license=notice/'Runtime-LICENSE.txt'
   exact={header:b'actual header\n',license:b'exact license\n'}
   for p,data in exact.items():p.write_bytes(data)
   record={'native_time_sdk':{'sha256':{'Runtime.h':hashlib.sha256(exact[header]).hexdigest(),'LICENSE':hashlib.sha256(exact[license]).hexdigest()}}}
   sources={};verify_native_sdk(record,[include],root,sources)
   self.assertEqual(set(sources),{str(header),str(license)})
   for p,data in exact.items():
    with self.subTest(path=p):
     p.write_bytes(data+b'changed')
     with self.assertRaises(AssertionError):verify_native_sdk(record,[include],root,{})
     p.unlink()
     with self.assertRaises(AssertionError):verify_native_sdk(record,[include],root,{})
     p.write_bytes(data)

if __name__=='__main__':unittest.main()
