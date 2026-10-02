#!/usr/bin/env python3
"""Read-only parity gate for actual driver bytes and immutable upstream snapshots."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import stat
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SHA = re.compile(r"[0-9a-f]{40}\Z")
VERSION = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+\Z")


def command(args):
    return subprocess.check_output(args, text=True, timeout=60)


def object_hash(kind, data):
    return hashlib.sha1(kind.encode() + b" " + str(len(data)).encode() + b"\0" + data).hexdigest()


def local_tree(path):
    """Hash actual files, including additions, deletions and executable modes.

    Never touch the Git index, ignore rules or object database. Symlinks and
    special files fail closed rather than traversing outside the source tree.
    """
    if path.is_symlink() or not path.is_dir():
        raise ValueError(f"not a regular source directory: {path}")
    entries = []
    for child in path.iterdir():
        mode = child.lstat().st_mode
        name = child.name.encode('utf-8')
        if stat.S_ISDIR(mode):
            digest = local_tree(child)
            entries.append((name + b'/', b'40000', name, digest))
        elif stat.S_ISREG(mode):
            digest = object_hash('blob', child.read_bytes())
            entries.append((name, b'100755' if mode & 0o111 else b'100644', name, digest))
        else:
            raise ValueError(f"unsupported source entry: {child}")
    data = b''.join(mode + b' ' + name + b'\0' + bytes.fromhex(digest)
                    for _, mode, name, digest in sorted(entries))
    return object_hash('tree', data)


class Source:
    def __init__(self, repository, reader=None):
        self.repository = repository
        self.reader = reader

    def api(self, path, ref=None):
        args = ['gh', 'api', '--method', 'GET', f'repos/{self.repository}/{path}']
        if ref:
            args += ['-f', f'ref={ref}']
        return json.loads(command(args))

    def git(self, *args):
        return command(['git', '-C', str(self.reader), *args])

    def resolve(self, ref):
        if self.reader:
            sha = self.git('rev-parse', '--verify', '--end-of-options', ref + '^{commit}').strip()
        else:
            sha = self.api('commits/' + ref)['sha']
        if not SHA.fullmatch(sha):
            raise ValueError('invalid resolved commit: ' + str(sha))
        return sha

    def read_json(self, path, sha):
        if self.reader:
            return json.loads(self.git('show', f'{sha}:{path}'))
        payload = self.api('contents/' + path, sha)
        if payload.get('type') != 'file' or payload.get('encoding') != 'base64':
            raise ValueError('expected JSON file: ' + path)
        return json.loads(base64.b64decode(payload['content']))

    def driver_dirs(self, sha):
        if self.reader:
            rows = []
            for line in self.git('ls-tree', sha, 'Drivers/').splitlines():
                meta, path = line.split('\t', 1)
                _, kind, digest = meta.split()
                if kind == 'tree':
                    rows.append({'path': path, 'sha': digest})
            return rows
        return [r for r in self.api('contents/Drivers', sha) if r['type'] == 'dir']


def unique(rows, context):
    result = {}
    for row in rows:
        identity, version = row.get('id'), row.get('version')
        if not isinstance(identity, str) or not identity or not isinstance(version, str) or not VERSION.fullmatch(version):
            raise ValueError(f'invalid identity/version in {context}')
        if identity in result:
            raise ValueError(f'duplicate {context} driver id {identity}')
        result[identity] = row
    return result


def snapshot(source, source_ref, release_ref, release_path, non_package_directories=()):
    # Resolve both branches exactly once. Every subsequent read uses these SHAs.
    head, release = source.resolve(source_ref), source.resolve(release_ref)
    released = unique(source.read_json(release_path, release)['drivers'], 'upstream released')
    directories = source.driver_dirs(head)
    excluded = list(non_package_directories)
    if any(not isinstance(path, str) or not re.fullmatch(r'Drivers/[A-Za-z0-9_-]+', path)
           for path in excluded) or len(excluded) != len(set(excluded)):
        raise ValueError('invalid or duplicate configured non-package source directory')
    listed = {entry['path'] for entry in directories}
    missing = set(excluded) - listed
    if missing:
        raise ValueError('configured non-package source directory missing upstream: ' + ', '.join(sorted(missing)))
    rows = []
    for entry in directories:
        if entry['path'] in excluded:
            continue
        manifest = source.read_json(entry['path'] + '/manifest.json', head)
        rows.append(dict(manifest, source_path=entry['path'], tree_sha=entry['sha']))
    return head, release, unique(rows, 'upstream source'), released


def classify(base, local, upstream):
    if local == upstream:
        return 'unchanged' if local == base else 'converged'
    if local == base:
        return 'upstream-only'
    if upstream == base:
        return 'external-only'
    return 'conflict'


def validate(root, inventory, trees, source, released, head, release):
    entries = unique(inventory['drivers'] + inventory.get('source_only_drivers', []), 'local')
    local_released = unique(inventory['drivers'], 'local released')
    errors, rows, paths = [], [], set()
    origin = inventory['parity_source']
    if trees['source_repository'] != origin['repository']:
        errors.append('source-tree repository mismatch')
    if origin['source_master_sha'] != trees['source_master_sha']:
        errors.append('source/release recorded baseline mismatch')
    for identity in sorted(set(released) | set(local_released)):
        if identity not in released or identity not in local_released:
            errors.append(f'released inventory mismatch: {identity}')
        elif released[identity]['version'] != local_released[identity]['version']:
            errors.append(f'released version mismatch: {identity}')
    for identity in sorted(set(entries) | set(source) | set(trees['drivers'])):
        if identity not in entries or identity not in source or identity not in trees['drivers']:
            errors.append(f'source inventory mismatch: {identity}')
            continue
        entry, upstream = entries[identity], source[identity]
        path = entry.get('source_path', '')
        if not isinstance(path, str) or not re.fullmatch(r'Drivers/[A-Za-z0-9_-]+', path) or path in paths:
            errors.append(f'invalid or duplicate source path: {identity}')
            continue
        paths.add(path)
        if path != upstream['source_path']:
            errors.append(f'source path mismatch: {identity}')
        if entry.get('source_version', entry['version']) != upstream['version']:
            errors.append(f'source version mismatch: {identity}')
        baseline = trees['drivers'][identity]
        actual = local_tree(root / path)
        state = classify(baseline, actual, upstream['tree_sha'])
        rows.append({'id': identity, 'path': path, 'baseline_tree': baseline,
                     'local_tree': actual, 'upstream_tree': upstream['tree_sha'], 'state': state})
        if actual != upstream['tree_sha']:
            errors.append(f'{identity}: {state} source drift (preserve/reconcile local edits)')
        if baseline != upstream['tree_sha']:
            errors.append(f'{identity}: recorded source tree needs refresh')
    for path in (root / 'Drivers').iterdir():
        if (path.is_dir() or path.is_symlink()) and 'Drivers/' + path.name not in paths:
            errors.append('untracked local source directory: ' + path.name)
    return {'source_repository': origin['repository'], 'source_commit': head,
            'release_index_commit': release, 'recorded_source_commit': trees['source_master_sha'],
            'released_count': len(released), 'source_count': len(source),
            'files_scope': 'actual driver directory bytes and modes; SDK/build/runtime excluded',
            'drivers': rows, 'errors': errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reader', type=Path, help='Optional read-only local Reader Git checkout')
    parser.add_argument('--source-ref', default='master')
    parser.add_argument('--release-ref', help='Defaults to recorded release-index branch')
    parser.add_argument('--output', type=Path, help='Optional provenance/drift JSON report')
    args = parser.parse_args()
    inventory = json.loads((ROOT / 'manifest/released-drivers.json').read_text())
    trees = json.loads((ROOT / 'manifest/source-trees.json').read_text())
    origin = inventory['parity_source']
    try:
        head, release, source, released = snapshot(Source(origin['repository'], args.reader),
            args.source_ref, args.release_ref or origin['branch'], origin['path'],
            trees.get('non_package_directories', []))
        report = validate(ROOT, inventory, trees, source, released, head, release)
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as exc:
        print(f'driver parity failed: {exc}', file=sys.stderr)
        return 1
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    if report['errors']:
        print('\n'.join(report['errors']), file=sys.stderr)
        return 1
    print(f"driver parity OK: {report['released_count']} released, {report['source_count']} actual source trees; "
          f"master {head}, release-index {release}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
