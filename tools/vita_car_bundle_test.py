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


def good_log(run_id, build=None):
    probe_lines = []
    for name in cb.REQUIRED_PROBES:
        detail = "linked=1" if name == "engine_program" else "rgba=1 expected_rgb=1 error=0.0"
        probe_lines.append(f"GPU PROBE {name} PASS {detail}")
    build_line = (f"CAR BUILD revision={build['compiled_revision']} source_sha256={build['source_sha256']}\n"
                  if build else "")
    return (f"CAR SESSION run_id={run_id}\n" + build_line
            + f"GPU SUITE passed=11 failed=0 unsupported=0 elapsed_ms=2.50\n"
            + "\n".join(probe_lines)
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

    def prepare(self, name="bundle", build_manifest=None):
        output = self.base / name
        argv = ["prepare", "--data-root", str(self.data), "--vpk", str(self.vpk)]
        if build_manifest:
            argv += ["--build-manifest", str(build_manifest)]
        argv += ["--output", str(output)]
        rc = cb.main(argv)
        self.assertEqual(rc, 0)
        return output / "ready"

    def test_prepare_streamed_layout_and_exclusive_output(self):
        bundle = self.prepare()
        manifest = json.loads((bundle / "manifest.json").read_text())
        self.assertEqual(len(manifest["run_id"]), 24)
        self.assertEqual(len(manifest["assets"]), 3)
        self.assertIn("CAR SESSION run_id=" + manifest["run_id"],
                      (bundle / "data/openug2/diagnostic-session.txt").read_text())
        config = manifest["session_config"]
        self.assertEqual(config["run_id"], manifest["run_id"])
        self.assertEqual(cb.info(bundle / config["bundle_path"]),
                         {k: config[k] for k in ("bytes", "sha256")})
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
        run_id = json.loads((bundle / "manifest.json").read_text())["run_id"]
        (mount / "data/openug2/startup-guard.log").write_text(
            "STARTUP SESSION run_id=" + run_id + "\nSTARTUP END stage=car_load elapsed_ms=0\n")
        (mount / "data/openug2/shader-vertex.cg").write_text(
            "// CAR SESSION run_id=" + run_id + "\nvoid main() {}\n")
        (mount / "data/openug2/shader-fragment.cg").write_text(
            "// CAR SESSION run_id=" + "0" * 24 + "\nstale source\n")
        with mock.patch.object(vita_device, "USB", FakeUSB):
            self.assertEqual(cb.main(["deploy", "--bundle", str(bundle), "--usb-root", str(mount)]), 0)
            out = self.base / "collected"
            self.assertEqual(cb.main(["collect", "--bundle", str(bundle), "--usb-root", str(mount),
                                      "--output", str(out)]), 0)
        report = json.loads((out / "result.json").read_text())
        self.assertEqual(report["state"], "DEVICE_LOG_PASS_MANUAL_REVIEW_REQUIRED")
        self.assertTrue(report["checks"]["installed_eboot_matches_bundle"])
        self.assertTrue(report["startup_diagnostics"]["startup-guard.log"]["session_matches"])
        self.assertTrue(report["startup_diagnostics"]["shader-vertex.cg"]["session_matches"])
        self.assertFalse(report["startup_diagnostics"]["shader-fragment.cg"]["session_matches"])
        self.assertEqual((out / "shader-fragment.cg").read_bytes(),
                         (mount / "data/openug2/shader-fragment.cg").read_bytes())
        self.assertIn("real-car appearance", report["manual_review_required"])

    def test_static_profile_requires_viewer_probe_and_cannot_approve_full_engine(self):
        run = "a" * 24
        build = {"compiled_revision": "fixture", "source_sha256": "b" * 64, "shader_profile": "static_car"}
        log = good_log(run, build).replace("GPU PROBE engine_program PASS", "GPU PROBE viewer_program PASS")
        log += "GPU SHADER PROFILE static_car full_engine=deferred\n"
        report = cb.assess_log(log, run, True, build)
        self.assertTrue(report["machine_log_pass"])
        self.assertEqual(report["state"], "DEVICE_STATIC_CAR_LOG_PASS_MANUAL_REVIEW_REQUIRED")
        self.assertEqual(report["full_engine_validation"], "DEFERRED")
        self.assertFalse(cb.assess_log(log.replace("viewer_program", "engine_program"), run, True, build)["machine_log_pass"])
        self.assertFalse(cb.assess_log(log.replace("GPU SHADER PROFILE static_car full_engine=deferred\n", ""), run, True, build)["machine_log_pass"])
        full_build = {**build, "shader_profile": "full_engine"}
        self.assertFalse(cb.assess_log(log, run, True, full_build)["machine_log_pass"])

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
        log = good_log(run).replace("GPU PROBE mip_generation PASS rgba=1 expected_rgb=1 error=0.0\n", "")
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

    def test_second_bundle_redeploy_updates_session_only(self):
        first = self.prepare("bundle-one")
        second = self.prepare("bundle-two")
        first_manifest = json.loads((first / "manifest.json").read_text())
        second_manifest = json.loads((second / "manifest.json").read_text())
        self.assertNotEqual(first_manifest["run_id"], second_manifest["run_id"])
        mount = self.base / "ux0"
        mount.mkdir()
        (mount / "app").mkdir()
        with mock.patch.object(vita_device, "USB", FakeUSB):
            self.assertEqual(cb.main(["deploy", "--bundle", str(first), "--usb-root", str(mount)]), 0)
            asset_path = mount / "data/openug2/CARS/240SX/GEOMETRY.BIN"
            asset_before = asset_path.read_bytes()
            self.assertEqual(cb.main(["deploy", "--bundle", str(second), "--usb-root", str(mount)]), 0)
        config = mount / cb.SESSION_DEVICE
        self.assertIn(second_manifest["run_id"], config.read_text())
        self.assertEqual(cb.info(config), {k: second_manifest["session_config"][k]
                                           for k in ("bytes", "sha256")})
        self.assertEqual(asset_path.read_bytes(), asset_before)
        (mount / "app/OPENUG200").mkdir(parents=True)
        with zipfile.ZipFile(first / "data" / cb.VPK_REL) as archive:
            (mount / "app/OPENUG200/eboot.bin").write_bytes(archive.read("eboot.bin"))
        (mount / cb.LOG_REL).parent.mkdir(parents=True, exist_ok=True)
        (mount / cb.LOG_REL).write_text(good_log(first_manifest["run_id"]))
        with mock.patch.object(vita_device, "USB", FakeUSB):
            self.assertEqual(cb.main(["collect", "--bundle", str(first), "--usb-root", str(mount),
                                      "--output", str(self.base / "old-config")]), 2)

    def test_build_manifest_matches_vpk_and_eboot(self):
        with zipfile.ZipFile(self.vpk) as archive:
            eboot_sha = hashlib.sha256(archive.read("eboot.bin")).hexdigest()
        build = {"compiled_revision": "abc123", "source_sha256": "a" * 64,
                 "source_status": "clean", "vpk_sha256": cb.info(self.vpk)["sha256"],
                 "eboot_sha256": eboot_sha}
        manifest_path = self.base / "build.json"
        manifest_path.write_text(json.dumps(build))
        bundle = self.prepare("build-bundle", manifest_path)
        prepared = json.loads((bundle / "manifest.json").read_text())
        self.assertEqual(prepared["compiled_build_identity"], build)
        run = prepared["run_id"]
        good = cb.assess_log(good_log(run, build), run, True, build)
        self.assertTrue(good["checks"]["compiled_build_identity_matches_sidecar"])
        self.assertEqual(good["state"], "DEVICE_LOG_PASS_MANUAL_REVIEW_REQUIRED")
        missing = cb.assess_log(good_log(run), run, True, build)
        self.assertFalse(missing["checks"]["compiled_build_identity_matches_sidecar"])
        divergent = dict(build, compiled_revision="different-revision")
        mismatch = cb.assess_log(good_log(run, divergent), run, True, build)
        self.assertFalse(mismatch["checks"]["compiled_build_identity_matches_sidecar"])
        duplicate = cb.assess_log(good_log(run, build) +
                                  f"CAR BUILD revision={build['compiled_revision']} source_sha256={build['source_sha256']}\n",
                                  run, True, build)
        self.assertFalse(duplicate["checks"]["compiled_build_identity_matches_sidecar"])
        build["vpk_sha256"] = "0" * 64
        manifest_path.write_text(json.dumps(build))
        self.assertEqual(cb.main(["prepare", "--data-root", str(self.data), "--vpk", str(self.vpk),
                                  "--build-manifest", str(manifest_path),
                                  "--output", str(self.base / "build-bundle-bad")]), 2)

    def test_conflicting_suite_duplicate_or_any_probe_failure_rejected(self):
        run = "a" * 24
        log = good_log(run) + "GPU SUITE passed=8 failed=1 unsupported=0 elapsed_ms=3\n"
        self.assertFalse(cb.assess_log(log, run, True)["checks"]["gpu_suite_zero_failures"])
        log = good_log(run).replace("GPU PROBE mip_sampling PASS", "GPU PROBE mip_sampling FAIL")
        self.assertFalse(cb.assess_log(log, run, True)["checks"]["required_gpu_probes"])
        unknown_failure = cb.assess_log(good_log(run) + "GPU PROBE optional_future_probe FAIL reason=bad\n", run, True)
        self.assertFalse(unknown_failure["checks"]["no_gpu_probe_failures"])


if __name__ == "__main__":
    unittest.main()
