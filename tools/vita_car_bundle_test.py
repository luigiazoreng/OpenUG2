#!/usr/bin/env python3
"""Synthetic filesystem tests for vita_car_bundle; never accesses a Vita."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vita_car_bundle as cb
import vita_device


def make_sfo(path):
    key = b"TITLE_ID\0"
    value = b"OPENUG200\0"
    keys_off, vals_off, count = 20 + 16, 20 + 16 + len(key), 1
    entry = struct.pack("<HHIII", 0, 0x0204, len(value), len(value), 0)
    header = struct.pack("<4s4I", b"\0PSF", 0x101, keys_off, vals_off, count)
    Path(path).write_bytes(header + entry + key + value)


def make_vpk(path, eboot=b"synthetic eboot"):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("eboot.bin", eboot)
        z.writestr("sce_sys/param.sfo", make_sfo_bytes())


def make_sfo_bytes():
    key, value = b"TITLE_ID\0", b"OPENUG200\0"
    keys_off, vals_off = 36, 36 + len(key)
    return (struct.pack("<4s4I", b"\0PSF", 0x101, keys_off, vals_off, 1)
            + struct.pack("<HHIII", 0, 0x0204, len(value), len(value), 0) + key + value)


class FakeUSB(vita_device.USB):
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.description = {"kind": "fake-usb"}


def good_log(run_id):
    return (f"CAR SESSION run_id={run_id}\nGPU SUITE passed=8 failed=0 unsupported=0\n"
            + "\n".join(f"{name}=PASS" for name in cb.PROBES)
            + "\nCAR READY meshes=31 textures=12 wheels=4\nCAR SHUTDOWN frames=20 status=0\n")


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.data = self.base / "assets"
        (self.data / "CARS/240SX").mkdir(parents=True)
        (self.data / "GLOBAL").mkdir()
        for rel, _ in cb.ASSETS:
            f = self.data / rel
            f.write_bytes((rel + "-fixture").encode())
        self.vpk = self.base / "viewer.vpk"
        make_vpk(self.vpk)

    def tearDown(self):
        self.tmp.cleanup()

    def prepare(self, name="bundle"):
        output = self.base / name
        rc = cb.main(["prepare", "--data-root", str(self.data), "--vpk", str(self.vpk),
                      "--output", str(output)])
        self.assertEqual(rc, 0)
        return output / "ready"

    def test_prepare_streamed_layout_and_exclusive_output(self):
        bundle = self.prepare()
        manifest = json.loads((bundle / "manifest.json").read_text())
        self.assertEqual(len(manifest["run_id"]), 24)
        self.assertEqual(len(manifest["assets"]), 3)
        self.assertIn("CAR SESSION run_id=" + manifest["run_id"],
                      (bundle / "data/openug2/diagnostic-session.txt").read_text())
        self.assertEqual(cb.load_bundle(bundle)[1]["title_id"], cb.TITLE)
        self.assertEqual(cb.main(["prepare", "--data-root", str(self.data), "--vpk", str(self.vpk),
                                  "--output", str(bundle.parent)]), 2)

    def test_path_traversal_and_symlink_are_rejected(self):
        for path in ("../escape", "data/../../escape", "/data/x", "data\\escape"):
            with self.assertRaises(ValueError):
                cb.safe_rel(path)
        root = self.base / "root"
        root.mkdir()
        outside = self.base / "outside"
        outside.mkdir()
        (root / "link").symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            cb.under(root, "link/file")
        usb_root = self.base / "usb"
        usb_root.mkdir()
        (usb_root / "data").mkdir()
        (usb_root / "data/link").symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            FakeUSB(usb_root).path("data/link/file")

    def test_oversize_stream_is_refused(self):
        huge = self.base / "oversize"
        with huge.open("wb") as f:
            f.truncate(cb.MAX_FILE + 1)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            cb.info(huge)

    def test_deploy_and_collect_fake_mount(self):
        bundle = self.prepare()
        mount = self.base / "ux0"
        mount.mkdir()
        (mount / "app/OPENUG200").mkdir(parents=True)
        with zipfile.ZipFile(bundle / "data" / cb.VPK_REL) as archive:
            (mount / "app/OPENUG200/eboot.bin").write_bytes(archive.read("eboot.bin"))
        (mount / "data/openug2").mkdir(parents=True)
        (mount / "data/openug2/vita-car-viewer.log").write_text(
            good_log(json.loads((bundle / "manifest.json").read_text())["run_id"]), encoding="utf-8")
        with mock.patch.object(vita_device, "USB", FakeUSB):
            self.assertEqual(cb.main(["deploy", "--bundle", str(bundle), "--usb-root", str(mount)]), 0)
            out = self.base / "collected"
            self.assertEqual(cb.main(["collect", "--bundle", str(bundle), "--usb-root", str(mount),
                                      "--output", str(out)]), 0)
        report = json.loads((out / "result.json").read_text())
        self.assertEqual(report["state"], "APPROVED_HOST_ONLY")
        self.assertTrue(report["checks"]["installed_eboot_matches_bundle"])
        self.assertIn("real-car appearance", report["manual_review_required"])

    def test_existing_asset_mismatch_preserved_and_vpk_replacement(self):
        bundle = self.prepare()
        mount = self.base / "ux0"
        mount.mkdir()
        (mount / "app").mkdir()
        target = mount / "data/openug2/CARS/240SX/GEOMETRY.BIN"
        target.parent.mkdir(parents=True)
        target.write_bytes(b"retail data preserve")
        with mock.patch.object(vita_device, "USB", FakeUSB):
            self.assertEqual(cb.main(["deploy", "--bundle", str(bundle), "--usb-root", str(mount)]), 2)
        self.assertEqual(target.read_bytes(), b"retail data preserve")

    def test_stale_run_or_shader_failure_cannot_pass(self):
        run = "a" * 24
        stale = cb.assess_log(good_log("b" * 24), run, True)
        self.assertFalse(stale["checks"]["fresh_session_id"])
        failed = cb.assess_log(good_log(run) + "shader_fallback=FAIL\n", run, True)
        self.assertFalse(failed["checks"]["no_shader_fallback_failure"])

    def test_missing_required_probe_and_installed_hash_mismatch(self):
        run = "a" * 24
        log = good_log(run).replace("mip_generation=PASS\n", "")
        report = cb.assess_log(log, run, False)
        self.assertFalse(report["checks"]["required_gpu_probes"])
        self.assertFalse(report["checks"]["installed_eboot_matches_bundle"])

    def test_collect_rejects_stale_log_and_installed_executable_drift(self):
        bundle = self.prepare()
        manifest = json.loads((bundle / "manifest.json").read_text())
        mount = self.base / "ux0"
        mount.mkdir()
        (mount / "app/OPENUG200").mkdir(parents=True)
        with zipfile.ZipFile(bundle / "data" / cb.VPK_REL) as archive:
            eboot = archive.read("eboot.bin")
        installed = mount / "app/OPENUG200/eboot.bin"
        installed.write_bytes(eboot)
        log_path = mount / cb.LOG_REL
        log_path.parent.mkdir(parents=True)
        log_path.write_text(good_log("f" * 24))
        with mock.patch.object(vita_device, "USB", FakeUSB):
            self.assertEqual(cb.main(["collect", "--bundle", str(bundle), "--usb-root", str(mount),
                                      "--output", str(self.base / "stale")]), 2)
            log_path.write_text(good_log(manifest["run_id"]))
            installed.write_bytes(eboot + b"changed")
            self.assertEqual(cb.main(["collect", "--bundle", str(bundle), "--usb-root", str(mount),
                                      "--output", str(self.base / "drift")]), 2)


if __name__ == "__main__":
    unittest.main()
