/* Independent reproductions for the 2026-10-05 TPK review findings.
 * Build with ASan/UBSan and pass exactly one case name: header or metadata. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/nfsu2.h"

static void put32(unsigned char *p, uint32_t v) {
    p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8);
    p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24);
}
static int fail(const char *s) { fprintf(stderr, "FAIL: %s\n", s); return 1; }

static int test_header(void) {
    unsigned char *d = calloc(1, 0x70);
    if (!d) return fail("allocation");
    put32(d, 0xb3310000u); put32(d+4, 0x10000u);
    put32(d+0x60, 0x33320002u); put32(d+0x64, 8);
    N2Tpk t = n2_tpk_open(d, 0x70);
    uint32_t keys[4];
    int n = n2_tpk_keys(d, t, keys, 4);
    free(t.blk); free(d);
    if (n != 0) return fail("truncated declared header was accepted");
    puts("PASS: truncated 0x70-byte file declaring a 64 KiB header is rejected");
    return 0;
}

static int append_block(unsigned char *d, long *at, int short_record,
                        uint32_t hash, int bad_pixels) {
    long rec_len = short_record ? 0x44 : 0x4c;
    long start = *at;
    put32(d+start, 0xb3310000u); put32(d+start+4, (uint32_t)rec_len);
    unsigned char *r = d+start+8;
    memcpy(r, "REVIEW_RECORD", 13);
    put32(r+0x18, hash);
    put32(r+0x24, bad_pixels ? UINT32_MAX : 0);
    r[0x38]=4; r[0x3a]=4; r[0x3e]=N2_TEXFMT_DXT1;
    if (!short_record) {
        r[0x45]=7; r[0x49]=9; r[0x4a]=3; r[0x4b]=5;
    }
    *at += 8+rec_len;
    put32(d+*at, 0x33320002u); put32(d+*at+4, 8);
    memset(d+*at+8, 0, 8); *at += 16;
    return 0;
}

static int test_metadata(void) {
    unsigned char d[512] = {0};
    long at = 0; const uint32_t hash=0x13572468u;
    append_block(d, &at, 0, hash, 1); /* writes 7/9/3/5, then bad pixel offset */
    append_block(d, &at, 1, hash, 0); /* succeeds, without the 0x4c metadata tail */
    N2Tpk t = n2_tpk_open(d, at);
    N2Tex tex;
    int ok = n2_tpk_decode(d, at, t, hash, &tex);
    int valid = ok && tex.rgb && tex.order==0 && tex.usage==0 &&
                tex.blend==0 && tex.wz==0;
    if (tex.rgb) free(tex.rgb);
    if (tex.alpha) free(tex.alpha);
    if (tex.dxt) free(tex.dxt);
    free(t.blk);
    if (!valid) {
        fprintf(stderr, "FAIL: ok=%d order=%u usage=%u blend=%u wz=%u\n",
                ok, tex.order, tex.usage, tex.blend, tex.wz);
        return 1;
    }
    puts("PASS: rejected candidate metadata does not contaminate the short successful record");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s header|metadata\n", argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "header")) return test_header();
    if (!strcmp(argv[1], "metadata")) return test_metadata();
    fprintf(stderr, "unknown case: %s\n", argv[1]);
    return 2;
}
