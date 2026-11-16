"""Canonical schema-2 resource paths shared by packaging and index checks."""
import re

_COMPONENT = re.compile(r'[a-z0-9](?:[a-z0-9_.-]*[a-z0-9])?\Z')
MAX_DEPTH = 8
MAX_PATH_BYTES = 127


def safe_resource_path(value):
    if not isinstance(value, str) or not 0 < len(value) <= MAX_PATH_BYTES:
        return False
    parts = value.split('/')
    return len(parts) <= MAX_DEPTH and all(
        _COMPONENT.fullmatch(part) and '..' not in part for part in parts)


def parent_paths(value):
    parts = value.split('/')
    return {'/'.join(parts[:depth]) for depth in range(1, len(parts))}


def path_conflicts(value, existing):
    return any(value == old or value.startswith(old + '/') or
               old.startswith(value + '/') for old in existing)


def validate_resource_imports(value):
    """Bounded ordered requests; the runtime manager independently grants access."""
    if not isinstance(value, list) or len(value) > 4:
        raise ValueError('resource imports exceed four requests')
    ids = set()
    for item in value:
        if not isinstance(item, dict) or set(item) != {'id', 'min_version'}:
            raise ValueError('resource import requires exact id/min_version fields')
        identity, version = item['id'], item['min_version']
        if (not isinstance(identity, str) or not 0 < len(identity) < 64 or
                not re.fullmatch(r'[a-z0-9](?:[a-z0-9_-]*[a-z0-9])?', identity) or identity in ids):
            raise ValueError('invalid or duplicate resource import identity')
        if (not isinstance(version, str) or len(version) >= 32 or
                not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version) or
                any(int(part) > 0xffffffff for part in version.split('.'))):
            raise ValueError('invalid resource import minimum version')
        ids.add(identity)
    return value
