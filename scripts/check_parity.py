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
# Historical consumers copy/load this script alone without optional support data.
# Load the exact sibling only when that optional declaration is actually present.
def validate_imported_support(root, declaration, *args):
    if declaration is None:
        return [], []
    import importlib.util
    spec = importlib.util.spec_from_file_location('parity_imported_support', Path(__file__).with_name('imported_support.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.validate_imported_support(root, declaration, *args)


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

    def support_tree(self, commit, path):
        if self.resolve(commit) != commit:
            raise ValueError('imported support commit mismatch')
        if self.reader:
            tree = self.git('rev-parse', '--verify', '--end-of-options', commit+':'+path).strip()
            lines = self.git('ls-tree', '-r', tree).splitlines()
            entries = []
            for line in lines:
                meta, name = line.split('\t', 1)
                mode, kind, sha = meta.split()
                entries.append({'path':name,'mode':mode,'type':kind,'sha':sha})
        else:
            listing = self.api('contents/'+path, commit)
            if not isinstance(listing, list): raise ValueError('imported support is not directory')
            parent = self.api('contents/Drivers', commit)
            matches = [r for r in parent if r.get('path') == path and r.get('type') == 'dir']
            if len(matches) != 1: raise ValueError('missing imported support directory')
            tree = matches[0]['sha']
            result = self.api('git/trees/'+tree+'?recursive=1')
            if result.get('truncated'): raise ValueError('truncated imported support tree')
            entries = [e for e in result['tree'] if e['type'] != 'tree']
        if any(e['type'] != 'blob' or e['mode'] not in ('100644','100755') for e in entries):
            raise ValueError('nonregular upstream support entry')
        return tree, {e['path']:{'mode':e['mode'],'sha':e['sha']} for e in entries}

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


def current_observation(root, inventory, trees, source, released, head, release, custody_rows=(), original_rows=(), custody=None):
    """Separate current source observations from retained release declarations.

    'imported' means exact directory bytes/modes only; 'variant' does not assert
    intent, safety or completion. No observation updates an accepted-byte lock.
    """
    entries = unique(inventory['drivers'] + inventory.get('source_only_drivers', []), 'local')
    historical = unique(inventory['drivers'], 'local released')
    rows = []
    path_counts = {}
    for entry in entries.values():
        path = entry.get('source_path')
        if isinstance(path, str):
            path_counts[path] = path_counts.get(path, 0) + 1
    for identity in sorted(set(entries) | set(source) | set(released) | set(trees['drivers'])):
        local, upstream = entries.get(identity), source.get(identity)
        actual, problem = None, None
        if local:
            path = local.get('source_path', '')
            if not isinstance(path, str) or not re.fullmatch(r'Drivers/[A-Za-z0-9_-]+', path):
                problem = 'invalid local source path'
            elif path_counts[path] != 1:
                problem = 'duplicate local source path'
            else:
                try:
                    actual = local_tree(root / path)
                except (OSError, ValueError) as exc:
                    problem = str(exc)
        if problem:
            status = 'unresolved'
        elif not local or actual is None:
            status = 'missing_local' if upstream else 'unresolved'
        elif not upstream:
            status = 'upstream_absent'
        elif actual == upstream['tree_sha'] and local.get('source_path') == upstream.get('source_path'):
            status = 'imported'
        else:
            status = 'variant'
        rows.append({'id': identity, 'status': status,
                     'local_declaration': local, 'local_tree': actual,
                     'upstream_source': upstream,
                     'historical_release_declaration': historical.get(identity),
                     'observed_upstream_release': released.get(identity),
                     'recorded_baseline_tree': trees['drivers'].get(identity),
                     'directory_drift_resolved': status == 'imported',
                     'path_matches_upstream': local.get('source_path') == upstream.get('source_path') if local and upstream else None,
                     'bytes_match_upstream': actual == upstream['tree_sha'] if actual and upstream else None,
                     'migration_qualification': 'unknown; SDK, ABI, build and behavior not qualified here',
                     'intent_and_behavior': 'not established by tree comparison',
                     'acquisition_error': problem})
    locks = {}
    for name in ('released-drivers.json', 'source-trees.json'):
        path = root / 'manifest' / name
        locks[name] = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
    return {'schema': 1, 'source_repository': inventory['parity_source']['repository'],
            'source_commit': head, 'release_index_commit': release,
            'historical_declaration_sha256': locks,
            'scope': 'source observation, not accepted release replay or behavioral qualification',
            'rows': rows,
            'local_custody_observations': list(custody_rows),
            'local_custody_declarations': custody,
            'original_source_declarations': inventory.get('original_drivers', []),
            'original_source_observations': list(original_rows)}


def verify_observation(expected, actual):
    """An optional exact frozen observation is a freshness check, never a waiver."""
    if expected != actual:
        return ['current source observation differs from frozen observation (pins, bytes or rows)']
    return []


def pinned_file(root, path, sha256):
    """Read a repository-relative, non-symlink evidence file at an exact digest."""
    if not isinstance(path, str) or not path or path.startswith('/') or any(p in ('', '.', '..') for p in path.split('/')):
        raise ValueError('invalid qualification evidence path')
    if not isinstance(sha256, str) or not re.fullmatch(r'[0-9a-f]{64}', sha256):
        raise ValueError('invalid qualification evidence digest')
    target = root
    for part in path.split('/'):
        target = target / part
        if target.is_symlink():
            raise ValueError('symlink qualification evidence: ' + path)
    data = target.read_bytes()
    if hashlib.sha256(data).hexdigest() != sha256:
        raise ValueError('qualification evidence changed: ' + path)
    return data


def qualify_sources(root, report, qualifications, inventory, released):
    """Bind reviewed build evidence; resolve only historical/current metadata scope.

    Neither local/upstream byte equality nor behavioral equivalence is inferred.
    Hashes bind documentary evidence; they do not authenticate its author or CI.
    """
    report['legacy_diagnostics'] = list(report['errors'])
    report['source_qualification_declarations'] = qualifications
    report['metadata_resolutions'] = []
    report['qualification_errors'] = []
    if not isinstance(qualifications, dict) or qualifications.get('schema') != 1 or not isinstance(qualifications.get('sources'), list):
        report['qualification_errors'].append('invalid current source qualification schema')
        report['errors'].extend(report['qualification_errors'])
        return
    rows = report['current_source_observation']['rows']
    by_id = {r['id']: r for r in rows}
    counts = {}
    for item in qualifications['sources']:
        identity = item.get('id') if isinstance(item, dict) else None
        if isinstance(identity, str):
            counts[identity] = counts.get(identity, 0) + 1
    for item in qualifications['sources']:
        identity = item.get('id') if isinstance(item, dict) else None
        try:
            if not isinstance(identity, str) or counts[identity] != 1 or identity not in by_id:
                raise ValueError('unknown or duplicate qualification identity')
            row = by_id[identity]
            local, upstream = row['local_declaration'], row['upstream_source']
            if row['acquisition_error'] or not local or not upstream:
                raise ValueError('unresolved source acquisition')
            if item['path'] != local['source_path'] or item['tree_sha'] != row['local_tree']:
                raise ValueError('qualified local path/tree changed')
            if item['release_index_commit'] != report['release_index_commit']:
                raise ValueError('qualified release-index observation changed')
            if item['upstream'] != {'repository': report['source_repository'], 'commit': report['source_commit'],
                                    'path': upstream['source_path'], 'version': upstream['version'], 'tree_sha': upstream['tree_sha']}:
                raise ValueError('qualified upstream observation changed')
            if not isinstance(item['attribution'], str) or not item['attribution'].strip() or not isinstance(item['rationale'], str) or not item['rationale'].strip() or not isinstance(item['limits'], list) or not item['limits'] or not all(isinstance(v, str) and v.strip() for v in item['limits']):
                raise ValueError('qualification attribution/rationale/limits missing')
            evidence = {name: json.loads(pinned_file(root, item['evidence'][name]['path'], item['evidence'][name]['sha256']))
                        for name in ('current_build', 'current_manifest', 'historical_replay', 'historical_manifest')}
            if not all(isinstance(value, dict) for value in evidence.values()):
                raise ValueError('qualification evidence documents must be JSON objects')
            current, artifact = evidence['current_build'], evidence['current_manifest']
            history, old_artifact = evidence['historical_replay'], evidence['historical_manifest']
            source_dir = root / item['path']
            actual_manifest = json.loads((source_dir / 'manifest.json').read_text())
            if actual_manifest['id'] != identity or actual_manifest['version'] != item['version'] or item['version'] != upstream['version']:
                raise ValueError('current source identity/version mismatch')
            actual_files = {str(p.relative_to(source_dir)): hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in source_dir.rglob('*') if p.is_file()}
            if current['mode'] != 'current-source' or current['expected_version'] != item['version'] or current['source_files'] != actual_files:
                raise ValueError('current build does not bind complete current source')
            # Current build inputs changing invalidates that qualification even
            # when the driver directory itself is unchanged.
            if not isinstance(current['build_inputs'], dict) or not current['build_inputs']:
                raise ValueError('current build input pins missing')
            for path, digest in current['build_inputs'].items():
                pinned_file(root, path, digest)
            if {k: v for k, v in artifact.items() if k not in ('sha256', 'size_bytes')} != actual_manifest:
                raise ValueError('current artifact/source manifest mismatch')
            for receipt, manifest in ((current, artifact), (history, old_artifact)):
                compiler_pins = (receipt['compiler_sha256'], receipt['libgcc']['sha256'], receipt['readelf']['sha256'])
                if not all(isinstance(v, str) and re.fullmatch(r'[0-9a-f]{64}', v) for v in compiler_pins):
                    raise ValueError('compiler/archive/inspection tool pins missing')
                if not isinstance(receipt['compiler_version'], str) or not receipt['compiler_version'].strip() or not isinstance(receipt['command'], list) or not receipt['command'] or not all(isinstance(v, str) for v in receipt['command']):
                    raise ValueError('build execution settings missing')
                if receipt['actual_sha256'] != manifest['sha256'] or receipt['actual_size'] != manifest['size_bytes']:
                    raise ValueError('artifact/build receipt mismatch')
                if not isinstance(receipt['actual_sha256'], str) or not re.fullmatch(r'[0-9a-f]{64}', receipt['actual_sha256']) or type(receipt['actual_size']) is not int or receipt['actual_size'] <= 0:
                    raise ValueError('invalid artifact identity')
            historical = row['historical_release_declaration']
            accepted = released.get(identity)
            if not historical or not accepted or item['historical']['version'] != historical['version'] or accepted['version'] != historical['version']:
                raise ValueError('historical release scope mismatch')
            if item['historical'] != {'version': historical['version'], 'source_tree': row['recorded_baseline_tree'],
                                      'sha256': accepted['sha256'], 'size': accepted['size']}:
                raise ValueError('historical accepted identity changed')
            if history['mode'] != 'historical' or history['source_tree'] != row['recorded_baseline_tree'] or history['expected_version'] != historical['version'] or history['byte_parity'] is not True:
                raise ValueError('historical replay not established')
            if (history['actual_sha256'], history['actual_size']) != (accepted['sha256'], accepted['size']):
                raise ValueError('historical replay bytes differ from accepted release')
            if (history['accepted_historical_sha256'], history['accepted_historical_size']) != (accepted['sha256'], accepted['size']):
                raise ValueError('historical replay used a different lock')
            old_files = history['source_files']
            if not isinstance(old_files, dict) or 'manifest.json' not in old_files or not SHA.fullmatch(str(row['recorded_baseline_tree'])):
                raise ValueError('historical source pins missing')
            historical_paths = subprocess.check_output(['git', '-C', str(root), 'ls-tree', '-r', '--name-only', row['recorded_baseline_tree']], timeout=60).decode().splitlines()
            if set(historical_paths) != set(old_files):
                raise ValueError('historical source receipt coverage incomplete')
            for path, digest in old_files.items():
                if not isinstance(path, str) or path.startswith('/') or any(p in ('', '.', '..') for p in path.split('/')):
                    raise ValueError('invalid historical source path')
                data = subprocess.check_output(['git', '-C', str(root), 'show', row['recorded_baseline_tree'] + ':' + path], timeout=60)
                if hashlib.sha256(data).hexdigest() != digest:
                    raise ValueError('historical source receipt mismatch')
            old_source_manifest = json.loads(subprocess.check_output(['git', '-C', str(root), 'show', row['recorded_baseline_tree'] + ':manifest.json'], timeout=60))
            if not isinstance(old_source_manifest, dict):
                raise ValueError('historical source manifest must be a JSON object')
            if old_source_manifest.get('id') != identity or old_source_manifest.get('version') != historical['version']:
                raise ValueError('historical source identity/version mismatch')
            if {k: v for k, v in old_artifact.items() if k not in ('sha256', 'size_bytes')} != old_source_manifest:
                raise ValueError('historical artifact/source manifest mismatch')
            row['current_source_qualification'] = {'status': 'source-and-build-evidence-bound', 'declaration': item,
                'evidence': evidence, 'behavioral_equivalence': 'not established',
                'authentication': 'local bindings checked; external CI execution requires retained independent provenance review'}
            row['migration_qualification'] = 'current source/build and historical replay evidence bound; conflict and behavioral limits retained'
            # Retain the old tree as historical, not a stale current baseline.
            for diagnostic in ('source version mismatch: ' + identity, identity + ': recorded source tree needs refresh'):
                if diagnostic in report['errors']:
                    report['errors'].remove(diagnostic)
                    report['metadata_resolutions'].append({'diagnostic': diagnostic, 'id': identity,
                        'scope': 'qualified current source distinguished from immutable historical release/source baseline',
                        'qualification': item})
        except (KeyError, TypeError, ValueError, OSError, subprocess.SubprocessError) as exc:
            report['qualification_errors'].append('current source qualification failed for ' + str(identity) + ': ' + str(exc))
    report['errors'].extend(report['qualification_errors'])


def validate(root, inventory, trees, source, released, head, release, custody=None, qualifications=None, imported_support=None, support_source=None):
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
    imported_rows, imported_errors = validate_imported_support(root, imported_support, paths,
        origin['repository'], support_source, local_tree, object_hash)
    errors.extend(imported_errors)
    paths.update(row['path'] for row in imported_rows)
    for path in (root / 'Drivers').iterdir():
        if (path.is_dir() or path.is_symlink()) and 'Drivers/' + path.name not in paths:
            errors.append('untracked local source directory: ' + path.name)
    report = {'source_repository': origin['repository'], 'source_commit': head,
            'release_index_commit': release, 'recorded_source_commit': trees['source_master_sha'],
            'released_count': len(released), 'source_count': len(source), 'original_count': len(original),
            'files_scope': 'actual driver directory bytes and modes; SDK/build/runtime excluded',
            'drivers': rows, 'local_sources': custody_rows, 'imported_support': imported_rows,
            'imported_support_declaration': imported_support, 'errors': errors,
            'current_source_observation': current_observation(root, inventory, trees, source, released, head, release,
                custody_rows, [row for row in rows if row.get('state') == 'original-experimental'], custody)}
    if imported_support is not None:
        report['current_source_observation']['imported_support'] = imported_rows
        report['current_source_observation']['imported_support_declaration'] = imported_support
        report['current_source_observation']['imported_support_errors'] = imported_errors
    if qualifications is not None:
        qualify_sources(root, report, qualifications, inventory, released)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reader', type=Path, help='Optional read-only local Reader Git checkout')
    parser.add_argument('--source-ref', default='master')
    parser.add_argument('--release-ref', help='Defaults to recorded release-index branch')
    parser.add_argument('--output', type=Path, help='Optional provenance/drift JSON report')
    parser.add_argument('--expected-observation', type=Path,
                        help='Compare an exact prior current_source_observation; never suppress drift errors')
    args = parser.parse_args()
    inventory = json.loads((ROOT / 'manifest/released-drivers.json').read_text())
    trees = json.loads((ROOT / 'manifest/source-trees.json').read_text())
    origin = inventory['parity_source']
    try:
        head, release, source, released = snapshot(Source(origin['repository'], args.reader),
            args.source_ref, args.release_ref or origin['branch'], origin['path'])
        custody_path = ROOT / 'manifest/local-source-custody.json'
        custody = json.loads(custody_path.read_text()) if custody_path.exists() else None
        qualification_path = ROOT / 'qualification/current-sources.json'
        if qualification_path.is_symlink():
            raise ValueError('symlink current source qualification file')
        qualifications = json.loads(qualification_path.read_text()) if qualification_path.exists() else None
        if qualification_path.exists() and qualifications is None:
            raise ValueError('invalid current source qualification schema')
        imported_path = ROOT / 'qualification/imported-support.json'
        imported = json.loads(imported_path.read_text()) if imported_path.exists() else None
        report = validate(ROOT, inventory, trees, source, released, head, release, custody, qualifications, imported, Source(origin['repository'], args.reader))
        if args.expected_observation:
            expected = json.loads(args.expected_observation.read_text())
            report['errors'].extend(verify_observation(expected, report['current_source_observation']))
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
