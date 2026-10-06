#!/usr/bin/env python3
"""Synthetic transport/evidence tests. Optional pyftpdlib exercises real FTP."""
import argparse
import contextlib
import io
import json
import logging
from pathlib import Path
import struct
import tempfile
import threading
import unittest
import warnings
from unittest.mock import patch
import zipfile

import vita_device as vd

try:
    from pyftpdlib.authorizers import DummyAuthorizer
    from pyftpdlib.handlers import FTPHandler
    from pyftpdlib.servers import FTPServer
except ImportError:
    FTPServer = None

GOOD_LOG = """OpenUG2 isolated Vita bring-up; data_root=ux0:data/openug2 exists=1
SDL driver=VITA drawable=960x544
controller: OpenUG2 Vita
GLSL + VBO + u16 IBO + RGBA texture: initialized (device visibility still requires inspection)
240SX stock parser: meshes=101 vertices=30571 indices=18237 material_hashes=24 source_bytes=7675600 mesh_cpu_bytes=1 load_ms=1.0
Start=pause Cross=retry parser Select+Start=exit; green background=asset parsed, red=asset unavailable
world/physics/audio inactive; resident timings and traffic inactive; GPU ms/process RAM unavailable
fps=30.0 cpu_submit_ms=0.2 frame_ms=33.3
vgl_free_bytes: cdram=1 ram=1 phycont=1
shutdown complete: frames=900 status=0
"""
OBS = dict(installed_vpk="yes", triangle="yes", green="yes", inputs="yes", device_info="synthetic fixture")


def make_vpk(path, title=vd.TITLE, extra=None):
    key = b"TITLE_ID\0"
    value = title.encode() + b"\0"
    sfo = struct.pack("<4s4I", b"\0PSF", 0x101, 36, 36 + len(key), 1)
    sfo += struct.pack("<HHIII", 0, 0x204, len(value), len(value), 0) + key + value
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("eboot.bin", b"synthetic, not executable")
        archive.writestr("sce_sys/param.sfo", sfo)
        if extra:
            archive.writestr(extra, b"unexpected")


class EvidenceTest(unittest.TestCase):
    def test_stale_missing_shutdown_and_wrong_parser_never_pass(self):
        fingerprint = {"sha256": "abc", "bytes": 1}
        self.assertEqual(vd.assess(GOOD_LOG, None, fingerprint, OBS)["state"], "SMOKE_ACCEPTED_WITH_OPERATOR_OBSERVATIONS")
        cases = [GOOD_LOG.replace("shutdown complete:", "interrupted:"),
                 GOOD_LOG.replace("meshes=101", "meshes=100"),
                 GOOD_LOG + "draw failed: 0x1\n",
                 GOOD_LOG + "240SX parser: asset unavailable (0.0 ms)\n",
                 GOOD_LOG + "shader type=0x8b31 failed: error\n",
                 GOOD_LOG + "built-in controller unavailable: error\n",
                 GOOD_LOG.replace("frames=900 status=0", "frames=0 status=0")]
        for log in cases:
            self.assertEqual(vd.assess(log, None, fingerprint, OBS)["state"], "REVIEW_REQUIRED")
        self.assertEqual(vd.assess(GOOD_LOG, fingerprint, fingerprint, OBS)["state"], "REVIEW_REQUIRED")
        self.assertEqual(vd.assess(GOOD_LOG, None, fingerprint, {})["state"], "REVIEW_REQUIRED")

    def test_paths_and_read_budget(self):
        for path in ("../data/a", "data/../../app/a", "/data/a", "data/a\r\nDELE x", "data/a\\b"):
            with self.assertRaises(ValueError):
                vd.checked_path(path)
        with self.assertRaises(ValueError):
            vd.digest(io.BytesIO(b"abc"), 2)

    def test_vitashell_list_protocol(self):
        from unittest.mock import Mock
        ftp = object.__new__(vd.FTP)
        ftp.client = Mock()
        ftp.client.retrlines.side_effect = lambda command, callback: callback("drwxr-xr-x 1 root root 0 Oct 05 2026 name with spaces")
        self.assertEqual(ftp.names("/ux0:/data"), ["name with spaces"])
        ftp.client.cwd.assert_called_once_with("/ux0:/data")
        self.assertEqual(ftp.client.retrlines.call_args.args[0], "LIST")

    def test_vpk_identity_and_content(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.vpk"
            make_vpk(path)
            self.assertGreater(vd.validate_vpk(path)["bytes"], 0)
            make_vpk(path, title="VITASHELL")
            with self.assertRaises(ValueError):
                vd.validate_vpk(path)
            make_vpk(path, extra="CARS/GEOMETRY.BIN")
            with self.assertRaises(ValueError):
                vd.validate_vpk(path)


class TransportTests:
    def args(self):
        return argparse.Namespace(usb_root=self.usb_root, host=self.host, port=self.port,
                                  vpk=self.vpk, geometry=self.geometry, reports=self.root / "reports")

    def exercise(self):
        args = self.args()
        with contextlib.redirect_stdout(io.StringIO()):
            vd.deploy(args)
            run = next(args.reports.iterdir())
            manifest = json.loads((run / "deployment.json").read_text())
            self.assertEqual((self.volume / vd.GEOMETRY).read_bytes(), self.geometry.read_bytes())
            args.run = run
            for key, value in OBS.items():
                setattr(args, key, value)
            (self.volume / vd.LOG).write_text(GOOD_LOG)
            self.assertEqual(vd.collect(args), 0)
            # A second deployment snapshots old evidence, which must not pass
            # until a new run produces a different log and operator confirms it.
            vd.deploy(args)
            args.run = next(p for p in args.reports.iterdir() if p != run)
            self.assertEqual(vd.collect(args), 3)
            self.assertEqual(manifest["assets"][0]["sha256"], vd.file_info(self.geometry)["sha256"])
            # Mismatched existing data is never overwritten.
            (self.volume / vd.GEOMETRY).write_bytes(b"different user content")
            with self.assertRaisesRegex(ValueError, "geometry differs"):
                vd.deploy(args)
            self.assertEqual((self.volume / vd.GEOMETRY).read_bytes(), b"different user content")
            # Collection also rejects artifacts changed after deployment.
            args.run = run
            with self.assertRaisesRegex(ValueError, "artifacts changed"):
                vd.collect(args)

    def fixtures(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.vpk = self.root / "test.vpk"
        self.geometry = self.root / "geometry.bin"
        make_vpk(self.vpk)
        self.geometry.write_bytes(b"synthetic geometry" * 17000)  # Multiple 64 KiB transfer blocks.


class USBTest(TransportTests, unittest.TestCase):
    def test_deploy_collect_refusal_and_symlinks(self):
        self.fixtures()
        try:
            self.volume = self.root / "volume"
            (self.volume / "app").mkdir(parents=True)
            self.usb_root, self.host, self.port = self.volume, None, 1337
            with patch.object(Path, "is_mount", return_value=True):
                self.exercise()
                with self.assertRaises(ValueError):
                    vd.USB(self.volume).fetch(vd.GEOMETRY, self.root / "bounded-read", limit=16)
                (self.volume / "data/link").symlink_to(self.root)
                with self.assertRaises(ValueError):
                    vd.USB(self.volume).path("data/link/outside")
            with self.assertRaises(ValueError):
                vd.USB(self.volume)  # An arbitrary host directory is not ux0.
        finally:
            self.temp.cleanup()


@unittest.skipIf(FTPServer is None, "optional integration dependency: pyftpdlib")
class FTPTest(TransportTests, unittest.TestCase):
    def test_real_passive_ftp_deploy_and_collect(self):
        self.fixtures()
        self.volume = self.root / "ftp/ux0:"
        (self.volume / "app").mkdir(parents=True)
        authorizer = DummyAuthorizer()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", RuntimeWarning)
            authorizer.add_user("anonymous", "", str(self.volume.parent), perm="elradfmwMT")
        class Handler(FTPHandler):
            pass
        Handler.authorizer = authorizer
        from pyftpdlib.log import config_logging
        config_logging(level=logging.CRITICAL)
        server = FTPServer(("127.0.0.1", 0), Handler)
        self.usb_root, self.host, self.port = None, "127.0.0.1", server.socket.getsockname()[1]
        thread = threading.Thread(target=server.serve_forever, kwargs={"timeout": 0.05}, daemon=True)
        thread.start()
        try:
            self.exercise()
        finally:
            server.close_all()
            thread.join(timeout=2)
            self.temp.cleanup()


if __name__ == "__main__":
    unittest.main()
