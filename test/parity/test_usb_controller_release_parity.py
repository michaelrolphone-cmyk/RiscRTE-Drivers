"""Compiler-free contracts for the separate historical controller replay."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import build_usb_controller_release_parity as entry
import check_parity

replay = entry._impl
DRIVERS_COMMIT = "179979b94cc5a1d4787c0ad99515c5c07edd92d8"
CONTROLLER_TREE = "15c27125c163a50e1d41d7e9b15194ea03680488"
READER_COMMIT = "491e06131a8e9dc39c7b7dbb9e4b3e7fc128b3a7"
CANONICAL_SHA256 = "18c95f4264dfff2b21af13b0f0366327be896c75a0ae2d4fc1c9b0a220424da6"
LEGACY_SOURCE = os.environ.get("HISTORICAL_CONTROLLER_SOURCE")


class HistoricalControllerContracts(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.driver = root / "producer"
        self.workspace = root / "historical"
        self.historical = self.workspace / "Drivers/usb_controller_esp32s3"
        for directory in (self.driver, self.historical):
            (directory / "nested").mkdir(parents=True)
            (directory / "driver.cpp").write_bytes(b"exact released source\n")
            (directory / "nested/header.h").write_bytes(b"exact released header\n")
        self.addCleanup(patch.stopall)
        patch.object(replay, "DRIVER", self.driver).start()

    def test_pinned_reader_and_canonical_identity(self):
        self.assertEqual(replay.HISTORICAL_COMMIT, READER_COMMIT)
        self.assertEqual(replay.CANONICAL_SIZE, 789504)
        self.assertEqual(replay.CANONICAL_SHA256, CANONICAL_SHA256)
        self.assertEqual(replay.builder.CANONICAL_SIZE, 789504)
        self.assertEqual(replay.builder.CANONICAL_SHA256, CANONICAL_SHA256)
        self.assertEqual(replay.builder.EXPECTED["version"], "0.1.19")

    def test_identical_complete_source_is_accepted(self):
        self.assertEqual(replay._overlay_migrated_source(self.workspace),
                         ["driver.cpp", "nested/header.h"])

    def assert_rejected_before_overlay(self, message):
        with patch.object(replay.shutil, "copyfile") as copy:
            with self.assertRaisesRegex(RuntimeError, message):
                replay._overlay_migrated_source(self.workspace)
            copy.assert_not_called()

    def test_extra_native_source_is_rejected(self):
        (self.driver / "NativeIdf61.h").write_bytes(b"native cohort only\n")
        self.assert_rejected_before_overlay("file-set differs.*extra=.*NativeIdf61.h")

    def test_missing_historical_source_is_rejected(self):
        (self.driver / "nested/header.h").unlink()
        self.assert_rejected_before_overlay("file-set differs.*missing=.*nested/header.h")

    def test_changed_source_bytes_are_rejected(self):
        (self.driver / "driver.cpp").write_bytes(b"changed source\n")
        self.assert_rejected_before_overlay("source differs.*driver.cpp")

    def test_changed_nested_header_is_rejected(self):
        (self.driver / "nested/header.h").write_bytes(b"changed header\n")
        self.assert_rejected_before_overlay("source differs.*nested/header.h")


@unittest.skipUnless(LEGACY_SOURCE, "set HISTORICAL_CONTROLLER_SOURCE to the pinned checkout")
class HistoricalControllerCheckout(unittest.TestCase):
    def setUp(self):
        self.legacy = Path(LEGACY_SOURCE).resolve()
        self.driver = self.legacy / "Drivers/usb_controller_esp32s3"

    def test_exact_clean_producer_and_controller_tree(self):
        def git(*args):
            return subprocess.check_output(["git", "-C", str(self.legacy), *args],
                                           text=True).strip()
        self.assertEqual(git("rev-parse", "HEAD"), DRIVERS_COMMIT)
        self.assertEqual(git("rev-parse", "HEAD:Drivers/usb_controller_esp32s3"),
                         CONTROLLER_TREE)
        self.assertEqual(check_parity.local_tree(self.driver), CONTROLLER_TREE)
        # The active wrapper and all original replay/audit checks remain exact.
        for name in ("build_usb_controller_release_parity.py",
                     "build_usb_controller_release_parity_v018_impl.py",
                     "build_usb_controller_esp32s3.py",
                     "build_usb_controller_esp32s3_v018_impl.py",
                     "verify_usb_controller_loader_map.py"):
            with self.subTest(script=name):
                self.assertEqual((ROOT / "scripts" / name).read_bytes(),
                                 (self.legacy / "scripts" / name).read_bytes())
        self.assertEqual(git("status", "--porcelain", "--untracked-files=all",
                             "--", "Drivers/usb_controller_esp32s3", "scripts"), "")

    def test_pinned_historical_source_passes_existing_overlay(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary)
            shutil.copytree(self.driver, workspace / "Drivers/usb_controller_esp32s3")
            with patch.object(replay, "DRIVER", self.driver):
                self.assertEqual(len(replay._overlay_migrated_source(workspace)), 11)

    def test_current_native_source_still_fails_historical_replay(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary)
            shutil.copytree(self.driver, workspace / "Drivers/usb_controller_esp32s3")
            with patch.object(replay, "DRIVER", ROOT / "Drivers/usb_controller_esp32s3"), \
                    patch.object(replay.shutil, "copyfile") as copy:
                with self.assertRaisesRegex(RuntimeError, "controller source file-set differs"):
                    replay._overlay_migrated_source(workspace)
                copy.assert_not_called()


if __name__ == "__main__":
    unittest.main()
