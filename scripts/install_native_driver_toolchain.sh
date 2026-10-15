#!/usr/bin/env bash
# ESP-IDF 6.1 pin: fff9895c82d744c7237be8847347bdd1b07c6643/tools/tools.json.
# Installs only the native compiler, with no PlatformIO or Arduino framework.
set -euo pipefail
if [[ "$(uname -s):$(uname -m)" != Linux:x86_64 ]]; then
  echo 'This CI installer supports Linux x86_64; use ESP-IDF install/export on other hosts.' >&2
  exit 1
fi
version=esp-15.2.0_20251204
archive=xtensa-esp-elf-15.2.0_20251204-x86_64-linux-gnu.tar.xz
sha256=3d50f5cd5f173acfd524e07c1cd69bc99585731a415ca2e5bce879997fe602b8
dest="${1:-${RUNNER_TEMP:-/tmp}/riscrte-native-toolchain/$version}"
mkdir -p "$dest"
archive_path="$dest/$archive"
curl --fail --location --retry 2 --max-time 300 \
  "https://github.com/espressif/crosstool-NG/releases/download/$version/$archive" -o "$archive_path"
printf '%s  %s\n' "$sha256" "$archive_path" | sha256sum --check
# Use the archive's matching S3 frontend: bare unified GCC targets generic Xtensa.
tar -xJf "$archive_path" -C "$dest"
bin="$(cd "$dest/xtensa-esp-elf/bin" && pwd)"
"$bin/xtensa-esp32s3-elf-gcc" --version | head -1
if [[ -n "${GITHUB_PATH:-}" ]]; then
  printf '%s\n' "$bin" >> "$GITHUB_PATH"
fi
printf 'export PATH="%s:$PATH"\n' "$bin"
