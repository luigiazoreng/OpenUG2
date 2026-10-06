#!/usr/bin/env python3
"""Extract the pinned real multiplication rewriter; check operand boundaries."""
import argparse
from pathlib import Path
import subprocess
import tempfile

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--expect-broken', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    text = args.source.read_text()
    buffers = text[text.index('#define GLSL_BUFFER_MIN_CAPACITY'):text.index('static void glsl_replace_marker(')]
    body = text[text.index('GLboolean glsl_inject_mul('):text.index('\nvoid glsl_translator_process(')]
    preamble = '''#include <stdio.h>
    #include <stdint.h>
    #include <stdlib.h>
    #include <string.h>
    #include <ctype.h>
    typedef int GLboolean;
    typedef int GLsizei;
    #define GL_FALSE 0
    #define GL_TRUE 1
    #define vgl_realloc realloc
    #define vgl_free free
    #define vgl_fast_memcpy memcpy
    #define sceClibMemmove memmove
    '''
    fixture = r'''
    static void compact(char *out, const char *in) {
        while (*in) { if (!isspace((unsigned char)*in)) *out++=*in; in++; } *out=0;
    }
    int main(int argc, char **argv) {
        const char *cases[][2] = {
          {"if(wet>0.001 || uPaintLights*uClearcoat>0.001)","if(wet>0.001||vglMul(uPaintLights,uClearcoat)>0.001)"},
          {"if(wet>0.001 && edge*cutoff>0.0)","if(wet>0.001&&vglMul(edge,cutoff)>0.0)"},
          {"if(a*b>0.0 && ready)","if(vglMul(a,b)>0.0&&ready)"},
          {"if(a*b==1.0)","if(vglMul(a,b)==1.0)"},
          {"if(a*b!=1.0)","if(vglMul(a,b)!=1.0)"},
          {"float v=a*b?1.0:0.0;","floatv=vglMul(a,b)?1.0:0.0;"},
          {"float v=a*b/c;","floatv=vglMul(a,b)/c;"},
          {"float v=a/b*c;","floatv=vglMul(a/b,c);"},
          {"float v=a*b*c;","floatv=vglMul(vglMul(a,b),c);"},
          {"float v=(a+b)*(c-d);","floatv=vglMul((a+b),(c-d));"},
          {"float v=light[2].x*gain;","floatv=vglMul(light[2].x,gain);"},
          {"if((a>0.0 || b>0.0) && c*d>0.0)","if((a>0.0||b>0.0)&&vglMul(c,d)>0.0)"},
          {"float v=matrix*vec4(pos,1.0);","floatv=vglMul(matrix,vec4(pos,1.0));"},
          {"float v=a*-1.0;","floatv=vglMul(a,-1.0);"},
          {"float v=a*1.0e-3;","floatv=vglMul(a,1.0e-3);"},
          {"float v=a*b && c*d;","floatv=vglMul(a,b)&&vglMul(c,d);"},
          {"float v=a*b || c*d;","floatv=vglMul(a,b)||vglMul(c,d);"}
        };
        int failed=0;
        for (size_t i=0;i<sizeof cases/sizeof *cases;i++) {
            char input[512], actual[2048], expected[2048];
            const char *prefix="void main(){ ";
            snprintf(input,sizeof input,"%s%s}",prefix,cases[i][0]);
            glsl_buffer out={0};
            if (!glsl_inject_mul(input,(GLsizei)strlen(input),&out,(GLsizei)strlen(prefix))) return 2;
            compact(actual,out.data+strlen(prefix));
            snprintf(expected,sizeof expected,"%s}",cases[i][1]);
            int ok=!strcmp(actual,expected);
            printf("case=%zu %s actual=%s expected=%s\n",i,ok?"PASS":"FAIL",actual,expected);
            failed+=!ok;glsl_buffer_release(&out);
        }
        printf("operand_cases=%zu failed=%d\n",sizeof cases/sizeof *cases,failed);
        return argc>1 && !strcmp(argv[1],"broken") ? (failed>0 ? 0 : 3) : (failed ? 1 : 0);
    }
    '''
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output/'extracted-test.c').write_text(preamble+buffers+body+fixture)
    with tempfile.TemporaryDirectory() as tmp:
        binary = Path(tmp)/'test'
        subprocess.run(['cc','-std=gnu99','-Wall','-Wextra','-Werror','-Wno-unused-function',
                        '-fsanitize=address,undefined','-fno-omit-frame-pointer',str(args.output/'extracted-test.c'),'-o',str(binary)],check=True)
        with (args.output/'result.log').open('w') as log:
            result=subprocess.run([str(binary)]+(['broken'] if args.expect_broken else []),stdout=log,stderr=subprocess.STDOUT)
        print((args.output/'result.log').read_text())
        raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
