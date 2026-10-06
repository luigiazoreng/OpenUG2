#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "resource.h"

struct AlignLongDouble { char byte; long double value; };
struct AlignPointer { char byte; void *value; };
struct AlignUint64 { char byte; uint64_t value; };

static size_t fundamental_alignment(void) {
    size_t alignment = offsetof(struct AlignLongDouble, value);
    if (offsetof(struct AlignPointer, value) > alignment)
        alignment = offsetof(struct AlignPointer, value);
    if (offsetof(struct AlignUint64, value) > alignment)
        alignment = offsetof(struct AlignUint64, value);
    return alignment;
}

#ifdef RESOURCE_TESTING
extern size_t res_test_read_limit;
#endif

static void write_bytes(const char *path, const unsigned char *bytes, size_t len) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(bytes, 1, len, file) == len);
    assert(fclose(file) == 0);
}

static uint32_t open_cost(ResBudget *budget, const char *path) {
    ResFile file = {0}; char error[256];
    assert(res_file_open(path, budget, &file, error, sizeof error));
    uint32_t cost = budget->used;
    res_file_close(&file);
    assert(!file.data && file.len == 0 && budget->used == 0 && budget->live == 0);
    return cost;
}

static void test_budgeted_owner(const char *a, const char *b, const char *large) {
    ResBudget budget = {0};
    assert(res_budget_init(&budget, 1u << 20, 1u << 19));
    uint32_t cost_a = open_cost(&budget, a), cost_b = open_cost(&budget, b);
    assert(cost_a > 0 && cost_b > 0);

    assert(res_budget_init(&budget, cost_a + cost_b, 64));
    ResFile fa = {0}, fb = {0}; char error[256];
    assert(res_file_open(a, &budget, &fa, error, sizeof error));
    assert(budget.live == 1 && fa.len == 8 && !memcmp(fa.data, "12345678", 8));
    assert((uintptr_t)fa.data % fundamental_alignment() == 0);
    ResBudgetStats snapshot;
    res_budget_snapshot(&budget, &snapshot);
    assert(snapshot.limit == cost_a + cost_b && snapshot.per_file == 64 &&
           snapshot.used == budget.used && snapshot.peak == budget.peak && snapshot.live == 1);
    assert(res_file_open(b, &budget, &fb, error, sizeof error));
    assert(budget.used == cost_a + cost_b && budget.peak == budget.used && budget.live == 2);
    res_file_close(&fa);
    assert(!fa.data && fa.len == 0 && budget.used == cost_b && budget.live == 1);
    assert(budget.peak == cost_a + cost_b);
    res_file_close(&fb);
    assert(budget.used == 0 && budget.live == 0 && budget.peak == cost_a + cost_b);

    /* A single allocation exactly at its total-budget boundary succeeds. */
    assert(res_budget_init(&budget, cost_a, 8));
    assert(res_file_open(a, &budget, &fa, error, sizeof error));
    assert(budget.used == cost_a && budget.peak == cost_a);
    res_file_close(&fa);

    /* One byte under the payload limit rejects before reserving or allocating. */
    assert(res_budget_init(&budget, 4096, 7));
    assert(!res_file_open(a, &budget, &fa, error, sizeof error));
    assert(error[0] && !fa.data && fa.len == 0 && !budget.used && !budget.live);

    /* Total-budget and oversized-per-file failures preserve all counters. */
    assert(res_budget_init(&budget, cost_a - 1, 8));
    assert(!res_file_open(a, &budget, &fa, error, sizeof error));
    assert(error[0] && !budget.used && !budget.peak && !budget.live);
    assert(res_budget_init(&budget, 4096, 16));
    assert(!res_file_open(large, &budget, &fa, error, sizeof error));
    assert(error[0] && !fa.data && fa.len == 0 && !budget.used && !budget.live);

#ifdef RESOURCE_TESTING
    /* Deterministic short-read path exercises close, rollback and zero output. */
    assert(res_budget_init(&budget, 4096, 64));
    res_test_read_limit = 3;
    assert(!res_file_open(a, &budget, &fa, error, sizeof error));
    res_test_read_limit = SIZE_MAX;
    assert(strstr(error, "short read") && !fa.data && fa.len == 0);
    assert(!budget.used && !budget.live && budget.peak > 0);
#endif

    puts("resource_budget_test: PASS (owners, exact limits, release, short read, failures)");
}

static void test_file_kinds(const char *a, const char *dir, const char *linkpath) {
    assert(symlink(a, linkpath) == 0);
    ResBudget budget = {0}; assert(res_budget_init(&budget, 4096, 64));
    ResFile file = {0}; char error[256];
    assert(res_file_open(linkpath, &budget, &file, error, sizeof error));
    assert(file.len == 8);
    res_file_close(&file);
    assert(!res_file_open(dir, &budget, &file, error, sizeof error));
    assert(error[0] && !file.data && file.len == 0 && !budget.used && !budget.live);
    puts("resource_budget_test: PASS (symlink target regular-file check)");
}

static void test_legacy(const char *path, const char *missing, const char *large) {
#ifdef OPENUG2_RESOURCE_BUFFERED
    assert(res_map_budget_configure(4096, 128));
    long len = 99;
    unsigned char *data = res_map_file(path, &len);
    assert(data && len == 8 && !memcmp(data, "12345678", 8));
#ifdef OPENUG2_RESOURCE_BUFFERED
    assert((uintptr_t)data % fundamental_alignment() == 0);
#endif
    assert(!res_map_budget_configure(8192, 128));
    res_unmap_file(data, len);
    assert(!res_map_file(large, &len) && len == 0);
    assert(!res_map_file(missing, &len) && len == 0);
    assert(!res_map_budget_configure(0, 1));
#else
    (void)large;
    long len = 99;
    unsigned char *data = res_map_file(path, &len);
    assert(data && len == 8 && !memcmp(data, "12345678", 8));
    res_unmap_file(data, len);
    assert(!res_map_file(missing, &len) && len == 0);
#endif
    puts("resource_budget_test: PASS (legacy map/unmap backend)");
}

int main(void) {
    char root[] = "/tmp/openug2-resource-budget-XXXXXX";
    assert(mkdtemp(root));
    char a[512], b[512], large[512], dir[512], linkpath[512], missing[512];
    snprintf(a, sizeof a, "%s/a.bin", root);
    snprintf(b, sizeof b, "%s/b.bin", root);
    snprintf(large, sizeof large, "%s/large.bin", root);
    snprintf(dir, sizeof dir, "%s/subdir", root);
    snprintf(linkpath, sizeof linkpath, "%s/link.bin", root);
    snprintf(missing, sizeof missing, "%s/missing.bin", root);
    assert(mkdir(dir, 0700) == 0);
    write_bytes(a, (const unsigned char *)"12345678", 8);
    write_bytes(b, (const unsigned char *)"abcdefghij", 10);
    int fd = creat(large, 0600); assert(fd >= 0);
    assert(ftruncate(fd, 4096) == 0); assert(close(fd) == 0);

    test_budgeted_owner(a, b, large);
    test_file_kinds(a, dir, linkpath);
    test_legacy(a, missing, large);

    assert(unlink(linkpath) == 0);
    assert(unlink(a) == 0 && unlink(b) == 0 && unlink(large) == 0);
    assert(rmdir(dir) == 0 && rmdir(root) == 0);
    puts("resource_budget_test: PASS (cleanup)");
    return 0;
}
