/* resource.h — OpenUG2 ResourceManager module: file mapping and asset
 * discovery (which tracks / cars / circuits exist on disk). Chunk PARSING
 * stays in nfsu2.h — the ground-truth parser this module feeds. */
#ifndef OPENUG2_RESOURCE_H
#define OPENUG2_RESOURCE_H

#include <stddef.h>
#include <stdint.h>

/* Explicitly budgeted, buffered file owner. Limits count payload bytes per
 * file and payload plus private owner metadata against total/live accounting.
 * The budget must outlive its open files. Zero it, initialize before sharing,
 * and do not modify its public counters directly. */
typedef struct {
    uint32_t limit, per_file, used, peak, live;
    uint32_t initialized;
    volatile uint32_t lock;
} ResBudget;

typedef struct {
    uint32_t limit, per_file, used, peak, live;
} ResBudgetStats;

typedef struct {
    unsigned char *data;
    long len;
} ResFile;

int res_budget_init(ResBudget *budget, uint32_t limit_bytes,
                    uint32_t per_file_bytes);
void res_budget_snapshot(ResBudget *budget, ResBudgetStats *out);
int res_file_open(const char *path, ResBudget *budget, ResFile *out,
                  char *error, size_t errorcap);
void res_file_close(ResFile *file);

/* The compatibility API uses mmap on desktop. Define
 * OPENUG2_RESOURCE_BUFFERED to use a bounded heap buffer (no sys/mman header),
 * with a default 64 MiB total / 32 MiB per-file budget. Configuration must
 * happen before the first mapping; it fails while buffered mappings are live.
 * `len` is set to zero on every failed open. */
int res_map_budget_configure(uint32_t limit_bytes, uint32_t per_file_bytes);
unsigned char *res_map_file(const char *path, long *len);
void res_unmap_file(unsigned char *data, long len);

/* Enumerate selectable tracks: STREAM*.BUN files under troot. Writes up to
 * max names (extension stripped) and sets *sel to the entry matching cur.
 * Returns the count. */
int res_list_tracks(const char *troot, char (*list)[64], int max,
                    const char *cur, int *sel);

/* Enumerate selectable cars: folders under DATAROOT/CARS with a GEOMETRY.BIN,
 * minus the shared part folders (WHEELS, SPOILER, ...). */
int res_list_cars(const char *dataroot, char (*list)[64], int max,
                  const char *cur, int *sel);

/* Enumerate the selected region's own selectable circuits: closed-loop
 * Paths*.bin (first waypoint ~= last) from the ONE route directory that
 * belongs to trackname -- STREAML4RA -> ROUTESL4RA, a 1:1 shipped mapping
 * (M87: every race catalog lives in exactly one region directory, and no
 * event references a second bundle). trackname "ALL" returns 0: a blind union
 * of bundles is not a valid race world (M86), so it offers no circuits.
 * Paths are written relative to troot ("ROUTESX/PathsN.bin"). */
int res_list_circuits(const char *troot, const char *trackname,
                      char (*list)[256], int max);

#endif
