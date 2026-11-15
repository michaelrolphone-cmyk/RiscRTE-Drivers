"""Stage a single header root: current Runtime ABI plus Drivers capability SDK.

The source trees use quoted adjacent includes and #pragma once. Mixing both
sdk/driver search paths loads duplicate provider ABI declarations. This staging
copies existing files unchanged, with Runtime winning shared names; it defines
no ABI and never edits either SDK tree.
"""
from pathlib import Path
import shutil


def stage_sdk(drivers: Path, runtime: Path, destination: Path) -> Path:
    for relative in ('sdk/hardware/RiscTouchI2cV2.h', 'sdk/driver/GardenPlatformV1.h',
                     'sdk/driver/RiscProviderSyncV1.h'):
        if not (runtime / relative).is_file():
            raise ValueError('Required current Runtime header missing: ' + str(runtime / relative))
    destination.mkdir(parents=True, exist_ok=True)
    for source in (drivers / 'sdk/driver', runtime / 'sdk/app', runtime / 'sdk/driver', runtime / 'sdk/hardware'):
        for header in source.glob('*.h'):
            shutil.copyfile(header, destination / header.name)
    return destination


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--drivers', required=True, type=Path)
    p.add_argument('--runtime', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    stage_sdk(a.drivers.resolve(), a.runtime.resolve(), a.output.resolve())
