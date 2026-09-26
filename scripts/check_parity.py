#!/usr/bin/env python3
import base64, json, subprocess, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LOCAL=json.loads((ROOT/"manifest/released-drivers.json").read_text())
SOURCE=LOCAL["parity_source"]

def upstream_index():
    repo=SOURCE["repository"]
    ref=SOURCE["branch"]
    path=SOURCE["path"]
    cmd=["gh","api","--method","GET",f"repos/{repo}/contents/{path}","-f",f"ref={ref}","--jq",".content"]
    raw=subprocess.check_output(cmd,text=True)
    return json.loads(base64.b64decode("".join(raw.split())).decode())

def main():
    upstream=upstream_index()
    u={d["id"]:d["version"] for d in upstream.get("drivers",[])}
    l={d["id"]:d["version"] for d in LOCAL.get("drivers",[])}
    errors=[]
    for identity,version in sorted(u.items()):
        if identity not in l: errors.append(f"missing local driver {identity} v{version}")
        elif l[identity]!=version: errors.append(f"version mismatch {identity}: local {l[identity]} upstream {version}")
    for identity in sorted(set(l)-set(u)):
        errors.append(f"local manifest has non-upstream driver {identity} v{l[identity]}")
    if errors:
        print("\n".join(errors),file=sys.stderr)
        return 1
    print(f"release parity metadata OK: {len(u)} drivers")
    return 0
if __name__=="__main__": raise SystemExit(main())
