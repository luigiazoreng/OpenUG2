#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "car_viewer.h"
#include "render.h"

enum { VIEW_W = 960, VIEW_H = 544 };

void vita_log(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s --data-root DIR --output NEW_DIR\n"
                    "       %s DIR NEW_DIR\n", argv0, argv0);
}

static void parse_args(int argc, char **argv, const char **root, const char **out) {
    *root = NULL; *out = NULL;
    if (argc == 3 && argv[1][0] != '-') {
        *root = argv[1]; *out = argv[2]; return;
    }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data-root") && i + 1 < argc) *root = argv[++i];
        else if (!strcmp(argv[i], "--output") && i + 1 < argc) *out = argv[++i];
        else { usage(argv[0]); exit(2); }
    }
    if (!*root || !*out) { usage(argv[0]); exit(2); }
}

static void output_path(char *out, size_t cap, const char *dir, const char *name) {
    int n = snprintf(out, cap, "%s/%s", dir, name);
    assert(n >= 0 && (size_t)n < cap);
}

static unsigned char *read_rgba(void) {
    size_t bytes = (size_t)VIEW_W * VIEW_H * 4;
    unsigned char *pixels = malloc(bytes);
    assert(pixels);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, VIEW_W, VIEW_H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(glGetError() == GL_NO_ERROR);
    return pixels;
}

static void save_rgb_png(const char *path, const unsigned char *rgba) {
    unsigned char *rgb = malloc((size_t)VIEW_W * VIEW_H * 3);
    assert(rgb);
    for (int y = 0; y < VIEW_H; y++) {
        int src_y = VIEW_H - 1 - y; /* GL bottom-left to PNG top-left */
        for (int x = 0; x < VIEW_W; x++) {
            const unsigned char *p = rgba + ((size_t)src_y * VIEW_W + x) * 4;
            unsigned char *q = rgb + ((size_t)y * VIEW_W + x) * 3;
            q[0] = p[0]; q[1] = p[1]; q[2] = p[2];
        }
    }
    write_png(path, VIEW_W, VIEW_H, rgb);
    free(rgb);
}

static unsigned long long region_delta(const unsigned char *a, const unsigned char *b,
                                       int *changed) {
    unsigned long long total = 0;
    *changed = 0;
    /* Exclude the top-left help strip and the screen edges. */
    for (int y = 120; y < 450; y++) for (int x = 200; x < 760; x++) {
        size_t p = ((size_t)y * VIEW_W + x) * 4;
        unsigned d = 0;
        for (int c = 0; c < 3; c++) {
            unsigned av = a[p+c], bv = b[p+c];
            d += av > bv ? av - bv : bv - av;
        }
        total += d;
        if (d >= 24) (*changed)++;
    }
    return total;
}

static unsigned count_car_pixels(const unsigned char *rgba) {
    unsigned count = 0;
    /* Bright surfaces in the upper-center ROI distinguish body/rim pixels
       from both the floor and background, without treating a clear as a car. */
    for (int y = 150; y < 395; y++) for (int x = 220; x < 740; x++) {
        size_t p = ((size_t)y * VIEW_W + x) * 4;
        int r = rgba[p], g = rgba[p+1], b = rgba[p+2];
        if (r > 88 || g > 88 || b > 88) count++;
    }
    return count;
}

static unsigned pixel_delta(const unsigned char *a, const unsigned char *b,
                            int x, int y) {
    size_t p = ((size_t)y * VIEW_W + x) * 4;
    unsigned d = 0;
    for (int c = 0; c < 3; c++) {
        unsigned av = a[p+c], bv = b[p+c];
        d += av > bv ? av - bv : bv - av;
    }
    return d;
}

static void draw_frame(CarViewer *viewer, float yaw, int paint, int lights,
                       int paused, unsigned char **out) {
    assert(car_viewer_draw(viewer, yaw, 0.28f, 7.0f, paint, lights, paused,
                           VIEW_W, VIEW_H));
    glFinish();
    assert(glGetError() == GL_NO_ERROR);
    *out = read_rgba();
}

static void save_frame(const char *dir, const char *name, const unsigned char *rgba) {
    char path[1024]; output_path(path, sizeof path, dir, name);
    save_rgb_png(path, rgba);
    struct stat st;
    assert(stat(path, &st) == 0 && st.st_size > 64);
}

static void run_viewer_cycle(const char *root, const char *output, int cycle) {
    char error[512] = {0};
    CarViewer *viewer = car_viewer_create(root, error, sizeof error);
    if (!viewer) { fprintf(stderr, "car_viewer_create: %s\n", error); abort(); }
    CarViewerStats stats = {0};
    car_viewer_stats(viewer, &stats);
    assert(stats.meshes == 101 && stats.textures == 7);
    assert(stats.cpu_mesh_bytes > 0 && stats.cpu_mesh_bytes < 2u * 1024u * 1024u);
    assert(stats.file_peak_bytes > 0 && stats.file_peak_bytes < 32u * 1024u * 1024u);
    assert(stats.gpu_estimated_bytes > 0 && stats.gpu_estimated_bytes < 24u * 1024u * 1024u);
    assert(glGetError() == GL_NO_ERROR);

    unsigned char *front0 = NULL, *front1 = NULL, *front_lights = NULL, *rear = NULL;
    unsigned char *running = NULL, *paused = NULL;
    const float front_yaw = -0.6f, rear_yaw = 2.5f;
    draw_frame(viewer, front_yaw, 0, 0, 0, &front0);
    draw_frame(viewer, front_yaw, 1, 0, 0, &front1);
    draw_frame(viewer, front_yaw, 0, 1, 0, &front_lights);
    draw_frame(viewer, rear_yaw, 0, 0, 0, &rear);
    draw_frame(viewer, rear_yaw, 0, 0, 0, &running);
    draw_frame(viewer, rear_yaw, 0, 0, 1, &paused);

    unsigned visible_front = count_car_pixels(front0);
    unsigned visible_rear = count_car_pixels(rear);
    int paint_changed = 0, lights_changed = 0, pause_changed = 0;
    unsigned long long paint_delta = region_delta(front0, front1, &paint_changed);
    unsigned long long light_delta = region_delta(front0, front_lights, &lights_changed);
    unsigned long long pause_delta = region_delta(running, paused, &pause_changed);
    unsigned pause_bg_delta = pixel_delta(running, paused, 900, 500);
    car_viewer_stats(viewer, &stats);
    fprintf(stderr, "pixels cycle=%d draws=%u foreground front=%u rear=%u paint changed=%d delta=%llu front-lights changed=%d delta=%llu pause changed=%d delta=%llu bg-pixel delta=%u\n",
            cycle + 1, stats.draws, visible_front, visible_rear, paint_changed, paint_delta,
            lights_changed, light_delta, pause_changed, pause_delta, pause_bg_delta);
    if (cycle == 0) {
        save_frame(output, "front-stock.png", front0);
        save_frame(output, "front-paint.png", front1);
        save_frame(output, "rear-stock.png", rear);
        save_frame(output, "front-lights.png", front_lights);
        save_frame(output, "paused.png", paused);
    }
    /* Both camera views must show the car. Paint and the proven front light
       emitters must affect the projected car; pause changes a background pixel. */
    assert(visible_front > 1000 && visible_rear > 1000 && stats.draws > 100 && stats.draws < 200);
    assert(paint_changed > 1000 && paint_delta > 100000);
    assert(lights_changed > 100 && light_delta > 10000);
    assert(pause_bg_delta > 40 && pause_changed > 1000 && pause_delta > 100000);

    printf("viewer cycle=%d meshes=%u textures=%u draws=%u gpu_estimated=%lu cpu_mesh=%lu file_peak=%lu\n",
           cycle + 1, stats.meshes, stats.textures, stats.draws,
           (unsigned long)stats.gpu_estimated_bytes,
           (unsigned long)stats.cpu_mesh_bytes, (unsigned long)stats.file_peak_bytes);
    printf("pixels cycle=%d foreground front=%u rear=%u paint changed=%d delta=%llu front-lights changed=%d delta=%llu pause changed=%d delta=%llu bg-pixel delta=%u\n",
           cycle + 1, visible_front, visible_rear, paint_changed, paint_delta,
           lights_changed, light_delta, pause_changed, pause_delta, pause_bg_delta);

    free(front0); free(front1); free(front_lights); free(rear); free(running); free(paused);
    car_viewer_destroy(viewer);
    assert(glGetError() == GL_NO_ERROR);
}

int main(int argc, char **argv) {
    const char *root, *output; parse_args(argc, argv, &root, &output);
    struct stat st;
    assert(stat(output, &st) != 0 && mkdir(output, 0700) == 0);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1;
    }
#ifdef N2_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window *window = SDL_CreateWindow("240SX stock GL harness", 0, 0,
        VIEW_W, VIEW_H, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }
    const GLubyte *version = glGetString(GL_VERSION), *renderer = glGetString(GL_RENDERER);
    printf("GL version=%s renderer=%s\n", version ? (const char *)version : "?",
           renderer ? (const char *)renderer : "?");
    assert(version && renderer && glGetError() == GL_NO_ERROR);
    glDisable(GL_DITHER);
    run_viewer_cycle(root, output, 0);
    run_viewer_cycle(root, output, 1);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("vita_car_viewer_test: PASS (production renderer, 240SX stock and teardown)");
    return 0;
}
