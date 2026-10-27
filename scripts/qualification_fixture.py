#!/usr/bin/env python3
"""Materialize/preflight a hash-locked test overlay; never alter firmware sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RECEIPT = 'QUALIFICATION-FIXTURE.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative(value):
    p = PurePosixPath(value)
    if p.is_absolute() or not p.parts or any(x in ('..', '.') for x in p.parts) or str(p) != value:
        raise ValueError('Unsafe profile path: ' + value)
    return p


def profile_load(directory):
    directory = Path(directory).resolve()
    manifest = directory/'profile.json'
    if manifest.is_symlink():
        raise ValueError('Profile manifest cannot be a link')
    d = json.loads(manifest.read_text())
    if d['schema'] != 1 or not d['files']:
        raise ValueError('Unsupported/empty profile')
    files = {}
    for item in d['files']:
        name = str(relative(item['path']))
        p = directory/name
        if name in files or p.is_symlink() or not p.is_file() or not p.resolve().is_relative_to(directory):
            raise ValueError('Missing/unsafe/duplicate observer: ' + name)
        if sha(p) != item['sha256']:
            raise ValueError('Observer hash mismatch: ' + name)
        files[name] = item['sha256']
    if d['runner'] not in files:
        raise ValueError('Runner must be a locked observer')
    return d, files, sha(manifest)


def git(source, *args):
    return subprocess.check_output(['git', '-C', str(source), *args], text=True).strip()


def source_state(source, revision):
    source = Path(source).resolve()
    if len(revision) != 40 or any(c not in '0123456789abcdef' for c in revision):
        raise ValueError('An exact source revision is required')
    if git(source, 'rev-parse', '--show-toplevel') != str(source) or git(source, 'rev-parse', 'HEAD') != revision:
        raise ValueError('Selected shared source identity mismatch')
    if git(source, 'status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Selected shared source is dirty')
    paths = git(source, 'ls-files').splitlines()
    for name in paths:
        relative(name)
        p = source/name
        if not p.is_file() or p.is_symlink() or not p.resolve().is_relative_to(source):
            raise ValueError('Missing or unsafe shared source: ' + name)
    return source, paths, git(source, 'rev-parse', 'HEAD^{tree}')


def layout(paths, observers):
    # Match the existing overlay: real scripts/tests directories, shared other roots.
    branches = {PurePosixPath(p).parts[0] for p in observers}
    dirs = set(branches)
    links = set()
    for name in paths:
        if name in observers:
            continue
        p = PurePosixPath(name)
        if p.parts[0] not in branches:
            links.add(p.parts[0])
        else:
            # Individual shared files avoid exposing a directory that contains overrides.
            links.add(name)
            dirs.update(str(parent) for parent in p.parents if str(parent) != '.')
    for name in observers:
        dirs.update(str(parent) for parent in PurePosixPath(name).parents if str(parent) != '.')
    return sorted(dirs), sorted(links)


def output_path(output):
    p = Path(os.path.abspath(output))
    if any(x.is_symlink() for x in (p, *p.parents)):
        raise ValueError('Output or ancestor is a symlink')
    return p


def verify(output, expected=None):
    output = output_path(output)
    receipt = output/RECEIPT
    if receipt.is_symlink() or not receipt.is_file():
        raise ValueError('Existing output lacks a regular provenance receipt')
    d = json.loads(receipt.read_text())
    if d.get('schema') != 1 or (expected is not None and d != expected):
        raise ValueError('Fixture provenance mismatch; explicit rebind required')
    expected_names = {RECEIPT, *d['directories'], *d['links'], *d['observers']}
    found = set()
    for root, dirs, files in os.walk(output, followlinks=False):
        for name in dirs + files:
            p = Path(root)/name
            found.add(p.relative_to(output).as_posix())
    if found != expected_names:
        raise ValueError('Fixture has missing or unexpected entries')
    source = Path(d['shared_source']['path'])
    if not source.is_absolute() or source.resolve() != source:
        raise ValueError('Invalid shared source provenance')
    if git(source, 'rev-parse', '--show-toplevel') != str(source):
        raise ValueError('Prior shared source is not a repository root')
    prior = d['shared_source']['commit']
    if len(prior) != 40 or any(c not in '0123456789abcdef' for c in prior):
        raise ValueError('Invalid prior source revision')
    if git(source, 'rev-parse', prior+'^{tree}') != d['shared_source']['tree']:
        raise ValueError('Prior shared tree identity mismatch')
    prior_paths = git(source, 'ls-tree', '-r', '--name-only', prior).splitlines()
    prior_dirs, prior_links = layout(prior_paths, d['observers'])
    if prior_dirs != d['directories'] or prior_links != d['links']:
        raise ValueError('Shared layout does not match its pinned source tree')
    for name in d['directories']:
        relative(name)
        p = output/name
        if p.is_symlink() or not p.is_dir():
            raise ValueError('Unsafe overlay directory: ' + name)
    for name in d['links']:
        relative(name)
        p = output/name
        if not p.is_symlink() or os.readlink(p) != str(source/name):
            raise ValueError('Unsafe/rebound shared link: ' + name)
    for name, digest in d['observers'].items():
        relative(name)
        p = output/name
        if p.is_symlink() or not p.is_file() or sha(p) != digest:
            raise ValueError('Observer missing/tampered: ' + name)
    return d


def setup(profile, source, revision, output, rebind=False, check=False):
    profile = Path(profile).resolve()
    d, observers, manifest_hash = profile_load(profile)
    source, paths, tree = source_state(source, revision)
    output = output_path(output)
    if output == source or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('Fixture output must be separate from selected source')
    dirs, links = layout(paths, observers)
    expected = {'schema': 1, 'profile': d['profile'], 'profile_sha256': manifest_hash,
                'runner': d['runner'], 'observers': observers,
                'shared_source': {'path': str(source), 'commit': revision, 'tree': tree},
                'directories': dirs, 'links': links}
    if output.exists():
        old = verify(output)
        if old == expected:
            return verify(output, expected)
        if check or not rebind or old['observers'] != observers or old['profile_sha256'] != manifest_hash:
            raise ValueError('Different source/profile; verified explicit rebind required')
    elif check:
        raise ValueError('Required fixture has not been materialized')
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=output.name+'.setup-', dir=output.parent))
    backup = None
    try:
        for name in dirs:
            (temporary/name).mkdir(parents=True, exist_ok=True)
        for name in links:
            (temporary/name).symlink_to(source/name)
        for name in observers:
            shutil.copyfile(profile/name, temporary/name)
        (temporary/RECEIPT).write_text(json.dumps(expected, indent=2)+'\n')
        verify(temporary, expected)
        # No lane is started until the complete new overlay has passed verification.
        if output.exists():
            backup = Path(tempfile.mkdtemp(prefix=output.name+'.prior-', dir=output.parent))
            backup.rmdir()
            output.rename(backup)
        temporary.rename(output)
        verify(output, expected)
        if backup is not None:
            shutil.rmtree(backup)
        return expected
    except BaseException:
        if backup is not None and backup.exists() and not output.exists():
            backup.rename(output)
        raise
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--profile', choices=['local-quick'], required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--source-revision', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--rebind', action='store_true')
    p.add_argument('--check', action='store_true', help='Preflight only; missing fixtures fail')
    p.add_argument('--run', action='store_true', help='Preflight then run the locked observer runner')
    p.add_argument('runner_args', nargs=argparse.REMAINDER)
    a = p.parse_args()
    try:
        selected_profile = ROOT/'tests/qualification_profiles'/a.profile
        manifest = selected_profile/'profile.json'
        committed = subprocess.check_output(['git', '-C', str(ROOT), 'show',
            'HEAD:'+manifest.relative_to(ROOT).as_posix()])
        if manifest.read_bytes() != committed:
            raise ValueError('Profile manifest differs from the tooling commit')
        d = setup(selected_profile, a.source, a.source_revision,
                  a.output, a.rebind, a.check)
        print(json.dumps(d, sort_keys=True), flush=True)
        if a.runner_args and not a.run:
            p.error('Runner arguments require --run')
        if a.run:
            args = a.runner_args[1:] if a.runner_args[:1] == ['--'] else a.runner_args
            return subprocess.call([sys.executable, '-B', str(output_path(a.output)/d['runner']), *args],
                                   env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        return 0
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        p.error(str(error))


if __name__ == '__main__':
    sys.exit(main())
