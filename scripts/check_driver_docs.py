#!/usr/bin/env python3
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
inventory = json.loads((ROOT / "manifest/released-drivers.json").read_text(encoding="utf-8"))
readme = (ROOT / "README.md").read_text(encoding="utf-8")

errors = []
migrated = [d for d in inventory.get("drivers", []) if d.get("migrated")]
migrated += [d for d in inventory.get("source_only_drivers", []) if d.get("migrated")]

for entry in migrated:
    identity = entry["id"]
    doc = ROOT / "docs/drivers" / f"{identity}.md"
    rel = f"docs/drivers/{identity}.md"
    if not doc.is_file():
        errors.append(f"{identity}: missing {rel}")
        continue
    body = doc.read_text(encoding="utf-8")
    if identity not in body:
        errors.append(f"{identity}: documentation does not identify the driver")
    if entry.get("version") and entry["version"] not in body:
        errors.append(f"{identity}: documentation does not mention version {entry['version']}")
    if not re.search(rf"\[[^\]]*{re.escape(identity)}[^\]]*\]\({re.escape(rel)}\)", readme):
        errors.append(f"{identity}: README does not link {rel}")

if errors:
    raise SystemExit("\n".join(errors))
print(f"documentation parity OK: {len(migrated)} migrated drivers documented and indexed")
