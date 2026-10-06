#ifndef OPENUG2_GPU_PROBE_H
#define OPENUG2_GPU_PROBE_H

/* Small synthetic GPU tests. Requires the owning thread's current GL context.
   Leaves default framebuffer, texture unit zero and ordinary opaque state.
   Reports native compression separately from decoded RGBA fallback. */
int vita_gpu_probe_run(void);

#endif
