# Vita stock-car viewer bundle and test procedure

`tools/vita_car_bundle.py` prepares one review bundle from exactly these data
files: `CARS/240SX/GEOMETRY.BIN`, `CARS/240SX/TEXTURES.BIN`, and
`GLOBAL/GLOBALB.BUN`. Each input is limited to 32 MiB. The tool validates that
the supplied VPK contains only `eboot.bin` and `sce_sys/param.sfo`, checks the
`OPENUG200` title, and verifies ZIP CRCs. It copies and hashes files in 64 KiB
chunks; it does not commit or fetch retail assets.

Prepare a new output directory for each attempt:

```sh
python3 tools/vita_car_bundle.py prepare \
  --data-root /path/to/retail-data \
  --vpk build/vita-car-viewer.vpk \
  --build-manifest build/vita-car-viewer-build.json \
  --output /path/to/new/car-viewer-bundle-01
```

The command creates `car-viewer-bundle-01/ready/` with the three assets under
`data/openug2/`, the VPK at `data/openug2/openug2-vita.vpk`,
`data/openug2/diagnostic-session.txt`, and `manifest.json`. The manifest records
a fresh 24-hex-character run ID, source asset paths and SHA-256/size, VPK hash,
required log path, and expected diagnostic markers. The current checkout's
revision/dirty state is recorded only as `tooling_checkout_identity`; it is
not represented as the compiled VPK identity. If `--build-manifest` is
provided, its compiled identity is recorded separately and both its VPK SHA
and embedded eboot SHA must match the supplied VPK. The sidecar JSON contract
is `compiled_revision`, `source_sha256`, `source_status`, `vpk_sha256`, and
`eboot_sha256`. The VPK is only staged; it is never installed by this command.
The generated `diagnostic-session.txt` is a first-class, separately hashed
configuration record in the manifest, not a retail asset.

Connect the Vita in USB mode and mount its `ux0:` volume on the host. Deploy
only from a prepared `ready/` directory:

```sh
python3 tools/vita_car_bundle.py deploy \
  --bundle /path/to/car-viewer-bundle-01/ready \
  --usb-root /media/$USER/Vita
```

The tool refuses symlinked or escaping paths. It preserves an existing asset
when the SHA-256 and size match, and refuses to overwrite an asset whose
content differs. It writes the viewer VPK only to the canonical
`ux0:data/openug2/car-viewer.vpk` path using a temporary file and atomic
replacement. It also atomically replaces only the generated
`ux0:data/openug2/diagnostic-session.txt` so every deployment receives the
new bundle run ID. A second bundle may update this config and VPK while all
three car assets remain guarded against replacement. It then streams all
staged assets, the VPK, and session config back to the host and compares
hashes. It does not touch `ux0:app/OPENUG200` or uninstall an application.
Install the staged VPK manually with VitaShell, then launch the viewer.

The expected log is `ux0:data/openug2/vita-car-viewer.log`, copied to the host
as `VITA_ROOT/data/openug2/vita-car-viewer.log`. Reconnect USB after exiting
the viewer and collect into a new directory:

```sh
python3 tools/vita_car_bundle.py collect \
  --bundle /path/to/car-viewer-bundle-01/ready \
  --usb-root /media/$USER/Vita \
  --output /path/to/new/car-viewer-evidence-01
```

Collection is bounded to a 2 MiB log and a 32 MiB installed executable. It
compares `ux0:app/OPENUG200/eboot.bin` against `eboot.bin` inside the bundled
VPK. It rejects a stale/mismatched session run ID or an installed executable
whose hash differs. Logs and executable are copied before association; the
new output directory is exclusive and retained if collection fails.

The machine log protocol is intentionally simple and line based. The viewer
should emit exactly one of each session/suite/ready/shutdown record, and one
final status for every probe:

```text
CAR SESSION run_id=<24 lowercase hex characters>
CAR BUILD revision=<compiled-revision> source_sha256=<64 lowercase hex characters>
GPU SUITE passed=<N> failed=0 unsupported=<K> elapsed_ms=<finite-number>
GPU PROBE engine_program PASS linked=1
GPU PROBE framebuffer_depth PASS rgba=1 expected_rgb=1 error=0.0
GPU PROBE rgba_alpha PASS rgba=1 expected_rgb=1 error=0.0
GPU PROBE dxt1_rgba_fallback PASS rgba=1 expected_rgb=1 error=0.0
GPU PROBE dxt3_rgba_fallback PASS rgba=1 expected_rgb=1 error=0.0
GPU PROBE dxt5_rgba_fallback PASS rgba=1 expected_rgb=1 error=0.0
GPU PROBE dxt1_cutout_rgba PASS rgba=0,0,255,255 expected_rgb=0,0,255 error=0x0
GPU PROBE uniform_arrays PASS rgba=255,255,0,255 expected_rgb=255,255,0 error=0x0
GPU PROBE uniform_array_element PASS rgba=255,0,255,255 expected_rgb=255,0,255 error=0x0
GPU PROBE mip_sampling PASS rgba=1 expected_rgb=1 error=0.0
GPU PROBE mip_generation PASS rgba=1 expected_rgb=1 error=0.0
CAR READY meshes=<positive> textures=<positive> wheels=<positive>
CAR SHUTDOWN frames=<positive> status=0
```

For a bundle with a compiled build sidecar, `CAR BUILD` must appear exactly
once and its revision/source SHA must match that sidecar. Without a sidecar,
this marker is informational. Probe diagnostic suffixes remain in the
collected raw log; the parser checks marker names/states and accepts suffix
fields without interpreting their formatting.
Any required-probe `FAIL` or `UNSUPPORTED` at any stage fails the check. GPU
suite records may repeat only when their pass/fail/unsupported counts agree;
each must have finite elapsed time and zero failures. Native DXT compression
may be unsupported if all three RGBA fallback probes pass. A logged
`shader_fallback=FAIL` or `shader fallback failed` is fatal. A successful
device log report is `DEVICE_LOG_PASS_MANUAL_REVIEW_REQUIRED`; that means the
bundle, installed executable, session config, and machine-readable run
evidence agree. It does not claim that the car looked correct or that controls
worked. Missing or failing machine evidence yields `DEVICE_REVIEW_REQUIRED`.
`result.json` explicitly lists the
manual observations still required; this collector does not infer them from
logs or synthesize visual/input evidence.

## Vita operator review

After installing and launching the VPK, perform and record these checks on the
actual device:

1. Confirm the static 240SX model has the expected body and wheel textures.
   Orbit the camera through front, side, and rear views and record device
   photos or video showing all four wheels across those angles.
2. Move the left stick to orbit/elevate the camera and move the right stick's
   Y axis to zoom. This viewer has no steering animation or vehicle physics;
   do not expect the wheels to steer.
3. Press Start to pause and resume.
4. Press Triangle to cycle the camera preset.
5. Press Square to toggle lights.
6. Press Cross to cycle the color preset.
7. Press Select+Start to exit, then reconnect USB and collect the log.

Record the Vita model, firmware, VitaShell version, whether each step passed,
and any device capture path in the review notes. A successful `collect`
command reports `DEVICE_LOG_PASS_MANUAL_REVIEW_REQUIRED`; it does not provide
visual approval or hardware sign-off. If the current viewer control mapping
differs, update this procedure to match the implementation before device
review; do not treat a command being accepted as evidence that its visible
behavior is correct.

## Synthetic host tests

Run filesystem-only tests under the shared validation lock:

```sh
flock /tmp/openug2-validation.lock python3 tools/vita_car_bundle_test.py
python3 -m py_compile tools/vita_car_bundle.py tools/vita_car_bundle_test.py
```

The tests use temporary files and a fake USB root. They cover VPK validation,
exclusive output paths, streaming size limits, traversal and symlink refusal,
asset mismatch preservation, VPK staging/replacement, readback, valid log
collection, stale run IDs, shader fallback failure, required probe failure,
installed executable drift, session config deployment/currentness, second
bundle run-ID replacement, probe-suite duplicate handling, and build-manifest
hash validation. They do not contact a Vita, compile the engine, or perform
device/rendering approval.
