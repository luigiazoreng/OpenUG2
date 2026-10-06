/* Host GL parity check. glGetUniformfv appears only in tests, as an oracle. */
#include "render.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static GLuint shader(GLenum type,const char *text) {
    GLuint s=glCreateShader(type); GLint ok=0; glShaderSource(s,1,&text,NULL); glCompileShader(s);
    glGetShaderiv(s,GL_COMPILE_STATUS,&ok); if(!ok) { char log[1024]; glGetShaderInfoLog(s,sizeof log,NULL,log); fprintf(stderr,"shader: %s\n",log); }
    assert(ok); return s;
}
static void same(const GLfloat *a,const GLfloat *b,unsigned n) { unsigned i; for(i=0;i<n;i++)if(fabsf(a[i]-b[i])>=0.0001f) { fprintf(stderr,"value mismatch %u: cache=%g GL=%g\n",i,a[i],b[i]); assert(0); } }
int main(void) {
    const char *vs="uniform mat4 uM; attribute vec4 p; void main(){gl_Position=uM*p;}";
    const char *fs="uniform float uS; uniform vec3 uV; uniform vec4 uA[2]; void main(){gl_FragColor=vec4(uV*uS,1.0)+uA[0]*0.001+uA[1]*0.001;}";
    SDL_Window *win; SDL_GLContext ctx; GLuint p,sv,fv; GLint lS,lV,lM,lA;
    GLfloat saved[16],oracle[16],scalar=0.375f,restore_scalar,vec[3]={0.2f,0.4f,0.6f};
    GLfloat array[8]={1,2,3,4,5,6,7,8}, matrix[16], transposed_input[16]; unsigned i;
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,0);
    win=SDL_CreateWindow("uniform-state",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN); assert(win);
    ctx=SDL_GL_CreateContext(win); assert(ctx);
    sv=shader(GL_VERTEX_SHADER,vs); fv=shader(GL_FRAGMENT_SHADER,fs); p=glCreateProgram();
    glAttachShader(p,sv); glAttachShader(p,fv); glBindAttribLocation(p,0,"p"); glLinkProgram(p);
    { GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok); assert(ok); }
    render_use_program(p); lS=render_uniform_location(p,"uS"); lV=render_uniform_location(p,"uV");
    lM=render_uniform_location(p,"uM"); lA=render_uniform_location(p,"uA[0]");
    assert(lS>=0&&lV>=0&&lM>=0&&lA>=0);
    for(i=0;i<16;i++) { matrix[i]=(GLfloat)(i+1)*0.125f; transposed_input[i]=(GLfloat)(i+1)*0.25f; }
    render_uniform1f(lS,scalar); render_uniform3fv(lV,1,vec);
    render_uniform_matrix4fv(lM,1,GL_FALSE,matrix); render_uniform4fv(lA,2,array);
    glGetUniformfv(p,lS,oracle); render_uniform_getfv(p,lS,saved,1); same(saved,oracle,1);
    glGetUniformfv(p,lV,oracle); render_uniform_getfv(p,lV,saved,3); same(saved,oracle,3);
    glGetUniformfv(p,lM,oracle); render_uniform_getfv(p,lM,saved,16); same(saved,oracle,16);
    render_uniform_matrix4fv(lM,1,GL_TRUE,transposed_input);
    glGetUniformfv(p,lM,oracle); render_uniform_getfv(p,lM,saved,16); same(saved,oracle,16);
    render_uniform_getfv(p,render_uniform_location(p,"uA[1]"),saved,4);
    glGetUniformfv(p,render_uniform_location(p,"uA[1]"),oracle); same(saved,oracle,4);
    render_uniform_getfv(p,lS,&restore_scalar,1); render_uniform1f(lS,9.0f);
    render_uniform1f(lS,restore_scalar);
    glGetUniformfv(p,lS,oracle); same(oracle,&scalar,1);
    render_delete_program(p); glDeleteShader(sv); glDeleteShader(fv);
    SDL_GL_DeleteContext(ctx); SDL_DestroyWindow(win); SDL_Quit();
    puts("render uniform live GL parity passed"); return 0;
}
