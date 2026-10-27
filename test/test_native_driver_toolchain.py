"""Offline discovery contracts and opt-in real native GCC15 build witnesses."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import native_driver_toolchain as tc


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.env = patch.dict(os.environ, {"PATH": str(self.bin),
                              "HOME": str(self.root), "PLATFORMIO_CORE_DIR": str(self.root / "pio")}, clear=True)
        self.env.start()
        self.addCleanup(self.env.stop)
        tc._validate.cache_clear()
        tc.is_unified.cache_clear()

    def tool(self, name, target="xtensa-esp32s3-elf", macros=None, directory=None):
        path = (directory or self.bin) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if macros is None:
            macros = "#define __XTENSA_EL__ 1\n#define __XTENSA_WINDOWED_ABI__ 1"
        path.write_text(f"#!/bin/sh\nif [ \"$1\" = -dumpmachine ]; then\n echo '{target}'\nelse\n echo '{macros}'\nfi\n")
        path.chmod(0o755)
        return str(path)

    def native(self, directory=None):
        self.tool(tc.NATIVE_CC, "xtensa-esp-elf", directory=directory)
        return self.tool(tc.LEGACY_CC, "xtensa-esp-elf", directory=directory)

    def test_native_s3_frontend_selected(self):
        expected = self.native()
        self.assertEqual(tc.compiler(), expected)

    def test_native_preferred_over_earlier_legacy_on_path(self):
        legacy = self.root / "legacy"
        self.tool(tc.LEGACY_CC, directory=legacy)
        native = self.native()
        os.environ["PATH"] = str(legacy) + os.pathsep + str(self.bin)
        self.assertEqual(tc.compiler(), native)

    def test_legacy_path_supported(self):
        expected = self.tool(tc.LEGACY_CC)
        self.assertEqual(tc.compiler(), expected)

    def test_explicit_legacy_overrides_native(self):
        self.native()
        expected = self.tool(tc.LEGACY_CC, directory=self.root / "override")
        os.environ["NATIVE_DRIVER_CC"] = expected
        self.assertEqual(tc.compiler(), expected)

    def test_argument_overrides_environment(self):
        expected = self.tool(tc.LEGACY_CC)
        os.environ["NATIVE_DRIVER_CC"] = "/missing/compiler"
        self.assertEqual(tc.compiler(expected), expected)

    def test_explicit_command_name(self):
        expected = self.tool(tc.LEGACY_CC)
        self.assertEqual(tc.compiler(tc.LEGACY_CC), expected)

    def test_unified_override_maps_only_to_matching_s3_frontend(self):
        expected = self.native()
        self.assertEqual(tc.compiler(str(self.bin / tc.NATIVE_CC)), expected)

    def test_missing_explicit_override_does_not_fall_back(self):
        self.native()
        with self.assertRaisesRegex(SystemExit, "NATIVE_DRIVER_CC"):
            tc.compiler("/missing/compiler")

    def test_non_executable_override_does_not_fall_back(self):
        path = Path(self.native())
        path.chmod(0o644)
        with self.assertRaisesRegex(SystemExit, "non-executable"):
            tc.compiler(str(path))

    def test_missing_native_frontend_fails_closed(self):
        self.tool(tc.NATIVE_CC, "xtensa-esp-elf")
        with self.assertRaisesRegex(SystemExit, "missing its ESP32-S3 frontend"):
            tc.compiler()

    def test_custom_platformio_core_fallback(self):
        expected = self.tool(tc.LEGACY_CC, directory=self.root / "pio/packages/toolchain-xtensa-esp32s3/bin")
        self.assertEqual(tc.compiler(), expected)

    def test_home_platformio_fallback(self):
        del os.environ["PLATFORMIO_CORE_DIR"]
        expected = self.tool(tc.LEGACY_CC, directory=self.root / ".platformio/packages/toolchain-xtensa-esp32s3/bin")
        self.assertEqual(tc.compiler(), expected)

    def test_board_legacy_fallback(self):
        expected = self.tool(tc.LEGACY_CC, directory=self.root / "board")
        self.assertEqual(tc.compiler(legacy_fallbacks=(Path(expected),)), expected)

    def test_missing_compiler_actionable(self):
        with self.assertRaisesRegex(SystemExit, "activate ESP-IDF 6.1"):
            tc.compiler()

    def test_host_target_rejected(self):
        self.tool(tc.LEGACY_CC, "x86_64-linux-gnu")
        with self.assertRaisesRegex(SystemExit, "unsupported driver compiler target"):
            tc.compiler()

    def test_big_endian_target_rejected(self):
        self.tool(tc.LEGACY_CC, "xtensa-esp-elf", "#define __XTENSA_EB__ 1\n#define __XTENSA_WINDOWED_ABI__ 1")
        with self.assertRaisesRegex(SystemExit, "little-endian"):
            tc.compiler()

    def test_call0_target_rejected(self):
        self.tool(tc.LEGACY_CC, macros="#define __XTENSA_EL__ 1")
        with self.assertRaisesRegex(SystemExit, "windowed ABI"):
            tc.compiler()

    def test_symlink_frontend_name_preserved(self):
        target = Path(self.tool("real-compiler"))
        frontend = self.bin / tc.LEGACY_CC
        frontend.symlink_to(target)
        self.assertEqual(tc.compiler(), str(frontend))

    def test_legacy_has_no_extra_link_support(self):
        cc = self.tool(tc.LEGACY_CC)
        self.assertEqual(tc.native_support_sources(cc), ())

    def test_native_support_is_source_owned(self):
        cc = self.native()
        support = tc.native_support_sources(cc)
        self.assertEqual(len(support), 1)
        self.assertTrue(Path(support[0]).is_file())

    def test_parity_is_explicit(self):
        with patch.object(sys, "argv", ["builder"]):
            self.assertFalse(tc.parity_requested())
        with patch.object(sys, "argv", ["builder", "--require-byte-parity"]):
            self.assertTrue(tc.parity_requested())


@unittest.skipUnless(os.environ.get("NATIVE_DRIVER_INTEGRATION") == "1", "set NATIVE_DRIVER_INTEGRATION=1 for real GCC15 builds")
class NativeBuildTests(unittest.TestCase):
    def test_gcc15_representative_elf_contract(self):
        cc = tc.compiler()
        self.assertEqual(subprocess.check_output([cc, "-dumpfullversion"], text=True).strip(), "15.2.0")
        nm = str(Path(cc).with_name(tc.LEGACY_CC.replace("gcc", "nm")))
        expected = {
            "platform_clock_v1": ("platform-clock-v1", {"clock_gettime", "usleep"}),
            "s3_radio_iq_v1": ("s3-radio-iq-v1", {"strcmp", "memcpy", "memset"}),
            "usb_mass_storage_legacy_prefix": ("usb-mass-storage-legacy-prefix", {"memcpy", "memset"}),
            "display_epd_video": ("display-epd-video", {"t5_video_get_api", "usleep", "memcpy", "memset"}),
        }
        for script, (identity, allowed) in expected.items():
            with self.subTest(driver=identity):
                subprocess.run([sys.executable, str(ROOT / f"scripts/build_{script}.py")], check=True)
                elf = ROOT / "dist" / identity / "driver.elf"
                data = elf.read_bytes()
                self.assertEqual(data[:7], b"\x7fELF\x01\x01\x01")
                self.assertEqual(data[16:20], b"\x03\x00\x5e\x00")
                symbols = subprocess.check_output([nm, "-D", str(elf)], text=True)
                imports = {line.split()[-1] for line in symbols.splitlines() if " U " in " " + line}
                exports = {line.split()[-1] for line in symbols.splitlines() if len(line.split()) >= 3 and line.split()[-2] in ("T", "D", "R", "B")}
                self.assertLessEqual(imports, allowed)
                self.assertIn("t5_driver_get", exports)
                self.assertLessEqual(exports, {"t5_driver_get", "_edata", "__bss_start", "_end"})
                self.assertNotIn("__udivdi3", imports)
                validator = os.environ.get("NATIVE_DRIVER_ELF_VALIDATOR")
                if validator:
                    subprocess.run([validator, str(elf)], check=True)
                manifest = json.loads((elf.parent / "manifest.json").read_text())
                self.assertEqual(manifest["driver_abi"], 2)
                if script == "usb_mass_storage_legacy_prefix":
                    self.assertEqual(manifest["id"], "usb-mass-storage")
                    self.assertEqual(manifest["version"], "0.1.2")
                    self.assertIs(manifest["canonical_byte_parity_claim"], False)
                if "sha256" in manifest:
                    self.assertEqual(manifest["sha256"], hashlib.sha256(data).hexdigest())

    def test_native_cannot_claim_historical_parity(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/build_display_epd_video.py"), "--require-byte-parity"], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("canonical byte parity mismatch", result.stderr)


if __name__ == "__main__":
    unittest.main()
