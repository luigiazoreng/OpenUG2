Validation commands and captured logs for the renderer uniform-state migration.
All commands use `/tmp/openug2-validation.lock`, the guarded issue-7 runner,
a 512 MiB cgroup budget, and a single make job. SDL headers/libraries come
from `/tmp/openug2-vita-reference/desktop-sdl`.

Targets captured here: `normal`, `menu`, `gles`, `debug`, `render-uniform-cache-test`,
and `render-uniform-live-test`. Each build/test used `-j1` and the same guard:

```sh
flock /tmp/openug2-validation.lock env OPENUG2_VALIDATION_BUDGET_MIB=512 \
  CFLAGS='-O2 -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-unused-function' \
  python3 /home/luigi/Documents/luigi/nfsu2-data/reports/guarded-issue7-validation.py \
  make -j1 SDL_CFLAGS='-I/tmp/openug2-vita-reference/desktop-sdl/usr/include/SDL2 -I/tmp/openug2-vita-reference/desktop-sdl/usr/include/x86_64-linux-gnu -D_REENTRANT' \
  SDL_LIBS='-l:libSDL2-2.0.so.0' TARGET
```

The cache test recipe adds `-fsanitize=address,undefined` and
`-fno-omit-frame-pointer`. The live test runs against the host desktop GL
context and uses `glGetUniformfv` only as its test oracle.

`renderer-regressions.log` captures the existing wheel, headlight, weather, and
light-state GL tests under the same guard; their uniform setup now uses the
renderer wrappers while their `glGetUniformfv` assertions remain test-only
oracles.

`cache-review-fix.log` is the post-review run of the ASan/UBSan cache test,
live GL parity test, and wheel/headlight/weather/light-state renderer regressions.
It also verifies that repeated array writes reuse cached locations and that
capacity or unknown-state faults terminate with diagnostics.
