"""Exact imported support subsets; not package, release, or behavior equivalence."""
import re
import stat
from pathlib import PurePosixPath

SHA = re.compile(r'[0-9a-f]{40}\Z')

def safe_relative(value):
    return isinstance(value, str) and value != '' and all(p not in ('', '.', '..') for p in value.split('/')) and not PurePosixPath(value).is_absolute() and '\\' not in value

def validate_imported_support(root, declaration, occupied, repository, source, local_tree, object_hash):
    if declaration is None:
        return [], []
    if not isinstance(declaration, dict) or declaration.get('schema') != 1 or not isinstance(declaration.get('sources'), list):
        raise ValueError('invalid imported support schema')
    if source is None: raise ValueError('imported support requires exact provenance reader')
    rows, errors, seen = [], [], set(occupied)
    for entry in declaration['sources']:
        try:
            if not isinstance(entry, dict): raise ValueError('invalid record')
            path = entry.get('path')
            if not isinstance(path, str) or not re.fullmatch(r'Drivers/[A-Za-z0-9_-]+', path) or path in seen: raise ValueError('invalid or occupied path')
            seen.add(path)
            origin = entry.get('provenance')
            if not isinstance(origin, dict) or origin.get('repository') != repository or origin.get('path') != path or not isinstance(origin.get('commit'), str) or not SHA.fullmatch(origin['commit']): raise ValueError('invalid provenance')
            for key in ('tree_sha', 'upstream_tree'):
                if not isinstance(entry.get(key), str) or not SHA.fullmatch(entry[key]): raise ValueError('invalid tree pin')
            selected = entry.get('files')
            if not isinstance(selected, list) or not selected: raise ValueError('empty or invalid selected files')
            wanted = {}
            for item in selected:
                if not isinstance(item, dict) or not safe_relative(item.get('path')) or item['path'] in wanted or item.get('mode') not in ('100644','100755') or not isinstance(item.get('sha'), str) or not SHA.fullmatch(item['sha']): raise ValueError('invalid selected file')
                wanted[item['path']] = {'mode': item['mode'], 'sha': item['sha']}
            tree, upstream = source.support_tree(origin['commit'], path)
            if tree != entry['upstream_tree']: raise ValueError('upstream tree mismatch')
            if 'manifest.json' in upstream: raise ValueError('support became package')
            for name, pin in wanted.items():
                if upstream.get(name) != pin: raise ValueError('selected upstream file mismatch: '+name)
            actual_tree = local_tree(root/path)
            actual = {}
            for file in (root/path).rglob('*'):
                mode = file.lstat().st_mode
                if stat.S_ISREG(mode): actual[file.relative_to(root/path).as_posix()] = {'mode':'100755' if mode & 0o111 else '100644', 'sha':object_hash('blob',file.read_bytes())}
                elif not stat.S_ISDIR(mode): raise ValueError('nonregular local entry')
            if actual != wanted or actual_tree != entry['tree_sha']: raise ValueError('local subset mismatch')
            rows.append({'path':path,'state':'imported-support-subset','local_tree':actual_tree,'upstream_tree':tree,'provenance':origin,'selected_files':selected,'omitted_upstream_files':sorted(set(upstream)-set(wanted)),'whole_directory_equal':actual_tree == tree,'release_qualified':False,'behavioral_equivalence':'unknown'})
        except (ValueError, KeyError, OSError) as exc:
            errors.append('imported support rejected: '+str(entry.get('path') if isinstance(entry,dict) else None)+': '+str(exc))
    return rows, errors
