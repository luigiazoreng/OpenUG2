/* Bounded CPU mirror of uniforms written by OpenUG2. vitaGL does not expose
 * glGetUniformfv; GL-free cached values provide deterministic save/restore. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "render.h"

#define RU_PROGRAMS 8
#define RU_UNIFORMS 128
#define RU_NAMES 128
#define RU_NAME 64
#define RU_FLOATS 16

typedef struct {
    GLint location;
    GLfloat value[RU_FLOATS];
    unsigned char written_floats;
    unsigned char valid;
} UniformEntry;
typedef struct { GLint location; char name[RU_NAME]; unsigned char valid; } NameEntry;
typedef struct {
    GLuint program;
    unsigned char used;
    UniformEntry uniforms[RU_UNIFORMS];
    NameEntry names[RU_NAMES];
} ProgramEntry;
static ProgramEntry cache[RU_PROGRAMS];
static GLuint current_program;

static void fail_contract(const char *reason, GLuint program, GLint location) {
    fprintf(stderr, "OpenUG2 uniform state failure: %s (program %u, location %d)\n",
            reason, (unsigned)program, (int)location);
    fflush(stderr);
    abort();
}
static void capacity_error(const char *kind, GLuint program, GLint location) {
    char reason[128];
    snprintf(reason,sizeof reason,"cache capacity exceeded: %s",kind);
    fail_contract(reason,program,location);
}
static ProgramEntry *find_program(GLuint program, int create) {
    unsigned i, free_slot=RU_PROGRAMS;
    for(i=0;i<RU_PROGRAMS;i++) {
        if(cache[i].used && cache[i].program==program)return cache+i;
        if(!cache[i].used && free_slot==RU_PROGRAMS)free_slot=i;
    }
    if(!create)return NULL;
    if(program==0)fail_contract("program zero has no uniform state",program,-1);
    if(free_slot==RU_PROGRAMS)capacity_error("program slots",program,-1);
    memset(cache+free_slot,0,sizeof cache[free_slot]);
    cache[free_slot].used=1;cache[free_slot].program=program;
    return cache+free_slot;
}
static UniformEntry *find_uniform(GLuint program,GLint location,int create) {
    ProgramEntry *p; unsigned i,free_slot=RU_UNIFORMS;
    if(location==-1)return NULL;
    p=find_program(program,create);if(!p)return NULL;
    for(i=0;i<RU_UNIFORMS;i++) {
        if(p->uniforms[i].valid && p->uniforms[i].location==location)return p->uniforms+i;
        if(!p->uniforms[i].valid && free_slot==RU_UNIFORMS)free_slot=i;
    }
    if(!create)return NULL;
    if(free_slot==RU_UNIFORMS)capacity_error("uniform locations",program,location);
    p->uniforms[free_slot].valid=1;p->uniforms[free_slot].location=location;
    return p->uniforms+free_slot;
}
static NameEntry *find_name(ProgramEntry *p,const char *name) {
    unsigned i;
    for(i=0;i<RU_NAMES;i++)
        if(p->names[i].valid && strcmp(p->names[i].name,name)==0)return p->names+i;
    return NULL;
}
static void register_name(GLuint program,GLint location,const char *name) {
    ProgramEntry *p; NameEntry *n; unsigned i,free_slot=RU_NAMES;
    size_t len;
    if(!name)fail_contract("null uniform name",program,location);
    len=strlen(name);
    if(len>=RU_NAME)capacity_error("uniform name length",program,location);
    p=find_program(program,1);
    n=find_name(p,name);
    if(n) {
        if(n->location!=location)fail_contract("uniform name changed location",program,location);
        if(location!=-1)(void)find_uniform(program,location,1);
        return;
    }
    if(location!=-1)(void)find_uniform(program,location,1);
    for(i=0;i<RU_NAMES;i++)if(!p->names[i].valid && free_slot==RU_NAMES)free_slot=i;
    if(free_slot==RU_NAMES)capacity_error("uniform names",program,location);
    p->names[free_slot].valid=1;p->names[free_slot].location=location;
    memcpy(p->names[free_slot].name,name,len+1);
}
static void require_width(UniformEntry *u,GLuint program,GLint location,unsigned width) {
    if(!u)fail_contract("unknown uniform location",program,location);
    if(width==0 || width>RU_FLOATS)fail_contract("unsupported uniform cardinality",program,location);
    if(u->written_floats && u->written_floats!=width)
        fail_contract("uniform cardinality changed",program,location);
}
static void store_values(GLuint program,GLint location,const GLfloat *values,unsigned n) {
    UniformEntry *u=find_uniform(program,location,0);
    require_width(u,program,location,n);
    if(!values)fail_contract("null uniform value",program,location);
    memcpy(u->value,values,n*sizeof *values);u->written_floats=(unsigned char)n;
}
static NameEntry *array_source(ProgramEntry *p,GLuint program,GLint location) {
    unsigned i; NameEntry *fallback=NULL;
    for(i=0;i<RU_NAMES;i++)if(p->names[i].valid && p->names[i].location==location) {
        const char *br=strrchr(p->names[i].name,'[');
        if(br && p->names[i].name[strlen(p->names[i].name)-1]==']')return p->names+i;
        if(!fallback)fallback=p->names+i;
    }
    if(fallback)return fallback;
    fail_contract("array base location has no registered name",program,location);
    return NULL;
}
static void parse_array_name(const char *name,size_t *prefix,unsigned *start,
                             GLuint program,GLint location) {
    const char *open=strrchr(name,'['),*p; unsigned long value=0;
    size_t n=strlen(name);
    if(!open) { *prefix=n;*start=0;return; }
    if(n<3 || name[n-1]!=']' || open>=name+n-2)
        fail_contract("array uniform name has unsupported form",program,location);
    for(p=open+1;p<name+n-1;p++) {
        if(*p<'0'||*p>'9')fail_contract("array uniform name has unsupported index",program,location);
        value=value*10+(unsigned long)(*p-'0');
        if(value>UINT_MAX)fail_contract("array index overflow",program,location);
    }
    *prefix=(size_t)(open-name);*start=(unsigned)value;
}
/* Cache both active and optimized-out names so a repeated upload performs no
 * per-frame glGetUniformLocation calls. 0 means optimized out; 1 means active. */
static int array_location(GLuint program,GLint base,unsigned element,GLint *out) {
    ProgramEntry *p=find_program(program,0); NameEntry *src,*known;
    char name[RU_NAME]; size_t prefix; unsigned start,index; int n;
    if(!p)fail_contract("array upload for unknown program",program,base);
    src=array_source(p,program,base);parse_array_name(src->name,&prefix,&start,program,base);
    if(element>UINT_MAX-start)fail_contract("array element index overflow",program,base);
    index=start+element;
    if(prefix+20>=sizeof name)capacity_error("array uniform name length",program,base);
    memcpy(name,src->name,prefix);name[prefix]='\0';
    n=snprintf(name+prefix,sizeof name-prefix,"[%u]",index);
    if(n<0 || (size_t)n>=sizeof name-prefix)capacity_error("array uniform name length",program,base);
    known=find_name(p,name);
    if(known) {
        *out=known->location;
        if(element==0 && *out==-1)
            fail_contract("array base index is not resolvable by the GL adapter",program,base);
        return *out!=-1;
    }
    *out=glGetUniformLocation(program,name);
    register_name(program,*out,name);
    if(element==0 && *out==-1)
        fail_contract("array base index is not resolvable by the GL adapter",program,base);
    return *out!=-1;
}
static void prepare_write(GLuint program,GLint location,unsigned width) {
    UniformEntry *u;
    if(location==-1)return;
    u=find_uniform(program,location,0);
    require_width(u,program,location,width);
}
void render_use_program(GLuint program) { glUseProgram(program);current_program=program; }
GLint render_uniform_location(GLuint program,const char *name) {
    ProgramEntry *p;NameEntry *known;GLint location;
    if(!name)fail_contract("null uniform name",program,-1);
    if(strlen(name)>=RU_NAME)capacity_error("uniform name length",program,-1);
    p=find_program(program,1);known=find_name(p,name);
    if(known)return known->location;
    location=glGetUniformLocation(program,name);register_name(program,location,name);return location;
}
void render_uniform_getfv(GLuint program,GLint location,GLfloat *out,GLsizei nfloats) {
    UniformEntry *u;ProgramEntry *p;
    if(!out)fail_contract("null getter output",program,location);
    if(nfloats!=1 && nfloats!=3 && nfloats!=4 && nfloats!=16)
        fail_contract("unsupported getter cardinality",program,location);
    if(location==-1) { memset(out,0,(size_t)nfloats*sizeof *out);return; }
    p=find_program(program,0);
    if(!p)fail_contract("getter for unknown program",program,location);
    u=find_uniform(program,location,0);
    if(!u)fail_contract("getter for unknown location",program,location);
    if(u->written_floats && u->written_floats!=(unsigned char)nfloats)
        fail_contract("getter cardinality does not match written uniform",program,location);
    if(u->written_floats)memcpy(out,u->value,(size_t)nfloats*sizeof *out);
    else memset(out,0,(size_t)nfloats*sizeof *out); /* valid GLSL default */
}
void render_uniform1f(GLint l,GLfloat x) {
    GLfloat v[1]={x};if(l==-1)return;prepare_write(current_program,l,1);glUniform1f(l,x);store_values(current_program,l,v,1);
}
void render_uniform1i(GLint l,GLint x) {
    GLfloat v[1]={(GLfloat)x};if(l==-1)return;prepare_write(current_program,l,1);glUniform1i(l,x);store_values(current_program,l,v,1);
}
void render_uniform2f(GLint l,GLfloat x,GLfloat y) {
    GLfloat v[2]={x,y};if(l==-1)return;prepare_write(current_program,l,2);glUniform2f(l,x,y);store_values(current_program,l,v,2);
}
void render_uniform3f(GLint l,GLfloat x,GLfloat y,GLfloat z) {
    GLfloat v[3]={x,y,z};if(l==-1)return;prepare_write(current_program,l,3);glUniform3f(l,x,y,z);store_values(current_program,l,v,3);
}
void render_uniform4f(GLint l,GLfloat x,GLfloat y,GLfloat z,GLfloat w) {
    GLfloat v[4]={x,y,z,w};if(l==-1)return;prepare_write(current_program,l,4);glUniform4f(l,x,y,z,w);store_values(current_program,l,v,4);
}
static void uniform_array(GLint l,GLsizei count,unsigned width,const GLfloat *values) {
    GLint locations[RU_UNIFORMS];unsigned i;ProgramEntry *p;UniformEntry *u;
    if(l==-1)return;
    if(count<=0 || (unsigned)count>RU_UNIFORMS)fail_contract("unsupported uniform array count",current_program,l);
    if(!values)fail_contract("null uniform array",current_program,l);
    p=find_program(current_program,0);if(!p)fail_contract("array write for unknown program",current_program,l);
    if(count>1)(void)array_source(p,current_program,l);
    for(i=0;i<(unsigned)count;i++) {
        if(count==1)locations[i]=l;
        else (void)array_location(current_program,l,i,locations+i);
        if(locations[i]!=-1) {
            u=find_uniform(current_program,locations[i],0);require_width(u,current_program,locations[i],width);
        }
    }
    if(width==2)glUniform2fv(l,count,values);
    else if(width==3)glUniform3fv(l,count,values);
    else glUniform4fv(l,count,values);
    for(i=0;i<(unsigned)count;i++)if(locations[i]!=-1)
        store_values(current_program,locations[i],values+(size_t)i*width,width);
}
void render_uniform2fv(GLint l,GLsizei n,const GLfloat *v) { uniform_array(l,n,2,v); }
void render_uniform3fv(GLint l,GLsizei n,const GLfloat *v) { uniform_array(l,n,3,v); }
void render_uniform4fv(GLint l,GLsizei n,const GLfloat *v) { uniform_array(l,n,4,v); }
void render_uniform_matrix4fv(GLint l,GLsizei n,GLboolean transpose,const GLfloat *v) {
    GLint locations[RU_UNIFORMS];unsigned i,row,col;ProgramEntry *p;UniformEntry *u;
    if(l==-1)return;
    if(n<=0 || (unsigned)n>RU_UNIFORMS)fail_contract("unsupported matrix array count",current_program,l);
    if(!v)fail_contract("null matrix array",current_program,l);
    p=find_program(current_program,0);if(!p)fail_contract("matrix write for unknown program",current_program,l);
    if(n>1)(void)array_source(p,current_program,l);
    for(i=0;i<(unsigned)n;i++) {
        if(n==1)locations[i]=l;
        else (void)array_location(current_program,l,i,locations+i);
        if(locations[i]!=-1) {
            u=find_uniform(current_program,locations[i],0);require_width(u,current_program,locations[i],16);
        }
    }
    glUniformMatrix4fv(l,n,transpose,v);
    for(i=0;i<(unsigned)n;i++)if(locations[i]!=-1) {
        GLfloat transposed[16];const GLfloat *matrix=v+(size_t)i*16;
        if(transpose) {
            for(row=0;row<4;row++)for(col=0;col<4;col++)transposed[col*4+row]=matrix[row*4+col];
            matrix=transposed;
        }
        store_values(current_program,locations[i],matrix,16);
    }
}
void render_delete_program(GLuint program) {
    unsigned i;if(program)glDeleteProgram(program);
    for(i=0;i<RU_PROGRAMS;i++)if(cache[i].used && cache[i].program==program) {
        memset(cache+i,0,sizeof cache[i]);break;
    }
}
