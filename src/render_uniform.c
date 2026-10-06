/* Bounded CPU mirror of uniforms written by OpenUG2. vitaGL does not expose
 * glGetUniformfv; GL-free cached values provide deterministic save/restore. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "render.h"

#define RU_PROGRAMS 8
#define RU_UNIFORMS 128
#define RU_NAME 64
#define RU_FLOATS 16

typedef struct {
    GLint location;
    char name[RU_NAME];
    GLfloat value[RU_FLOATS];
    unsigned char valid;
} UniformEntry;
typedef struct { GLuint program; unsigned char used; UniformEntry uniforms[RU_UNIFORMS]; } ProgramEntry;
static ProgramEntry cache[RU_PROGRAMS];
static GLuint current_program;

static void capacity_error(const char *kind, GLuint program, GLint location) {
    fprintf(stderr, "OpenUG2 uniform cache capacity exhausted (%s, program %u, location %d); state mirror unavailable\n",
            kind, (unsigned)program, (int)location);
}
static ProgramEntry *find_program(GLuint program, int create) {
    unsigned i, free_slot = RU_PROGRAMS;
    for (i=0; i<RU_PROGRAMS; ++i) {
        if (cache[i].used && cache[i].program == program) return cache+i;
        if (!cache[i].used && free_slot == RU_PROGRAMS) free_slot = i;
    }
    if (!create) return NULL;
    if (free_slot == RU_PROGRAMS) { capacity_error("program slots",program,-1); return NULL; }
    cache[free_slot].used=1; cache[free_slot].program=program;
    memset(cache[free_slot].uniforms,0,sizeof cache[free_slot].uniforms);
    return cache+free_slot;
}
static UniformEntry *find_uniform(GLuint program, GLint location, int create) {
    ProgramEntry *p; unsigned i, free_slot=RU_UNIFORMS;
    if (location < 0) return NULL;
    p=find_program(program,create); if (!p) return NULL;
    for(i=0;i<RU_UNIFORMS;i++) {
        if(p->uniforms[i].valid && p->uniforms[i].location==location) return p->uniforms+i;
        if(!p->uniforms[i].valid && free_slot==RU_UNIFORMS) free_slot=i;
    }
    if(!create) return NULL;
    if(free_slot==RU_UNIFORMS) { capacity_error("uniform slots",program,location); return NULL; }
    p->uniforms[free_slot].valid=1; p->uniforms[free_slot].location=location;
    return p->uniforms+free_slot;
}
static void register_name(GLuint program, GLint location, const char *name) {
    UniformEntry *u;
    if(location<0 || !name) return;
    u=find_uniform(program,location,1); if(!u) return;
    if(strlen(name)>=RU_NAME) { capacity_error("uniform name length",program,location); return; }
    if(!u->name[0]) memcpy(u->name,name,strlen(name)+1);
}
static void cache_values(GLuint program, GLint location, const GLfloat *values, unsigned n) {
    UniformEntry *u;
    if(location<0 || !values || n>RU_FLOATS) return;
    u=find_uniform(program,location,1); if(!u) return;
    memcpy(u->value,values,n*sizeof *values);
}
void render_uniform_getfv(GLuint program, GLint location, GLfloat *out, GLsizei nfloats) {
    UniformEntry *u; GLsizei n;
    if(!out || nfloats<=0) return;
    n=nfloats>RU_FLOATS?RU_FLOATS:nfloats;
    memset(out,0,(size_t)nfloats*sizeof *out);
    if(location<0) return;
    u=find_uniform(program,location,0);
    if(u) memcpy(out,u->value,(size_t)n*sizeof *out);
}
static int array_location(GLuint program, GLint base, unsigned element, GLint *out) {
    UniformEntry *u=find_uniform(program,base,0); char name[RU_NAME]; const char *bracket;
    if(!u || !u->name[0]) { capacity_error("unregistered array name",program,base); return 0; }
    bracket=strrchr(u->name,'[');
    if(bracket && u->name[strlen(u->name)-1]==']') {
        size_t prefix=(size_t)(bracket-u->name);
        if(prefix+20>=sizeof name) { capacity_error("array name length",program,base); return 0; }
        memcpy(name,u->name,prefix); name[prefix]='\0';
        snprintf(name+prefix,sizeof name-prefix,"[%u]",element);
    } else {
        if(snprintf(name,sizeof name,"%s[%u]",u->name,element)>=(int)sizeof name) {
            capacity_error("array name length",program,base); return 0;
        }
    }
    *out=glGetUniformLocation(program,name);
    register_name(program,*out,name);
    return *out>=0;
}
void render_use_program(GLuint program) { glUseProgram(program); current_program=program; }
GLint render_uniform_location(GLuint program,const char *name) {
    GLint location=glGetUniformLocation(program,name); register_name(program,location,name); return location;
}
void render_uniform1f(GLint l,GLfloat x) { GLfloat v[1]={x}; if(l<0)return; glUniform1f(l,x); cache_values(current_program,l,v,1); }
void render_uniform1i(GLint l,GLint x) { GLfloat v[1]={(GLfloat)x}; if(l<0)return; glUniform1i(l,x); cache_values(current_program,l,v,1); }
void render_uniform2f(GLint l,GLfloat x,GLfloat y) { GLfloat v[2]={x,y}; if(l<0)return; glUniform2f(l,x,y); cache_values(current_program,l,v,2); }
void render_uniform3f(GLint l,GLfloat x,GLfloat y,GLfloat z) { GLfloat v[3]={x,y,z}; if(l<0)return; glUniform3f(l,x,y,z); cache_values(current_program,l,v,3); }
void render_uniform4f(GLint l,GLfloat x,GLfloat y,GLfloat z,GLfloat w) { GLfloat v[4]={x,y,z,w}; if(l<0)return; glUniform4f(l,x,y,z,w); cache_values(current_program,l,v,4); }
static void uniform_array(GLint l,GLsizei count,GLsizei width,const GLfloat *values,unsigned matrix) {
    GLsizei i; if(l<0 || count<=0 || !values)return;
    if(matrix) glUniformMatrix4fv(l,count,GL_FALSE,values);
    else if(width==2) glUniform2fv(l,count,values);
    else if(width==3) glUniform3fv(l,count,values);
    else glUniform4fv(l,count,values);
    if(count==1) { cache_values(current_program,l,values,width); return; }
    for(i=0;i<count;i++) {
        GLint element_location; unsigned stride=(unsigned)(matrix?16:width);
        if(array_location(current_program,l,(unsigned)i,&element_location))
            cache_values(current_program,element_location,values+(size_t)i*stride,stride);
    }
}
void render_uniform2fv(GLint l,GLsizei n,const GLfloat *v) { uniform_array(l,n,2,v,0); }
void render_uniform3fv(GLint l,GLsizei n,const GLfloat *v) { uniform_array(l,n,3,v,0); }
void render_uniform4fv(GLint l,GLsizei n,const GLfloat *v) { uniform_array(l,n,4,v,0); }
void render_uniform_matrix4fv(GLint l,GLsizei n,GLboolean transpose,const GLfloat *v) {
    if(l<0 || n<=0 || !v)return;
    glUniformMatrix4fv(l,n,transpose,v);
    {
        GLsizei i;
        for(i=0;i<n;i++) {
            GLint element_location=l; GLfloat transposed[16]; const GLfloat *matrix=v+(size_t)i*16;
            unsigned row,col;
            if(n>1 && !array_location(current_program,l,(unsigned)i,&element_location)) continue;
            if(transpose) {
                for(row=0;row<4;row++)for(col=0;col<4;col++)
                    transposed[col*4+row]=matrix[row*4+col];
                matrix=transposed;
            }
            cache_values(current_program,element_location,matrix,16);
        }
    }
}
void render_delete_program(GLuint program) {
    unsigned i;
    if(program) glDeleteProgram(program);
    for(i=0;i<RU_PROGRAMS;i++) if(cache[i].used && cache[i].program==program) {
        memset(cache+i,0,sizeof cache[i]);
        break;
    }
}
