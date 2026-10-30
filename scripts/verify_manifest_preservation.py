"""Shared historical manifest check using the approved custody validator.

Callers retain their own released-inventory byte locks and other prefix checks.
The helper validates the single additive custody file before excluding it.
"""
import argparse
from pathlib import Path
from verify_usb_hid_preservation import verify_manifest

ROOT = Path(__file__).resolve().parents[1]

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True)
    args = parser.parse_args(argv)
    verify_manifest(ROOT, args.baseline)

if __name__ == '__main__':
    main()
