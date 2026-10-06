#ifndef OPENUG2_CAR_VIEWER_H
#define OPENUG2_CAR_VIEWER_H
#include <stddef.h>

typedef struct CarViewer CarViewer;
typedef struct {
    unsigned draws, textures, meshes;
    size_t gpu_estimated_bytes, cpu_mesh_bytes, file_peak_bytes;
} CarViewerStats;

CarViewer *car_viewer_create(const char *root, char *error, size_t errorcap);
int car_viewer_draw(CarViewer *viewer, float yaw, float pitch, float distance,
                    int paint, int lights, int paused, int width, int height);
void car_viewer_stats(const CarViewer *viewer, CarViewerStats *out);
void car_viewer_destroy(CarViewer *viewer);

#endif
