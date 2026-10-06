/* GL-free behavioral test for render_uniform.c. The fake driver deliberately
 * assigns array elements noncontiguous locations to catch location+i guesses. */
#include "render.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { GLuint program; GLint location; const char *name; GLfloat values[16]; } FakeUniform;
static FakeUniform uniforms[96]; static unsigned nuniforms; static GLuint active_program;
static const struct { GLuint p; const char *name; GLint location; } locations[] = {
    {11,"scalar",2}, {12,"scalar",2}, {11,"vector",3}, {11,"matrix",7},
    {11,"items[0]",4}, {11,"items[1]",14}, {11,"items[2]",25},
    {11,"mats[0]",32}, {11,"mats[1]",47}, {11,"optimized",-1}
};
static GLint lookup(GLuint p,const char *name) {
    unsigned i; for(i=0;i<sizeof locations/sizeof locations[0];i++)
        if(locations[i].p==p && strcmp(locations[i].name,name)==0)return locations[i].location;
    return -1;
}
static const char *name_for(GLuint p,GLint l) {
    unsigned i; for(i=0;i<sizeof locations/sizeof locations[0];i++)
        if(locations[i].p==p && locations[i].location==l)return locations[i].name;
    return "";
}
static FakeUniform *entry(GLuint p,GLint l,int create) {
    unsigned i; for(i=0;i<nuniforms;i++) if(uniforms[i].program==p && uniforms[i].location==l)return &uniforms[i];
    if(!create || nuniforms>=sizeof uniforms/sizeof uniforms[0])return NULL;
    memset(&uniforms[nuniforms],0,sizeof uniforms[nuniforms]);
    uniforms[nuniforms].program=p; uniforms[nuniforms].location=l; return &uniforms[nuniforms++];
}
static void put(GLint l,const GLfloat *v,unsigned n) { FakeUniform *u=entry(active_program,l,1); assert(u); memcpy(u->values,v,n*sizeof *v); }
static void put_array(GLint l,const GLfloat *v,unsigned width,unsigned count) {
    const char *base=name_for(active_program,l); char key[64]; unsigned i;
    const char *br=strrchr(base,'['); size_t prefix=br?(size_t)(br-base):strlen(base);
    for(i=0;i<count;i++) { GLint el; snprintf(key,sizeof key,"%.*s[%u]",(int)prefix,base,i); el=lookup(active_program,key); if(el>=0)put(el,v+i*width,width); }
}
void glUseProgram(GLuint p) { active_program=p; }
GLint glGetUniformLocation(GLuint p,const GLchar *name) { return lookup(p,name); }
void glDeleteProgram(GLuint p) { unsigned i=0; while(i<nuniforms) { if(uniforms[i].program==p)uniforms[i]=uniforms[--nuniforms]; else i++; } }
void glUniform1f(GLint l,GLfloat x) { put(l,&x,1); }
void glUniform1i(GLint l,GLint x) { GLfloat f=(GLfloat)x; put(l,&f,1); }
void glUniform2f(GLint l,GLfloat x,GLfloat y) { GLfloat v[]={x,y}; put(l,v,2); }
void glUniform3f(GLint l,GLfloat x,GLfloat y,GLfloat z) { GLfloat v[]={x,y,z}; put(l,v,3); }
void glUniform4f(GLint l,GLfloat x,GLfloat y,GLfloat z,GLfloat w) { GLfloat v[]={x,y,z,w}; put(l,v,4); }
void glUniform2fv(GLint l,GLsizei n,const GLfloat *v) { if(n==1)put(l,v,2); else put_array(l,v,2,(unsigned)n); }
void glUniform3fv(GLint l,GLsizei n,const GLfloat *v) { if(n==1)put(l,v,3); else put_array(l,v,3,(unsigned)n); }
void glUniform4fv(GLint l,GLsizei n,const GLfloat *v) { if(n==1)put(l,v,4); else put_array(l,v,4,(unsigned)n); }
void glUniformMatrix4fv(GLint l,GLsizei n,GLboolean t,const GLfloat *v) { (void)t; if(n==1)put(l,v,16); else put_array(l,v,16,(unsigned)n); }
void glGetUniformfv(GLuint p,GLint l,GLfloat *v) { FakeUniform *u=entry(p,l,0); if(u)memcpy(v,u->values,16*sizeof *v); else memset(v,0,16*sizeof *v); }
static void eq(const GLfloat *a,const GLfloat *b,unsigned n) { unsigned i; for(i=0;i<n;i++)assert(fabsf(a[i]-b[i])<0.0001f); }
int main(void) {
    GLfloat out[16], vec[]={1,2,3}, mat[32], arr[]={1,2,3,4,5,6,7,8}; unsigned i;
    for(i=0;i<32;i++)mat[i]=(GLfloat)(i+0.5f);
    render_use_program(11); (void)render_uniform_location(11,"scalar");
    render_uniform1f(2,3.25f); render_uniform_getfv(11,2,out,1); assert(out[0]==3.25f);
    render_use_program(12); (void)render_uniform_location(12,"scalar");
    render_uniform1f(2,8.0f); render_uniform_getfv(11,2,out,1); assert(out[0]==3.25f);
    render_uniform_getfv(12,2,out,1); assert(out[0]==8.0f);
    render_use_program(11);
    render_uniform_getfv(11,3,out,3); eq(out,(GLfloat[3]){0,0,0},3); /* GL default */
    render_uniform3fv(3,1,vec); render_uniform_getfv(11,3,out,3); eq(out,vec,3);
    assert(render_uniform_location(11,"optimized")==-1);
    render_uniform1f(-1,99); render_uniform_getfv(11,-1,out,1); assert(out[0]==0);
    (void)render_uniform_location(11,"items[0]"); render_uniform4fv(4,2,arr);
    render_uniform_getfv(11,4,out,4); eq(out,arr,4);
    render_uniform_getfv(11,14,out,4); eq(out,arr+4,4);
    (void)render_uniform_location(11,"mats[0]"); render_uniform_matrix4fv(32,2,GL_FALSE,mat);
    render_uniform_getfv(11,32,out,16); eq(out,mat,16);
    render_uniform_getfv(11,47,out,16); eq(out,mat+16,16);
    render_delete_program(11); render_uniform_getfv(11,2,out,1); assert(out[0]==0);
    render_use_program(11); render_uniform1i(2,4); render_uniform_getfv(11,2,out,1); assert(out[0]==4);
    puts("render uniform cache test passed"); return 0;
}
