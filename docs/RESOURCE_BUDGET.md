# Bounded file owners

`resource.h` exposes a small C99 API for loading explicitly budgeted files.
Zero-initialize a `ResBudget` with `{0}`, then call `res_budget_init` once
before sharing it with `res_file_open`. Initialization returns false if an
existing budget still has live owners; after all owners close, it may be
reconfigured. The budget must remain alive until all corresponding
`res_file_close` calls finish. Initialize each `ResFile` to `{0}` and close it
before reusing it. A failed open into an empty output leaves it empty and
returns a path-specific error string when a buffer is supplied; an already
owned output is left untouched and rejected.

`limit` and `per_file` are byte counts stored as `uint32_t`. `per_file` limits
the payload length. `used` and `peak` include the private owner header, aligned
to the greatest C fundamental alignment it uses (including `long double` and
pointers), as well as payload, so metadata cannot escape the simultaneous-
memory limit.
`live` counts active owners. Releasing the last owner returns `used` to zero
and keeps `peak`; a failed short-read can raise `peak` because its buffer was
reserved and allocated before EOF was discovered. `res_budget_snapshot` reads
all counters under the same short lock used for reservation and release; the
lock is not held during file I/O.
Budgets use GCC/Clang 32-bit atomic spin-lock builtins and introduce no SDL or
thread-library link dependency.
The budget is a public C struct for stack/static allocation; treat its fields
as read-only and use `res_budget_snapshot` for synchronized counter reads.

`res_file_open` opens first, then requires `fstat` to report a non-empty regular
file (a symlink to a regular file is accepted). It checks representability by
`long`, `size_t` and the 32-bit accounting fields, and checks both limits before
allocation. A short read, allocation failure, or close failure frees the owner
and rolls back the reservation. This budget covers file buffers only; it does
not include decoded CPU parser meshes, GPU buffers, texture uploads or other
application allocations.

The compatibility `res_map_file` API retains lazy `mmap` on desktop. Builds
that define `OPENUG2_RESOURCE_BUFFERED` compile without `sys/mman.h` and use an
owned heap buffer instead. That backend has a 64 MiB total / 32 MiB per-file
default. `res_map_budget_configure(total, per_file)` changes it only before the
first legacy map attempt. Its private prefix records the owner and budget so
`res_unmap_file` releases both the allocation and accounting. Legacy callers
still receive `NULL` and length zero on a failed open; this is deliberately
not a silent `malloc` replacement for large lazy-mapped world bundles.

The focused tests exercise simultaneous owners, exact and over-limit budgets,
reinitialization refusal while owners are live, release/peak behavior,
deterministic short reads, sparse oversized files, owner alignment, regular-
file symlinks, rejection of directories, and both legacy backends under
ASan/UBSan. A separate pthread test races two opens against one budget slot
without compiling the short-read test hook; the buffered legacy API prints the
path, failure, and active limits when an open fails.
