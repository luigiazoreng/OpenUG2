/* Exercise the real native monitor with deterministic kernel/IO shims. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int SceUID;
typedef unsigned SceSize;
typedef struct { unsigned buttons; } SceCtrlData;
#define SCE_CTRL_SELECT 1u
#define SCE_CTRL_START 8u
#define SCE_O_WRONLY 1
#define SCE_O_CREAT 2
#define SCE_O_TRUNC 4
static int sceIoOpen(const char *, int, int);
static int sceIoWrite(SceUID, const void *, SceSize);
static int sceIoClose(SceUID);
static int64_t sceKernelGetSystemTimeWide(void);
static int sceCtrlPeekBufferPositive(int, SceCtrlData *, int);
static int sceKernelDelayThread(unsigned);
static int sceKernelExitProcess(int);
static int sceKernelCreateThread(const char *, int (*)(SceSize, void *), int, SceSize, unsigned, int, const void *);
static int sceKernelStartThread(SceUID, SceSize, void *);
static int sceKernelDeleteThread(SceUID);
static int sceKernelWaitThreadEnd(SceUID, int *, unsigned *);
#define OPENUG2_STARTUP_GUARD_TEST
#include "../platform/vita/startup_guard.c"
static uint64_t clock_us, step_us;
static int steps, mode, exit_code, open_fail, create_fail, start_fail, deleted, closed;
static int (*entry)(SceSize, void *);
static char log_text[4096];
static int sceIoOpen(const char *p, int flags, int perms) {
    assert(strstr(p,"startup-guard.log") && flags == 7 && perms == 0666); return open_fail ? -1 : 10;
}
static int sceIoWrite(SceUID fd, const void *buf, SceSize n) {
    assert(fd==10); size_t at=strlen(log_text); assert(at+n<sizeof log_text);
    memcpy(log_text+at,buf,n); log_text[at+n]=0; return (int)n;
}
static int sceIoClose(SceUID fd) { assert(fd==10); closed++; return 0; }
static int64_t sceKernelGetSystemTimeWide(void) { return (int64_t)clock_us; }
static int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad, int count) {
    assert(port==0 && count==1);
    pad->buttons=mode==1 ? SCE_CTRL_SELECT|SCE_CTRL_START : SCE_CTRL_START;
    return mode==4 ? -1 : 1;
}
static int sceKernelDelayThread(unsigned delay) {
    assert(delay==50000); clock_us+=step_us; steps++;
    if(mode==3) vita_startup_guard_stage(VITA_STARTUP_FRAGMENT);
    if((mode==0 || mode==4) && steps==4) __atomic_store_n(&guard_running,0,__ATOMIC_RELEASE);
    return 0;
}
static int sceKernelExitProcess(int code) { assert(exit_code==-1); exit_code=code; return 0; }
static int sceKernelCreateThread(const char *name,int (*fn)(SceSize,void *),int priority,SceSize stack,unsigned attr,int affinity,const void *option) {
    assert(strstr(name,"startup_guard") && priority==0x10000100 && stack==0x4000 && !attr && !affinity && !option);
    entry=fn; return create_fail ? -1 : 11;
}
static int sceKernelStartThread(SceUID id,SceSize count,void *arg) { assert(id==11 && !count && !arg); return start_fail ? -1 : 0; }
static int sceKernelDeleteThread(SceUID id) { assert(id==11); deleted++; return 0; }
static int sceKernelWaitThreadEnd(SceUID id,int *stat,unsigned *timeout) { assert(id==11 && !stat && !timeout); assert(!guard_running); return entry(0,NULL); }
static void reset(int m,uint64_t increment) {
    assert(guard_thread==-1 && guard_log==-1);
    mode=m;step_us=increment;clock_us=0;steps=0;exit_code=-1;open_fail=create_fail=start_fail=deleted=closed=0;log_text[0]=0;
}
int main(void) {
    reset(0,50000);assert(!vita_startup_guard_start(NULL));assert(!vita_startup_guard_start("bad"));assert(!vita_startup_guard_start("zzzzzzzzzzzzzzzzzzzzzzzz"));assert(vita_startup_guard_start("0123456789abcdef01234567")); assert(!vita_startup_guard_start("0123456789abcdef01234567"));
    vita_startup_guard_stage(VITA_STARTUP_CAR);entry(0,NULL);assert(exit_code==-1 && steps==4);vita_startup_guard_stop();assert(deleted==1 && closed==1);vita_startup_guard_stop();assert(closed==1);
    reset(1,50000);assert(vita_startup_guard_start("0123456789abcdef01234567"));entry(0,NULL);assert(exit_code==0 && strstr(log_text,"EXIT_SELECT_START"));vita_startup_guard_stop();
    reset(2,60000000);assert(vita_startup_guard_start("0123456789abcdef01234567"));entry(0,NULL);assert(exit_code==1 && clock_us==120000000 && strstr(log_text,"TIMEOUT stage=context"));vita_startup_guard_stop();
    reset(3,60000000);assert(vita_startup_guard_start("0123456789abcdef01234567"));entry(0,NULL);assert(exit_code==1 && clock_us==300000000);vita_startup_guard_stop();
    reset(4,50000);assert(vita_startup_guard_start("0123456789abcdef01234567"));entry(0,NULL);assert(exit_code==-1);vita_startup_guard_stop();
    reset(0,50000);open_fail=1;assert(!vita_startup_guard_start("0123456789abcdef01234567"));assert(!deleted && !closed);
    reset(0,50000);create_fail=1;assert(!vita_startup_guard_start("0123456789abcdef01234567"));assert(!deleted && closed==1);
    reset(0,50000);start_fail=1;assert(!vita_startup_guard_start("0123456789abcdef01234567"));assert(deleted==1 && closed==1);
    reset(0,50000);assert(vita_startup_guard_start("0123456789abcdef01234567"));vita_startup_guard_stop();assert(exit_code==-1 && deleted==1 && closed==1);
    puts("PASS native startup guard: blocked main/input, phase/total deadlines, read failure, lifecycle rollback");return 0;
}
