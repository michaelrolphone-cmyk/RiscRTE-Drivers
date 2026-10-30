#!/usr/bin/env python3
"""Hydrate declared custody commits before offline validation; never retarget HEAD."""
import argparse
import json
from pathlib import Path
import re
import subprocess

SHA = re.compile(r'[0-9a-f]{40}')
REPOSITORY = re.compile(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+')

def recover(root, repository, run=subprocess.run, output=subprocess.check_output):
    root = Path(root).resolve()
    if not isinstance(repository, str) or not REPOSITORY.fullmatch(repository):
        raise ValueError('Invalid repository identity')
    origin = output(['git', '-C', str(root), 'remote', 'get-url', 'origin'], text=True).strip()
    if origin.removesuffix('.git') != 'https://github.com/' + repository:
        raise ValueError('Origin does not match custody repository')
    custody = json.loads((root / 'manifest/local-source-custody.json').read_text())
    if not isinstance(custody, dict) or custody.get('schema') != 1 or not isinstance(custody.get('sources'), list):
        raise ValueError('Invalid custody schema')
    records = []
    paths = set()
    for entry in custody['sources']:
        provenance = entry.get('provenance', {}) if isinstance(entry, dict) else {}
        if not isinstance(provenance, dict):
            raise ValueError('Invalid custody provenance')
        path = entry.get('path') if isinstance(entry, dict) else None
        commit, tree = provenance.get('commit'), entry.get('tree_sha') if isinstance(entry, dict) else None
        if (not isinstance(path, str) or not re.fullmatch(r'Drivers/[A-Za-z0-9_-]+', path)
                or path in paths or provenance.get('path') != path or provenance.get('repository') != repository
                or not isinstance(commit, str) or not SHA.fullmatch(commit)
                or not isinstance(tree, str) or not SHA.fullmatch(tree)):
            raise ValueError('Invalid custody provenance')
        paths.add(path)
        records.append({'commit': commit, 'path': path, 'tree': tree})
    hydrated = []
    for commit in sorted({r['commit'] for r in records}):
        present = run(['git', '-C', str(root), 'cat-file', '-e', commit + '^{commit}'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if present.returncode:
            run(['git', '-C', str(root), 'fetch', '--no-tags', '--depth=1', 'origin', commit], check=True)
            hydrated.append(commit)
        actual = output(['git', '-C', str(root), 'rev-parse', '--verify', '--end-of-options', commit + '^{commit}'], text=True).strip()
        if actual != commit:
            raise ValueError('Custody commit identity mismatch')
    for record in records:
        tree = output(['git', '-C', str(root), 'rev-parse', '--verify', '--end-of-options', record['commit'] + ':' + record['path']], text=True).strip()
        kind = output(['git', '-C', str(root), 'cat-file', '-t', tree], text=True).strip()
        if tree != record['tree'] or kind != 'tree':
            raise ValueError('Custody provenance tree mismatch: ' + record['path'])
    return {'repository': repository, 'provenance': records, 'fetched_commits': hydrated, 'latest_ref_fallback': False}

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    receipt = recover(args.root, args.repository)
    args.output.write_text(json.dumps(receipt, indent=2) + '\n')
