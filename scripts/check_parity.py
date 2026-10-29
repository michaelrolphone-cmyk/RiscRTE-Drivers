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
# Exact upstream support libraries, never inferred from a missing manifest.
# storage_fatfs is compiled into T5/X4 SD providers; x4pro_board supplies their
# shared board/register headers. Each pinned tree must still lack a manifest.
SHARED_SOURCE_DIRS = frozenset({
    "Drivers/common", "Drivers/storage_fatfs", "Drivers/x4pro_board",
})
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
        else:
            rows = [r for r in self.api('contents/Drivers', sha) if r['type'] == 'dir']
        result = []
        for row in rows:
            if row['path'] in SHARED_SOURCE_DIRS:
                # This exact documented support directory is not a package.
                # Inspect the resolved tree; never suppress a failed read/404.
                if self.reader:
                    names = [line.split('\t', 1)[1] for line in self.git('ls-tree', row['sha']).splitlines()]
                else:
                    tree = self.api('git/trees/' + row['sha'])
                    if tree.get('truncated'): raise ValueError('truncated shared source tree')
                    names = [entry['path'] for entry in tree['tree']]
                if 'manifest.json' in names:
                    raise ValueError('shared source directory became a package: ' + row['path'])
                continue
            result.append(row)
        return result


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


def snapshot(source, source_ref, release_ref, release_path):
    # Resolve both branches exactly once. Every subsequent read uses these SHAs.
    head, release = source.resolve(source_ref), source.resolve(release_ref)
    released = unique(source.read_json(release_path, release)['drivers'], 'upstream released')
    rows = []
    for entry in source.driver_dirs(head):
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


def validate_custody(root, custody, occupied_paths, package_ids):
    """Validate local-only custody without claiming upstream or release parity.

    Path-scoped candidates may share an existing package ID, but must explicitly
    name it. Support directories must remain manifestless. Every record pins the
    entire actual tree, including modes; unknown siblings remain untracked.
    """
    if custody is None:
        return [], []
    if not isinstance(custody, dict) or custody.get('schema') != 1 or not isinstance(custody.get('sources'), list):
        raise ValueError('invalid local source custody schema')
    paths, identities, rows, errors = set(occupied_paths), set(package_ids), [], []
    for entry in custody['sources']:
        if not isinstance(entry, dict):
            raise ValueError('invalid local source custody record')
        path, kind = entry.get('path'), entry.get('kind')
        if not isinstance(path, str) or not re.fullmatch(r'Drivers/[A-Za-z0-9_-]+', path) or path in paths:
            raise ValueError('invalid or duplicate local custody path: ' + str(path))
        paths.add(path)
        if kind not in ('support', 'adapter', 'candidate'):
            raise ValueError('invalid local custody kind: ' + path)
        provenance = entry.get('provenance', {})
        if (not isinstance(provenance, dict)
                or not isinstance(provenance.get('repository'), str)
                or not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', provenance['repository'])
                or not isinstance(provenance.get('commit'), str)
                or not SHA.fullmatch(provenance['commit'])
                or provenance.get('path') != path
                or not isinstance(entry.get('tree_sha'), str)
                or not SHA.fullmatch(entry['tree_sha'])):
            raise ValueError('invalid local custody provenance: ' + path)
        if provenance['repository'] != 'michaelrolphone-cmyk/RiscRTE-Drivers':
            raise ValueError('foreign local custody provenance: ' + path)
        commit = command(['git', '-C', str(root), 'rev-parse', '--verify',
                          '--end-of-options', provenance['commit'] + '^{commit}']).strip()
        recorded = command(['git', '-C', str(root), 'rev-parse', '--verify',
                            '--end-of-options', commit + ':' + path]).strip()
        kind_at_origin = command(['git', '-C', str(root), 'cat-file', '-t', recorded]).strip()
        if commit != provenance['commit'] or kind_at_origin != 'tree' or recorded != entry['tree_sha']:
            raise ValueError('local custody provenance tree mismatch: ' + path)
        actual = local_tree(root / path)
        if actual != entry['tree_sha']:
            errors.append('local source custody tree mismatch: ' + path)
        manifest_path = root / path / 'manifest.json'
        if kind == 'support':
            if manifest_path.exists() or any(k in entry for k in ('id', 'version', 'candidate_for')):
                errors.append('local support became a package: ' + path)
        else:
            identity, version = entry.get('id'), entry.get('version')
            if not isinstance(identity, str) or not identity or not isinstance(version, str) or not VERSION.fullmatch(version):
                raise ValueError('invalid local custody identity/version: ' + path)
            if kind == 'candidate':
                if entry.get('candidate_for') != identity or identity not in package_ids:
                    raise ValueError('invalid local candidate lineage: ' + path)
            elif identity in identities or 'candidate_for' in entry:
                raise ValueError('duplicate local adapter identity: ' + identity)
            identities.add(identity)
            manifest = json.loads(manifest_path.read_text())
            if manifest.get('id') != identity or manifest.get('version') != version:
                errors.append('local source custody manifest mismatch: ' + path)
        rows.append({'path': path, 'kind': kind, 'local_tree': actual,
                     'recorded_tree': entry['tree_sha'], 'provenance': provenance,
                     'state': 'local-custody', 'upstream_parity': False,
                     'release_qualified': False})
    return rows, errors


def validate(root, inventory, trees, source, released, head, release, custody=None):
    entries = unique(inventory['drivers'] + inventory.get('source_only_drivers', []), 'local')
    local_released = unique(inventory['drivers'], 'local released')
    errors, rows, paths = [], [], set()
    local_paths = {}
    # A missing upstream identity does not make a declared local package unknown.
    # Register only valid, unique local paths; reconciliation below still fails
    # for every missing upstream or recorded-tree entry.
    for identity, entry in entries.items():
        path = entry.get('source_path', '')
        if not isinstance(path, str) or not re.fullmatch(r'Drivers/[A-Za-z0-9_-]+', path) or path in paths:
            errors.append(f'invalid or duplicate source path: {identity}')
            continue
        paths.add(path)
        local_paths[identity] = path
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
        if identity not in local_paths:
            continue
        path = local_paths[identity]
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
    original = unique(inventory.get('original_drivers', []), 'original')
    for identity, entry in original.items():
        path = entry.get('source_path', '')
        if identity in entries or identity in source or not re.fullmatch(r'Drivers/[A-Za-z0-9_-]+', path) or path in paths:
            errors.append('invalid original driver identity/path: ' + identity)
            continue
        paths.add(path)
        manifest = json.loads((root / path / 'manifest.json').read_text())
        actual = local_tree(root / path)
        if manifest.get('id') != identity or manifest.get('version') != entry['version'] or manifest.get('status') != 'experimental-unpublished' or actual != entry.get('source_tree_sha'):
            errors.append('original driver custody mismatch: ' + identity)
        rows.append({'id':identity,'path':path,'local_tree':actual,'state':'original-experimental','upstream_tree':None})
    custody_rows, custody_errors = validate_custody(root, custody, paths, set(entries) | set(source) | set(original))
    errors.extend(custody_errors)
    paths.update(row['path'] for row in custody_rows)
    for path in (root / 'Drivers').iterdir():
        if (path.is_dir() or path.is_symlink()) and 'Drivers/' + path.name not in paths:
            errors.append('untracked local source directory: ' + path.name)
    return {'source_repository': origin['repository'], 'source_commit': head,
            'release_index_commit': release, 'recorded_source_commit': trees['source_master_sha'],
            'released_count': len(released), 'source_count': len(source), 'original_count': len(original),
            'files_scope': 'actual driver directory bytes and modes; SDK/build/runtime excluded',
            'drivers': rows, 'local_sources': custody_rows, 'errors': errors}


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
            args.source_ref, args.release_ref or origin['branch'], origin['path'])
        custody_path = ROOT / 'manifest/local-source-custody.json'
        custody = json.loads(custody_path.read_text()) if custody_path.exists() else None
        report = validate(ROOT, inventory, trees, source, released, head, release, custody)
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
