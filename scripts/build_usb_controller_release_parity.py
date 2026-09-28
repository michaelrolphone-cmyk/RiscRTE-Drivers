#!/usr/bin/env python3
"""Canonical usb-controller build entry point.

The historical release builder generated two macro-body lines with four-space
indentation. Keep that source layout exact before invoking the migrated builder
because GCC emits source-column information into the unstripped release ELF.
"""
import build_usb_controller_esp32s3 as builder
from normalize_usb_controller_instrumentation import normalize

_original_instrument_hub = builder.instrument_hub

def _canonical_instrument_hub(source: str) -> str:
    return normalize(_original_instrument_hub(source))

builder.instrument_hub = _canonical_instrument_hub

if __name__ == "__main__":
    builder.main()
