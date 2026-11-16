"""Verify and import an owning repository's published exact Git objects."""
import hashlib
import json
import re
import subprocess
from pathlib import Path


def restore(repository, directory, expected_repository, run=subprocess.run,
            output=subprocess.check_output):
    directory = Path(directory)
    manifest = json.loads((directory / 'manifest.json').read_text())
    if manifest['schema'] != 1 or manifest['repository'] != expected_repository:
        raise ValueError('Recovery repository identity mismatch')
    for name in ('source_commit', 'source_tree'):
        if not re.fullmatch('[0-9a-f]{40}', manifest[name]):
            raise ValueError('Invalid recovery object identity')
    pack = bytearray()
    seen = set()
    for part in manifest['parts']:
        name = part['path']
        if not re.fullmatch(r'objects-[0-9]{3}\.packpart', name) or name in seen:
            raise ValueError('Invalid or duplicate recovery part')
        seen.add(name)
        data = (directory / name).read_bytes()
        if len(data) != part['bytes'] or hashlib.sha256(data).hexdigest() != part['sha256']:
            raise ValueError('Recovery part integrity mismatch: ' + name)
        pack.extend(data)
    if hashlib.sha256(pack).hexdigest() != manifest['pack_sha256']:
        raise ValueError('Combined recovery pack integrity mismatch')
    # The Git pack header records the number of objects, before any import.
    if pack[:4] != b'PACK' or int.from_bytes(pack[8:12], 'big') != manifest['object_count']:
        raise ValueError('Recovery pack object count mismatch')
    boundaries = manifest['shallow_boundaries']
    if len(set(boundaries)) != len(boundaries) or any(
            not re.fullmatch('[0-9a-f]{40}', pin) for pin in boundaries):
        raise ValueError('Invalid shallow boundaries')
    run(['git', '-C', str(repository), 'index-pack', '--stdin'], input=pack, check=True)
    actual = output(['git', '-C', str(repository), 'rev-parse',
                     manifest['source_commit'] + '^{tree}'], text=True).strip()
    if actual != manifest['source_tree']:
        raise ValueError('Restored source tree mismatch')
    for pin in boundaries:
        run(['git', '-C', str(repository), 'cat-file', '-e', pin + '^{commit}'], check=True)
    shallow = Path(output(['git', '-C', str(repository), 'rev-parse',
                          '--path-format=absolute', '--git-path', 'shallow'], text=True).strip())
    existing = set(shallow.read_text().splitlines()) if shallow.exists() else set()
    shallow.write_text(''.join(pin + '\n' for pin in sorted(existing | set(boundaries))))
    run(['git', '-C', str(repository), 'fsck', '--connectivity-only', '--no-dangling',
         manifest['source_commit']], check=True)
    return {key: manifest[key] for key in ('repository', 'source_commit', 'source_tree',
                                          'object_count', 'pack_sha256', 'shallow_boundaries')}
