#!/usr/bin/env python3
"""Compile the complete production fragment shader after vitaGL's real mul pass."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--expect-broken', action='store_true')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
repo = Path(__file__).resolve().parent.parent
source = a.source.read_text()
buffers = source[source.index('#define GLSL_BUFFER_MIN_CAPACITY'):source.index('static void glsl_replace_marker(')]
body = source[source.index('GLboolean glsl_inject_mul('):source.index('\nvoid glsl_translator_process(')]
shim = '''#include <stdint.h>
#include <ctype.h>
#define vgl_realloc realloc
#define vgl_free free
#define vgl_fast_memcpy memcpy
#define sceClibMemmove memmove
'''
helpers = ['float vglMul(float a,float b){return a*b;}\n']
for n in (2, 3, 4):
    v, m = f'vec{n}', f'mat{n}'
    for x, y, ret in [(v, v, v), (v, 'float', v), ('float', v, v),
                      (m, m, m), (m, v, v), (v, m, v), (m, 'float', m), ('float', m, m)]:
        helpers.append(f'{ret} vglMul({x} a,{y} b){{return a*b;}}\n')
helper_literal = ''.join('    '+repr(x).replace("'", '"')+'\n' for x in helpers)
fixture = (repo/'tools/render_swizzle_macro_test.c').read_text()
fixture = fixture.replace('extern void __real_glShaderSource', shim+buffers+body+'\nextern void __real_glShaderSource', 1)
fixture = fixture.replace('static const char macros[] =', 'static const char mul_helpers[] =\n'+helper_literal+';\n\nstatic const char macros[] =', 1)
fixture = fixture.replace('    size_t prefix_len = (size_t)(newline + 1 - input);', '''    size_t prefix_len = (size_t)(newline + 1 - input);
    glsl_buffer translated = {0};
    if (!glsl_inject_mul(input, (GLsizei)input_len, &translated, (GLsizei)prefix_len)) {
        free(input); return;
    }
    free(input); input = translated.data; input_len = translated.len;
    const char *capture = getenv("OPENUG2_MUL_SHADER_CAPTURE");
    if (capture) { FILE *f=fopen(capture,"w"); if(f){fwrite(input,1,input_len,f);fclose(f);} }
''', 1)
fixture = fixture.replace('    size_t macros_len = sizeof(macros) - 1;', '    size_t macros_len = sizeof(macros) - 1 + sizeof(mul_helpers) - 1;', 1)
fixture = fixture.replace('    memcpy(injected + prefix_len, macros, macros_len);', '''    memcpy(injected + prefix_len, macros, sizeof(macros)-1);
    memcpy(injected + prefix_len + sizeof(macros)-1, mul_helpers, sizeof(mul_helpers)-1);''', 1)
a.output.mkdir(parents=True, exist_ok=False)
(a.output/'full-shader-test.c').write_text(fixture)
sdl = Path('/tmp/openug2-vita-reference/desktop-sdl/usr/include')
import os
with tempfile.TemporaryDirectory() as tmp:
    for api in ('gl', 'gles'):
        binary = Path(tmp)/api
        flags = ['-DN2_GLES'] if api == 'gles' else []
        argv = ['cc','-std=c99','-D_DEFAULT_SOURCE',*flags,'-Wall','-Wextra','-Werror','-Wno-unused-function',
                '-I'+str(sdl/'SDL2'),'-I'+str(sdl/'x86_64-linux-gnu'),'-I'+str(repo/'src'),
                str(a.output/'full-shader-test.c'),str(repo/'src/render.c'),str(repo/'src/render_uniform.c'),
                '-o',str(binary),'-l:libSDL2-2.0.so.0','-lGLESv2' if api=='gles' else '-lGL','-lz','-lm',
                '-Wl,--wrap=glShaderSource','-Wl,--wrap=glGetShaderiv']
        with (a.output/(api+'.log')).open('w') as log:
            subprocess.run(argv,stdout=log,stderr=subprocess.STDOUT,check=True)
            env = dict(os.environ, OPENUG2_MUL_SHADER_CAPTURE=str(a.output/(api+'-rewritten.frag')))
            r = subprocess.run(['xvfb-run','-a',str(binary)],stdout=log,stderr=subprocess.STDOUT,env=env)
        text = (a.output/(api+'.log')).read_text()
        if a.expect_broken:
            assert r.returncode==1 and 'compiled=0 linked=0' in text, text
        else:
            assert r.returncode==0, text
        print(api, 'EXPECTED_SHADER_FAILURE' if a.expect_broken else 'PASS_FULL_SHADER')
