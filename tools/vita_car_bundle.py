#!/usr/bin/env python3
"""Prepare, deploy, and collect bounded evidence for the Vita stock-car viewer.

All file copies and hashes are streamed in 64 KiB blocks. This tool never
opens game assets from Git and never installs or removes the application.
"""
import argparse
import contextlib
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import uuid
import zipfile

import vita_device

CHUNK = 64 * 1024
MAX_FILE = 32 * 1024 * 1024
MAX_LOG = 2 * 1024 * 1024
TITLE = "OPENUG200"
ASSETS = (
    ("CARS/240SX/GEOMETRY.BIN", "CARS/240SX/GEOMETRY.BIN"),
    ("CARS/240SX/TEXTURES.BIN", "CARS/240SX/TEXTURES.BIN"),
    ("GLOBAL/GLOBALB.BUN", "GLOBAL/GLOBALB.BUN"),
)
VPK_REL = "openug2/openug2-vita.vpk"
VPK_DEVICE = "data/openug2/car-viewer.vpk"
LOG_REL = "data/openug2/vita-car-viewer.log"
PROBES = ("engine_program", "framebuffer_depth", "rgba_alpha", "dxt1_rgba_fallback",
          "dxt3_rgba_fallback", "dxt5_rgba_fallback", "mip_sampling", "mip_generation")


def info(path):
    digest, size = hashlib.sha256(), 0
    with Path(path).open("rb") as stream:
        while block := stream.read(CHUNK):
            size += len(block)
            if size > MAX_FILE:
                raise ValueError(f"File exceeds {MAX_FILE} bytes: {path}")
            digest.update(block)
    return {"bytes": size, "sha256": digest.hexdigest()}


def copy_stream(source, destination):
    size, digest = 0, hashlib.sha256()
    with Path(source).open("rb") as src, Path(destination).open("xb") as dst:
        while block := src.read(CHUNK):
            size += len(block)
            if size > MAX_FILE:
                raise ValueError(f"File exceeds {MAX_FILE} bytes: {source}")
            dst.write(block)
            digest.update(block)
    return {"bytes": size, "sha256": digest.hexdigest()}


def safe_rel(name):
    p = PurePosixPath(name)
    if p.is_absolute() or not p.parts or any(part in ("", ".", "..") for part in p.parts):
        raise ValueError(f"Unsafe relative path: {name!r}")
    if any(ch in name for ch in "\\\r\n\x00:"):
        raise ValueError(f"Unsafe path characters: {name!r}")
    return p


def under(root, relative):
    rel = safe_rel(relative)
    root = Path(root).resolve(strict=True)
    target = root.joinpath(*rel.parts)
    for part in (target, *target.parents):
        if part == root:
            break
        if part.is_symlink():
            raise ValueError(f"Symlink in path: {relative}")
    if not target.resolve(strict=False).is_relative_to(root):
        raise ValueError(f"Path escapes root: {relative}")
    return target


def source_root(path):
    root = Path(path).resolve(strict=True)
    for rel, _ in ASSETS:
        f = under(root, rel)
        if not f.is_file():
            raise ValueError(f"Missing required car asset: {f}")
    return root


def git_identity():
    try:
        rev = subprocess.run(["git", "rev-parse", "HEAD"], check=True, capture_output=True,
                             text=True, timeout=5).stdout.strip()
        dirty = bool(subprocess.run(["git", "status", "--porcelain"], check=True,
                                    capture_output=True, text=True, timeout=5).stdout.strip())
        return {"revision": rev, "dirty": dirty}
    except (OSError, subprocess.SubprocessError):
        return {"revision": "unknown", "dirty": None}


def prepare(args):
    root = source_root(args.data_root)
    vpk_info = vita_device.validate_vpk(args.vpk)
    output = Path(args.output).absolute()
    output.mkdir(parents=True, exist_ok=False)
    data = output / "ready" / "data" / "openug2"
    data.mkdir(parents=True)
    run_id = uuid.uuid4().hex[:24]
    assets = []
    try:
        for source_rel, dest_rel in ASSETS:
            source = under(root, source_rel)
            destination = data / dest_rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            copied = copy_stream(source, destination)
            assets.append({"source": str(source), "device_path": "data/openug2/" + dest_rel,
                           **copied})
        vpk_target = data / "openug2-vita.vpk"
        copied_vpk = copy_stream(args.vpk, vpk_target)
        if copied_vpk != vpk_info:
            raise ValueError("VPK changed while preparing bundle")
        manifest = {
            "schema": 1, "run_id": run_id, "title_id": TITLE,
            "build_identity": git_identity(), "source_data_root": str(root),
            "assets": assets,
            "vpk": {"source": str(Path(args.vpk).resolve()), "bundle_path": "data/" + VPK_REL,
                    "device_path": VPK_DEVICE,
                    **vpk_info},
            "expected_log": "VITA_ROOT/" + LOG_REL,
            "expected_markers": {"shader_fallback": "FAIL", "gpu_probes": list(PROBES)},
            "state": "PREPARED_DEVICE_INSTALL_AND_RUN_PENDING",
        }
        (output / "ready" / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        (data / "diagnostic-session.txt").write_text(
            f"CAR SESSION run_id={run_id}\n"
            "Log marker shader_fallback=FAIL is fatal; absence of that marker is required.\n"
            "Expected log: VITA_ROOT/" + LOG_REL + "\n")
        print(f"BUNDLE={output / 'ready'}\nRUN_ID={run_id}\nVPK={data / 'openug2-vita.vpk'}")
    except Exception:
        # Keep the failed, uniquely named output for inspection; never erase user data.
        raise


def load_bundle(path):
    root = Path(path).resolve(strict=True)
    manifest_path = root / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("schema") != 1 or manifest.get("title_id") != TITLE:
        raise ValueError("Unrecognized bundle manifest")
    if not re.fullmatch(r"[0-9a-f]{24}", manifest.get("run_id", "")):
        raise ValueError("Invalid bundle run_id")
    for record in manifest["assets"] + [manifest["vpk"]]:
        local_rel = record.get("bundle_path", record["device_path"]).removeprefix("data/")
        local = under(root / "data", local_rel)
        if info(local) != {k: record[k] for k in ("bytes", "sha256")}:
            raise ValueError(f"Bundle payload mismatch: {local}")
    return root, manifest


def fetch_installed_eboot(usb, destination):
    """Read only this title's installed executable using the mounted ux0 root."""
    root = usb.root.resolve(strict=True)
    relative = PurePosixPath("app/OPENUG200/eboot.bin")
    target = root.joinpath(*relative.parts)
    for part in (target, *target.parents):
        if part == root:
            break
        if part.is_symlink():
            raise ValueError("Symlink in installed application path")
    if not target.resolve(strict=True).is_relative_to(root) or not target.is_file():
        raise ValueError("Installed application executable is missing or unsafe")
    return copy_stream(target, destination)


def deploy(args):
    bundle, manifest = load_bundle(args.bundle)
    with contextlib.closing(vita_device.USB(args.usb_root)) as usb:
        for record in manifest["assets"]:
            target = usb.path(record["device_path"])
            if target.exists():
                if info(target) != {k: record[k] for k in ("bytes", "sha256")}:
                    raise ValueError(f"Existing asset differs; preserved unchanged: {record['device_path']}")
            else:
                usb.put_new(bundle / "data" / record["device_path"].removeprefix("data/"),
                            record["device_path"])
        # The canonical viewer VPK is the one explicitly replaceable artifact.
        vpk = manifest["vpk"]
        target = usb.path(vpk["device_path"])
        target.parent.mkdir(parents=True, exist_ok=True)
        staged = target.with_name(target.name + ".partial-" + uuid.uuid4().hex)
        try:
            local = bundle / vpk["bundle_path"]
            copy_stream(local, staged)
            staged.replace(target)
        finally:
            staged.unlink(missing_ok=True)
        for record in manifest["assets"] + [vpk]:
            actual = usb.path(record["device_path"])
            if info(actual) != {k: record[k] for k in ("bytes", "sha256")}:
                raise ValueError(f"Device read-back mismatch: {record['device_path']}")
    print(f"DEPLOYED run_id={manifest['run_id']} title={TITLE}; install ux0:/data/openug2/car-viewer.vpk with VitaShell")


def assess_log(log, run_id, expected_eboot):
    session = re.findall(r"^CAR SESSION run_id=([0-9a-f]{24})\s*$", log, re.M)
    suite = re.findall(r"^GPU SUITE passed=(\d+) failed=(\d+) unsupported=(\d+)\s*$", log, re.M)
    ready = re.findall(r"^CAR READY meshes=(\d+) textures=(\d+) wheels=(\d+)\s*$", log, re.M)
    shutdown = re.findall(r"^CAR SHUTDOWN frames=(\d+) status=(\d+)\s*$", log, re.M)
    probes = {name: (re.findall(rf"^{re.escape(name)}=(PASS|FAIL|UNSUPPORTED)\s*$", log, re.M))
              for name in PROBES}
    shader_fail = bool(re.search(r"shader[_ ]fallback\s*=\s*FAIL|shader fallback failed", log, re.I))
    rgba_ok = all(probes[n] and probes[n][-1] == "PASS"
                  for n in ("rgba_alpha", "dxt1_rgba_fallback", "dxt3_rgba_fallback", "dxt5_rgba_fallback"))
    probe_ok = all(probes[n] and probes[n][-1] == "PASS" for n in PROBES)
    suite_ok = len(suite) == 1 and int(suite[0][1]) == 0
    counts_ok = (len(ready) == 1 and all(int(v) > 0 for v in ready[0]))
    shutdown_ok = len(shutdown) == 1 and int(shutdown[0][0]) > 0 and shutdown[0][1] == "0"
    checks = {
        "fresh_session_id": session == [run_id],
        "gpu_suite_zero_failures": suite_ok,
        "required_gpu_probes": probe_ok,
        "rgba_fallbacks_passed": rgba_ok,
        "no_shader_fallback_failure": not shader_fail,
        "car_ready_counts_nonzero": counts_ok,
        "clean_shutdown": shutdown_ok,
        "installed_eboot_matches_bundle": expected_eboot,
    }
    machine_pass = all(checks.values())
    return {"checks": checks, "probes": {k: (v[-1] if v else "MISSING") for k, v in probes.items()},
            "gpu_suite": suite[-1] if suite else None, "car_ready": ready[-1] if ready else None,
            "shutdown": shutdown[-1] if shutdown else None,
            "machine_log_pass": machine_pass,
            "manual_review_required": ["real-car appearance", "four wheels", "textures", "camera controls",
                                       "pause/resume", "lights", "colors", "SELECT+START exit"],
            "state": "APPROVED_HOST_ONLY" if machine_pass else "DEVICE_REVIEW_REQUIRED"}


def collect(args):
    bundle, manifest = load_bundle(args.bundle)
    output = Path(args.output).absolute()
    output.mkdir(parents=True, exist_ok=False)
    log_path = output / "vita-car-viewer.log"
    with contextlib.closing(vita_device.USB(args.usb_root)) as usb:
        log_info = usb.fetch(LOG_REL, log_path, MAX_LOG)
        if log_info is None:
            raise ValueError(f"Device log missing: {LOG_REL}")
        installed = output / "installed-eboot.bin"
        eboot_info = fetch_installed_eboot(usb, installed)
    with zipfile.ZipFile(bundle / "data" / VPK_REL) as archive:
        expected_eboot = hashlib.sha256()
        eboot_bytes = 0
        with archive.open("eboot.bin") as stream:
            while block := stream.read(CHUNK):
                eboot_bytes += len(block)
                expected_eboot.update(block)
    eboot_matches = eboot_info == {"bytes": eboot_bytes, "sha256": expected_eboot.hexdigest()}
    log = log_path.read_text(errors="replace")
    report = assess_log(log, manifest["run_id"], eboot_matches)
    if not report["checks"]["fresh_session_id"]:
        raise ValueError("Stale or mismatched CAR SESSION run_id; refusing to associate this log")
    if not eboot_matches:
        raise ValueError("Installed app/OPENUG200/eboot.bin differs from the bundle VPK")
    report.update({"schema": 1, "run_id": manifest["run_id"], "log": log_info,
                   "installed_eboot": eboot_info, "bundle_vpk": manifest["vpk"],
                   "manual_observations": "not supplied; no visual/input evidence inferred"})
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"{report['state']}\n{output / 'result.json'}")
    return 0 if report["machine_log_pass"] else 3


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command", required=True)
    p = subs.add_parser("prepare", help="create a new isolated bundle from three assets and a VPK")
    p.add_argument("--data-root", required=True, type=Path)
    p.add_argument("--vpk", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path, help="new, nonexistent output directory")
    p.set_defaults(func=prepare)
    p = subs.add_parser("deploy", help="stage bundle assets and canonical VPK on a mounted ux0")
    p.add_argument("--bundle", required=True, type=Path, help="the ready/ directory")
    p.add_argument("--usb-root", required=True, type=Path)
    p.set_defaults(func=deploy)
    p = subs.add_parser("collect", help="collect bounded log and verify installed eboot against VPK")
    p.add_argument("--bundle", required=True, type=Path)
    p.add_argument("--usb-root", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path, help="new, nonexistent evidence directory")
    p.set_defaults(func=collect)
    args = parser.parse_args(argv)
    try:
        result = args.func(args)
        return result if isinstance(result, int) else 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile, json.JSONDecodeError) as exc:
        print(f"vita_car_bundle: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
