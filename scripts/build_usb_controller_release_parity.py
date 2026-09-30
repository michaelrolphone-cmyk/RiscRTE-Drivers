#!/usr/bin/env python3
"""Canonical replay entry point for usb-controller-esp32s3 v0.1.19.

The proven historical-workspace replay implementation is preserved in
build_usb_controller_release_parity_v018_impl.py. This wrapper supplies the
current release commit and canonical artifact identity without changing that
replay/audit mechanism.
"""
import build_usb_controller_release_parity_v018_impl as _impl

_impl.HISTORICAL_COMMIT = "491e06131a8e9dc39c7b7dbb9e4b3e7fc128b3a7"
_impl.CANONICAL_SIZE = 789504
_impl.CANONICAL_SHA256 = "18c95f4264dfff2b21af13b0f0366327be896c75a0ae2d4fc1c9b0a220424da6"

main = _impl.main

if __name__ == "__main__":
    main()
