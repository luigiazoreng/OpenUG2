/* GL-free behavioral test for render_uniform.c. The fake driver deliberately
 * assigns array elements noncontiguous locations to catch location+i guesses. */
#include "render.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

typedef struct { GLuint program; GLint location; const char *name; GLfloat values[16]; } FakeUniform;
static FakeUniform uniforms[256]; static unsigned nuniforms; static GLuint active_program;
static unsigned location_queries;
static const struct { GLuint p; const char *name; GLint location; } locations[] = {
    {11,"scalar",2}, {12,"scalar",2}, {11,"vector",3}, {11,"matrix",7},
    {11,"items[0]",4}, {11,"items[1]",14}, {11,"items[2]",25},
    {11,"mats[0]",32}, {11,"mats[1]",47}, {11,"optimized",-1},
    {11,"tail[1]",60}, {11,"tail[2]",80}, {11,"tail[3]",100},
    {11,"reverse[0]",110}, {11,"reverse[1]",130}, {11,"reverse[2]",150},
    {11,"partial[0]",170}, {11,"partial[1]",-1}, {11,"partial[2]",190},
    {11,"opaque",-123}, {11,"alias",220}, {11,"alias[0]",220},
    {11,"alias[1]",240}, {11,"alias[2]",260}
};
static GLint lookup(GLuint p,const char *name) {
    unsigned i; int slot;
    if(p==77 && sscanf(name,"slot%d",&slot)==1)return 1000+slot;
    if(p==11 && sscanf(name,"alias%d",&slot)==1)return 2;
    if(p==11 && strcmp(name,"items")==0)return 4;
    if(p==11 && strcmp(name,"reverse")==0)return 110;
    for(i=0;i<sizeof locations/sizeof locations[0];i++)
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
    const char *base=name_for(active_program,l); char key[64]; unsigned i,start=0;
    const char *br=strrchr(base,'['); size_t prefix=br?(size_t)(br-base):strlen(base);
    if(br) (void)sscanf(br+1,"%u]",&start);
    for(i=0;i<count;i++) { GLint el; snprintf(key,sizeof key,"%.*s[%u]",(int)prefix,base,start+i); el=lookup(active_program,key); if(el!=-1)put(el,v+i*width,width); }
}
void glUseProgram(GLuint p) { active_program=p; }
GLint glGetUniformLocation(GLuint p,const GLchar *name) { location_queries++; return lookup(p,name); }
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

static void expect_abort(const char *which,const char *reason) {
    int fds[2],status; char output[4096]; ssize_t got; size_t used=0; pid_t pid;
    assert(pipe(fds)==0); pid=fork(); assert(pid>=0);
    if(pid==0) {
        close(fds[0]); assert(dup2(fds[1],STDERR_FILENO)>=0); close(fds[1]);
        if(strcmp(which,"program capacity")==0) {
            GLuint p; for(p=100;p<120;p++)(void)render_uniform_location(p,"missing");
        } else if(strcmp(which,"uniform capacity")==0) {
            int i; for(i=0;i<129;i++){char name[64];snprintf(name,sizeof name,"slot%d",i);(void)render_uniform_location(77,name);}
        } else if(strcmp(which,"name capacity")==0) {
            int i; for(i=0;i<129;i++){char name[64];snprintf(name,sizeof name,"alias%d",i);(void)render_uniform_location(11,name);}
        } else if(strcmp(which,"name length")==0) {
            char name[96];memset(name,'x',sizeof name);name[sizeof name-1]='\0';
            (void)render_uniform_location(11,name);
        } else if(strcmp(which,"getter cardinality")==0) {
            GLfloat out[8];render_uniform_getfv(11,2,out,5);
        } else if(strcmp(which,"unknown getter")==0) {
            GLfloat out;render_uniform_getfv(11,999999,&out,1);
        }
        _exit(0);
    }
    close(fds[1]); while(used+1<sizeof output && (got=read(fds[0],output+used,sizeof output-used-1))>0)used+=(size_t)got;
    output[used]='\0';close(fds[0]);assert(waitpid(pid,&status,0)==pid);
    assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
    assert(strstr(output,reason)!=NULL);
}

int main(void) {
    GLfloat out[16], vec[]={1,2,3}, mat[32], arr[]={1,2,3,4,5,6,7,8};
    GLfloat partial[]={1,2,3,4,5,6,7,8,9,10,11,12}; unsigned i,q;
    for(i=0;i<32;i++)mat[i]=(GLfloat)(i+0.5f);
    render_use_program(11); (void)render_uniform_location(11,"scalar");
    render_uniform1f(2,3.25f); render_uniform_getfv(11,2,out,1); assert(out[0]==3.25f);
    render_use_program(12); (void)render_uniform_location(12,"scalar");
    render_uniform1f(2,8.0f); render_uniform_getfv(11,2,out,1); assert(out[0]==3.25f);
    render_uniform_getfv(12,2,out,1); assert(out[0]==8.0f);
    render_use_program(11);
    (void)render_uniform_location(11,"vector");
    render_uniform_getfv(11,3,out,3); eq(out,(GLfloat[3]){0,0,0},3); /* GL default */
    render_uniform3fv(3,1,vec); render_uniform_getfv(11,3,out,3); eq(out,vec,3);
    assert(render_uniform_location(11,"optimized")==-1);
    render_uniform1f(-1,99); render_uniform_getfv(11,-1,out,1); assert(out[0]==0);
    (void)render_uniform_location(11,"items"); render_uniform4fv(4,2,arr);
    render_uniform_getfv(11,4,out,4); eq(out,arr,4);
    render_uniform_getfv(11,14,out,4); eq(out,arr+4,4);
    q=location_queries; render_uniform4fv(4,2,arr); assert(location_queries==q);
    (void)render_uniform_location(11,"items[0]"); assert(location_queries==q);
    render_uniform4fv(14,2,arr); render_uniform_getfv(11,14,out,4); eq(out,arr,4);
    render_uniform_getfv(11,25,out,4); eq(out,arr+4,4);
    (void)render_uniform_location(11,"tail[1]"); render_uniform4fv(60,2,arr);
    render_uniform_getfv(11,60,out,4); eq(out,arr,4);
    render_uniform_getfv(11,80,out,4); eq(out,arr+4,4);
    (void)render_uniform_location(11,"reverse[0]"); render_uniform4fv(110,2,arr);
    (void)render_uniform_location(11,"reverse"); q=location_queries;
    render_uniform4fv(110,2,arr); assert(location_queries==q);
    (void)render_uniform_location(11,"partial[0]"); render_uniform4fv(170,3,partial);
    q=location_queries; render_uniform4fv(170,3,partial); assert(location_queries==q);
    render_uniform_getfv(11,190,out,4); eq(out,partial+8,4);
    (void)render_uniform_location(11,"opaque"); render_uniform1f(-123,6.5f);
    render_uniform_getfv(11,-123,out,1); assert(out[0]==6.5f);
    (void)render_uniform_location(11,"mats[0]"); render_uniform_matrix4fv(32,2,GL_FALSE,mat);
    render_uniform_getfv(11,32,out,16); eq(out,mat,16);
    render_uniform_getfv(11,47,out,16); eq(out,mat+16,16);
    render_delete_program(11); render_use_program(11);
    (void)render_uniform_location(11,"scalar"); render_uniform_getfv(11,2,out,1); assert(out[0]==0);
    render_uniform1i(2,4); render_uniform_getfv(11,2,out,1); assert(out[0]==4);
    expect_abort("program capacity","program slots");
    expect_abort("uniform capacity","uniform locations");
    expect_abort("name capacity","uniform names");
    expect_abort("name length","uniform name length");
    expect_abort("getter cardinality","getter cardinality");
    expect_abort("unknown getter","unknown location");
    puts("render uniform cache test passed"); return 0;
}
