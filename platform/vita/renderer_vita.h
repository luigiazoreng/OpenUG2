#ifndef OPENUG2_VITA_RENDERER_H
#define OPENUG2_VITA_RENDERER_H

#include <stddef.h>

/* GL/Vita types stay inside the bootstrap renderer implementation. */
typedef struct VitaRenderer VitaRenderer;
VitaRenderer *vita_renderer_create(void);
int vita_renderer_draw(VitaRenderer *r, int asset_ok, int paused, int loading);
void vita_renderer_destroy(VitaRenderer *r);
size_t vita_renderer_estimated_bytes(void);
void vita_renderer_log_pools(void);

#endif
