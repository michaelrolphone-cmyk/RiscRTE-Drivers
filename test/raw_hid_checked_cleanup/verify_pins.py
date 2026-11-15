#!/usr/bin/env python3
"""Fail closed if the unmodified Reader source or candidate identity changes."""
import hashlib
import json
from pathlib import Path

here = Path(__file__).resolve().parent
root = here.parent.parent
pinned = here / "pinned_reader"
assert hashlib.sha256((pinned / "SOURCE.json").read_bytes()).hexdigest() == "1b037b2fb93407bc451caab6d6c8fc333d84a964d2aeabe5a27bd69379b4c494", "Reader source pin manifest changed"
source = json.loads((pinned / "SOURCE.json").read_text())
assert source["repository"] == "michaelrolphone-cmyk/T5S3-Reader"
assert source["commit"] == "c334919a05d393b7b3a536fceb117cfbb573a4ef"
assert len(source["files"]) == 11
for item in source["files"]:
    data = (pinned / item["path"]).read_bytes()
    blob = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
    assert blob == item["git_blob"], f"Pinned Reader file changed: {item['path']}"
manifest = json.loads((root / "candidates/usb-hid-checked-cleanup/manifest.json").read_text())
assert manifest["id"] == "usb-hid" and manifest["version"] == "0.1.3"
assert manifest["driver_abi"] == 2
assert manifest["provides"] == [{"capability": "usb.hid", "api": 1}]
assert manifest["requires"] == [{"capability": "usb.host", "api": 1}]
print("PASS exact Reader c334919 source/header pins and unchanged provider/ABI manifest identity")
