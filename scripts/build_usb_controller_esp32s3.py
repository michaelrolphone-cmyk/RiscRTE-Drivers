#!/usr/bin/env python3
"""Current usb-controller-esp32s3 build entry point.

The stable build implementation is preserved in
build_usb_controller_esp32s3_v018_impl.py. Version-specific release identity
and canonical artifact metadata are applied here so the same audited build
mechanism can validate the synchronized v0.1.19 source.
"""
import build_usb_controller_esp32s3_v018_impl as _impl

CANONICAL_SIZE = 789504
CANONICAL_SHA256 = "18c95f4264dfff2b21af13b0f0366327be896c75a0ae2d4fc1c9b0a220424da6"
_impl.CANONICAL_SIZE = CANONICAL_SIZE
_impl.CANONICAL_SHA256 = CANONICAL_SHA256
_impl.EXPECTED = dict(_impl.EXPECTED)
_impl.EXPECTED["version"] = "0.1.19"

EXPECTED = _impl.EXPECTED
audit = _impl.audit
main = _impl.main

if __name__ == "__main__":
    main()
