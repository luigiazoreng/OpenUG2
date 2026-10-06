#!/usr/bin/env python3
"""Bounded VitaShell USB/FTP deployment and evidence collection (stdlib only)."""
import argparse
import contextlib
from datetime import datetime, timezone
import ftplib
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import sys
import uuid
import zipfile

CHUNK = 64 * 1024
LIMIT = 32 * 1024 * 1024
LOG_LIMIT = 2 * 1024 * 1024
GEOMETRY = "data/openug2/CARS/240SX/GEOMETRY.BIN"
LOG = "data/openug2/vita-bringup.log"
TITLE = "OPENUG200"


def checked_path(name):
    parts = PurePosixPath(name).parts
    if not parts or parts[0] != "data" or any(p in ("..", "/") for p in parts):
        raise ValueError("Only relative data/ paths are supported")
    if any(c in name for c in "\r\n\x00\\"):
        raise ValueError("Invalid device path")
    return name


def digest(reader, limit=LIMIT):
    sha, size = hashlib.sha256(), 0
    while block := reader.read(CHUNK):
        size += len(block)
        if size > limit:
            raise ValueError(f"File exceeds {limit} bytes")
        sha.update(block)
    return {"sha256": sha.hexdigest(), "bytes": size}


def file_info(path):
    with Path(path).open("rb") as stream:
        return digest(stream)


def save_json(path, value):
    # Exclusive creation preserves prior evidence; interrupted runs stay visible.
    with Path(path).open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False)
        stream.write("\n")


def validate_vpk(path):
    info = file_info(path)
    with zipfile.ZipFile(path) as archive:
        members = archive.infolist()
        if sorted(m.filename for m in members) != ["eboot.bin", "sce_sys/param.sfo"]:
            raise ValueError("Expected isolated bring-up VPK (eboot.bin + param.sfo only)")
        if any(m.file_size > LIMIT for m in members):
            raise ValueError("VPK member exceeds bootstrap size budget")
        if archive.testzip() is not None:
            raise ValueError("VPK CRC verification failed")
        with archive.open("sce_sys/param.sfo") as stream:
            sfo = stream.read(65537)
        if len(sfo) > 65536 or len(sfo) < 20:
            raise ValueError("Invalid SFO size")
        magic, _, keys, values, count = struct.unpack_from("<4s4I", sfo)
        if magic != b"\x00PSF" or count > 128 or 20 + 16 * count > len(sfo):
            raise ValueError("Invalid SFO header")
        title = None
        for i in range(count):
            key, _, length, capacity, offset = struct.unpack_from("<HHIII", sfo, 20 + i * 16)
            if keys + key >= len(sfo) or values + offset + length > len(sfo) or length > capacity:
                raise ValueError("Invalid SFO entry")
            if sfo[keys + key:].split(b"\0", 1)[0] == b"TITLE_ID":
                title = sfo[values + offset:values + offset + length].rstrip(b"\0").decode("ascii")
        if title != TITLE:
            raise ValueError(f"Expected title {TITLE}, found {title!r}")
    return info


class USB:
    def __init__(self, root):
        self.root = Path(root).resolve(strict=True)
        if not self.root.is_mount() or not (self.root / "app").is_dir():
            raise ValueError("--usb-root must be the mounted ux0 volume, containing app/")
        self.description = {"kind": "usb", "root": str(self.root)}

    def path(self, name):
        target = self.root / checked_path(name)
        # Refuse symlinks, including links pointing elsewhere inside the volume.
        for p in [target, *target.parents]:
            if p == self.root:
                break
            if p.is_symlink():
                raise ValueError("Symlink in device path")
        if not target.resolve().is_relative_to(self.root):
            raise ValueError("Device path escapes mounted volume")
        return target

    def fetch(self, name, destination, limit=LIMIT):
        source = self.path(name)
        if not source.exists():
            return None
        with source.open("rb") as src, Path(destination).open("xb") as dst:
            size = 0
            while block := src.read(CHUNK):
                size += len(block)
                if size > limit:
                    raise ValueError("Device file exceeds read budget")
                dst.write(block)
        return file_info(destination)

    def put_new(self, source, name):
        target = self.path(name)
        target.parent.mkdir(parents=True, exist_ok=True)
        # Publish only after the full transfer, never overwrite an existing file.
        temp = target.with_name(target.name + ".partial-" + uuid.uuid4().hex)
        try:
            with Path(source).open("rb") as src, temp.open("xb") as dst:
                while block := src.read(CHUNK):
                    dst.write(block)
                dst.flush()
                os.fsync(dst.fileno())
            if target.exists():
                raise ValueError(f"Refusing to overwrite {name}")
            temp.rename(target)
            fd = os.open(target.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        finally:
            temp.unlink(missing_ok=True)

    def close(self):
        pass


class FTP:
    def __init__(self, host, port):
        self.client = ftplib.FTP(timeout=15)
        try:
            self.client.connect(host, port)
            self.client.login()
            self.client.cwd("/ux0:")  # Fail before mutation unless ux0 exists.
        except (OSError, ftplib.Error):
            self.client.close()
            raise
        self.description = {"kind": "ftp", "host": host, "port": port}

    def path(self, name):
        return "/ux0:/" + checked_path(name)

    def names(self, directory):
        names = []
        def append(line):
            if len(names) >= 4096:
                raise ValueError("FTP directory exceeds listing budget")
            fields = line.split(maxsplit=8)
            if len(fields) != 9 or fields[0][0] not in "d-l":
                raise ValueError("Unrecognized FTP LIST format")
            names.append(fields[8])
        # VitaShell's libftpvita implements LIST, but not NLST/MLSD. Its
        # LIST path handling can fall back to cwd; set cwd explicitly first.
        self.client.cwd(directory)
        self.client.retrlines("LIST", append)
        return names

    def exists(self, name):
        current = "/ux0:"
        for part in PurePosixPath(checked_path(name)).parts:
            if part not in self.names(current):
                return False
            current += "/" + part
        return True

    def fetch(self, name, destination, limit=LIMIT):
        if not self.exists(name):
            return None
        size = 0
        with Path(destination).open("xb") as stream:
            def write(block):
                nonlocal size
                size += len(block)
                if size > limit:
                    raise ValueError("Device file exceeds read budget")
                stream.write(block)
            self.client.retrbinary("RETR " + self.path(name), write, CHUNK)
        return file_info(destination)

    def put_new(self, source, name):
        target = self.path(name)
        parent = PurePosixPath(target).parent
        current = "/ux0:"
        for part in parent.parts[2:]:
            current += "/" + part
            try:
                self.client.mkd(current)
            except ftplib.error_perm:
                self.client.cwd(current)  # Existing directory, or a real error.
        if PurePosixPath(target).name in self.names(str(parent)):
            raise ValueError(f"Refusing to overwrite {name}")
        temp = target + ".partial-" + uuid.uuid4().hex
        with Path(source).open("rb") as stream:
            self.client.storbinary("STOR " + temp, stream, CHUNK)
        # Read-back validation in deploy detects transport/storage corruption.
        self.client.rename(temp, target)

    def close(self):
        with contextlib.suppress(OSError, ftplib.Error):
            self.client.quit()
        self.client.close()


def transport(args):
    return USB(args.usb_root) if args.usb_root else FTP(args.host, args.port)


def deploy(args):
    vpk = validate_vpk(args.vpk)
    geometry = file_info(args.geometry)
    if geometry["bytes"] == 0:
        raise ValueError("Empty geometry file")
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:8]
    run = args.reports.resolve() / ("vita-" + run_id)
    run.mkdir(parents=True, exist_ok=False)
    vpk_target = f"data/openug2/deploy/{run_id}/openug2-vita.vpk"
    with contextlib.closing(transport(args)) as device:
        previous = device.fetch(LOG, run / "previous.log", LOG_LIMIT)
        existing = device.fetch(GEOMETRY, run / "existing-geometry.bin")
        if existing is not None and existing != geometry:
            raise ValueError("Device geometry differs; preserved unchanged. Choose the correct ux0 volume/data first")
        if existing is None:
            device.put_new(args.geometry, GEOMETRY)
        device.put_new(args.vpk, vpk_target)
        assets = []
        for name, expected, label in [(GEOMETRY, geometry, "geometry"), (vpk_target, vpk, "vpk")]:
            actual = device.fetch(name, run / (label + ".readback"))
            if actual != expected:
                raise ValueError(f"Read-back verification failed: {name}")
            assets.append({"device_path": name, **expected})
        save_json(run / "deployment.json", {
            "schema": 1, "run_id": run_id, "transport": device.description,
            "title_id": TITLE, "assets": assets, "previous_log": previous,
            "vpk_source": str(args.vpk.resolve()), "geometry_source": str(args.geometry.resolve()),
            "state": "TRANSFER_VERIFIED_INSTALL_AND_DEVICE_RUN_PENDING",
        })
    print(f"RUN={run}\nInstall in VitaShell: ux0:/{vpk_target}\nLaunch OpenUG2 Vita Bring-up; test controls; exit with SELECT+START; reconnect and collect.")


def assess(log, previous, current, observations):
    counts = re.findall(r"240SX stock parser: meshes=(\d+) vertices=(\d+) indices=(\d+) material_hashes=(\d+)", log)
    shutdown = re.findall(r"shutdown complete: frames=(\d+) status=(\d+)", log)
    checks = {
        "changed_since_deploy": previous is None or previous != current,
        "bootstrap_header": "OpenUG2 isolated Vita bring-up;" in log,
        "drawable_960x544": "drawable=960x544" in log,
        "gpu_resources_initialized": "GLSL + VBO + u16 IBO + RGBA texture: initialized" in log,
        "stock_240sx_counts": bool(counts) and all(tuple(map(int, c)) == (101, 30571, 18237, 24) for c in counts),
        "clean_shutdown": len(shutdown) == 1 and int(shutdown[0][0]) > 0 and shutdown[0][1] == "0",
        "profiling_present": "cpu_submit_ms=" in log and "vgl_free_bytes:" in log,
        # Match emitted diagnostics, not help text explaining unavailable
        # telemetry or the red background for a missing/failed asset.
        "no_reported_failure": not re.search(
            r"^(?:shader type=.* failed:|shader link failed:|required uniforms missing|"
            r"GPU resource upload failed:|draw failed:|240SX parser:|"
            r"unexpected drawable size|built-in controller unavailable:|controller mapping:|"
            r"(?:SDL init|GL attributes|window|context|timer init):|GL version=unavailable)",
            log, re.MULTILINE),
    }
    observed = all(observations.get(k) == "yes" for k in ("installed_vpk", "triangle", "green", "inputs")) and bool(observations.get("device_info", "").strip())
    state = "SMOKE_ACCEPTED_WITH_OPERATOR_OBSERVATIONS" if all(checks.values()) and observed else "REVIEW_REQUIRED"
    return {"state": state, "checks": checks, "observations": observations,
            "scope": "V0 context/shutdown, V1 indexed RGBA triangle only, V2 stock parser; full engine/DXT/FBO/VRAM peak excluded"}


def collect(args):
    run = args.run.resolve(strict=True)
    deployment = json.loads((run / "deployment.json").read_text())
    if deployment.get("schema") != 1 or deployment.get("title_id") != TITLE:
        raise ValueError("Unrecognized deployment manifest")
    collection = run / ("collection-" + uuid.uuid4().hex[:8])
    collection.mkdir()
    with contextlib.closing(transport(args)) as device:
        for i, asset in enumerate(deployment["assets"]):
            actual = device.fetch(asset["device_path"], collection / f"asset-{i}.readback")
            if actual != {k: asset[k] for k in ("sha256", "bytes")}:
                raise ValueError("Staged artifacts changed; refusing to associate this log with that deployment")
        current = device.fetch(LOG, collection / "vita-bringup.log", LOG_LIMIT)
        if current is None:
            raise ValueError("Device log missing; run the application and reconnect first")
        observations = {k: getattr(args, k) for k in ("installed_vpk", "triangle", "green", "inputs", "device_info")}
        report = assess((collection / "vita-bringup.log").read_text(errors="replace"), deployment["previous_log"], current, observations)
        report.update({"schema": 1, "run_id": deployment["run_id"], "log": current, "transport": device.description})
        save_json(collection / "result.json", report)
    print(f"{report['state']}\n{collection / 'result.json'}")
    return 0 if report["state"] == "SMOKE_ACCEPTED_WITH_OPERATOR_OBSERVATIONS" else 3


def doctor(args):
    report = {}
    for label, cmd in [("usb", ["lsusb"]), ("storage", ["lsblk", "-J", "-e", "7", "-o", "NAME,TRAN,SIZE,MOUNTPOINTS"]),
                       ("network", ["ip", "-j", "-4", "addr", "show", "scope", "global"])]:
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=10, check=True)
            report[label] = json.loads(result.stdout) if label != "usb" else result.stdout.splitlines()
        except (OSError, subprocess.SubprocessError) as exc:
            report[label] = {"error": str(exc)}
    if args.usb_root or args.host:
        with contextlib.closing(transport(args)) as device:
            report["connected"] = device.description
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("doctor", "deploy", "collect"):
        sub = commands.add_parser(name)
        connection = sub.add_mutually_exclusive_group(required=name != "doctor")
        connection.add_argument("--usb-root", type=Path, help="Mounted ux0 volume root (not data/)")
        connection.add_argument("--host", help="VitaShell FTP IP; console and Linux must share a LAN")
        sub.add_argument("--port", type=int, default=1337)
        if name == "deploy":
            sub.add_argument("--vpk", type=Path, required=True)
            sub.add_argument("--geometry", type=Path, required=True)
            sub.add_argument("--reports", type=Path, required=True)
        if name == "collect":
            sub.add_argument("--run", type=Path, required=True)
            for field in ("installed-vpk", "triangle", "green", "inputs"):
                sub.add_argument("--" + field, choices=("yes", "no", "unknown"), default="unknown")
            sub.add_argument("--device-info", default="", help="Model, firmware, VitaShell version; manual evidence")
    args = parser.parse_args()
    try:
        if not 1 <= args.port <= 65535:
            raise ValueError("FTP port must be between 1 and 65535")
        return {"doctor": doctor, "deploy": deploy, "collect": collect}[args.command](args) or 0
    except (OSError, ValueError, ftplib.Error, zipfile.BadZipFile, struct.error) as exc:
        print(f"vita_device: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
