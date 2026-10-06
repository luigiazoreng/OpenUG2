# TPK independent review fixes

Branch `fix-tpk-review-findings`, based on `6414e1aea837671efcc2c1c409038053ff0919ad`.

## Header bounds

`n2_tpk_open` now checks the declared `uint32_t` header length against the
remaining file bytes using `uint64_t`, before converting it to `long` or storing
it in the block. Therefore every block created by the opener has a bounded
`hbeg + hsize`; the additions in `n2_tpk_keys` and `n2_tpk_decode` cannot
overflow signed `long` or address bytes beyond the file. Retail headers whose
declared ranges fit the file retain their original values.

The synthetic case allocates exactly `0x70` bytes, declares a `0x10000`-byte
header, and calls `n2_tpk_keys`. Before the fix, ASan reports a heap-buffer-
overflow in `n2_tpk_keys`. After the fix, the malformed block is discarded and
the case passes with zero keys.

## Candidate draw metadata

Each matching record candidate now starts with a zeroed `N2Tex`. A rejected
candidate's freed pixel buffers are still freed on the existing rejection
path; a later candidate cannot inherit its `order`, `usage`, `blend`, or `wz`.
Successful candidates without the full `0x4c`-byte prefix retain zero defaults,
and the existing final `memset` still guarantees zero output after total
failure.

The synthetic case has two blocks with the same hash. The first writes
`order=7 usage=9 blend=3 wz=5`, then fails because its pixel offset is invalid.
The next candidate succeeds with a `0x44`-byte header (long enough to hold the
format and dimensions, shorter than the metadata tail). Before the fix it
returns those stale values; after the fix all four fields are zero.

## Reproduction and evidence

The test accepts one case per process: `header` or `metadata`. Each command
below was run through
`/home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py`
with `OPENUG2_VALIDATION_BUDGET_MIB=512`; the guard used a 512 MiB cgroup limit,
zero swap, and a 4 GiB host-available-memory reserve. The four guard JSON
records and full stdout/stderr logs are in
`/home/luigi/Documents/luigi/nfsu2-data/reports/tpk-independent-review-fixes-2026-10-05/`.

Fail-before setup and header reproduction:

```sh
git show 6414e1a:src/nfsu2.h > /tmp/nfsu2-tpk-baseline.h
OPENUG2_VALIDATION_BUDGET_MIB=512 python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py bash -lc 'gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-function -Isrc -include /tmp/nfsu2-tpk-baseline.h tools/tpk_review_test.c -lz -lm -o /tmp/tpk-review-baseline && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=0 /tmp/tpk-review-baseline header'
```

Fail-before metadata reproduction, built with the same compiler arguments and
baseline header:

```sh
OPENUG2_VALIDATION_BUDGET_MIB=512 python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py bash -lc 'gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-function -Isrc -include /tmp/nfsu2-tpk-baseline.h tools/tpk_review_test.c -lz -lm -o /tmp/tpk-review-baseline && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/tpk-review-baseline metadata'
```

Pass-after commands (each built and run in its own guarded invocation):

```sh
OPENUG2_VALIDATION_BUDGET_MIB=512 python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py bash -lc 'gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-function -Isrc tools/tpk_review_test.c -lz -lm -o /tmp/tpk-review-fixed && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/tpk-review-fixed header'
OPENUG2_VALIDATION_BUDGET_MIB=512 python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py bash -lc 'gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-unused-function -Isrc tools/tpk_review_test.c -lz -lm -o /tmp/tpk-review-fixed && ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/tpk-review-fixed metadata'
```

Fail-before results: header case reports ASan `heap-buffer-overflow` in
`n2_tpk_keys`; metadata case reports `ok=1 order=7 usage=9 blend=3 wz=5`.
Pass-after results: both cases print `PASS`, with no ASan/UBSan findings.
The host test cannot exercise Vita's 32-bit `long` ABI at runtime; the overflow
protection is established by the unsigned range check before conversion and
the invariant that all stored header ranges fit the nonnegative `long len`.

SHA-256 values for the reviewed source and test inputs are recorded alongside
the logs in `sha256.txt`.
