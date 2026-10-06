#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "resource.h"

/* Five synthetic 24-byte waypoints in one closed loop. */
static void write_path(const char *path) {
    unsigned char data[8 + 5 * 24] = {0};
    uint32_t magic = 0x34148, size = 5 * 24;
    memcpy(data, &magic, sizeof magic);
    memcpy(data + 4, &size, sizeof size);
    const float points[5][2] = {{0,0},{40,0},{40,40},{0,40},{0,0}};
    for (int i = 0; i < 5; i++) memcpy(data + 8 + i * 24, points[i], sizeof points[i]);
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(data, 1, sizeof data, file) == sizeof data);
    assert(fclose(file) == 0);
}

static void path_name(char *out, size_t length) {
    memset(out, 'x', length);
    memcpy(out, "Paths", 5);
    memcpy(out + length - 4, ".bin", 4);
    out[length] = 0;
}

static void test_boundary(void) {
    char root[] = "/tmp/openug2-resource-test-XXXXXX";
    assert(mkdtemp(root));
    char directory[512], path[1024];
    snprintf(directory, sizeof directory, "%s/ROUTESL4RA", root);
    assert(mkdir(directory, 0700) == 0);

    char names[3][256] = {"Paths4000.bin"};
    path_name(names[1], 244); /* Relative name: exactly 255 bytes plus NUL. */
    path_name(names[2], 245); /* Relative name: one byte beyond the output. */
    for (int i = 0; i < 3; i++) {
        int length = snprintf(path, sizeof path, "%s/%s", directory, names[i]);
        assert(length >= 0 && (size_t)length < sizeof path);
        write_path(path);
    }
    char list[4][256]; memset(list, 0xa5, sizeof list);
    int count = res_list_circuits(root, "STREAML4RA", list, 4);
    assert(count == 2);
    int short_found = 0, boundary_found = 0;
    for (int i = 0; i < count; i++) {
        assert(memchr(list[i], 0, sizeof list[i]));
        if (!strcmp(list[i], "ROUTESL4RA/Paths4000.bin")) short_found = 1;
        if (strlen(list[i]) == 255 && !strcmp(list[i] + 11, names[1])) boundary_found = 1;
    }
    assert(short_found && boundary_found);
    assert(res_list_circuits(root, "ALL", list, 4) == 0);
    for (int i = 0; i < 3; i++) {
        int length = snprintf(path, sizeof path, "%s/%s", directory, names[i]);
        assert(length >= 0 && (size_t)length < sizeof path);
        assert(unlink(path) == 0);
    }
    assert(rmdir(directory) == 0);
    assert(rmdir(root) == 0);
    puts("resource_test: PASS (boundary names, termination, oversized paths)");
}

static void test_prefix(void) {
    char root[] = "/tmp/openug2-resource-prefix-test-XXXXXX";
    assert(mkdtemp(root));
    char directory[512], path[1024];
    char list[4][256]; memset(list, 0xa5, sizeof list);
    /* A request must not open the existing directory at its truncated prefix. */
    char stem[251], truncated[256];
    memset(stem, 'L', 250); stem[250] = 0;
    memcpy(truncated, "ROUTES", 6);
    memcpy(truncated + 6, stem, 249); truncated[255] = 0;
    snprintf(directory, sizeof directory, "%s/%s", root, truncated);
    assert(mkdir(directory, 0700) == 0);
    snprintf(path, sizeof path, "%s/Paths4000.bin", directory);
    write_path(path);
    assert(res_list_circuits(root, stem, list, 4) == 0);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    assert(rmdir(root) == 0);
    puts("resource_test: PASS (oversized circuit prefix is rejected)");
}

int main(int argc, char **argv) {
    int run_boundary = argc == 1 || (argc == 2 && !strcmp(argv[1], "boundary"));
    int run_prefix = argc == 1 || (argc == 2 && !strcmp(argv[1], "prefix"));
    if (!run_boundary && !run_prefix) {
        fprintf(stderr, "usage: %s [boundary|prefix]\n", argv[0]);
        return 2;
    }
    if (run_boundary) test_boundary();
    if (run_prefix) test_prefix();
    return 0;
}
