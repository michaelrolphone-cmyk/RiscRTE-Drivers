#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all); fi
"${CC:-cc}" "${flags[@]}" -I"$root/sdk/driver" -I"$root/test" "$root/test/s3_radio_iq_lifecycle_test.c" -o "$build/lifecycle"
"$build/lifecycle"
"${CC:-cc}" "${flags[@]}" "$root/test/s3_radio_iq_plan_test.c" -o "$build/plan"
"$build/plan"
python3 - "$root" <<'PY'
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
sys.path.insert(0, str(root / 'scripts'))
from check_parity import local_tree

manifest = json.loads((root / 'Drivers/s3_radio_iq_v1/manifest.json').read_text())
inventory = json.loads((root / 'manifest/released-drivers.json').read_text())
entries = [entry for entry in inventory['original_drivers'] if entry['id'] == manifest['id']]
assert len(entries) == 1
entry = entries[0]
assert entry['version'] == manifest['version']
assert entry['source_path'] == 'Drivers/s3_radio_iq_v1'
assert entry['source_tree_sha'] == local_tree(root / entry['source_path'])
assert manifest['status'] == 'experimental-unpublished'
print('IQ source custody: manifest version and complete driver tree match')
PY
