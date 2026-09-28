#!/usr/bin/env python3
import base64
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
LOCAL=json.loads((ROOT/"manifest/released-drivers.json").read_text())
TREES=json.loads((ROOT/"manifest/source-trees.json").read_text())
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

def source_head():
    return json.loads(api("git/ref/heads/master"))["object"]["sha"]

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
        found[identity]={
            "version":version,
            "path":path,
            "driver_abi":manifest.get("driver_abi"),
            "tree_sha":entry.get("sha"),
        }
    return found

def main():
    upstream=upstream_index()
    released={d["id"]:d["version"] for d in upstream.get("drivers",[])}
    local_released={d["id"]:d["version"] for d in LOCAL.get("drivers",[])}
    entries={}
    for section in ("drivers","source_only_drivers"):
        for d in LOCAL.get(section,[]):
            if d["id"] in entries:
                raise SystemExit(f"duplicate local driver id {d['id']}")
            entries[d["id"]]=d
    source=source_manifests()
    head=source_head()
    errors=[]

    released_baseline=SOURCE.get("source_master_sha")
    tree_baseline=TREES.get("source_master_sha")
    if TREES.get("source_repository")!=REPO:
        errors.append(f"source-tree repository mismatch: local {TREES.get('source_repository')} expected {REPO}")
    if released_baseline!=tree_baseline:
        errors.append(f"parity baseline mismatch: released manifest {released_baseline} source trees {tree_baseline}")

    for identity,version in sorted(released.items()):
        if identity not in local_released:
            errors.append(f"missing released driver {identity} v{version}")
        elif local_released[identity]!=version:
            errors.append(f"released version mismatch {identity}: local {local_released[identity]} upstream {version}")
    for identity in sorted(set(local_released)-set(released)):
        errors.append(f"local released manifest has non-released driver {identity} v{local_released[identity]}")

    local_trees=TREES.get("drivers",{})
    for identity,meta in sorted(source.items()):
        entry=entries.get(identity)
        if entry is None:
            errors.append(f"untracked source driver {identity} v{meta['version']} at {meta['path']}")
            continue
        local_source_version=entry.get("source_version",entry["version"])
        if local_source_version!=meta["version"]:
            errors.append(f"source version mismatch {identity}: local {local_source_version} master {meta['version']}")
        if entry.get("source_path")!=meta["path"]:
            errors.append(f"source path mismatch {identity}: local {entry.get('source_path')} master {meta['path']}")
        if local_trees.get(identity)!=meta["tree_sha"]:
            errors.append(f"source tree mismatch {identity}: local {local_trees.get(identity)} master {meta['tree_sha']}")
    for identity in sorted(set(entries)-set(source)):
        errors.append(f"local inventory driver absent from source master: {identity}")
    for identity in sorted(set(local_trees)-set(source)):
        errors.append(f"local source-tree entry absent from source master: {identity}")

    if errors:
        print("\n".join(errors),file=sys.stderr)
        return 1
    divergent=sum(1 for d in LOCAL.get("drivers",[]) if d.get("source_version") and d["source_version"]!=d["version"])
    baseline_note = ""
    if tree_baseline != head:
        baseline_note = f", recorded baseline {tree_baseline} checked against master {head}"
    print(f"driver parity OK: {len(released)} released, {len(source)} source manifests, {divergent} source versions ahead of release{baseline_note}")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
