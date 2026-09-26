#!/usr/bin/env python3
import base64
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LOCAL=json.loads((ROOT/"manifest/released-drivers.json").read_text())
SOURCE=LOCAL["parity_source"]
REPO=SOURCE["repository"]

def api(path, ref=None):
    cmd=["gh","api","--method","GET",f"repos/{REPO}/{path}"]
    if ref:
        cmd += ["-f",f"ref={ref}"]
    return subprocess.check_output(cmd,text=True)

def upstream_index():
    raw=api(f"contents/{SOURCE['path']}", SOURCE["branch"])
    payload=json.loads(raw)
    return json.loads(base64.b64decode("".join(payload["content"].split())).decode())

def source_manifests():
    entries=json.loads(api("contents/Drivers","master"))
    found={}
    for entry in entries:
        if entry.get("type")!="dir":
            continue
        path=entry["path"]
        try:
            payload=json.loads(api(f"contents/{path}/manifest.json","master"))
        except subprocess.CalledProcessError:
            continue
        manifest=json.loads(base64.b64decode("".join(payload["content"].split())).decode())
        identity=manifest.get("id")
        version=manifest.get("version")
        if not isinstance(identity,str) or not identity or not isinstance(version,str) or not version:
            raise SystemExit(f"invalid driver manifest at {path}")
        if identity in found:
            raise SystemExit(f"duplicate source driver id {identity}")
        found[identity]={"version":version,"path":path,"driver_abi":manifest.get("driver_abi")}
    return found

def main():
    upstream=upstream_index()
    released={d["id"]:d["version"] for d in upstream.get("drivers",[])}
    local_released={d["id"]:d["version"] for d in LOCAL.get("drivers",[])}
    local_source={
        d["id"]:d["version"]
        for section in ("drivers","source_only_drivers")
        for d in LOCAL.get(section,[])
    }
    source=source_manifests()
    errors=[]

    for identity,version in sorted(released.items()):
        if identity not in local_released:
            errors.append(f"missing released driver {identity} v{version}")
        elif local_released[identity]!=version:
            errors.append(f"released version mismatch {identity}: local {local_released[identity]} upstream {version}")
    for identity in sorted(set(local_released)-set(released)):
        errors.append(f"local released manifest has non-released driver {identity} v{local_released[identity]}")

    for identity,meta in sorted(source.items()):
        if identity not in local_source:
            errors.append(f"untracked source driver {identity} v{meta['version']} at {meta['path']}")
        elif local_source[identity]!=meta["version"]:
            errors.append(f"source version mismatch {identity}: local {local_source[identity]} master {meta['version']}")
    for identity in sorted(set(local_source)-set(source)):
        errors.append(f"local inventory driver absent from source master: {identity}")

    if errors:
        print("\n".join(errors),file=sys.stderr)
        return 1
    print(f"driver inventory parity OK: {len(released)} released, {len(source)} source manifests")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
