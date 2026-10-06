/* resource.c — OpenUG2 ResourceManager module implementation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>
#ifndef OPENUG2_RESOURCE_BUFFERED
#include <sys/mman.h>
#endif

#include "nfsu2.h"
#include "resource.h"

#define RES_OWNER_MAGIC UINT32_C(0x52534f57)
#define RES_BUDGET_MAGIC UINT32_C(0x52534247)
#define RES_MIB UINT32_C(1048576)

/* The header's size is a multiple of its alignment, so payloads retain malloc
 * alignment. It is deliberately part of the accounted heap reservation. */
typedef struct {
    uint32_t magic, reserved;
    ResBudget *budget;
    uint32_t allocation;
    uint32_t payload;
} ResOwner;

static ResBudget default_budget = {
    UINT32_C(64) * RES_MIB, UINT32_C(32) * RES_MIB, 0, 0, 0,
    RES_BUDGET_MAGIC, 0
};
static int default_budget_started;
#ifdef RESOURCE_TESTING
size_t res_test_read_limit = SIZE_MAX;
#endif

static void budget_lock(ResBudget *budget) {
    while (__sync_lock_test_and_set(&budget->lock, 1u))
        while (__sync_val_compare_and_swap(&budget->lock, 0u, 0u) != 0u) { }
}

static void budget_unlock(ResBudget *budget) {
    __sync_lock_release(&budget->lock);
}

int res_budget_init(ResBudget *budget, uint32_t limit_bytes,
                    uint32_t per_file_bytes) {
    if (!budget || !limit_bytes || !per_file_bytes || per_file_bytes > limit_bytes ||
        per_file_bytes > UINT32_MAX - (uint32_t)sizeof(ResOwner)) return 0;
    if (budget->initialized == RES_BUDGET_MAGIC) {
        budget_lock(budget);
        if (budget->live) { budget_unlock(budget); return 0; }
        budget->limit = limit_bytes;
        budget->per_file = per_file_bytes;
        budget->used = 0;
        budget->peak = 0;
        budget_unlock(budget);
        return 1;
    }
    if (budget->limit || budget->per_file || budget->used || budget->peak ||
        budget->live || budget->initialized || budget->lock) return 0;
    budget->limit = limit_bytes;
    budget->per_file = per_file_bytes;
    budget->used = 0;
    budget->peak = 0;
    budget->live = 0;
    budget->initialized = RES_BUDGET_MAGIC;
    budget->lock = 0;
    return 1;
}

void res_budget_snapshot(ResBudget *budget, ResBudgetStats *out) {
    if (!budget || !out || budget->initialized != RES_BUDGET_MAGIC) return;
    budget_lock(budget);
    out->limit = budget->limit;
    out->per_file = budget->per_file;
    out->used = budget->used;
    out->peak = budget->peak;
    out->live = budget->live;
    budget_unlock(budget);
}

static int budget_reserve(ResBudget *budget, uint32_t payload,
                          uint32_t total) {
    int ok = 0;
    budget_lock(budget);
    if (payload && payload <= budget->per_file && total >= payload &&
        total <= UINT32_MAX - budget->used &&
        budget->used + total <= budget->limit && budget->live < UINT32_MAX) {
        budget->used += total;
        if (budget->used > budget->peak) budget->peak = budget->used;
        budget->live++;
        ok = 1;
    }
    budget_unlock(budget);
    return ok;
}

static void budget_release(ResBudget *budget, uint32_t total) {
    budget_lock(budget);
    if (budget->used >= total) budget->used -= total;
    else budget->used = 0;
    if (budget->live) budget->live--;
    budget_unlock(budget);
}

int res_map_budget_configure(uint32_t limit_bytes, uint32_t per_file_bytes) {
    if (!limit_bytes || !per_file_bytes || per_file_bytes > limit_bytes ||
        per_file_bytes > UINT32_MAX - (uint32_t)sizeof(ResOwner)) return 0;
    budget_lock(&default_budget);
    if (default_budget.live || default_budget_started) {
        budget_unlock(&default_budget);
        return 0;
    }
    default_budget.limit = limit_bytes;
    default_budget.per_file = per_file_bytes;
    default_budget.used = 0;
    default_budget.peak = 0;
    budget_unlock(&default_budget);
    return 1;
}

static void set_error(char *error, size_t cap, const char *path,
                      const char *reason) {
    if (error && cap) snprintf(error, cap, "%s: %s", path ? path : "file", reason);
}

int res_file_open(const char *path, ResBudget *budget, ResFile *out,
                  char *error, size_t errorcap) {
    if (error && errorcap) error[0] = 0;
    if (!path || !budget || !out) {
        set_error(error, errorcap, path, "invalid argument");
        return 0;
    }
    if (budget->initialized != RES_BUDGET_MAGIC) {
        set_error(error, errorcap, path, "resource budget is not initialized");
        return 0;
    }
    if (out->data || out->len) {
        set_error(error, errorcap, path, "output owner is not empty");
        return 0;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        char message[160]; snprintf(message, sizeof message, "open failed: %s", strerror(errno));
        set_error(error, errorcap, path, message);
        return 0;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        int saved = errno;
        close(fd);
        set_error(error, errorcap, path, strerror(saved));
        return 0;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd); set_error(error, errorcap, path, "not a regular file");
        return 0;
    }
    if (st.st_size <= 0) {
        close(fd); set_error(error, errorcap, path, "empty file"); return 0;
    }
    if ((uintmax_t)st.st_size > (uintmax_t)LONG_MAX ||
        (uintmax_t)st.st_size > (uintmax_t)SIZE_MAX - sizeof(ResOwner) ||
        (uintmax_t)st.st_size > UINT32_MAX) {
        close(fd); set_error(error, errorcap, path, "file size is not representable"); return 0;
    }
    uint32_t payload = (uint32_t)st.st_size;
    if (payload > UINT32_MAX - (uint32_t)sizeof(ResOwner)) {
        close(fd); set_error(error, errorcap, path, "file plus owner header is too large"); return 0;
    }
    uint32_t total = payload + (uint32_t)sizeof(ResOwner);
    if (!budget_reserve(budget, payload, total)) {
        close(fd); set_error(error, errorcap, path, "resource budget or per-file limit exceeded"); return 0;
    }
    ResOwner *owner = (ResOwner *)malloc((size_t)total);
    if (!owner) {
        budget_release(budget, total); close(fd);
        set_error(error, errorcap, path, "allocation failed"); return 0;
    }
    owner->magic = RES_OWNER_MAGIC;
    owner->reserved = 0;
    owner->budget = budget;
    owner->allocation = total;
    owner->payload = payload;
    unsigned char *data = (unsigned char *)(owner + 1);
    size_t done = 0;
#ifdef RESOURCE_TESTING
    extern size_t res_test_read_limit;
#endif
    while (done < (size_t)payload) {
        size_t ask = (size_t)payload - done;
        if (ask > (size_t)INT_MAX) ask = (size_t)INT_MAX;
#ifdef RESOURCE_TESTING
        if (res_test_read_limit < ask) ask = res_test_read_limit;
        if (!ask) break;
#endif
        ssize_t n = read(fd, data + done, ask);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += (size_t)n;
#ifdef RESOURCE_TESTING
        res_test_read_limit -= (size_t)n;
#endif
    }
    int close_error = close(fd);
    if (done != (size_t)payload || close_error != 0) {
        free(owner); budget_release(budget, total);
        set_error(error, errorcap, path,
                  done != (size_t)payload ? "short read" : "close failed");
        return 0;
    }
    out->data = data;
    out->len = (long)payload;
    return 1;
}

void res_file_close(ResFile *file) {
    if (!file) return;
    if (file->data) {
        ResOwner *owner = ((ResOwner *)file->data) - 1;
        if (owner->magic == RES_OWNER_MAGIC && owner->budget) {
            ResBudget *budget = owner->budget;
            uint32_t allocation = owner->allocation;
            owner->magic = 0;
            free(owner);
            budget_release(budget, allocation);
        }
    }
    file->data = NULL;
    file->len = 0;
}

unsigned char *res_map_file(const char *path, long *len) {
    if (len) *len = 0;
    if (!path || !len) return NULL;
#ifdef OPENUG2_RESOURCE_BUFFERED
    ResFile file = {0};
    char error[256];
    budget_lock(&default_budget);
    default_budget_started = 1;
    budget_unlock(&default_budget);
    if (!res_file_open(path, &default_budget, &file, error, sizeof error)) return NULL;
    unsigned char *data = file.data;
    *len = file.len;
    file.data = NULL; file.len = 0; /* ownership is now legacy pointer metadata */
    return data;
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (uintmax_t)st.st_size > (uintmax_t)LONG_MAX ||
        (uintmax_t)st.st_size > (uintmax_t)SIZE_MAX) { close(fd); return NULL; }
    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return NULL;
    *len = (long)st.st_size;
    return (unsigned char *)p;
#endif
}

void res_unmap_file(unsigned char *data, long len) {
    if (!data || len <= 0) return;
#ifdef OPENUG2_RESOURCE_BUFFERED
    ResFile file = {data, len};
    res_file_close(&file);
#else
    munmap(data, (size_t)len);
#endif
}

int res_list_tracks(const char *troot, char (*list)[64], int max,
                    const char *cur, int *sel) {
    int n = 0;
    DIR *td = opendir(troot); struct dirent *de;
    while (td && n < max && (de = readdir(td))) {
        char *dot = strstr(de->d_name, ".BUN");
        if (strncmp(de->d_name, "STREAM", 6) || !dot) continue;
        int L = (int)(dot - de->d_name); if (L > 63) L = 63;
        memcpy(list[n], de->d_name, L); list[n][L] = 0;
        if (!strcmp(list[n], cur)) *sel = n;
        n++;
    }
    if (td) closedir(td);
    return n;
}

int res_list_cars(const char *dataroot, char (*list)[64], int max,
                  const char *cur, int *sel) {
    static const char *parts[] = { "AUDIO","BRAKES","EXHAUST","PLATES","ROOF",
        "WHEELS","SPOILER","SPOILER_HATCH","SPOILER_SUV","MIRRORS_BODY",
        "MIRRORS_HUMMER","MIRRORS_POST","MIRRORS_SUV" };
    int n = 0;
    char croot[1024];
    int root_length = snprintf(croot, sizeof croot, "%s/CARS", dataroot);
    if (root_length < 0 || (size_t)root_length >= sizeof croot) return 0;
    DIR *cd = opendir(croot); struct dirent *de;
    while (cd && n < max && (de = readdir(cd))) {
        if (de->d_name[0] == '.') continue;
        int isp = 0;
        for (unsigned p = 0; p < sizeof parts/sizeof parts[0]; p++)
            if (!strcmp(de->d_name, parts[p])) isp = 1;
        if (isp) continue;
        char gp[1200];
        int length = snprintf(gp, sizeof gp, "%s/%s/GEOMETRY.BIN", croot, de->d_name);
        if (length < 0 || (size_t)length >= sizeof gp) continue;
        FILE *f = fopen(gp, "rb"); if (!f) continue; fclose(f);
        int L = (int)strlen(de->d_name); if (L > 63) L = 63;
        memcpy(list[n], de->d_name, L); list[n][L] = 0;
        if (!strcmp(list[n], cur)) *sel = n;
        n++;
    }
    if (cd) closedir(cd);
    return n;
}

int res_list_circuits(const char *troot, const char *trackname,
                      char (*list)[256], int max) {
    /* ALL is a blind union of incompatible bundles, not a race world. */
    if (!trackname || !strcmp(trackname, "ALL")) return 0;
    /* the shipped 1:1 mapping: STREAM<stem>.BUN <-> ROUTES<stem>/ */
    const char *stem = strncmp(trackname, "STREAM", 6) ? trackname : trackname + 6;
    char dname[256];
    int directory_length = snprintf(dname, sizeof dname, "ROUTES%s", stem);
    if (directory_length < 0 || (size_t)directory_length >= sizeof dname) return 0;
    char rdir[1200];
    directory_length = snprintf(rdir, sizeof rdir, "%s/%s", troot, dname);
    if (directory_length < 0 || (size_t)directory_length >= sizeof rdir) return 0;

    int n = 0;
    DIR *rd = opendir(rdir); struct dirent *re;
    while (rd && n < max && (re = readdir(rd))) {
        if (strncmp(re->d_name, "Paths", 5)) continue;
        char rel[256];
        int length = snprintf(rel, sizeof rel, "%s/%s", dname, re->d_name);
        if (length < 0 || (size_t)length >= sizeof rel) continue;
        int dup=0; for (int i=0;i<n;i++) if(!strcmp(list[i],rel)) dup=1;
        if (dup) continue;
        char pp[1500];
        int path_length = snprintf(pp, sizeof pp, "%s/%s", troot, rel);
        if (path_length < 0 || (size_t)path_length >= sizeof pp) continue;
        long pl; unsigned char *pd = n2_read_file(pp, &pl); N2Path tp = {0};
        if (pd && n2_load_path(pd, pl, &tp) > 4) {
            /* closed loop: first waypoint ~= last (sprints are open routes) */
            float dx=tp.xy[0]-tp.xy[(tp.n-1)*2], dy=tp.xy[1]-tp.xy[(tp.n-1)*2+1];
            if (dx*dx+dy*dy < 900.0f) {
                memcpy(list[n], rel, (size_t)length + 1);
                n++;
            }
        }
        free(tp.xy); free(pd);
    }
    if (rd) closedir(rd);
    return n;
}
