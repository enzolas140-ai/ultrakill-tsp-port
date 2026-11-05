/* BENDER Unity IL2CPP AArch64 probe 2.7.66 - fonte copiada de
 * /roms/ports/60_parsecs_v1_3_6_patched/unity_probe_source/unity_probe.c
 * (port funcionante nesta mesma firmware) e re-identificada para ULTRAKILL
 * (arkboxgames.ultrakill). Proveniencia: framework de terceiros, uso
 * pessoal neste cartao SD. NADA do codigo de jogo da Arkbox Games aqui.
 */
#define _GNU_SOURCE
#define BENDER_APP_PACKAGE "arkboxgames.ultrakill"
#define BENDER_APP_VERSION_NAME "1.1a"
#define BENDER_APP_VERSION_CODE 110
#define BENDER_APP_MIN_SDK 22
#define BENDER_APP_TARGET_SDK 33
#define BENDER_FIREBASE_SOURCE ""
#define BENDER_FIREBASE_API_KEY ""
#define BENDER_FIREBASE_APPLICATION_ID ""
#define BENDER_FIREBASE_DATABASE_URL ""
#define BENDER_FIREBASE_GCM_SENDER_ID ""
#define BENDER_FIREBASE_STORAGE_BUCKET ""
#define BENDER_FIREBASE_PROJECT_ID ""
#include <dlfcn.h>
#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <inttypes.h>
#include <link.h>
#include <stdarg.h>
#include <stdbool.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ucontext.h>

#ifndef R_AARCH64_ABS64
#define R_AARCH64_ABS64 257
#endif
#ifndef R_AARCH64_GLOB_DAT
#define R_AARCH64_GLOB_DAT 1025
#endif
#ifndef R_AARCH64_JUMP_SLOT
#define R_AARCH64_JUMP_SLOT 1026
#endif
#ifndef R_AARCH64_RELATIVE
#define R_AARCH64_RELATIVE 1027
#endif

#define MAX_MODULES 64
#define MAX_NEEDED 64
#define MAX_RANGES 8
#define MAX_GUEST_ATEXIT 4096

typedef struct {
    uintptr_t vaddr;
    size_t size;
    size_t entsz;
} RelaRange;

typedef struct Module {
    char name[128];
    char path[1024];
    uint8_t *raw;
    size_t raw_size;
    Elf64_Ehdr *eh;
    Elf64_Phdr *ph;
    void *reserve;
    size_t reserve_size;
    uintptr_t min_vaddr;
    uintptr_t bias;
    Elf64_Dyn *dynamic;
    const char *strtab;
    Elf64_Sym *symtab;
    size_t syment;
    size_t symcount;
    RelaRange ranges[MAX_RANGES];
    int range_count;
    uintptr_t init_array;
    size_t init_array_count;
    char needed[MAX_NEEDED][128];
    int needed_count;
    size_t relative_count;
    size_t self_count;
    size_t cross_count;
    size_t host_count;
    size_t missing_count;
    size_t unsupported_count;
    int init_state; /* 0=nao iniciado, 1=executando, 2=concluido */
    int jni_onload_state; /* opcionais: 0=nao chamado, 1=executando, 2=concluido */
    int lazy_module; /* Firebase e outros modulos grandes: parse no bootstrap, map/reloc somente no dlopen */
    int relocate_state; /* 0=nao relocada, 1=em progresso, 2=pronta */
    int protect_state; /* 0=RWX temporario, 2=protecoes ELF aplicadas */
} Module;

static FILE *g_log;
static int bender_render_scale_guest_width_2768(void);
static int bender_render_scale_guest_height_2768(void);
static void bender_render_scale_map_input_2768(float *x,float *y);
static Module g_modules[MAX_MODULES];
static int g_module_count;
static long g_pagesize;
static int g_log_fd = -1;
static char g_checkpoint[384];
static volatile sig_atomic_t g_checkpoint_len;
static unsigned long g_bender_progress_serial;
static volatile pid_t g_bender_render_tid;
static char g_bender_render_checkpoint[384];
static volatile sig_atomic_t g_bender_render_checkpoint_len;
static int g_guest_allow_unity_init;
static int g_guest_allow_il2cpp_init;
static int g_guest_block_il2cpp;
static char g_guest_dlerror[512];

typedef struct {
    void (*fn)(void *);
    void *arg;
    void *dso;
} GuestAtExit;
static GuestAtExit g_guest_atexit[MAX_GUEST_ATEXIT];
static size_t g_guest_atexit_count;
static size_t g_guest_atexit_dropped;
static size_t g_guest_finalize_calls;

static int g_bender_runtime_logging_2762=-1;
static int bender_runtime_logging_enabled_2762(void){
    if(g_bender_runtime_logging_2762>=0)return g_bender_runtime_logging_2762;
    const char*v=getenv("BENDER_RUNTIME_LOGGING");
    if(v&&(!strcasecmp(v,"0")||!strcasecmp(v,"false")||!strcasecmp(v,"off")||!strcasecmp(v,"no")))g_bender_runtime_logging_2762=0;
    else g_bender_runtime_logging_2762=1;
    return g_bender_runtime_logging_2762;
}
static volatile unsigned long g_bender_panel_log_dropped;
static int g_bender_panel_log_nonblock;

static void bender_panel_log_transport_init(void){
    const char*panel=getenv("BENDER_PANEL_STREAM");
    if(!(panel&&panel[0]=='1'&&panel[1]=='\0'))return;
    int f1=fcntl(STDOUT_FILENO,F_GETFL,0);
    if(f1>=0&&(f1&O_NONBLOCK)==0)(void)fcntl(STDOUT_FILENO,F_SETFL,f1|O_NONBLOCK);
    int f2=fcntl(STDERR_FILENO,F_GETFL,0);
    if(f2>=0&&(f2&O_NONBLOCK)==0)(void)fcntl(STDERR_FILENO,F_SETFL,f2|O_NONBLOCK);
    g_bender_panel_log_nonblock=1;
}

/* ULTRAKILL: o cartao e exFAT sem journal e o device perde o cache se for
 * desligado/puxado sem ejectar (foi assim que dois unity_probe.log sumiram).
 * Empurra o log para a midia de tempos em tempos - custo desprezivel perto
 * de um frame em llvmpipe. BENDER_UK_LOG_SYNC=0 desliga. */
static void bender_log_flush_media(int fd) {
    static unsigned long writes;
    static int off=-1;
    if(off!=0){ const char*e=getenv("BENDER_UK_LOG_SYNC"); if(e&&!strcmp(e,"0"))off=0; }
    if(off==0) return;
    if(++writes % 24ul) return;
    fsync(fd);   /* unistd.h sempre declara; seguro em qualquer glibc */
}
static void bender_log_write_best_effort(int fd,const char*buf,size_t len){
    if(fd<0||!buf||len==0)return;
    size_t off=0;
    int wrote=0;
    while(off<len){
        ssize_t wr=write(fd,buf+off,len-off);
        if(wr>0){off+=(size_t)wr;wrote=1;continue;}
        if(wr<0&&errno==EINTR)continue;
        if(wr<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){
            __atomic_add_fetch(&g_bender_panel_log_dropped,1ul,__ATOMIC_RELAXED);
        }
        break;
    }
    if(wrote) bender_log_flush_media(fd);
}

static void logmsg(const char *fmt, ...) {
    if(!bender_runtime_logging_enabled_2762())return;
    char line[4096];
    va_list ap;
    va_start(ap,fmt);
    int n=vsnprintf(line,sizeof(line),fmt,ap);
    va_end(ap);
    if(n<=0)return;
    size_t len=(size_t)n;
    if(len>=sizeof(line))len=sizeof(line)-1u;
    bender_log_write_best_effort(STDOUT_FILENO,line,len);
    if(g_log_fd>=0&&g_log_fd!=STDOUT_FILENO)bender_log_write_best_effort(g_log_fd,line,len);
}


static void set_checkpoint(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n=vsnprintf(g_checkpoint,sizeof(g_checkpoint),fmt,ap);
    va_end(ap);
    if(n<0) n=0;
    if((size_t)n>=sizeof(g_checkpoint)) n=(int)sizeof(g_checkpoint)-1;
    g_checkpoint_len=(sig_atomic_t)n;
    pid_t tid=(pid_t)syscall(SYS_gettid);
    pid_t render_tid=__atomic_load_n(&g_bender_render_tid,__ATOMIC_RELAXED);
    if(render_tid<=0 || tid==render_tid){
        __atomic_add_fetch(&g_bender_progress_serial,1ul,__ATOMIC_RELAXED);
        memcpy(g_bender_render_checkpoint,g_checkpoint,(size_t)n+1u);
        g_bender_render_checkpoint_len=(sig_atomic_t)n;
    }
    if(n) logmsg("[UNITY_PROBE][CHECKPOINT] %s\n",g_checkpoint);
}
static void set_checkpoint_quiet(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n=vsnprintf(g_checkpoint,sizeof(g_checkpoint),fmt,ap);
    va_end(ap);
    if(n<0) n=0;
    if((size_t)n>=sizeof(g_checkpoint)) n=(int)sizeof(g_checkpoint)-1;
    g_checkpoint_len=(sig_atomic_t)n;
    pid_t tid=(pid_t)syscall(SYS_gettid);
    pid_t render_tid=__atomic_load_n(&g_bender_render_tid,__ATOMIC_RELAXED);
    if(render_tid<=0 || tid==render_tid){
        __atomic_add_fetch(&g_bender_progress_serial,1ul,__ATOMIC_RELAXED);
        memcpy(g_bender_render_checkpoint,g_checkpoint,(size_t)n+1u);
        g_bender_render_checkpoint_len=(sig_atomic_t)n;
    }
}

#define BENDER_FATAL_SIGNAL_SLOTS 65
#define BENDER_CRASH_ALTSTACK_SIZE (64u*1024u)

/*
 * Android/Bionic arm64 signal ABI is NOT glibc's userspace ABI.
 * AOSP LP64 layout is: int sa_flags; handler; kernel-sized sigset_t; restorer.
 * On arm64 the kernel signal set is 64 bits (one unsigned long).
 * Keep these guest-facing structures explicit and translate to host glibc.
 */
typedef struct BionicSigSet64 {
    uint64_t bits;
} BionicSigSet64;

typedef struct BionicSigAction64 {
    int32_t sa_flags;
    uint32_t __pad_flags;
    uintptr_t handler;
    BionicSigSet64 sa_mask;
    uintptr_t sa_restorer;
} BionicSigAction64;

typedef struct BionicStack64 {
    uintptr_t ss_sp;
    int32_t ss_flags;
    uint32_t __pad_flags;
    uint64_t ss_size;
} BionicStack64;

_Static_assert(sizeof(BionicSigSet64)==8, "Bionic arm64 sigset_t must be 8 bytes");
_Static_assert(sizeof(BionicSigAction64)==32, "Bionic arm64 sigaction must be 32 bytes");
_Static_assert(sizeof(BionicStack64)==24, "Bionic arm64 stack_t must be 24 bytes");

static __thread unsigned char g_tls_crash_altstack[BENDER_CRASH_ALTSTACK_SIZE] __attribute__((aligned(16)));
static BionicSigAction64 g_guest_fatal_actions[BENDER_FATAL_SIGNAL_SLOTS];
static unsigned char g_guest_fatal_valid[BENDER_FATAL_SIGNAL_SLOTS];
static __thread volatile sig_atomic_t g_crash_handler_active;
static volatile unsigned long g_bender_thread_create_count;

static int bender_is_fatal_signal(int sig) {
    return sig==SIGSEGV || sig==SIGBUS || sig==SIGILL || sig==SIGABRT || sig==SIGFPE;
}
static size_t bender_async_strlen(const char *s) { size_t n=0; if(!s)return 0; while(s[n])n++; return n; }
static void bender_async_write(int fd,const char *s,size_t n) {
    while(n){ssize_t w=write(fd,s,n);if(w<=0)return;s+=(size_t)w;n-=(size_t)w;}
}
static void bender_async_cstr(int fd,const char *s){if(s)bender_async_write(fd,s,bender_async_strlen(s));}
static size_t bender_fmt_u64_dec(char *out,size_t cap,uint64_t v){
    char rev[32];size_t r=0,n=0;if(!cap)return 0;if(v==0){out[0]='0';return 1;}
    while(v&&r<sizeof(rev)){rev[r++]=(char)('0'+(v%10u));v/=10u;}
    while(r&&n<cap){out[n++]=rev[--r];}
    return n;
}
static size_t bender_fmt_i64_dec(char *out,size_t cap,int64_t v){
    size_t n=0;if(v<0&&cap){out[n++]='-';uint64_t u=(uint64_t)(-(v+1))+1u;n+=bender_fmt_u64_dec(out+n,cap-n,u);return n;}
    return bender_fmt_u64_dec(out,cap,(uint64_t)v);
}
static size_t bender_fmt_uptr_hex(char *out,size_t cap,uintptr_t v){
    static const char hx[]="0123456789abcdef";char rev[2*sizeof(uintptr_t)];size_t r=0,n=0;
    if(cap>=2){out[n++]='0';out[n++]='x';}else return n;
    if(v==0){if(n<cap)out[n++]='0';return n;}
    while(v&&r<sizeof(rev)){rev[r++]=hx[v&15u];v>>=4u;}
    while(r&&n<cap){out[n++]=rev[--r];}
    return n;
}
static void bender_async_field_hex(int fd,const char *key,uintptr_t v){char b[2+2*sizeof(uintptr_t)];bender_async_cstr(fd,key);size_t n=bender_fmt_uptr_hex(b,sizeof(b),v);bender_async_write(fd,b,n);}
static void bender_async_field_dec(int fd,const char *key,int64_t v){char b[40];bender_async_cstr(fd,key);size_t n=bender_fmt_i64_dec(b,sizeof(b),v);bender_async_write(fd,b,n);}
static Module *bender_module_for_runtime_addr(uintptr_t addr){
    if(!addr)return NULL;
    for(int i=0;i<g_module_count;i++){
        uintptr_t lo=(uintptr_t)g_modules[i].reserve;
        uintptr_t hi=lo+g_modules[i].reserve_size;
        if(addr>=lo&&addr<hi)return &g_modules[i];
    }
    return NULL;
}
static void bender_async_module_line(int fd,const char *tag,uintptr_t addr){
    Module*m=bender_module_for_runtime_addr(addr);bender_async_cstr(fd,tag);bender_async_field_hex(fd," addr=",addr);
    if(m){bender_async_cstr(fd," module=");bender_async_cstr(fd,m->name);bender_async_field_hex(fd," offset=",addr-m->bias);}
    else bender_async_cstr(fd," module=host-or-unknown");
    bender_async_cstr(fd,"\n");
}
static void bender_async_code_bytes(int fd,uintptr_t pc){
    Module*m=bender_module_for_runtime_addr(pc);if(!m)return;
    uintptr_t lo=(uintptr_t)m->reserve,hi=lo+m->reserve_size;
    if(pc<lo||pc>hi-16u)return;
    const unsigned char*p=(const unsigned char*)pc;static const char hx[]="0123456789abcdef";char out[32];
    for(size_t i=0;i<16u;i++){out[i*2u]=hx[(p[i]>>4)&15u];out[i*2u+1u]=hx[p[i]&15u];}
    bender_async_cstr(fd,"[UNITY_PROBE][CRASH_CODE] pc_bytes=");bender_async_write(fd,out,sizeof(out));bender_async_cstr(fd,"\n");
}
static void bender_async_code_window(int fd,uintptr_t pc){
    Module*m=bender_module_for_runtime_addr(pc);if(!m)return;
    uintptr_t lo=(uintptr_t)m->reserve,hi=lo+m->reserve_size;
    uintptr_t start=(pc>=lo+32u)?pc-32u:lo; size_t n=64u; if(start+n>hi)n=(size_t)(hi-start);
    static const char hx[]="0123456789abcdef"; char out[128]; if(n>64u)n=64u; const unsigned char*p=(const unsigned char*)start;
    for(size_t i=0;i<n;i++){out[i*2u]=hx[(p[i]>>4)&15u];out[i*2u+1u]=hx[p[i]&15u];}
    bender_async_cstr(fd,"[UNITY_PROBE][CRASH_CODE_WINDOW]");bender_async_field_hex(fd," start=",start);bender_async_cstr(fd," bytes=");bender_async_write(fd,out,n*2u);bender_async_cstr(fd,"\n");
}
static void crash_handler_info(int sig,siginfo_t *info,void *uctx) {
    if(g_crash_handler_active){_exit(128+sig);}
    g_crash_handler_active=1;
    uintptr_t pc=0,lr=0,sp=0,far=info?(uintptr_t)info->si_addr:0;
#if defined(__aarch64__)
    uintptr_t regs[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    if(uctx){ucontext_t*uc=(ucontext_t*)uctx;pc=(uintptr_t)uc->uc_mcontext.pc;sp=(uintptr_t)uc->uc_mcontext.sp;lr=(uintptr_t)uc->uc_mcontext.regs[30];for(int i=0;i<16;i++)regs[i]=(uintptr_t)uc->uc_mcontext.regs[i];if(!far)far=(uintptr_t)uc->uc_mcontext.fault_address;}
#else
    (void)uctx;
#endif
    int fdlist[2]={STDERR_FILENO,g_log_fd};
    for(int k=0;k<2;k++){
        int fd=fdlist[k];if(fd<0||(k==1&&fd==STDERR_FILENO))continue;
        bender_async_cstr(fd,"[UNITY_PROBE][CRASH_CTX]");bender_async_field_dec(fd," tid=",(int64_t)syscall(SYS_gettid));bender_async_field_dec(fd," signal=",sig);bender_async_field_dec(fd," code=",info?info->si_code:0);bender_async_field_hex(fd," fault=",far);bender_async_field_hex(fd," pc=",pc);bender_async_field_hex(fd," lr=",lr);bender_async_field_hex(fd," sp=",sp);bender_async_cstr(fd,"\n");
        bender_async_module_line(fd,"[UNITY_PROBE][CRASH_PC]",pc);bender_async_module_line(fd,"[UNITY_PROBE][CRASH_LR]",lr);
#if defined(__aarch64__)
        bender_async_cstr(fd,"[UNITY_PROBE][CRASH_REG0_7]");for(int i=0;i<8;i++){char key[8]={' ','x',(char)('0'+i),'=','\0',0,0,0};bender_async_field_hex(fd,key,regs[i]);}bender_async_cstr(fd,"\n");
        bender_async_cstr(fd,"[UNITY_PROBE][CRASH_REG8_15]");for(int i=8;i<16;i++){char key[9];if(i<10){key[0]=' ';key[1]='x';key[2]=(char)('0'+i);key[3]='=';key[4]='\0';}else{key[0]=' ';key[1]='x';key[2]='1';key[3]=(char)('0'+(i-10));key[4]='=';key[5]='\0';}bender_async_field_hex(fd,key,regs[i]);}bender_async_cstr(fd,"\n");
#endif
        bender_async_code_bytes(fd,pc); bender_async_code_window(fd,pc);
        bender_async_cstr(fd,"[UNITY_PROBE][CRASH_CHECKPOINT] ");if(g_checkpoint_len>0)bender_async_write(fd,g_checkpoint,(size_t)g_checkpoint_len);bender_async_cstr(fd,"\n");
    }
    _exit(128+sig);
}

static void bender_guest_sigset_to_host(sigset_t *dst,const BionicSigSet64 *src){
    sigemptyset(dst);if(!src)return;
    for(int sig=1;sig<=64;sig++) if(src->bits & (UINT64_C(1)<<(sig-1))) (void)sigaddset(dst,sig);
}
static void bender_host_sigset_to_guest(BionicSigSet64 *dst,const sigset_t *src){
    if(!dst) return;
    dst->bits=0;
    if(!src) return;
    for(int sig=1;sig<=64;sig++) if(sigismember(src,sig)==1) dst->bits|=(UINT64_C(1)<<(sig-1));
}
static void bender_guest_action_to_host(struct sigaction *dst,const BionicSigAction64 *src){
    memset(dst,0,sizeof(*dst));if(!src)return;
    dst->sa_flags=src->sa_flags;
    if(src->sa_flags & SA_SIGINFO) dst->sa_sigaction=(void(*)(int,siginfo_t*,void*))src->handler;
    else dst->sa_handler=(void(*)(int))src->handler;
    bender_guest_sigset_to_host(&dst->sa_mask,&src->sa_mask);
}
static void bender_host_action_to_guest(BionicSigAction64 *dst,const struct sigaction *src){
    if(!dst) return;
    memset(dst,0,sizeof(*dst));
    if(!src) return;
    dst->sa_flags=src->sa_flags;
    dst->handler=(uintptr_t)((src->sa_flags & SA_SIGINFO)?(void*)src->sa_sigaction:(void*)src->sa_handler);
    bender_host_sigset_to_guest(&dst->sa_mask,&src->sa_mask);
    dst->sa_restorer=0;
}
static __thread volatile sig_atomic_t g_bender_stall_handler_active;
static void bender_render_stall_handler(int sig,siginfo_t *info,void *uctx) {
    (void)sig;(void)info;(void)uctx;
    if(g_bender_stall_handler_active)return;
    g_bender_stall_handler_active=1;
    uintptr_t pc=0,lr=0,sp=0;
#if defined(__aarch64__)
    uintptr_t regs[8]={0,0,0,0,0,0,0,0};
    if(uctx){ucontext_t*uc=(ucontext_t*)uctx;pc=(uintptr_t)uc->uc_mcontext.pc;sp=(uintptr_t)uc->uc_mcontext.sp;lr=(uintptr_t)uc->uc_mcontext.regs[30];for(int i=0;i<8;i++)regs[i]=(uintptr_t)uc->uc_mcontext.regs[i];}
#endif
    int fdlist[2]={STDERR_FILENO,g_log_fd};
    for(int k=0;k<2;k++){
        int fd=fdlist[k];if(fd<0||(k==1&&fd==STDERR_FILENO))continue;
        bender_async_cstr(fd,"[BENDER_RENDER_STALL_CTX]");
        bender_async_field_dec(fd," tid=",(int64_t)syscall(SYS_gettid));
        bender_async_field_hex(fd," pc=",pc);bender_async_field_hex(fd," lr=",lr);bender_async_field_hex(fd," sp=",sp);
#if defined(__aarch64__)
        for(int i=0;i<8;i++){char key[12];key[0]=' ';key[1]='x';key[2]=(char)('0'+i);key[3]='=';key[4]=0;bender_async_field_hex(fd,key,regs[i]);}
#endif
        bender_async_cstr(fd,"\n");
        bender_async_module_line(fd,"[BENDER_RENDER_STALL_PC]",pc);
        bender_async_module_line(fd,"[BENDER_RENDER_STALL_LR]",lr);
        bender_async_code_window(fd,pc);
    }
    g_bender_stall_handler_active=0;
}
static void bender_render_request_stall_snapshot(void){
    pid_t tid=__atomic_load_n(&g_bender_render_tid,__ATOMIC_RELAXED);
    if(tid<=0)return;
    struct sigaction sa;memset(&sa,0,sizeof(sa));sa.sa_sigaction=bender_render_stall_handler;sigemptyset(&sa.sa_mask);sa.sa_flags=SA_SIGINFO|SA_ONSTACK;
    if(sigaction(SIGUSR2,&sa,NULL)!=0)return;
    logmsg("[BENDER_RENDER_WATCHDOG][STALL_SNAPSHOT_REQUEST] render_tid=%d signal=%d render_checkpoint=%s\n",(int)tid,SIGUSR2,g_bender_render_checkpoint_len?g_bender_render_checkpoint:"(none)");
    (void)syscall(SYS_tgkill,getpid(),tid,SIGUSR2);
    usleep(150000);
}

static void install_crash_handlers(void) {
    stack_t ss;memset(&ss,0,sizeof(ss));ss.ss_sp=g_tls_crash_altstack;ss.ss_size=sizeof(g_tls_crash_altstack);ss.ss_flags=0;(void)sigaltstack(&ss,NULL);
    struct sigaction sa;memset(&sa,0,sizeof(sa));sa.sa_sigaction=crash_handler_info;sigemptyset(&sa.sa_mask);sa.sa_flags=SA_SIGINFO|SA_ONSTACK;
    int sigs[]={SIGSEGV,SIGBUS,SIGILL,SIGABRT,SIGFPE};
    for(size_t i=0;i<sizeof(sigs)/sizeof(sigs[0]);i++)(void)sigaction(sigs[i],&sa,NULL);
}
static int bender_sigaction(int sig,const BionicSigAction64 *act,BionicSigAction64 *oldact){
    if(bender_is_fatal_signal(sig)){
        if(oldact){if(sig>0&&sig<BENDER_FATAL_SIGNAL_SLOTS&&g_guest_fatal_valid[sig])*oldact=g_guest_fatal_actions[sig];else memset(oldact,0,sizeof(*oldact));}
        if(act&&sig>0&&sig<BENDER_FATAL_SIGNAL_SLOTS){g_guest_fatal_actions[sig]=*act;g_guest_fatal_valid[sig]=1;logmsg("[BENDER_SIGNAL][SIGACTION_BIONIC] sig=%d flags=0x%x handler=%p mask=0x%016" PRIx64 " action=stored_bender_handler_kept\n",sig,(unsigned)act->sa_flags,(void*)act->handler,act->sa_mask.bits);}
        install_crash_handlers();return 0;
    }
    struct sigaction ha,ho;struct sigaction *hap=NULL,*hop=NULL;
    if(act){bender_guest_action_to_host(&ha,act);hap=&ha;}
    if(oldact)hop=&ho;
    int rc=sigaction(sig,hap,hop);
    if(rc==0&&oldact)bender_host_action_to_guest(oldact,&ho);
    return rc;
}
static int bender_sigaction64(int sig,const BionicSigAction64 *act,BionicSigAction64 *oldact){return bender_sigaction(sig,act,oldact);}
typedef void (*BenderSignalHandler)(int);
static BenderSignalHandler bender_signal(int sig,BenderSignalHandler handler){
    if(bender_is_fatal_signal(sig)){
        BenderSignalHandler old=SIG_DFL;
        if(sig>0&&sig<BENDER_FATAL_SIGNAL_SLOTS&&g_guest_fatal_valid[sig])old=(BenderSignalHandler)g_guest_fatal_actions[sig].handler;
        BionicSigAction64 ga;memset(&ga,0,sizeof(ga));ga.handler=(uintptr_t)handler;(void)bender_sigaction(sig,&ga,NULL);return old;
    }
    return signal(sig,handler);
}
static int bender_sigaltstack(const BionicStack64 *ss,BionicStack64 *old_ss){
    if(old_ss){stack_t hs;if(sigaltstack(NULL,&hs)==0){old_ss->ss_sp=(uintptr_t)hs.ss_sp;old_ss->ss_flags=hs.ss_flags;old_ss->__pad_flags=0;old_ss->ss_size=(uint64_t)hs.ss_size;}else memset(old_ss,0,sizeof(*old_ss));}
    if(ss)logmsg("[BENDER_SIGNAL][ALTSTACK_BIONIC] guest_sp=%p size=%" PRIu64 " flags=0x%x; BENDER TLS altstack preservada\n",(void*)ss->ss_sp,ss->ss_size,(unsigned)ss->ss_flags);
    install_crash_handlers();return 0;
}
static void bender_sanitize_guest_sigset(BionicSigSet64 *dst,const BionicSigSet64 *src){
    dst->bits=src?src->bits:0;
    int sigs[]={SIGSEGV,SIGBUS,SIGILL,SIGABRT,SIGFPE};
    for(size_t i=0;i<sizeof(sigs)/sizeof(sigs[0]);i++)dst->bits&=~(UINT64_C(1)<<(sigs[i]-1));
}
static int bender_sigemptyset(BionicSigSet64 *set){if(!set){errno=EINVAL;return -1;}set->bits=0;return 0;}
static int bender_sigfillset(BionicSigSet64 *set){if(!set){errno=EINVAL;return -1;}set->bits=UINT64_MAX;return 0;}
static int bender_sigaddset(BionicSigSet64 *set,int sig){if(!set||sig<1||sig>64){errno=EINVAL;return -1;}set->bits|=(UINT64_C(1)<<(sig-1));return 0;}
static int bender_sigdelset(BionicSigSet64 *set,int sig){if(!set||sig<1||sig>64){errno=EINVAL;return -1;}set->bits&=~(UINT64_C(1)<<(sig-1));return 0;}
static int bender_sigismember(const BionicSigSet64 *set,int sig){if(!set||sig<1||sig>64){errno=EINVAL;return -1;}return (set->bits&(UINT64_C(1)<<(sig-1)))?1:0;}
static int bender_sigprocmask(int how,const BionicSigSet64 *set,BionicSigSet64 *oldset){
    sigset_t hs,ho;sigset_t *hsp=NULL,*hop=oldset?&ho:NULL;
    BionicSigSet64 clean;if(set){bender_sanitize_guest_sigset(&clean,set);bender_guest_sigset_to_host(&hs,&clean);hsp=&hs;}
    int rc=sigprocmask(how,hsp,hop);if(rc==0&&oldset)bender_host_sigset_to_guest(oldset,&ho);return rc;
}
static int bender_sigprocmask64(int how,const BionicSigSet64 *set,BionicSigSet64 *oldset){return bender_sigprocmask(how,set,oldset);}
static int bender_pthread_sigmask(int how,const BionicSigSet64 *set,BionicSigSet64 *oldset){
    sigset_t hs,ho;sigset_t *hsp=NULL,*hop=oldset?&ho:NULL;
    BionicSigSet64 clean;if(set){bender_sanitize_guest_sigset(&clean,set);bender_guest_sigset_to_host(&hs,&clean);hsp=&hs;}
    int rc=pthread_sigmask(how,hsp,hop);if(rc==0&&oldset)bender_host_sigset_to_guest(oldset,&ho);install_crash_handlers();return rc;
}
static int bender_pthread_sigmask64(int how,const BionicSigSet64 *set,BionicSigSet64 *oldset){return bender_pthread_sigmask(how,set,oldset);}
static int bender_sigpending(BionicSigSet64 *set){sigset_t hs;if(!set){errno=EINVAL;return -1;}int rc=sigpending(&hs);if(rc==0)bender_host_sigset_to_guest(set,&hs);return rc;}
static int bender_sigsuspend(const BionicSigSet64 *set){sigset_t hs;if(!set){errno=EINVAL;return -1;}bender_guest_sigset_to_host(&hs,set);return sigsuspend(&hs);}


/* ---------------- BENDER Bionic pthread/sem ABI isolation ----------------
 * Guest Android objects are opaque Bionic layouts. Never hand their storage to glibc.
 * A stable side-table maps each guest address/type to a host synchronization object.
 */
#define BENDER_SYNC_MAX 8192
/* ==== ULTRAKILL fix -L: dialog Android nao pode travar o jogo ==== */
static void *bender_jni_proxy_dispatch(void*proxy,const char*method_name,void*arg0);
static void *g_dlg_listener[6];
static int   g_dlg_listener_n=0;
static int   g_dlg_pending=0;
static int   g_dlg_release_armed=0;
static int   g_dlg_release_used=0;
static void *g_dlg_shown_obj=NULL;
static unsigned long g_dlg_autodismiss_count=0;
static void  bender_dialog_flush(const char*reason);
static void *bender_dialog_wake_main(void *arg);
#define BENDER_SYNC_MUTEX 1
#define BENDER_SYNC_COND 2
#define BENDER_SYNC_RWLOCK 3
#define BENDER_SYNC_SEM 4
#define BENDER_SYNC_ONCE 5
#define BENDER_SYNC_ATTR 6
#define BENDER_SYNC_MUTEXATTR 7
#define BENDER_SYNC_CONDATTR 8
typedef union BenderSyncHost {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_rwlock_t rwlock;
    sem_t sem;
    pthread_once_t once;
    pthread_attr_t attr;
} BenderSyncHost;
typedef struct BenderSyncSlot {
    const void *guest;
    int type;
    int inited;
    int meta;
    BenderSyncHost host;
} BenderSyncSlot;
static BenderSyncSlot g_bender_sync[BENDER_SYNC_MAX];
static size_t g_bender_sync_count;
static pthread_mutex_t g_bender_sync_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned long g_bender_sync_calls;
static BenderSyncSlot *bender_sync_slot(const void *guest,int type,int create){
    if(!guest)return NULL;
    pthread_mutex_lock(&g_bender_sync_lock);
    BenderSyncSlot *free_slot=NULL;
    for(size_t i=0;i<g_bender_sync_count;i++){
        if(g_bender_sync[i].guest==guest&&g_bender_sync[i].type==type){pthread_mutex_unlock(&g_bender_sync_lock);return &g_bender_sync[i];}
    }
    if(create&&g_bender_sync_count<BENDER_SYNC_MAX){free_slot=&g_bender_sync[g_bender_sync_count++];memset(free_slot,0,sizeof(*free_slot));free_slot->guest=guest;free_slot->type=type;}
    pthread_mutex_unlock(&g_bender_sync_lock);return free_slot;
}
static void bender_sync_log_create(const char *kind,const void *guest,size_t index){
    unsigned long n=__atomic_add_fetch(&g_bender_sync_calls,1ul,__ATOMIC_RELAXED);
    if(n<=64ul||(n%1024ul)==0ul)logmsg("[BENDER_SYNC][CREATE] n=%lu kind=%s guest=%p slot=%zu\n",n,kind,guest,index);
}
static BenderSyncSlot *bender_mutex_slot(void *guest,int create){
    BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_MUTEX,create);if(!s)return NULL;
    pthread_mutex_lock(&g_bender_sync_lock);if(!s->inited&&create){pthread_mutexattr_t a;pthread_mutexattr_init(&a);int type=s->meta?s->meta:PTHREAD_MUTEX_NORMAL;pthread_mutexattr_settype(&a,type);int rc=pthread_mutex_init(&s->host.mutex,&a);pthread_mutexattr_destroy(&a);if(rc==0){s->inited=1;bender_sync_log_create("mutex",guest,(size_t)(s-g_bender_sync));}}pthread_mutex_unlock(&g_bender_sync_lock);return s;
}
static BenderSyncSlot *bender_cond_slot(void *guest,int create){
    BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_COND,create);if(!s)return NULL;
    pthread_mutex_lock(&g_bender_sync_lock);if(!s->inited&&create){pthread_condattr_t a;pthread_condattr_init(&a);if(s->meta) pthread_condattr_setclock(&a,(clockid_t)s->meta);int rc=pthread_cond_init(&s->host.cond,&a);pthread_condattr_destroy(&a);if(rc==0){s->inited=1;bender_sync_log_create("cond",guest,(size_t)(s-g_bender_sync));}}pthread_mutex_unlock(&g_bender_sync_lock);return s;
}
static BenderSyncSlot *bender_rwlock_slot(void *guest,int create){
    BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_RWLOCK,create);if(!s)return NULL;
    pthread_mutex_lock(&g_bender_sync_lock);if(!s->inited&&create){int rc=pthread_rwlock_init(&s->host.rwlock,NULL);if(rc==0){s->inited=1;bender_sync_log_create("rwlock",guest,(size_t)(s-g_bender_sync));}}pthread_mutex_unlock(&g_bender_sync_lock);return s;
}
static BenderSyncSlot *bender_sem_slot(void *guest,int create,unsigned initial){
    BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_SEM,create);if(!s)return NULL;
    pthread_mutex_lock(&g_bender_sync_lock);if(!s->inited&&create){int rc=sem_init(&s->host.sem,0,initial);if(rc==0){s->inited=1;bender_sync_log_create("sem",guest,(size_t)(s-g_bender_sync));}}pthread_mutex_unlock(&g_bender_sync_lock);return s;
}
static BenderSyncSlot *bender_once_slot(void *guest,int create){
    BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_ONCE,create);if(!s)return NULL;
    pthread_mutex_lock(&g_bender_sync_lock);if(!s->inited&&create){pthread_once_t init=PTHREAD_ONCE_INIT;memcpy(&s->host.once,&init,sizeof(init));s->inited=1;bender_sync_log_create("once",guest,(size_t)(s-g_bender_sync));}pthread_mutex_unlock(&g_bender_sync_lock);return s;
}
static int bender_is_render_thread(void){pid_t rt=__atomic_load_n(&g_bender_render_tid,__ATOMIC_RELAXED);return rt>0&&(pid_t)syscall(SYS_gettid)==rt;}
static void bender_sync_wait_diag(const char*phase,const char*kind,const void*guest,uintptr_t caller){
    if(!bender_is_render_thread())return;
    Module*m=bender_module_for_runtime_addr(caller);
    logmsg("[BENDER_SYNC][WAIT_%s] tid=%ld kind=%s guest=%p caller=%p module=%s offset=0x%llx\n",phase?phase:"?",(long)syscall(SYS_gettid),kind?kind:"?",guest,(void*)caller,m?m->name:"host-or-unknown",(unsigned long long)(m?(caller-m->bias):0u));
    if(phase&&strcmp(phase,"BEGIN")==0)set_checkpoint_quiet("sync wait kind=%s guest=%p caller=%p module=%s offset=0x%llx",kind?kind:"?",guest,(void*)caller,m?m->name:"host-or-unknown",(unsigned long long)(m?(caller-m->bias):0u));
}

static int bender_pthread_mutex_init(void *guest,const void *guest_attr){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_MUTEX,1);if(!s)return ENOMEM;int type=PTHREAD_MUTEX_NORMAL;if(guest_attr){BenderSyncSlot*a=bender_sync_slot(guest_attr,BENDER_SYNC_MUTEXATTR,0);if(a&&a->inited)type=a->meta;}pthread_mutex_lock(&g_bender_sync_lock);if(s->inited)pthread_mutex_destroy(&s->host.mutex);pthread_mutexattr_t ha;pthread_mutexattr_init(&ha);pthread_mutexattr_settype(&ha,type);int rc=pthread_mutex_init(&s->host.mutex,&ha);pthread_mutexattr_destroy(&ha);s->meta=type;s->inited=(rc==0);pthread_mutex_unlock(&g_bender_sync_lock);return rc;}
static int bender_pthread_mutex_destroy(void *guest){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_MUTEX,0);if(!s||!s->inited)return 0;int rc=pthread_mutex_destroy(&s->host.mutex);if(rc==0)s->inited=0;return rc;}
/* ULTRAKILL fix -L: acorda o cond wait de dialog por tempo. O libunity devolve
 * as vezes um Method de Runnable para onClick/onDismiss (NoSuchMethodError) e o
 * wait nao e sinalizado; sem isto o port fica parado ate o watchdog e parece
 * tela morta. Libera no maximo 8 waits, um a cada BENDER_DIALOG_WAIT_RELEASE_SEC. */
typedef struct { pthread_cond_t *cond; double sec; } BenderDlgWake;
static void *bender_dialog_wake_main(void *arg){
    BenderDlgWake *w=(BenderDlgWake*)arg;
    if(!w)return NULL;
    struct timespec ts; ts.tv_sec=(time_t)w->sec; ts.tv_nsec=(long)((w->sec-(double)ts.tv_sec)*1e9);
    while(ts.tv_sec>0||ts.tv_nsec>0){ if(nanosleep(&ts,&ts)!=0)break; }
    logmsg("[BENDER_JNI][DIALOG][WAIT_RELEASE] sinalizando cond=%p apos %.1fs (dialog sem UI)\n",(void*)w->cond,w->sec);
    pthread_cond_broadcast(w->cond);
    free(w);
    return NULL;
}
static int bender_pthread_mutex_lock(void *guest){BenderSyncSlot*s=bender_mutex_slot(guest,1);if(!s||!s->inited)return EINVAL;int rc=pthread_mutex_trylock(&s->host.mutex);if(rc==0)return 0;if(rc!=EBUSY)return rc;uintptr_t caller=(uintptr_t)__builtin_return_address(0);bender_sync_wait_diag("BEGIN","mutex",guest,caller);rc=pthread_mutex_lock(&s->host.mutex);bender_sync_wait_diag("END","mutex",guest,caller);return rc;}
static int bender_pthread_mutex_trylock(void *guest){BenderSyncSlot*s=bender_mutex_slot(guest,1);return s&&s->inited?pthread_mutex_trylock(&s->host.mutex):EINVAL;}
static int bender_pthread_mutex_unlock(void *guest){BenderSyncSlot*s=bender_mutex_slot(guest,1);return s&&s->inited?pthread_mutex_unlock(&s->host.mutex):EINVAL;}
static int bender_pthread_mutexattr_init(void *guest){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_MUTEXATTR,1);if(!s)return ENOMEM;s->meta=PTHREAD_MUTEX_NORMAL;s->inited=1;return 0;}
static int bender_pthread_mutexattr_settype(void *guest,int type){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_MUTEXATTR,1);if(!s)return ENOMEM;s->meta=type;s->inited=1;return 0;}
static int bender_pthread_mutexattr_destroy(void *guest){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_MUTEXATTR,0);if(s)s->inited=0;return 0;}
static int bender_pthread_cond_init(void *guest,const void *guest_attr){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_COND,1);if(!s)return ENOMEM;clockid_t clk=CLOCK_REALTIME;if(guest_attr){BenderSyncSlot*a=bender_sync_slot(guest_attr,BENDER_SYNC_CONDATTR,0);if(a&&a->inited&&a->meta)clk=(clockid_t)a->meta;}pthread_mutex_lock(&g_bender_sync_lock);if(s->inited)pthread_cond_destroy(&s->host.cond);pthread_condattr_t ha;pthread_condattr_init(&ha);pthread_condattr_setclock(&ha,clk);int rc=pthread_cond_init(&s->host.cond,&ha);pthread_condattr_destroy(&ha);s->meta=(int)clk;s->inited=(rc==0);pthread_mutex_unlock(&g_bender_sync_lock);return rc;}
static int bender_pthread_cond_destroy(void *guest){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_COND,0);if(!s||!s->inited)return 0;int rc=pthread_cond_destroy(&s->host.cond);if(rc==0)s->inited=0;return rc;}
static int bender_pthread_cond_wait(void *gcond,void *gmutex){BenderSyncSlot*c=bender_cond_slot(gcond,1);BenderSyncSlot*m=bender_mutex_slot(gmutex,1);if(!c||!m)return EINVAL;uintptr_t caller=(uintptr_t)__builtin_return_address(0);bender_sync_wait_diag("BEGIN","cond",gcond,caller);
    /* ULTRAKILL fix -L: Ultima linha de defesa. O Unity (AndroidHelper/Dialog)
     * mostra um AlertDialog e dorme num cond wait ate o usuario responder - e
     * aqui nao ha UI Android nenhuma. Se ainda existe um dialog pendente,
     * dispara os listeners ANTES de dormir (o sinal cai exatamente quando o
     * thread ja esta pronto para esperar). BENDER_DIALOG_AUTODISMIS=0 desliga.
     *
     * E porque o callback as vezes nao resolve (o libunity devolve um Method
     * de Runnable para onClick/onDismiss -> NoSuchMethodError e o wait continua),
     * aqui tambem arma uma libertacao por tempo: um thread curta o cond apos
     * BENDER_DIALOG_WAIT_RELEASE_SEC (padrao 3 s), no maximo 8 vezes. Sem isso o
     * port fica 90 s parado e vocer ve 'nao abriu' - e nao e tela preta, e wait. */
    bender_dialog_flush("pre-cond-wait");
    if(g_dlg_release_armed&&g_dlg_release_used<8&&c->inited){
        const char*rs=getenv("BENDER_DIALOG_WAIT_RELEASE_SEC");
        double sec=rs&&*rs?atof(rs):3.0;
        if(sec>0.0){
            g_dlg_release_armed=0; ++g_dlg_release_used;
            typedef struct { pthread_cond_t *cond; double sec; } BenderDlgWakeLocal;
            BenderDlgWakeLocal *w=(BenderDlgWakeLocal*)malloc(sizeof(BenderDlgWakeLocal));
            if(w){
                w->cond=&c->host.cond; w->sec=sec;
                pthread_t tw;
                if(pthread_create(&tw,NULL,bender_dialog_wake_main,w)==0){
                    pthread_detach(tw);
                    logmsg("[BENDER_JNI][DIALOG][WAIT_ARM] cond=%p liberado em %.1fs (tentativa %d/8)\n",(void*)gcond,sec,g_dlg_release_used);
                } else free(w);
            }
        }
    }
    int rc=pthread_cond_wait(&c->host.cond,&m->host.mutex);bender_sync_wait_diag("END","cond",gcond,caller);return rc;}
static int bender_pthread_cond_timedwait(void *gcond,void *gmutex,const struct timespec *ts){BenderSyncSlot*c=bender_cond_slot(gcond,1);BenderSyncSlot*m=bender_mutex_slot(gmutex,1);return c&&m?pthread_cond_timedwait(&c->host.cond,&m->host.mutex,ts):EINVAL;}
static int bender_pthread_cond_signal(void *guest){BenderSyncSlot*s=bender_cond_slot(guest,1);return s?pthread_cond_signal(&s->host.cond):EINVAL;}
static int bender_pthread_cond_broadcast(void *guest){BenderSyncSlot*s=bender_cond_slot(guest,1);return s?pthread_cond_broadcast(&s->host.cond):EINVAL;}
static int bender_pthread_condattr_init(void *guest){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_CONDATTR,1);if(!s)return ENOMEM;s->meta=(int)CLOCK_REALTIME;s->inited=1;return 0;}
static int bender_pthread_condattr_setclock(void *guest,clockid_t clk){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_CONDATTR,1);if(!s)return ENOMEM;s->meta=(int)clk;s->inited=1;return 0;}
static int bender_pthread_condattr_destroy(void *guest){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_CONDATTR,0);if(s)s->inited=0;return 0;}
static int bender_pthread_rwlock_init(void *guest,const void *attr){(void)attr;BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_RWLOCK,1);if(!s)return ENOMEM;pthread_mutex_lock(&g_bender_sync_lock);if(s->inited)pthread_rwlock_destroy(&s->host.rwlock);int rc=pthread_rwlock_init(&s->host.rwlock,NULL);s->inited=(rc==0);pthread_mutex_unlock(&g_bender_sync_lock);return rc;}
static int bender_pthread_rwlock_destroy(void *guest){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_RWLOCK,0);if(!s||!s->inited)return 0;int rc=pthread_rwlock_destroy(&s->host.rwlock);if(rc==0)s->inited=0;return rc;}
static int bender_pthread_rwlock_rdlock(void *guest){BenderSyncSlot*s=bender_rwlock_slot(guest,1);return s?pthread_rwlock_rdlock(&s->host.rwlock):EINVAL;}
static int bender_pthread_rwlock_wrlock(void *guest){BenderSyncSlot*s=bender_rwlock_slot(guest,1);return s?pthread_rwlock_wrlock(&s->host.rwlock):EINVAL;}
static int bender_pthread_rwlock_unlock(void *guest){BenderSyncSlot*s=bender_rwlock_slot(guest,1);return s?pthread_rwlock_unlock(&s->host.rwlock):EINVAL;}
static int bender_sem_init(void *guest,int pshared,unsigned value){(void)pshared;BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_SEM,1);if(!s)return -1;pthread_mutex_lock(&g_bender_sync_lock);if(s->inited)sem_destroy(&s->host.sem);int rc=sem_init(&s->host.sem,0,value);s->inited=(rc==0);pthread_mutex_unlock(&g_bender_sync_lock);return rc;}
static int bender_sem_destroy(void *guest){BenderSyncSlot*s=bender_sync_slot(guest,BENDER_SYNC_SEM,0);if(!s||!s->inited)return 0;int rc=sem_destroy(&s->host.sem);if(rc==0)s->inited=0;return rc;}
static int bender_sem_wait(void *guest){BenderSyncSlot*s=bender_sem_slot(guest,1,0);if(!s)return -1;uintptr_t caller=(uintptr_t)__builtin_return_address(0);bender_sync_wait_diag("BEGIN","sem",guest,caller);int rc=sem_wait(&s->host.sem);bender_sync_wait_diag("END","sem",guest,caller);return rc;}
static int bender_sem_timedwait(void *guest,const struct timespec *ts){BenderSyncSlot*s=bender_sem_slot(guest,1,0);return s?sem_timedwait(&s->host.sem,ts):-1;}
static int bender_sem_post(void *guest){BenderSyncSlot*s=bender_sem_slot(guest,1,0);return s?sem_post(&s->host.sem):-1;}
static int bender_sem_getvalue(void *guest,int *value){BenderSyncSlot*s=bender_sem_slot(guest,1,0);return s?sem_getvalue(&s->host.sem,value):-1;}
static int bender_pthread_once(void *guest,void(*init_routine)(void)){BenderSyncSlot*s=bender_once_slot(guest,1);return s?pthread_once(&s->host.once,init_routine):EINVAL;}
static BenderSyncSlot *bender_attr_slot(const void *guest,int create){return bender_sync_slot(guest,BENDER_SYNC_ATTR,create);}
static int bender_pthread_attr_init(void *guest){BenderSyncSlot*s=bender_attr_slot(guest,1);if(!s)return ENOMEM;if(s->inited)pthread_attr_destroy(&s->host.attr);int rc=pthread_attr_init(&s->host.attr);s->inited=(rc==0);return rc;}
static int bender_pthread_attr_destroy(void *guest){BenderSyncSlot*s=bender_attr_slot(guest,0);if(!s||!s->inited)return 0;int rc=pthread_attr_destroy(&s->host.attr);if(rc==0)s->inited=0;return rc;}
static int bender_pthread_attr_setdetachstate(void *guest,int state){BenderSyncSlot*s=bender_attr_slot(guest,1);if(!s)return ENOMEM;if(!s->inited){int rc=pthread_attr_init(&s->host.attr);if(rc)return rc;s->inited=1;}return pthread_attr_setdetachstate(&s->host.attr,state);}
static unsigned long g_bender_fix67_stack_calls_2867;
static int bender_pthread_attr_setstacksize(void *guest,size_t size){
    BenderSyncSlot*s=bender_attr_slot(guest,1);if(!s)return ENOMEM;
    if(!s->inited){int irc=pthread_attr_init(&s->host.attr);if(irc)return irc;s->inited=1;}
    size_t requested=size;
    long host_min_l=sysconf(_SC_THREAD_STACK_MIN);
    size_t host_min=(host_min_l>0)?(size_t)host_min_l:0u;
    long page_l=sysconf(_SC_PAGESIZE);
    size_t page=(page_l>0)?(size_t)page_l:4096u;
    size_t effective=requested;
    if(host_min&&effective<host_min)effective=host_min;
    if(page>1u&&(effective%page)){
        size_t rem=effective%page;
        if(effective<=SIZE_MAX-(page-rem))effective+=page-rem;
    }
    int rc=pthread_attr_setstacksize(&s->host.attr,effective);
    int retries=0;
    /* Some older/architecture-specific libpthread builds impose a stricter minimum
       than sysconf reports. EINVAL here is size/alignment-related because the host
       attr object above is initialized by BENDER itself. Grow only upward. */
    while(rc==EINVAL&&retries<5&&effective<(4u*1024u*1024u)){
        size_t next=effective?effective*2u:(128u*1024u);
        if(next<effective)break;
        if(page>1u&&(next%page)){
            size_t rem=next%page;
            if(next>SIZE_MAX-(page-rem))break;
            next+=page-rem;
        }
        effective=next;
        rc=pthread_attr_setstacksize(&s->host.attr,effective);
        retries++;
    }
    unsigned long n=__atomic_add_fetch(&g_bender_fix67_stack_calls_2867,1ul,__ATOMIC_RELAXED);
    if(n<=32ul||rc!=0||effective!=requested){
        logmsg("[BENDER_PTHREAD_STACK][SET] source=fix67 n=%lu requested=%zu effective=%zu host_min=%zu page=%zu retries=%d rc=%d policy=guest-minimum-host-compatible code_patch=0 forced_completion=0\n",
               n,requested,effective,host_min,page,retries,rc);
    }
    return rc;
}
static int bender_pthread_attr_getstack(const void *guest,void **addr,size_t *size){BenderSyncSlot*s=bender_attr_slot(guest,0);if(!s||!s->inited)return EINVAL;return pthread_attr_getstack(&s->host.attr,addr,size);}
static int bender_pthread_getattr_np(pthread_t thread,void *guest){BenderSyncSlot*s=bender_attr_slot(guest,1);if(!s)return ENOMEM;if(s->inited)pthread_attr_destroy(&s->host.attr);int rc=pthread_getattr_np(thread,&s->host.attr);s->inited=(rc==0);return rc;}
static const pthread_attr_t *bender_host_attr_from_guest(const void *guest){BenderSyncSlot*s=bender_attr_slot(guest,0);return(s&&s->inited)?&s->host.attr:NULL;}
/* ---------------- end BENDER Bionic pthread/sem bridge ---------------- */

typedef struct BenderThreadStartCtx { void *(*start)(void *); void *arg; unsigned long id; } BenderThreadStartCtx;
static void *bender_pthread_start_trampoline(void *opaque){
    BenderThreadStartCtx local=*(BenderThreadStartCtx*)opaque;free(opaque);install_crash_handlers();
    set_checkpoint_quiet("pthread guest thread id=%lu start=%p",local.id,(void*)local.start);
    return local.start(local.arg);
}
static int bender_pthread_create(pthread_t *thread,const pthread_attr_t *guest_attr,void *(*start)(void *),void *arg){
    BenderThreadStartCtx *ctx=(BenderThreadStartCtx*)malloc(sizeof(*ctx));if(!ctx)return ENOMEM;
    ctx->start=start;ctx->arg=arg;ctx->id=__sync_add_and_fetch(&g_bender_thread_create_count,1ul);
    const pthread_attr_t *host_attr=bender_host_attr_from_guest(guest_attr);
    if(ctx->id<=16ul || (ctx->id%32ul)==0ul) logmsg("[BENDER_THREAD][CREATE] id=%lu start=%p guest_attr=%p host_attr=%p\n",ctx->id,(void*)start,(const void*)guest_attr,(const void*)host_attr);
    int rc=pthread_create(thread,host_attr,bender_pthread_start_trampoline,ctx);if(rc!=0)free(ctx);return rc;
}

static uintptr_t align_down(uintptr_t v, uintptr_t a) { return v & ~(a - 1u); }
static uintptr_t align_up(uintptr_t v, uintptr_t a) { return (v + a - 1u) & ~(a - 1u); }

static int read_file(const char *path, uint8_t **data, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    uint8_t *p = (uint8_t *)malloc((size_t)n);
    if (!p) { fclose(f); return -1; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return -1; }
    fclose(f);
    *data = p; *size = (size_t)n; return 0;
}

static bool valid_range(size_t off, size_t need, size_t total) {
    return off <= total && need <= total - off;
}

static void *raw_vaddr(Module *m, uintptr_t vaddr, size_t need) {
    for (uint16_t i = 0; i < m->eh->e_phnum; ++i) {
        Elf64_Phdr *p = &m->ph[i];
        if (p->p_type != PT_LOAD) continue;
        if (vaddr < p->p_vaddr || vaddr + need > p->p_vaddr + p->p_filesz) continue;
        uint64_t off = p->p_offset + (vaddr - p->p_vaddr);
        if (!valid_range((size_t)off, need, m->raw_size)) return NULL;
        return m->raw + off;
    }
    return NULL;
}

static const char *raw_cstr(Module *m, uintptr_t strtab_v, uint64_t index) {
    uint8_t *p = raw_vaddr(m, strtab_v + index, 1);
    if (!p) return "";
    return (const char *)p;
}

static size_t gnu_hash_symcount_raw(Module *m, uintptr_t gh_vaddr) {
    uint32_t *h = (uint32_t *)raw_vaddr(m, gh_vaddr, 16);
    if (!h) return 0;
    uint32_t nbuckets = h[0], symoffset = h[1], bloom_size = h[2];
    size_t bloom_bytes = (size_t)bloom_size * sizeof(Elf64_Xword);
    uint32_t *buckets = (uint32_t *)raw_vaddr(m, gh_vaddr + 16 + bloom_bytes, (size_t)nbuckets * 4);
    if (!buckets) return 0;
    uintptr_t chain_v = gh_vaddr + 16 + bloom_bytes + (uintptr_t)nbuckets * 4;
    uint32_t maxidx = 0;
    for (uint32_t i = 0; i < nbuckets; ++i) {
        uint32_t idx = buckets[i];
        if (!idx || idx < symoffset) continue;
        uint32_t ci = idx - symoffset;
        for (uint32_t guard = 0; guard < 1000000; ++guard, ++idx, ++ci) {
            uint32_t *chain = (uint32_t *)raw_vaddr(m, chain_v + (uintptr_t)ci * 4, 4);
            if (!chain) break;
            if (idx > maxidx) maxidx = idx;
            if ((*chain & 1u) != 0u) break;
        }
    }
    return maxidx ? (size_t)maxidx + 1u : 0u;
}

static int parse_module(Module *m) {
    if (m->raw_size < sizeof(Elf64_Ehdr)) return -1;
    m->eh = (Elf64_Ehdr *)m->raw;
    if (memcmp(m->eh->e_ident, ELFMAG, SELFMAG) != 0 || m->eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        m->eh->e_ident[EI_DATA] != ELFDATA2LSB || m->eh->e_machine != EM_AARCH64) return -2;
    if (!valid_range((size_t)m->eh->e_phoff, (size_t)m->eh->e_phnum * m->eh->e_phentsize, m->raw_size)) return -3;
    m->ph = (Elf64_Phdr *)(m->raw + m->eh->e_phoff);
    uintptr_t strtab_v = 0, symtab_v = 0, hash_v = 0, ghash_v = 0;
    uintptr_t rela_v = 0, jmprel_v = 0;
    size_t rela_sz = 0, rela_ent = sizeof(Elf64_Rela), plt_sz = 0, syment = sizeof(Elf64_Sym);
    uintptr_t dynamic_v = 0; size_t dynamic_sz = 0;
    for (uint16_t i = 0; i < m->eh->e_phnum; ++i) {
        Elf64_Phdr *p = &m->ph[i];
        if (p->p_type == PT_DYNAMIC) { dynamic_v = p->p_vaddr; dynamic_sz = (size_t)p->p_filesz; }
    }
    Elf64_Dyn *dyn = (Elf64_Dyn *)raw_vaddr(m, dynamic_v, dynamic_sz ? dynamic_sz : sizeof(Elf64_Dyn));
    if (!dyn) return -4;
    size_t maxdyn = dynamic_sz / sizeof(Elf64_Dyn);
    for (size_t i = 0; i < maxdyn && dyn[i].d_tag != DT_NULL; ++i) {
        switch (dyn[i].d_tag) {
            case DT_STRTAB: strtab_v = dyn[i].d_un.d_ptr; break;
            case DT_SYMTAB: symtab_v = dyn[i].d_un.d_ptr; break;
            case DT_SYMENT: syment = (size_t)dyn[i].d_un.d_val; break;
            case DT_HASH: hash_v = dyn[i].d_un.d_ptr; break;
            case DT_GNU_HASH: ghash_v = dyn[i].d_un.d_ptr; break;
            case DT_RELA: rela_v = dyn[i].d_un.d_ptr; break;
            case DT_RELASZ: rela_sz = (size_t)dyn[i].d_un.d_val; break;
            case DT_RELAENT: rela_ent = (size_t)dyn[i].d_un.d_val; break;
            case DT_JMPREL: jmprel_v = dyn[i].d_un.d_ptr; break;
            case DT_PLTRELSZ: plt_sz = (size_t)dyn[i].d_un.d_val; break;
            case DT_INIT_ARRAY: m->init_array = dyn[i].d_un.d_ptr; break;
            case DT_INIT_ARRAYSZ: m->init_array_count = (size_t)dyn[i].d_un.d_val / sizeof(uintptr_t); break;
            default: break;
        }
    }
    if (!strtab_v || !symtab_v || !syment) return -5;
    for (size_t i = 0; i < maxdyn && dyn[i].d_tag != DT_NULL; ++i) {
        if (dyn[i].d_tag == DT_NEEDED && m->needed_count < MAX_NEEDED) {
            const char *n = raw_cstr(m, strtab_v, dyn[i].d_un.d_val);
            snprintf(m->needed[m->needed_count++], sizeof(m->needed[0]), "%s", n);
        }
    }
    if (hash_v) {
        uint32_t *h = (uint32_t *)raw_vaddr(m, hash_v, 8);
        if (h) m->symcount = h[1];
    }
    if (!m->symcount && ghash_v) m->symcount = gnu_hash_symcount_raw(m, ghash_v);
    if (rela_v && rela_sz && m->range_count < MAX_RANGES) m->ranges[m->range_count++] = (RelaRange){rela_v, rela_sz, rela_ent};
    if (jmprel_v && plt_sz && m->range_count < MAX_RANGES) m->ranges[m->range_count++] = (RelaRange){jmprel_v, plt_sz, sizeof(Elf64_Rela)};

    /* Se hash não permitiu contar dynsym, os índices de relocation dão um limite inferior seguro. */
    if (!m->symcount) {
        size_t maxsym = 0;
        for (int r = 0; r < m->range_count; ++r) {
            RelaRange *rr = &m->ranges[r];
            size_t count = rr->entsz ? rr->size / rr->entsz : 0;
            for (size_t i = 0; i < count; ++i) {
                Elf64_Rela *re = (Elf64_Rela *)raw_vaddr(m, rr->vaddr + i * rr->entsz, sizeof(Elf64_Rela));
                if (!re) break;
                size_t si = ELF64_R_SYM(re->r_info); if (si > maxsym) maxsym = si;
            }
        }
        m->symcount = maxsym + 1;
    }
    logmsg("[UNITY_PROBE] parse module=%s machine=AArch64 phnum=%u dynsym=%zu init_array=%zu\n", m->name, m->eh->e_phnum, m->symcount, m->init_array_count);
    for (int i = 0; i < m->needed_count; ++i) logmsg("[UNITY_PROBE][NEEDED] module=%s lib=%s\n", m->name, m->needed[i]);
    return 0;
}

static int map_module(Module *m) {
    uintptr_t minv = UINTPTR_MAX, maxv = 0;
    for (uint16_t i = 0; i < m->eh->e_phnum; ++i) {
        Elf64_Phdr *p = &m->ph[i];
        if (p->p_type != PT_LOAD) continue;
        uintptr_t a = align_down((uintptr_t)p->p_vaddr, (uintptr_t)g_pagesize);
        uintptr_t b = align_up((uintptr_t)(p->p_vaddr + p->p_memsz), (uintptr_t)g_pagesize);
        if (a < minv) minv = a;
        if (b > maxv) maxv = b;
    }
    if (minv == UINTPTR_MAX || maxv <= minv) return -1;
    size_t span = maxv - minv;
    void *mem = mmap(NULL, span, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return -2;
    m->reserve = mem; m->reserve_size = span; m->min_vaddr = minv; m->bias = (uintptr_t)mem - minv;
    for (uint16_t i = 0; i < m->eh->e_phnum; ++i) {
        Elf64_Phdr *p = &m->ph[i];
        if (p->p_type != PT_LOAD) continue;
        if (!valid_range((size_t)p->p_offset, (size_t)p->p_filesz, m->raw_size)) return -3;
        void *dst = (void *)(m->bias + p->p_vaddr);
        memcpy(dst, m->raw + p->p_offset, (size_t)p->p_filesz);
        if (p->p_memsz > p->p_filesz) memset((uint8_t *)dst + p->p_filesz, 0, (size_t)(p->p_memsz - p->p_filesz));
        logmsg("[UNITY_PROBE][MAP] module=%s vaddr=0x%" PRIx64 " mem=%" PRIu64 " filesz=%" PRIu64 " flags=%c%c%c\n",
               m->name, p->p_vaddr, p->p_memsz, p->p_filesz,
               (p->p_flags & PF_R) ? 'R' : '-', (p->p_flags & PF_W) ? 'W' : '-', (p->p_flags & PF_X) ? 'X' : '-');
    }
    /* Reaponta tabelas dinâmicas para a imagem carregada. */
    for (uint16_t i = 0; i < m->eh->e_phnum; ++i) if (m->ph[i].p_type == PT_DYNAMIC) m->dynamic = (Elf64_Dyn *)(m->bias + m->ph[i].p_vaddr);
    uintptr_t strtab_v = 0, symtab_v = 0;
    if (!m->dynamic) return -4;
    for (Elf64_Dyn *d = m->dynamic; d->d_tag != DT_NULL; ++d) {
        if (d->d_tag == DT_STRTAB) strtab_v = d->d_un.d_ptr;
        else if (d->d_tag == DT_SYMTAB) symtab_v = d->d_un.d_ptr;
        else if (d->d_tag == DT_SYMENT) m->syment = (size_t)d->d_un.d_val;
    }
    if (!m->syment) m->syment = sizeof(Elf64_Sym);
    m->strtab = (const char *)(m->bias + strtab_v); m->symtab = (Elf64_Sym *)(m->bias + symtab_v);
    logmsg("[UNITY_PROBE] mapped module=%s base=%p bias=0x%" PRIxPTR " span=%zu\n", m->name, m->reserve, m->bias, span);
    return 0;
}

/* ---------------- BENDER Android/Bionic probe bridge ---------------- */
typedef struct { int refs; int width; int height; int format; } BenderNativeWindow;
typedef struct { int refs; } BenderLooper;
typedef struct { int unused; } BenderChoreographer;
typedef struct { char name[96]; char value[128]; } BenderProperty;
static BenderNativeWindow g_native_window = {1, 640, 480, 1};
/* FIX78: preserve AImageReader window identity across ANativeWindow_toSurface/fromSurface. */
static void *g_bender_fix78_last_surface;
static BenderNativeWindow *g_bender_fix78_last_surface_window;
static size_t g_native_window_from_surface_calls;
static size_t g_native_window_acquire_calls;
static size_t g_native_window_release_calls;
static size_t g_native_window_geometry_calls;
static __thread BenderLooper g_tls_looper = {1};
static BenderChoreographer g_choreographer;
static unsigned char g_bionic_sF[3 * 256];

/*
 * BENDER V2.7.12 - Bionic stdio ABI bridge.
 *
 * Android/Bionic exposes the three standard FILE objects through __sF.
 * On the Traffic Rider arm64 build the stderr object is addressed as
 * __sF + 0x130, which matches 2 * sizeof(Bionic FILE) with a 0x98-byte
 * FILE layout. A glibc fwrite/vfprintf cannot consume that object.
 *
 * Guest fopen() results remain host FILE* and are passed through as opaque
 * handles. Only addresses inside our fake __sF storage are translated.
 */
#define BENDER_BIONIC_FILE_SIZE 0x98u
static unsigned long g_bender_stdio_bridge_calls;

static FILE *bender_stdio_host_stream(void *stream,const char **label,int *pseudo){
    if(label)*label="host";
    if(pseudo)*pseudo=0;
    if(!stream)return NULL;
    uintptr_t p=(uintptr_t)stream;
    uintptr_t base=(uintptr_t)g_bionic_sF;
    uintptr_t end=base+sizeof(g_bionic_sF);
    if(p>=base&&p<end){
        size_t off=(size_t)(p-base);
        unsigned idx=(unsigned)(off/BENDER_BIONIC_FILE_SIZE);
        if(idx>2u)idx=2u;
        if(pseudo)*pseudo=1;
        if(idx==0u){if(label)*label="stdin";return stdin;}
        if(idx==1u){if(label)*label="stdout";return stdout;}
        if(label)*label="stderr";
        return stderr;
    }
    return (FILE*)stream;
}
static void bender_stdio_trace(const char *op,void *guest,FILE *host,const char *label){
    unsigned long n=__atomic_add_fetch(&g_bender_stdio_bridge_calls,1ul,__ATOMIC_RELAXED);
    if(n<=24ul||(n%128ul)==0ul)
        logmsg("[BENDER_STDIO][%s] call=%lu guest=%p host=%p mapped=%s\\n",
               op?op:"?",n,guest,(void*)host,label?label:"host");
}
static size_t bender_fwrite(const void *ptr,size_t size,size_t nmemb,void *stream){
    const char *label=NULL;int pseudo=0;FILE *h=bender_stdio_host_stream(stream,&label,&pseudo);
    bender_stdio_trace("fwrite",stream,h,label);
    if(!h||(!ptr&&size&&nmemb)){errno=EINVAL;return 0;}
    size_t rc=fwrite(ptr,size,nmemb,h);
    if(pseudo)fflush(h);
    return rc;
}
static size_t bender_fread(void *ptr,size_t size,size_t nmemb,void *stream){
    const char *label=NULL;FILE *h=bender_stdio_host_stream(stream,&label,NULL);
    bender_stdio_trace("fread",stream,h,label);
    if(!h||(!ptr&&size&&nmemb)){errno=EINVAL;return 0;}
    return fread(ptr,size,nmemb,h);
}
static int bender_vfprintf(void *stream,const char *fmt,va_list ap){
    const char *label=NULL;int pseudo=0;FILE *h=bender_stdio_host_stream(stream,&label,&pseudo);
    bender_stdio_trace("vfprintf",stream,h,label);
    if(!h){errno=EINVAL;return -1;}
    int rc=vfprintf(h,fmt?fmt:"",ap);
    if(pseudo)fflush(h);
    return rc;
}
static int bender_fprintf(void *stream,const char *fmt,...){
    va_list ap;va_start(ap,fmt);int rc=bender_vfprintf(stream,fmt,ap);va_end(ap);return rc;
}
static int bender_fputc(int ch,void *stream){
    const char *label=NULL;int pseudo=0;FILE *h=bender_stdio_host_stream(stream,&label,&pseudo);
    bender_stdio_trace("fputc",stream,h,label);
    if(!h){errno=EINVAL;return EOF;}
    int rc=fputc(ch,h);if(pseudo)fflush(h);return rc;
}
static int bender_fflush(void *stream){
    if(!stream)return fflush(NULL);
    const char *label=NULL;FILE *h=bender_stdio_host_stream(stream,&label,NULL);
    bender_stdio_trace("fflush",stream,h,label);
    return h?fflush(h):EOF;
}
static int bender_fseek(void *stream,long offset,int whence){
    const char *label=NULL;FILE *h=bender_stdio_host_stream(stream,&label,NULL);
    bender_stdio_trace("fseek",stream,h,label);
    if(!h){errno=EINVAL;return -1;}
    return fseek(h,offset,whence);
}
static int bender_asset_path_resolve_2771(const char*path,char*out,size_t cap,const char*op);
static FILE *bender_fopen(const char *path,const char *mode){
    FILE *h=fopen(path,mode);
    if(!h&&path&&mode&&mode[0]=='r'&&!strchr(mode,'+')){
        int saved=errno;char resolved[2048];
        if(bender_asset_path_resolve_2771(path,resolved,sizeof(resolved),"fopen"))h=fopen(resolved,mode);
        if(!h)errno=saved;
    }
    logmsg("[BENDER_STDIO][fopen] path=%s mode=%s -> %p\n",path?path:"(null)",mode?mode:"(null)",(void*)h);
    return h;
}
static int bender_fclose(void *stream){
    const char *label=NULL;int pseudo=0;FILE *h=bender_stdio_host_stream(stream,&label,&pseudo);
    bender_stdio_trace("fclose",stream,h,label);
    if(!h){errno=EINVAL;return EOF;}
    if(pseudo){fflush(h);return 0;}
    return fclose(h);
}


static int bender_android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    (void)prio;
    fprintf(stderr, "[ANDROID_LOG][%s] ", tag ? tag : "Unity");
    int rc = vfprintf(stderr, fmt ? fmt : "", ap);
    fputc('\n', stderr); fflush(stderr); return rc;
}
static int bender_android_log_print(int prio, const char *tag, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int rc=bender_android_log_vprint(prio,tag,fmt,ap); va_end(ap); return rc;
}
static int bender_android_log_write(int prio, const char *tag, const char *text) {
    (void)prio; fprintf(stderr,"[ANDROID_LOG][%s] %s\n",tag?tag:"Unity",text?text:""); fflush(stderr); return text?(int)strlen(text):0;
}
static int bender_android_log_buf_write(int buf_id, int prio, const char *tag, const char *text) {
    int rc=bender_android_log_write(prio,tag,text);
    if(g_log) logmsg("[BENDER_LIBLOG] source=fix101 action=buf-write buffer=%d prio=%d tag=%s bytes=%d\n",buf_id,prio,tag?tag:"Unity",rc);
    return rc;
}
static int *bender___errno(void) { return &errno; }
static void bender_android_set_abort_message(const char *msg) { logmsg("[BENDER_BRIDGE][ABORT_MESSAGE] %s\n", msg?msg:""); }
static void bender___assert2(const char *file, int line, const char *func, const char *expr) {
    logmsg("[BENDER_BRIDGE][ASSERT] %s:%d %s: %s\n",file?file:"?",line,func?func:"?",expr?expr:"?");
}
static size_t bender_strlcpy(char *dst, const char *src, size_t size) {
    size_t n=src?strlen(src):0; if(size){size_t c=n<size-1?n:size-1; if(c&&src)memcpy(dst,src,c); dst[c]='\0';} return n;
}
static size_t bender___strlen_chk(const char *s,size_t s_len){
    if(!s)return 0;
    return strnlen(s,s_len);
}
static char *bender___strncpy_chk2(char *dst,const char *src,size_t n,size_t dst_len,size_t src_len){
    if(!dst||!src)return dst;
    size_t out_n=n<dst_len?n:dst_len;
    size_t sl=strnlen(src,src_len);
    size_t copy=sl<out_n?sl:out_n;
    if(copy)memcpy(dst,src,copy);
    if(out_n>copy)memset(dst+copy,0,out_n-copy);
    return dst;
}
static char *bender___strchr_chk(const char *s,int c,size_t s_len){
    if(!s)return NULL;
    const void *p=memchr(s,c,s_len);
    return (char*)p;
}
static mode_t bender___umask_chk(mode_t mask){
    return umask(mask);
}
static char *bender___gnu_strerror_r(int errnum, char *buf, size_t buflen) { return strerror_r(errnum, buf, buflen); }
static void bender___FD_SET_chk(int fd, fd_set *set, size_t set_size) { if(fd>=0 && set && set_size>=sizeof(fd_set) && fd<FD_SETSIZE) FD_SET(fd,set); }
static int bender___FD_ISSET_chk(int fd, const fd_set *set, size_t set_size) { return (fd>=0&&set&&set_size>=sizeof(fd_set)&&fd<FD_SETSIZE)?FD_ISSET(fd,set):0; }

static const struct { const char *name; const char *value; } g_props[] = {
    {"ro.build.version.sdk","35"},{"ro.build.version.release","15"},{"ro.product.cpu.abi","arm64-v8a"},
    {"ro.product.manufacturer","BENDER"},{"ro.product.model","R36S"},{"ro.hardware","rk3326"},
    {"ro.debuggable","0"},{"ro.secure","1"},{NULL,NULL}
};
static const char *prop_value(const char *name) { for(int i=0;g_props[i].name;i++) if(name&&strcmp(name,g_props[i].name)==0) return g_props[i].value; return ""; }
static int bender___system_property_get(const char *name, char *value) { const char *v=prop_value(name); if(value) bender_strlcpy(value,v,92); return (int)strlen(v); }
static const BenderProperty *bender___system_property_find(const char *name) { static __thread BenderProperty p; memset(&p,0,sizeof(p)); if(!name)return NULL; bender_strlcpy(p.name,name,sizeof(p.name)); bender_strlcpy(p.value,prop_value(name),sizeof(p.value)); return &p; }
static int bender___system_property_read(const BenderProperty *pi, char *name, char *value) { if(!pi)return 0; if(name)bender_strlcpy(name,pi->name,32); if(value)bender_strlcpy(value,pi->value,92); return (int)strlen(pi->value); }

static int bender_ANativeWindow_getWidth(BenderNativeWindow *w){int v=bender_render_scale_guest_width_2768();if(v<=0)v=w?w->width:640;logmsg("[BENDER_ANW] getWidth window=%p -> %d\n",(void*)w,v);return v;}
static int bender_ANativeWindow_getHeight(BenderNativeWindow *w){int v=bender_render_scale_guest_height_2768();if(v<=0)v=w?w->height:480;logmsg("[BENDER_ANW] getHeight window=%p -> %d\n",(void*)w,v);return v;}
static int bender_ANativeWindow_setBuffersGeometry(BenderNativeWindow *w,int width,int height,int format){if(!w)w=&g_native_window;++g_native_window_geometry_calls;if(width>0)w->width=width;if(height>0)w->height=height;w->format=format;logmsg("[BENDER_ANW] setBuffersGeometry call=%zu window=%p width=%d height=%d format=%d refs=%d\n",g_native_window_geometry_calls,(void*)w,w->width,w->height,w->format,w->refs);return 0;}
static void bender_ANativeWindow_acquire(BenderNativeWindow *w){++g_native_window_acquire_calls;if(w)++w->refs;logmsg("[BENDER_ANW] acquire call=%zu window=%p refs=%d\n",g_native_window_acquire_calls,(void*)w,w?w->refs:-1);}
static void bender_ANativeWindow_release(BenderNativeWindow *w){++g_native_window_release_calls;if(w&&w->refs>0)--w->refs;logmsg("[BENDER_ANW] release call=%zu window=%p refs=%d\n",g_native_window_release_calls,(void*)w,w?w->refs:-1);}
static BenderNativeWindow *bender_ANativeWindow_fromSurface(void *env, void *surface){
        /* ULTRAKILL fix -N: o default do nosso ANativeWindow fake e 640x480 (4:3),
         * mas o painel e 1280x720 (16:9). Alem do readback sair esticado, o budget
         * de 30 FPS foi calculado para 640x360. BENDER_UK_WINDOW_W/H ajustam isto
         * antes de o Unity criar a surface. */
        { static int geo_applied=0;
          if(!geo_applied){ geo_applied=1;
            const char*ew=getenv("BENDER_UK_WINDOW_W"),*eh=getenv("BENDER_UK_WINDOW_H");
            if(ew&&*ew&&eh&&*eh){ int a=atoi(ew),b=atoi(eh);
                if(a>16&&b>16){ g_native_window.width=a; g_native_window.height=b;
                    logmsg("[BENDER_ANW][GEOMETRY] window forcada para %dx%d (BENDER_UK_WINDOW_W/H)\n",a,b);
                }
            }
          }
        }
    (void)env;++g_native_window_from_surface_calls;
    BenderNativeWindow*w=(surface&&surface==g_bender_fix78_last_surface&&g_bender_fix78_last_surface_window)?g_bender_fix78_last_surface_window:&g_native_window;
    logmsg("[BENDER_ANW] fromSurface call=%zu surface=%p -> window=%p %dx%d format=%d refs=%d source=%s\n",g_native_window_from_surface_calls,surface,(void*)w,w?w->width:0,w?w->height:0,w?w->format:0,w?w->refs:-1,(w!=&g_native_window)?"fix78-aimage-reader":"default");
    return w;
}

static BenderChoreographer *bender_AChoreographer_getInstance(void){return &g_choreographer;}
typedef void (*BenderFrameCallback)(long, void *); typedef void (*BenderFrameCallback64)(int64_t, void *);
static void bender_AChoreographer_postFrameCallback(BenderChoreographer *c,BenderFrameCallback cb,void *data){(void)c;(void)cb;(void)data;}
static void bender_AChoreographer_postFrameCallback64(BenderChoreographer *c,BenderFrameCallback64 cb,void *data){(void)c;(void)cb;(void)data;}

static BenderLooper *bender_ALooper_prepare(int opts){(void)opts;g_tls_looper.refs=1;return &g_tls_looper;}
static BenderLooper *bender_ALooper_forThread(void){return &g_tls_looper;}
static void bender_ALooper_acquire(BenderLooper *l){if(l)++l->refs;}
static void bender_ALooper_release(BenderLooper *l){if(l&&l->refs>0)--l->refs;}
static void bender_ALooper_wake(BenderLooper *l){(void)l;}
static int bender_ALooper_addFd(BenderLooper *l,int fd,int ident,int events,int(*cb)(int,int,void*),void*data){(void)l;(void)fd;(void)ident;(void)events;(void)cb;(void)data;return 1;}
static int bender_ALooper_removeFd(BenderLooper *l,int fd){(void)l;(void)fd;return 1;}
static int bender_ALooper_pollOnce(int timeoutMillis,int*outFd,int*outEvents,void**outData){if(outFd)*outFd=-1;if(outEvents)*outEvents=0;if(outData)*outData=NULL;if(timeoutMillis>0){struct timespec ts={0,(long)(timeoutMillis>10?10:timeoutMillis)*1000000L};nanosleep(&ts,NULL);}return -3;}

/* ULTRAKILL: libunity.so 2019.4 importa ALooper_pollAll (o Unity 6 do port
 * modelo nao importa). Sem este simbolo o probe terminava em
 * "[JNI_FLOW][BLOCKED] imports/relocations pendentes" sem executar NADA do
 * guest. Mesma semantica do pollOnce acima: dorme pouco, diz que nao houve
 * evento - quem empurra os frames e o Choreographer proprio do BENDER. */
static int bender_ALooper_pollAll(int timeoutMillis,int*outFd,int*outEvents,void**outData){
    int r=bender_ALooper_pollOnce(timeoutMillis,outFd,outEvents,outData);
    return r==-3?-1:r;
}

static void *bender_ASensorManager_getInstance(void){return (void*)0x1;}
static int bender_ASensorManager_getSensorList(void*m,void***list){(void)m;if(list)*list=NULL;return 0;}
static void *bender_ASensorManager_getDefaultSensor(void*m,int type){(void)m;(void)type;return NULL;}
static void *bender_ASensorManager_createEventQueue(void*m,BenderLooper*l,int ident,int(*cb)(int,int,void*),void*data){(void)m;(void)l;(void)ident;(void)cb;(void)data;return (void*)0x2;}
static int bender_ASensorManager_destroyEventQueue(void*m,void*q){(void)m;(void)q;return 0;}
static int bender_ASensorEventQueue_enableSensor(void*q,void*s){(void)q;(void)s;return 0;}
static int bender_ASensorEventQueue_disableSensor(void*q,void*s){(void)q;(void)s;return 0;}
static int bender_ASensorEventQueue_setEventRate(void*q,void*s,int32_t us){(void)q;(void)s;(void)us;return 0;}
static int bender_ASensorEventQueue_hasEvents(void*q){(void)q;return 0;}
static ssize_t bender_ASensorEventQueue_getEvents(void*q,void*events,size_t count){(void)q;(void)events;(void)count;return 0;}
static int bender_ASensor_getType(void*s){(void)s;return 0;}
static float bender_ASensor_getResolution(void*s){(void)s;return 0.0f;}
static int bender_ASensor_getMinDelay(void*s){(void)s;return 0;}
static const char*bender_ASensor_getName(void*s){(void)s;return "BENDER Sensor";}
static const char*bender_ASensor_getVendor(void*s){(void)s;return "BENDER";}

/* Minimal fake JNI, suficiente para validar libmain/NativeLoader da Unity 6. */
typedef int32_t bjint; typedef int64_t bjlong; typedef float bjfloat; typedef double bjdouble; typedef uint8_t bjboolean; typedef uint16_t bjchar; typedef int32_t bjsize;
#define BENDER_DISPLAY_WIDTH 640
#define BENDER_DISPLAY_HEIGHT 480
#define BENDER_DISPLAY_DENSITY_DPI 240
#define BENDER_DISPLAY_REFRESH_HZ 60.0f
#define BENDER_DISPLAY_VSYNC_OFFSET_NS INT64_C(1000000)
#define BENDER_DISPLAY_PRESENTATION_DEADLINE_NS INT64_C(15666666)
typedef struct BObj { int kind; char a[1024]; char b[256]; bjchar *u16; bjsize u16_len; uintptr_t ref0; uintptr_t ref1; int64_t i64; int64_t i64b; float f0; float f1; float f2; float f3; float f4; float f5; float f6; float f7; int p0id; int p1id; uint32_t flags; } BObj;
#define BENDER_JNI_ARRAY_MAGIC UINT64_C(0xB36E27A3A22A11E5)
typedef enum BArrayKind { BARR_BOOLEAN=1, BARR_BYTE=2, BARR_CHAR=3, BARR_SHORT=4, BARR_INT=5, BARR_LONG=6, BARR_FLOAT=7, BARR_DOUBLE=8 } BArrayKind;
typedef struct BArray { uint64_t magic; uint32_t kind; uint32_t elem_size; bjsize length; uint32_t reserved; unsigned char *data; } BArray;
#define BENDER_JNI_OBJ_ARRAY_MAGIC UINT64_C(0xB36E27A30B1EC7A1)
typedef struct BObjArray { uint64_t magic; bjsize length; uint32_t reserved; void **items; char elem_sig[192]; } BObjArray;
typedef struct BEnv { void **functions; } BEnv;
typedef struct BVM { void **functions; } BVM;
typedef struct { const char *name; const char *signature; void *fnPtr; } BJNINativeMethod;
static void *g_jni_table[256]; static void *g_vm_table[8]; static BEnv g_env; static BVM g_vm;
#define BENDER_JNI_MAX_CLASSES 1024
#define BENDER_JNI_MAX_METHODS 2048
#define BENDER_JNI_MAX_OBJECTS 16384
static BObj g_classes[BENDER_JNI_MAX_CLASSES]; static int g_class_count;
static BObj g_methods[BENDER_JNI_MAX_METHODS]; static int g_method_count;
static BObj g_objects[BENDER_JNI_MAX_OBJECTS]; static int g_object_count;
static uint32_t g_object_local_refs[BENDER_JNI_MAX_OBJECTS];
static uint32_t g_object_global_refs[BENDER_JNI_MAX_OBJECTS];
static unsigned char g_object_pinned[BENDER_JNI_MAX_OBJECTS];
static int g_object_free_next[BENDER_JNI_MAX_OBJECTS];
static int g_object_free_head=-1;
static int g_object_active_count;
static unsigned long g_object_reuse_count;
static unsigned long g_object_release_count;
static unsigned long g_object_intern_hits;
static unsigned long g_object_local_delete_count;
#define BENDER_INPUT_EVENT_RING_SIZE 1024u
static BObj g_bender_input_event_ring[BENDER_INPUT_EVENT_RING_SIZE];
static unsigned g_bender_input_event_ring_next;
static BObj*bender_input_event_clone(void*src,const char*expected);
static pthread_mutex_t g_jni_handle_lock=PTHREAD_MUTEX_INITIALIZER;
static void *g_native_load_fn; static void *g_native_unload_fn; static void *g_native_loader_class;
/* Unity bitter.jnibridge.JNIBridge native callback captured from libunity JNI_OnLoad.
 * This is the missing Java dynamic-proxy return path used by Handler.Callback / Choreographer.FrameCallback. */
typedef void *(*BenderJNIBridgeInvokeFn)(BEnv*,void*,bjlong,void*,void*,void*);
static BenderJNIBridgeInvokeFn g_jnibridge_invoke_fn;
static void *g_jnibridge_class;
/* V2.7.40: ReflectionHelper AndroidJavaProxy has a different native callback ABI from bitter.jnibridge. */
typedef void *(*BenderReflectionProxyInvokeFn)(BEnv*,void*,bjlong,void*,void*);
static BenderReflectionProxyInvokeFn g_reflection_proxy_invoke_fn;
static void *g_reflection_helper_class;
/* V2.7.40: Unity Play Asset Delivery status callback. The Java wrapper normally forwards
 * AssetPackManager state arrays into this native instance method. In standalone APK ports
 * the Unity data is already present in base.apk, so BENDER reports install-time packs as complete. */
typedef void (*BenderUnityAssetPackStatusFn)(BEnv*,void*,void*,bjint,bjint);
static BenderUnityAssetPackStatusFn g_unity_asset_pack_status_fn;
static void *g_unity_asset_pack_status_class;
static unsigned long g_pad_status_dispatch_count;
/* V2.7.57: Firebase Java Task -> Firebase C++ Future bridge. */
typedef void (*BenderFirebaseJniResultFn2757)(BEnv*,void*,void*,bjboolean,bjboolean,void*,bjlong,bjlong);
static BenderFirebaseJniResultFn2757 g_firebase_jni_result_fn_2757;
static void *g_firebase_jni_result_class_2757;
static unsigned long g_firebase_task_created_2757;
static unsigned long g_firebase_task_completed_2757;
static unsigned long g_firebase_task_failed_2757;
static unsigned long g_jnibridge_dispatch_count;
static unsigned long g_reflection_proxy_dispatch_count;
/* FIX76: SurfaceTexture completion bridge for synthetic MediaNDK video fallback. */
static void *g_bender_surface_frame_proxies_fix76[4];
static unsigned g_bender_surface_frame_proxy_count_fix76;
static BObj *g_bender_surface_texture_fix76;
static unsigned long g_bender_surface_frame_dispatch_count_fix76;
/* V2.7.26 Java Choreographer bridge: asynchronous FrameCallback pump between nativeRender calls. */
static void *g_jni_choreographer_instance;
static void *g_jni_choreographer_pending_cb;
static unsigned long g_jni_choreographer_post_count;
static unsigned long g_jni_choreographer_dispatch_count;
static pthread_t g_jni_choreographer_worker;
static volatile int g_jni_choreographer_worker_started;
static volatile int g_jni_choreographer_worker_stop;
static unsigned long g_jni_choreographer_worker_ticks;
static void bender_jni_choreographer_ensure_worker(void);
static void bender_jni_choreographer_stop_worker(void);
static void *g_unity_player_class; static void *g_unity_activity_class; static void *g_unity_activity_instance;
static void *g_fmod_java_context; static int g_fmod_java_init_state;
static int g_fmod_audio_device_init_state;
static unsigned long g_fmod_audio_write_count;
static void *g_unity_init_jni_fn; static void *g_unity_native_pause_fn; static void *g_unity_native_resume_fn;
static void *g_unity_native_done_fn; static void *g_unity_native_render_fn; static void *g_unity_native_recreate_gfx_fn;
static void *g_unity_native_focus_fn; static void *g_unity_native_surface_changed_fn;
/* V2.7.28: UnityPlayer native input hook captured for the next generic Android KeyEvent/MotionEvent bridge. */
static void *g_unity_native_inject_event_fn;
/* V2.7.41: experimental host-controller -> Android InputEvent bridge. */
typedef bjboolean (*BenderUnityInjectEventFn)(BEnv*,void*,void*,bjint);
typedef bjboolean (*BenderUnityInjectEventFnUnity6Fix68)(BEnv*,void*,void*);
/* FIX68: 1 = Unity 6 InputEvent-only ABI; 2 = legacy InputEvent+int ABI. */
static int g_bender_unity_inject_abi_fix68;
static bjboolean bender_input_call_inject_fix68(void*event){
    if(!g_unity_native_inject_event_fn||!g_unity_player_class||!event)return 0;
    if(g_bender_unity_inject_abi_fix68==1){
        return ((BenderUnityInjectEventFnUnity6Fix68)g_unity_native_inject_event_fn)(&g_env,g_unity_player_class,event);
    }
    return ((BenderUnityInjectEventFn)g_unity_native_inject_event_fn)(&g_env,g_unity_player_class,event,0);
}
static int g_bender_input_ready;
static int g_bender_input_init_attempted;
static void *g_bender_input_controller;
static void *g_bender_input_joystick;
static int g_bender_input_fn_raw_button=16;
static int g_bender_input_keyboard_active;
static int g_bender_input_mouse_active;
static int g_bender_input_touchmap_active;
static int g_bender_input_keymap_active;
static int g_bender_input_prev_fn;
static unsigned char g_bender_input_prev_buttons[32];
static int g_bender_input_prev_lx_neg __attribute__((unused)),g_bender_input_prev_lx_pos __attribute__((unused)),g_bender_input_prev_ly_neg __attribute__((unused)),g_bender_input_prev_ly_pos __attribute__((unused));
static float g_bender_input_cursor_x=320.0f,g_bender_input_cursor_y=240.0f;
static BObj *g_bender_input_key_event;
static BObj *g_bender_input_motion_event;
static int64_t g_bender_input_down_time_ms;
static unsigned long g_bender_input_event_count;
static unsigned long g_bender_input_poll_count;
static int64_t g_bender_input_key_down_time_ms;
static int g_bender_keyboard_row;
static int g_bender_keyboard_col;
static int g_bender_keyboard_prev_up,g_bender_keyboard_prev_down,g_bender_keyboard_prev_left,g_bender_keyboard_prev_right,g_bender_keyboard_prev_a,g_bender_keyboard_prev_b;
static int g_bender_touch_prev_a;
static char g_bender_input_notice[64];
static int64_t g_bender_input_notice_until_ms;

/* Long-running mode must not leak one reflected Method/Object[]/Long box per vsync. */
static BObj *g_jnibridge_cached_rm_handler; static BObj *g_jnibridge_cached_rm_frame;
static BObjArray *g_jnibridge_cached_args_handler; static BObjArray *g_jnibridge_cached_args_frame;
static BObj *g_jni_choreographer_cached_long;
static volatile sig_atomic_t g_bender_run_stop_requested;
static volatile sig_atomic_t g_bender_exit_reason_code;
static volatile sig_atomic_t g_bender_exit_signal;
static volatile sig_atomic_t g_bender_exit_combo_recent;
static unsigned g_bender_exit_frame;
static int g_bender_continuous_run_mode;

static int bender_exit_panel_fd(void){
    const char*e=getenv("BENDER_EXIT_FD");
    return (e&&e[0]=='3'&&e[1]=='\0')?3:-1;
}
static const char*bender_exit_reason_name(int code){
    switch(code){
        case 1:return "SIGNAL_SIGTERM";
        case 2:return "SIGNAL_SIGINT";
        case 3:return "NATIVE_RENDER_ZERO";
        case 4:return "CHOREOGRAPHER_BLOCKED";
        case 5:return "RENDER_BLOCKED";
        case 6:return "START_SELECT_SIGTERM_LIKELY";
        default:return "UNKNOWN_CLEAN";
    }
}
static void __attribute__((unused)) bender_emit_exit_reason(void){
    int fd=bender_exit_panel_fd();
    if(fd<0)return;
    char line[256];
    int code=(int)g_bender_exit_reason_code;
    int sig=(int)g_bender_exit_signal;
    int combo=(int)g_bender_exit_combo_recent;
    int n=snprintf(line,sizeof(line),"[BENDER_EXIT][PROBE] reason=%s code=%d signal=%d combo_recent=%d frame=%u\\n",bender_exit_reason_name(code),code,sig,combo,g_bender_exit_frame);
    if(n>0){size_t len=(size_t)n;if(len>=sizeof(line))len=sizeof(line)-1u;(void)write(fd,line,len);}
}
static __thread int g_jni_exception; static __thread void *g_jni_exception_obj; static int g_jni_fatal_count; static size_t g_jni_call_count;
static char g_guest_lib_dir[1024];
static char g_guest_app_dir[1024];
static char g_guest_base_apk[1200];
static char g_guest_data_dir[1200];
static char g_guest_files_dir[1200];
static char g_guest_external_files_dir[1200];
static char g_guest_cache_dir[1200];
static char g_guest_obb_dir[1200];

/* BENDER FIX7: Android StreamingAssets/APK -> materialized Linux filesystem. */
static unsigned long g_bender_asset_path_requests_2771;
static unsigned long g_bender_asset_path_resolves_2771;
static unsigned long g_bender_asset_path_misses_2771;

static int bender_asset_path_safe_rel_2771(const char*rel){
    if(!rel)return 0;
    while(*rel=='/')rel++;
    if(strstr(rel,"../")||!strcmp(rel,"..")||strstr(rel,"/..")||strchr(rel,'\\'))return 0;
    return 1;
}
static int bender_asset_path_exists_2771(const char*p){
    struct stat st;
    return p&&*p&&stat(p,&st)==0;
}
static int bender_asset_path_join_2771(char*out,size_t cap,const char*root,const char*rel){
    if(!out||cap==0u||!root)return 0;
    while(rel&&*rel=='/')rel++;
    int n=(rel&&*rel)?snprintf(out,cap,"%s/%s",root,rel):snprintf(out,cap,"%s",root);
    return n>=0&&(size_t)n<cap;
}
static const char*bender_asset_path_rel_2771(const char*path,int*kind){
    if(kind)*kind=0;
    if(!path||!*path)return NULL;
    const char*p=NULL;
    if((p=strstr(path,"!/assets/"))!=NULL){if(kind)*kind=1;return p+9;}
    if((p=strstr(path,"!/assets"))!=NULL&&p[8]=='\0'){if(kind)*kind=1;return p+8;}
    if(!strncmp(path,"file:///android_asset/",22)){if(kind)*kind=2;return path+22;}
    if(!strcmp(path,"file:///android_asset")){if(kind)*kind=2;return path+21;}
    if(!strncmp(path,"file://android_asset/",21)){if(kind)*kind=2;return path+21;}
    if(!strcmp(path,"file://android_asset")){if(kind)*kind=2;return path+20;}
    if(!strncmp(path,"/android_asset/",15)){if(kind)*kind=2;return path+15;}
    if(!strcmp(path,"/android_asset")){if(kind)*kind=2;return path+14;}
    if(!strncmp(path,"android_asset/",14)){if(kind)*kind=2;return path+14;}
    if(!strcmp(path,"android_asset")){if(kind)*kind=2;return path+13;}
    return NULL;
}
static int bender_asset_path_resolve_2771(const char*path,char*out,size_t cap,const char*op){
    if(!path||!*path||!out||cap==0u)return 0;
    out[0]=0;
    unsigned long req=__atomic_add_fetch(&g_bender_asset_path_requests_2771,1ul,__ATOMIC_RELAXED);
    int kind=0;const char*rel=bender_asset_path_rel_2771(path,&kind);
    char stripped[2048];
    if(!rel&&!strncmp(path,"file://",7)&&path[7]=='/'){
        const char*local=path+7;
        if(strlen(local)<sizeof(stripped)){
            bender_strlcpy(stripped,local,sizeof(stripped));
            if(bender_asset_path_exists_2771(stripped)){
                bender_strlcpy(out,stripped,cap);
                unsigned long hit=__atomic_add_fetch(&g_bender_asset_path_resolves_2771,1ul,__ATOMIC_RELAXED);
                if(hit<=24ul||(hit%256ul)==0ul)logmsg("[BENDER_ASSET_PATH][RESOLVE] op=%s kind=file-uri request=%s resolved=%s exists=1 hit=%lu\n",op?op:"?",path,out,hit);
                return 1;
            }
        }
    }
    if(!rel)return 0;
    while(*rel=='/')rel++;
    if(!bender_asset_path_safe_rel_2771(rel)){
        logmsg("[BENDER_ASSET_PATH][REJECT] op=%s request=%s reason=unsafe-relative\n",op?op:"?",path);
        return 0;
    }
    if(req<=24ul||(req%256ul)==0ul)logmsg("[BENDER_ASSET_PATH][REQUEST] op=%s kind=%d request=%s rel=%s\n",op?op:"?",kind,path,rel);

    char root[1536],cand[2048];
    const char*roots[]={
        "/unity_payload/assets",
        "/unity_payload/assets/StreamingAssets",
        "/unity_payload/assets/bin/Data/StreamingAssets",
        "/unity_payload/assets/bin/data/StreamingAssets",
        "/assets"
    };
    for(size_t i=0;i<sizeof(roots)/sizeof(roots[0]);i++){
        int nr=snprintf(root,sizeof(root),"%s%s",g_guest_app_dir,roots[i]);
        if(nr<0||(size_t)nr>=sizeof(root))continue;
        if(!bender_asset_path_join_2771(cand,sizeof(cand),root,rel))continue;
        if(bender_asset_path_exists_2771(cand)){
            bender_strlcpy(out,cand,cap);
            unsigned long hit=__atomic_add_fetch(&g_bender_asset_path_resolves_2771,1ul,__ATOMIC_RELAXED);
            if(hit<=64ul||(hit%256ul)==0ul)logmsg("[BENDER_ASSET_PATH][RESOLVE] op=%s kind=%d request=%s resolved=%s exists=1 hit=%lu\n",op?op:"?",kind,path,out,hit);
            return 1;
        }
    }

    int nd=snprintf(root,sizeof(root),"%s/unity_payload/asset_packs",g_guest_app_dir);
    if(nd>0&&(size_t)nd<sizeof(root)){
        DIR*d=opendir(root);
        if(d){
            struct dirent*de;
            while((de=readdir(d))){
                if(de->d_name[0]=='.')continue;
                char packroot[2048];
                int np=snprintf(packroot,sizeof(packroot),"%s/%s/assets",root,de->d_name);
                if(np<0||(size_t)np>=sizeof(packroot))continue;
                if(!bender_asset_path_join_2771(cand,sizeof(cand),packroot,rel))continue;
                if(bender_asset_path_exists_2771(cand)){
                    closedir(d);bender_strlcpy(out,cand,cap);
                    unsigned long hit=__atomic_add_fetch(&g_bender_asset_path_resolves_2771,1ul,__ATOMIC_RELAXED);
                    if(hit<=64ul||(hit%256ul)==0ul)logmsg("[BENDER_ASSET_PATH][RESOLVE] op=%s kind=asset-pack request=%s resolved=%s exists=1 hit=%lu\n",op?op:"?",path,out,hit);
                    return 1;
                }
            }
            closedir(d);
        }
    }
    unsigned long miss=__atomic_add_fetch(&g_bender_asset_path_misses_2771,1ul,__ATOMIC_RELAXED);
    if(miss<=64ul||(miss%256ul)==0ul)logmsg("[BENDER_ASSET_PATH][MISS] op=%s kind=%d request=%s rel=%s miss=%lu\n",op?op:"?",kind,path,rel,miss);
    return 0;
}
static int bender_access_2771(const char*path,int mode){
    int rc=access(path,mode);if(rc==0||!path)return rc;
    int saved=errno;char resolved[2048];
    if(bender_asset_path_resolve_2771(path,resolved,sizeof(resolved),"access"))return access(resolved,mode);
    errno=saved;return rc;
}
static int bender_fstat_2771(int fd,struct stat*st){
    /* ULTRAKILL: libunity E libil2cpp importam fstat(); o port modelo so
     * tinha stat/lstat/open. Mesmo layout de struct stat que eles ja usam
     * no stat acima (host glibc), entao e so encaminhar para o fd. */
    if(!st){errno=EFAULT;return -1;}
    return fstat(fd,st);
}
static int bender_stat_2771(const char*path,struct stat*st){
    int rc=stat(path,st);if(rc==0||!path)return rc;
    int saved=errno;char resolved[2048];
    if(bender_asset_path_resolve_2771(path,resolved,sizeof(resolved),"stat"))return stat(resolved,st);
    errno=saved;return rc;
}
static int bender_lstat_2771(const char*path,struct stat*st){
    int rc=lstat(path,st);if(rc==0||!path)return rc;
    int saved=errno;char resolved[2048];
    if(bender_asset_path_resolve_2771(path,resolved,sizeof(resolved),"lstat"))return lstat(resolved,st);
    errno=saved;return rc;
}
static int bender_open_2771(const char*path,int flags,...){
    mode_t mode=0;int has_mode=(flags&O_CREAT)!=0;
    if(has_mode){va_list ap;va_start(ap,flags);mode=(mode_t)va_arg(ap,int);va_end(ap);}
    int fd=has_mode?open(path,flags,mode):open(path,flags);
    if(fd>=0||!path||(flags&(O_WRONLY|O_RDWR|O_CREAT|O_TRUNC|O_APPEND)))return fd;
    int saved=errno;char resolved[2048];
    if(bender_asset_path_resolve_2771(path,resolved,sizeof(resolved),"open"))return open(resolved,flags);
    errno=saved;return fd;
}
static DIR*bender_opendir_2771(const char*path){
    DIR*d=opendir(path);if(d||!path)return d;
    int saved=errno;char resolved[2048];
    if(bender_asset_path_resolve_2771(path,resolved,sizeof(resolved),"opendir"))return opendir(resolved);
    errno=saved;return NULL;
}
static char*bender_realpath_2771(const char*path,char*resolved_path){
    char*r=realpath(path,resolved_path);if(r||!path)return r;
    int saved=errno;char resolved[2048];
    if(bender_asset_path_resolve_2771(path,resolved,sizeof(resolved),"realpath"))return realpath(resolved,resolved_path);
    errno=saved;return NULL;
}
static void bender_asset_path_diag_2771(void){
    char test[2048],resolved[2048];
    snprintf(test,sizeof(test),"jar:file://%s!/assets",g_guest_base_apk);
    int root=bender_asset_path_resolve_2771(test,resolved,sizeof(resolved),"startup-root");
    char bundles[2048];snprintf(bundles,sizeof(bundles),"jar:file://%s!/assets/Bundles",g_guest_base_apk);
    int b=bender_asset_path_resolve_2771(bundles,resolved,sizeof(resolved),"startup-bundles");
    logmsg("[BENDER_ASSET_PATH][STARTUP] root=%d bundles=%d requests=%lu resolves=%lu misses=%lu\n",root,b,g_bender_asset_path_requests_2771,g_bender_asset_path_resolves_2771,g_bender_asset_path_misses_2771);
}

/* V2.7.33: synthetic libandroid.so handle + file-backed AAsset bridge for optional Android-native modules. */
static int g_bender_libandroid_token;
static int g_bender_libmediandk_token;
static int g_bender_asset_manager_token;
typedef struct BenderAAsset {
    FILE *fp;
    int64_t len;
    unsigned char *buffer;
    size_t buffer_len;
    char path[1536];
} BenderAAsset;
static void *bender_AAssetManager_fromJava(void *env,void *assetManager){
    (void)env;(void)assetManager;
    logmsg("[BENDER_ANDROID_ASSET][MANAGER] fromJava jobject=%p -> %p\n",assetManager,(void*)&g_bender_asset_manager_token);
    return &g_bender_asset_manager_token;
}
static void *bender_AAssetManager_open(void *mgr,const char *filename,int mode){
    (void)mgr;(void)mode;
    if(!filename||!*filename)return NULL;
    while(filename[0]=='/')filename++;
    char p[2048],request[2048];
    snprintf(request,sizeof(request),"/android_asset/%s",filename);
    FILE *fp=NULL;
    if(bender_asset_path_resolve_2771(request,p,sizeof(p),"AAssetManager_open"))fp=fopen(p,"rb");
    if(!fp){logmsg("[BENDER_ANDROID_ASSET][OPEN][MISS] name=%s errno=%d\n",filename,errno);return NULL;}
    if(fseeko(fp,0,SEEK_END)!=0){fclose(fp);return NULL;}
    off_t end=ftello(fp); if(end<0){fclose(fp);return NULL;}
    (void)fseeko(fp,0,SEEK_SET);
    BenderAAsset *a=(BenderAAsset*)calloc(1,sizeof(*a)); if(!a){fclose(fp);return NULL;}
    a->fp=fp;a->len=(int64_t)end;bender_strlcpy(a->path,p,sizeof(a->path));
    logmsg("[BENDER_ANDROID_ASSET][OPEN] name=%s path=%s len=%lld asset=%p\n",filename,p,(long long)a->len,(void*)a);
    return a;
}
static int bender_AAsset_read(void *asset,void *buf,size_t count){
    BenderAAsset*a=(BenderAAsset*)asset;if(!a||!a->fp||!buf)return -1;
    size_t n=fread(buf,1,count,a->fp);return (int)n;
}
static long bender_AAsset_seek(void *asset,long off,int whence){
    BenderAAsset*a=(BenderAAsset*)asset;if(!a||!a->fp)return -1;
    if(fseeko(a->fp,(off_t)off,whence)!=0)return -1;
    off_t p=ftello(a->fp);
    return p<0?-1:(long)p;
}
static int64_t bender_AAsset_seek64(void *asset,int64_t off,int whence){
    BenderAAsset*a=(BenderAAsset*)asset;if(!a||!a->fp)return -1;
    if(fseeko(a->fp,(off_t)off,whence)!=0)return -1;
    off_t p=ftello(a->fp);
    return p<0?-1:(int64_t)p;
}
static long bender_AAsset_getLength(void *asset){BenderAAsset*a=(BenderAAsset*)asset;return a?(long)a->len:0;}
static int64_t bender_AAsset_getLength64(void *asset){BenderAAsset*a=(BenderAAsset*)asset;return a?a->len:0;}
static long bender_AAsset_getRemainingLength(void *asset){BenderAAsset*a=(BenderAAsset*)asset;if(!a||!a->fp)return 0;off_t p=ftello(a->fp);return p<0?0:(long)(a->len-(int64_t)p);}
static int64_t bender_AAsset_getRemainingLength64(void *asset){BenderAAsset*a=(BenderAAsset*)asset;if(!a||!a->fp)return 0;off_t p=ftello(a->fp);return p<0?0:a->len-(int64_t)p;}
static const void *bender_AAsset_getBuffer(void *asset){
    BenderAAsset*a=(BenderAAsset*)asset;if(!a||!a->fp)return NULL;
    if(a->buffer)return a->buffer;
    if(a->len<=0||(uint64_t)a->len>SIZE_MAX)return NULL;
    a->buffer=(unsigned char*)malloc((size_t)a->len);if(!a->buffer)return NULL;
    off_t cur=ftello(a->fp);(void)fseeko(a->fp,0,SEEK_SET);size_t n=fread(a->buffer,1,(size_t)a->len,a->fp);
    if(cur>=0)(void)fseeko(a->fp,cur,SEEK_SET);
    if(n!=(size_t)a->len){free(a->buffer);a->buffer=NULL;return NULL;}
    a->buffer_len=n;return a->buffer;
}
static int bender_AAsset_isAllocated(void *asset){BenderAAsset*a=(BenderAAsset*)asset;return (a&&a->buffer)?1:0;}
static void bender_AAsset_close(void *asset){
    BenderAAsset*a=(BenderAAsset*)asset;if(!a)return;logmsg("[BENDER_ANDROID_ASSET][CLOSE] asset=%p path=%s\n",asset,a->path);
    if(a->fp)fclose(a->fp);
    free(a->buffer);
    free(a);
}
static volatile sig_atomic_t g_jni_kill_requested;
static volatile sig_atomic_t g_render_jmp_active;
static sigjmp_buf g_render_jmp;

static BObj *bender_known_bobj(void *p){
    uintptr_t u=(uintptr_t)p;
    if(!u)return NULL;
    uintptr_t c0=(uintptr_t)&g_classes[0], c1=(uintptr_t)&g_classes[BENDER_JNI_MAX_CLASSES];
    uintptr_t m0=(uintptr_t)&g_methods[0], m1=(uintptr_t)&g_methods[BENDER_JNI_MAX_METHODS];
    uintptr_t o0=(uintptr_t)&g_objects[0], o1=(uintptr_t)&g_objects[BENDER_JNI_MAX_OBJECTS];
    uintptr_t e0=(uintptr_t)&g_bender_input_event_ring[0], e1=(uintptr_t)&g_bender_input_event_ring[BENDER_INPUT_EVENT_RING_SIZE];
    if(u>=c0&&u<c1&&((u-c0)%sizeof(BObj))==0)return (BObj*)p;
    if(u>=m0&&u<m1&&((u-m0)%sizeof(BObj))==0)return (BObj*)p;
    if(u>=o0&&u<o1&&((u-o0)%sizeof(BObj))==0)return (BObj*)p;
    if(u>=e0&&u<e1&&((u-e0)%sizeof(BObj))==0)return (BObj*)p;
    return NULL;
}

static int bender_jni_object_index_2768(void*p){
    uintptr_t u=(uintptr_t)p,o0=(uintptr_t)&g_objects[0],o1=(uintptr_t)&g_objects[BENDER_JNI_MAX_OBJECTS];
    if(u<o0||u>=o1||((u-o0)%sizeof(BObj))!=0)return -1;
    return (int)((u-o0)/sizeof(BObj));
}
static int bender_jni_bridge_singleton_2768(int kind,const char*a,const char*b){
    if(kind==3)return 1;
    if(kind!=4||!a)return 0;
    if(!strcmp(a,"android/media/MediaRouter$RouteInfo")&&!strcmp(b?b:"","default-route"))return 1;
    if(!strcmp(a,"android/view/Display")&&!strcmp(b?b:"","displayId=0"))return 1;
    if(!strcmp(a,"android/content/res/Resources"))return 1;
    if(!strcmp(a,"android/content/res/Configuration"))return 1;
    if(!strcmp(a,"android/os/Looper")&&!strcmp(b?b:"","main"))return 1;
    if(!strcmp(a,"android/content/Context")&&(b&&*b))return 1;
    if(!strcmp(a,"java/util/Locale")&&(b&&*b))return 1;
    if(!strcmp(a,"android/content/SharedPreferences")&&(b&&*b))return 1;
    if(!strcmp(a,"android/view/WindowMetrics")&&!strcmp(b?b:"","640x480"))return 1;
    if(!strcmp(a,"android/graphics/Rect")&&!strcmp(b?b:"","0,0,640,480"))return 1;
    if(strstr(a,"Manager")&&(b&&*b))return 1;
    return 0;
}
static void bender_jni_release_object_locked_2768(int idx){
    if(idx<0||idx>=g_object_count||g_object_pinned[idx])return;
    if(g_object_local_refs[idx]||g_object_global_refs[idx])return;
    BObj*o=&g_objects[idx];
    if(o->u16){free(o->u16);o->u16=NULL;}
    memset(o,0,sizeof(*o));
    g_object_free_next[idx]=g_object_free_head;
    g_object_free_head=idx;
    if(g_object_active_count>0)--g_object_active_count;
    ++g_object_release_count;
}
static void bender_jni_pin_object_2768(void*obj){
    int idx=bender_jni_object_index_2768(obj);
    if(idx<0)return;
    pthread_mutex_lock(&g_jni_handle_lock);
    g_object_pinned[idx]=1u;
    pthread_mutex_unlock(&g_jni_handle_lock);
}
static void bender_jni_pool_health_2768(const char*reason){
    logmsg("[BENDER_JNI][POOL_HEALTH] reason=%s highwater=%d/%d active=%d free_head=%d reused=%lu released=%lu intern_hits=%lu local_deletes=%lu fatal=%d\n",
        reason?reason:"?",g_object_count,BENDER_JNI_MAX_OBJECTS,g_object_active_count,g_object_free_head,
        g_object_reuse_count,g_object_release_count,g_object_intern_hits,g_object_local_delete_count,g_jni_fatal_count);
}

static BObj *new_obj(int kind,const char*a,const char*b){
    BObj*o=NULL;int idx=-1;int cache_hit=0;int internable=bender_jni_bridge_singleton_2768(kind,a,b);
    pthread_mutex_lock(&g_jni_handle_lock);
    if(kind==1 && a && *a){
        for(int i=0;i<g_class_count;i++){
            if(g_classes[i].kind==1 && !strcmp(g_classes[i].a,a)){o=&g_classes[i];break;}
        }
    }
    if(!o&&internable&&kind!=1&&kind!=2){
        for(int i=0;i<g_object_count;i++){
            BObj*c=&g_objects[i];
            if(c->kind==kind&&!strcmp(c->a,a?a:"")&&!strcmp(c->b,b?b:"")){
                o=c;idx=i;cache_hit=1;
                if(g_object_local_refs[i]!=UINT32_MAX)++g_object_local_refs[i];
                ++g_object_intern_hits;
                break;
            }
        }
    }
    if(!o){
        if(kind==1){
            if(g_class_count<BENDER_JNI_MAX_CLASSES)o=&g_classes[g_class_count++];
        } else if(kind==2){
            if(g_method_count<BENDER_JNI_MAX_METHODS)o=&g_methods[g_method_count++];
        } else {
            if(g_object_free_head>=0){
                idx=g_object_free_head;
                g_object_free_head=g_object_free_next[idx];
                o=&g_objects[idx];
                ++g_object_reuse_count;
            } else if(g_object_count<BENDER_JNI_MAX_OBJECTS){
                idx=g_object_count++;
                o=&g_objects[idx];
            }
        }
        if(o){
            memset(o,0,sizeof(*o));o->kind=kind;
            if(a)bender_strlcpy(o->a,a,sizeof(o->a));
            if(b)bender_strlcpy(o->b,b,sizeof(o->b));
            if(idx>=0){
                g_object_local_refs[idx]=1u;
                g_object_global_refs[idx]=0u;
                g_object_pinned[idx]=internable?1u:0u;
                g_object_free_next[idx]=-1;
                ++g_object_active_count;
            }
        }
    }
    pthread_mutex_unlock(&g_jni_handle_lock);
    if(!o){
        ++g_jni_fatal_count;
        logmsg("[BENDER_JNI][HANDLE_POOL_FULL] kind=%d class=%d/%d method=%d/%d object_highwater=%d/%d object_active=%d reused=%lu released=%lu intern_hits=%lu\n",
            kind,g_class_count,BENDER_JNI_MAX_CLASSES,g_method_count,BENDER_JNI_MAX_METHODS,
            g_object_count,BENDER_JNI_MAX_OBJECTS,g_object_active_count,g_object_reuse_count,g_object_release_count,g_object_intern_hits);
    }else if(cache_hit&&(g_object_intern_hits<=8u||(g_object_intern_hits%4096u)==0u)){
        bender_jni_pool_health_2768("intern-hit");
    }else if(g_object_reuse_count&&(g_object_reuse_count%4096u)==0u){
        bender_jni_pool_health_2768("slot-reuse");
    }
    return o;
}
static uintptr_t jni_generic(void *env,...){(void)env;return 0;}
static const char *bender_reflected_method_sig(const BObj *bo){
    if(!bo)return "()V";
    BObj*stored=bender_known_bobj((void*)bo->ref1);
    if(stored&&stored->kind==2&&stored->b[0])return stored->b;
    if(bo->flags==1u)return "(Landroid/os/Message;)Z"; /* Handler.Callback.handleMessage */
    if(bo->flags==2u)return "(J)V";                    /* Choreographer.FrameCallback.doFrame */
    return "()V";
}
static const char *bender_jni_class_name(void *cls){
    BObj*c=bender_known_bobj(cls);
    return (c&&c->a[0])?c->a:"";
}
static BObj *bender_jni_intern_method(void *cls,const char *name,const char *sig,int is_static,const char *source){
    const char*cn=bender_jni_class_name(cls);if(!name)name="";if(!sig)sig="";
    BObj*o=NULL;int hit=0;
    pthread_mutex_lock(&g_jni_handle_lock);
    for(int i=0;i<g_method_count;i++){
        BObj*m=&g_methods[i];BObj*mc=bender_known_bobj((void*)m->ref0);const char*mcn=(mc&&mc->a[0])?mc->a:"";
        if(m->kind==2&&!strcmp(m->a,name)&&!strcmp(m->b,sig)&&(int)m->ref1==(is_static?1:0)&&!strcmp(mcn,cn)){o=m;hit=1;break;}
    }
    if(!o&&g_method_count<BENDER_JNI_MAX_METHODS){
        o=&g_methods[g_method_count++];memset(o,0,sizeof(*o));o->kind=2;
        bender_strlcpy(o->a,name,sizeof(o->a));bender_strlcpy(o->b,sig,sizeof(o->b));
        o->ref0=(uintptr_t)cls;o->ref1=(uintptr_t)(is_static?1u:0u);
        if(strstr(sig,"Landroid/os/Message;")&&!strcmp(name,"handleMessage"))o->flags=1u;
        else if(!strcmp(name,"doFrame")&&!strcmp(sig,"(J)V"))o->flags=2u;
    }
    pthread_mutex_unlock(&g_jni_handle_lock);
    if(!o){++g_jni_fatal_count;logmsg("[BENDER_JNI][METHOD_ID][POOL_FULL] class=%s name=%s sig=%s\n",cn,name,sig);return NULL;}
    logmsg("[BENDER_JNI][METHOD_ID][%s] source=%s class=%s cls=%p name=%s sig=%s static=%d -> mid=%p flags=%u\n",hit?"HIT":"NEW",source?source:"?",cn,cls,name,sig,is_static,(void*)o,o->flags);
    return o;
}
static BObj *bender_jni_find_reflected_cached_2768(void*cls,BObj*mid,int is_static){
    if(!mid)return NULL;
    BObj*hit=NULL;
    pthread_mutex_lock(&g_jni_handle_lock);
    for(int i=0;i<g_object_count;i++){
        BObj*o=&g_objects[i];
        if(o->kind==4&&!strcmp(o->a,"java/lang/reflect/Method")&&o->ref0==(uintptr_t)cls&&o->ref1==(uintptr_t)mid&&o->i64==(is_static?1:0)){
            hit=o;
            if(g_object_local_refs[i]!=UINT32_MAX)++g_object_local_refs[i];
            ++g_object_intern_hits;
            break;
        }
    }
    pthread_mutex_unlock(&g_jni_handle_lock);
    return hit;
}
static BObj *bender_jni_reflected_from_mid_2768(void*cls,BObj*mid,int is_static,const char*source){
    if(!mid)return NULL;
    BObj*rm=bender_jni_find_reflected_cached_2768(cls,mid,is_static);
    if(rm){
        logmsg("[BENDER_JNI][REFLECT_CACHE][HIT] source=%s class=%s name=%s sig=%s static=%d reflected=%p mid=%p\n",
            source?source:"?",bender_jni_class_name(cls),mid->a,mid->b,is_static,(void*)rm,(void*)mid);
        return rm;
    }
    rm=new_obj(4,"java/lang/reflect/Method",mid->a[0]?mid->a:"callback");
    if(rm){
        rm->ref0=(uintptr_t)cls;rm->ref1=(uintptr_t)mid;rm->flags=mid->flags;rm->i64=is_static?1:0;
        bender_jni_pin_object_2768(rm);
    }
    logmsg("[BENDER_JNI][REFLECT_CACHE][NEW] source=%s class=%s name=%s sig=%s static=%d reflected=%p mid=%p\n",
        source?source:"?",bender_jni_class_name(cls),mid->a,mid->b,is_static,(void*)rm,(void*)mid);
    return rm;
}
static BObj *bender_jni_make_reflected_method(void*cls,const char*name,const char*sig,int is_static,const char*source){
    BObj*mid=bender_jni_intern_method(cls,name?name:"",sig?sig:"",is_static,source?source:"ReflectionHelper");
    if(!mid)return NULL;
    BObj*rm=bender_jni_reflected_from_mid_2768(cls,mid,is_static,source?source:"ReflectionHelper");
    logmsg("[BENDER_JNI][REFLECTION_HELPER][METHOD] source=%s class=%s cls=%p name=%s sig=%s static=%d -> reflected=%p mid=%p cache=stable\n",
           source?source:"?",bender_jni_class_name(cls),cls,name?name:"",sig?sig:"",is_static,(void*)rm,(void*)mid);
    return rm;
}
static void *jni_FromReflectedMethod(BEnv*env,void*method){
    (void)env;BObj*bo=bender_known_bobj(method);
    if(bo&&bo->kind==4&&!strcmp(bo->a,"java/lang/reflect/Method")){
        const char*sig=bender_reflected_method_sig(bo);BObj*stored=bender_known_bobj((void*)bo->ref1);BObj*mid=NULL;
        if(stored&&stored->kind==2){mid=stored;logmsg("[BENDER_JNI][METHOD_ID][ROUNDTRIP] reflected=%p -> original mid=%p\n",method,(void*)mid);}
        else mid=bender_jni_intern_method((void*)bo->ref0,bo->b[0]?bo->b:"callback",sig,0,"FromReflectedMethod");
        if(mid&&bo->flags)mid->flags=bo->flags;
        ++g_jni_call_count;set_checkpoint("JNI FromReflectedMethod method=%s sig=%s reflected=%p mid=%p",bo->b,sig,method,(void*)mid);
        logmsg("[BENDER_JNI][REFLECT_METHOD][FROM] reflected=%p name=%s sig=%s decl=%p flags=%u -> mid=%p class=%s\n",method,bo->b,sig,(void*)bo->ref0,bo->flags,(void*)mid,bender_jni_class_name((void*)bo->ref0));
        return mid;
    }
    ++g_jni_call_count;logmsg("[BENDER_JNI][REFLECT_METHOD][FROM][FALLBACK] reflected=%p -> same-handle\n",method);return method;
}
static void *jni_ToReflectedMethod(BEnv*env,void*cls,void*method_id,bjboolean is_static){
    (void)env;BObj*mid=bender_known_bobj(method_id);
    void*canonical=(mid&&mid->ref0)?(void*)mid->ref0:cls;
    BObj*rm=(mid&&mid->kind==2)?bender_jni_reflected_from_mid_2768(canonical,mid,is_static?1:0,"ToReflectedMethod"):NULL;
    if(!rm){
        rm=new_obj(4,"java/lang/reflect/Method",(mid&&mid->a[0])?mid->a:"callback");
        if(rm){rm->ref0=(uintptr_t)canonical;rm->ref1=(uintptr_t)method_id;rm->flags=mid?mid->flags:0u;rm->i64=is_static?1:0;}
    }
    if(rm&&!rm->flags&&mid&&strstr(mid->b,"Landroid/os/Message;"))rm->flags=1u;
    else if(rm&&!rm->flags&&mid&&!strcmp(mid->b,"(J)V"))rm->flags=2u;
    ++g_jni_call_count;
    logmsg("[BENDER_JNI][REFLECT_METHOD][TO] mid=%p name=%s sig=%s class=%p canonical_class=%s static=%u -> reflected=%p flags=%u stored_mid=%p cache=stable\n",
        method_id,mid?mid->a:"?",mid?mid->b:"?",cls,bender_jni_class_name(canonical),(unsigned)is_static,(void*)rm,rm?rm->flags:0u,rm?(void*)rm->ref1:NULL);
    return rm;
}
static bjint jni_GetVersion(BEnv*env){(void)env;return 0x00010006;}
static void *jni_FindClass(BEnv*env,const char*name){(void)env;BObj*o=new_obj(1,name,NULL);
if(name&&strcmp(name,"com/unity3d/player/NativeLoader")==0)g_native_loader_class=o;
if(name&&strcmp(name,"com/unity3d/player/UnityPlayer")==0)g_unity_player_class=o;
if(name&&strcmp(name,"com/unity3d/player/UnityPlayerForActivityOrService")==0)g_unity_activity_class=o;
logmsg("[BENDER_JNI] FindClass %s -> %p canonical=1 class_count=%d\n",name?name:"",(void*)o,g_class_count);return o;}
static bjint jni_Throw(BEnv*env,void*obj){
    (void)env;g_jni_exception=1;g_jni_exception_obj=obj;
    if(!bender_known_bobj(obj)){BObj*e=new_obj(4,"java/lang/Throwable","JNI Throw foreign object");g_jni_exception_obj=e;}
    BObj*e=bender_known_bobj(g_jni_exception_obj);
    logmsg("[BENDER_JNI][EXCEPTION][THROW] obj=%p class=%s msg=%s\n",g_jni_exception_obj,e?e->a:"java/lang/Throwable",e?e->b:"");return 0;
}
static bjint jni_ThrowNew(BEnv*env,void*cls,const char*msg){
    (void)env;BObj*c=bender_known_bobj(cls);const char*cn=(c&&c->a[0])?c->a:"java/lang/RuntimeException";
    BObj*e=new_obj(4,cn,msg?msg:"");g_jni_exception=1;g_jni_exception_obj=e;
    logmsg("[BENDER_JNI][EXCEPTION][THROW_NEW] class=%s obj=%p msg=%s\n",cn,(void*)e,msg?msg:"");return 0;
}
static void jni_FatalError(BEnv*env,const char*msg){(void)env;++g_jni_fatal_count;logmsg("[BENDER_JNI][FATAL_INTERCEPTED] %s\n",msg?msg:"");}
static void jni_ExceptionDescribe(BEnv*env){
    (void)env;BObj*e=bender_known_bobj(g_jni_exception_obj);
    logmsg("[BENDER_JNI][EXCEPTION][DESCRIBE] pending=%d obj=%p class=%s msg=%s\n",g_jni_exception,g_jni_exception_obj,e?e->a:"(none)",e?e->b:"");
}
static void jni_ExceptionClear(BEnv*env){
    (void)env;if(g_jni_exception)logmsg("[BENDER_JNI][EXCEPTION][CLEAR] obj=%p\n",g_jni_exception_obj);
    g_jni_exception=0;g_jni_exception_obj=NULL;
}
static void *jni_ExceptionOccurred(BEnv*env){
    (void)env;void*o=g_jni_exception?g_jni_exception_obj:NULL;
    logmsg("[BENDER_JNI][EXCEPTION][OCCURRED] pending=%d -> %p\n",g_jni_exception,o);return o;
}
static bjboolean jni_ExceptionCheck(BEnv*env){(void)env;return g_jni_exception?1:0;}
static bjboolean jni_IsSameObject(BEnv*env,void*a,void*b){
    (void)env;int same=0;const char*reason="pointer-different";
    if(a==b){same=1;reason="same-pointer";}
    else if(!a||!b){same=0;reason="null-mismatch";}
    else {
        BObj*oa=bender_known_bobj(a);BObj*ob=bender_known_bobj(b);
        if(oa&&ob&&oa->kind==1&&ob->kind==1&&oa->a[0]&&ob->a[0]&&!strcmp(oa->a,ob->a)){same=1;reason="canonical-class";}
    }
    logmsg("[BENDER_JNI][SAME_OBJECT] a=%p b=%p class_a=%s class_b=%s -> %d reason=%s\n",a,b,bender_jni_class_name(a),bender_jni_class_name(b),same,reason);
    return (bjboolean)(same?1:0);
}
static void *jni_NewGlobalRef(BEnv*env,void*o){
    (void)env;int idx=bender_jni_object_index_2768(o);if(idx<0)return o;
    pthread_mutex_lock(&g_jni_handle_lock);
    if(g_objects[idx].kind&&g_object_global_refs[idx]!=UINT32_MAX)++g_object_global_refs[idx];
    pthread_mutex_unlock(&g_jni_handle_lock);
    return o;
}
static void jni_DeleteGlobalRef(BEnv*env,void*o){
    (void)env;int idx=bender_jni_object_index_2768(o);if(idx<0)return;
    pthread_mutex_lock(&g_jni_handle_lock);
    if(g_object_global_refs[idx])--g_object_global_refs[idx];
    bender_jni_release_object_locked_2768(idx);
    pthread_mutex_unlock(&g_jni_handle_lock);
}
static void jni_DeleteLocalRef(BEnv*env,void*o){
    (void)env;int idx=bender_jni_object_index_2768(o);if(idx<0)return;
    pthread_mutex_lock(&g_jni_handle_lock);
    if(g_object_local_refs[idx])--g_object_local_refs[idx];
    ++g_object_local_delete_count;
    bender_jni_release_object_locked_2768(idx);
    unsigned long n=g_object_local_delete_count;
    pthread_mutex_unlock(&g_jni_handle_lock);
    if(n<=8u||(n%4096u)==0u)bender_jni_pool_health_2768("DeleteLocalRef");
}
static void*jni_NewLocalRef(BEnv*env,void*o){
    (void)env;int idx=bender_jni_object_index_2768(o);if(idx<0)return o;
    pthread_mutex_lock(&g_jni_handle_lock);
    if(g_objects[idx].kind&&g_object_local_refs[idx]!=UINT32_MAX)++g_object_local_refs[idx];
    pthread_mutex_unlock(&g_jni_handle_lock);
    return o;
}

static const char *bender_jni_runtime_class(void*o,char*buf,size_t cap){
    if(buf&&cap){buf[0]=0;}
    if(!o){return "";}
    BObj*bo=bender_known_bobj(o);
    if(!bo){return "java/lang/Object";}
    if(bo->kind==1){return "java/lang/Class";}
    if(bo->kind==3){return "java/lang/String";}
    if(bo->kind==4&&bo->a[0]){if(!buf||cap<2)return bo->a;bender_strlcpy(buf,bo->a,cap);size_t n=strlen(buf);if(n>2&&buf[0]=='L'&&buf[n-1]==';'){memmove(buf,buf+1,n-2);buf[n-2]=0;}return buf;}
    return "java/lang/Object";
}
static int bender_jni_class_is_a(const char*child,const char*parent){
    if(!child||!parent||!*child||!*parent){return 0;}
    if(!strcmp(child,parent)){return 1;}
    if(!strcmp(parent,"java/lang/Object")){return 1;}
    if((!strcmp(child,"android/view/KeyEvent")||!strcmp(child,"android/view/MotionEvent"))&&!strcmp(parent,"android/view/InputEvent"))return 1;
    if((!strcmp(child,"android/view/KeyEvent")||!strcmp(child,"android/view/MotionEvent")||!strcmp(child,"android/view/InputEvent"))&&!strcmp(parent,"android/os/Parcelable"))return 1;
    if(!strcmp(child,"java/lang/String")&&(!strcmp(parent,"java/lang/CharSequence")||!strcmp(parent,"java/io/Serializable")||!strcmp(parent,"java/lang/Comparable")))return 1;
    if(!strcmp(child,"java/lang/Class")&&(!strcmp(parent,"java/io/Serializable")||!strcmp(parent,"java/lang/reflect/AnnotatedElement")||!strcmp(parent,"java/lang/reflect/Type")))return 1;
    if(!strcmp(child,"javax/net/ssl/X509TrustManager")&&!strcmp(parent,"javax/net/ssl/TrustManager"))return 1;
    if(!strcmp(child,"java/security/cert/X509Certificate")&&!strcmp(parent,"java/security/cert/Certificate"))return 1;
    /* ULTRAKILL(Unity 2019.4): o UnityPlayer pergunta se o Context que recebeu
     * e uma android/app/Activity; se nao for, ele chama UnityPlayer.kill() no
     * primeiro nativeRender e o port volta para a gamelist sem nenhum erro
     * (exception=0 fatal=0). Nossa JVM sintetica entrega o contexto como
     * android/content/Context, entao mapeamos a cadeia Activity/Context aqui. */
    if(strstr(child,"Activity")&&(!strcmp(parent,"android/app/Activity")||!strcmp(parent,"android/content/Context")
        ||!strcmp(parent,"android/app/Application")||!strcmp(parent,"android/content/ContextWrapper")
        ||!strcmp(parent,"android/os/Service")||!strcmp(parent,"android/app/Service")))return 1;
    if((strstr(child,"Context")||strstr(child,"Activity")||strstr(child,"Application"))&&
        (!strcmp(parent,"android/content/Context")||!strcmp(parent,"android/content/ContextWrapper")))return 1;
    if(!strcmp(child,"android/app/Activity")&&(!strcmp(parent,"android/content/Context")||!strcmp(parent,"android/content/ContextWrapper")
        ||!strcmp(parent,"android/app/Application")))return 1;
    return 0;
}
static bjboolean jni_IsInstanceOf(BEnv*env,void*obj,void*clazz){
    (void)env;if(!obj||!clazz){logmsg("[BENDER_JNI][INSTANCE_OF] obj=%p target=%s -> 0 reason=null\
",obj,bender_jni_class_name(clazz));return 0;}
    char obuf[1024];const char*oc=bender_jni_runtime_class(obj,obuf,sizeof(obuf));const char*tc=bender_jni_class_name(clazz);int yes=bender_jni_class_is_a(oc,tc);
    logmsg("[BENDER_JNI][INSTANCE_OF] obj=%p objClass=%s target=%s -> %d\
",obj,oc,tc,yes);return (bjboolean)(yes?1:0);
}
static bjboolean jni_IsAssignableFrom(BEnv*env,void*clazz1,void*clazz2){
    (void)env;const char*c1=bender_jni_class_name(clazz1);const char*c2=bender_jni_class_name(clazz2);int yes=bender_jni_class_is_a(c1,c2);
    logmsg("[BENDER_JNI][ASSIGNABLE_FROM] child=%s parent=%s -> %d\
",c1,c2,yes);return (bjboolean)(yes?1:0);
}
static void *jni_GetObjectClass(BEnv*env,void*o){
    if(!o)return jni_FindClass(env,"java/lang/Object");
    BObj*bo=bender_known_bobj(o);
    if(!bo)return jni_FindClass(env,"java/lang/Object");
    if(bo->kind==1)return jni_FindClass(env,"java/lang/Class");
    if(bo->kind==3)return jni_FindClass(env,"java/lang/String");
    if(bo->kind==4&&bo->a[0]){
        char cn[1024];bender_strlcpy(cn,bo->a,sizeof(cn));
        size_t n=strlen(cn);
        if(n>2&&cn[0]=='L'&&cn[n-1]==';'){memmove(cn,cn+1,n-2);cn[n-2]=0;}
        BObj*cls=(BObj*)jni_FindClass(env,cn);
        logmsg("[BENDER_JNI][OBJECT_CLASS] obj=%p object_type=%s -> class=%p class_name=%s\n",o,bo->a,(void*)cls,cn);
        return cls;
    }
    return jni_FindClass(env,"java/lang/Object");
}
static void *jni_GetMethodID(BEnv*env,void*cls,const char*name,const char*sig){(void)env;set_checkpoint("JNI GetMethodID class=%s name=%s sig=%s",bender_jni_class_name(cls),name?name:"(null)",sig?sig:"(null)");BObj*o=bender_jni_intern_method(cls,name,sig,0,"GetMethodID");logmsg("[BENDER_JNI] GetMethodID class=%s cls=%p %s %s -> %p\n",bender_jni_class_name(cls),cls,name?name:"",sig?sig:"",(void*)o);return o;}
static void *jni_GetStaticMethodID(BEnv*env,void*cls,const char*name,const char*sig){(void)env;set_checkpoint("JNI GetStaticMethodID class=%s name=%s sig=%s",bender_jni_class_name(cls),name?name:"(null)",sig?sig:"(null)");BObj*o=bender_jni_intern_method(cls,name,sig,1,"GetStaticMethodID");logmsg("[BENDER_JNI] GetStaticMethodID class=%s cls=%p %s %s -> %p\n",bender_jni_class_name(cls),cls,name?name:"",sig?sig:"",(void*)o);return o;}
static void *jni_GetFieldID(BEnv*env,void*cls,const char*name,const char*sig){(void)env;(void)cls;BObj*o=new_obj(2,name,sig);logmsg("[BENDER_JNI] GetFieldID %s %s -> %p\n",name?name:"",sig?sig:"",(void*)o);return o;}
static void *jni_GetStaticFieldID(BEnv*env,void*cls,const char*name,const char*sig){(void)env;BObj*o=new_obj(2,name,sig);if(o){o->ref0=(uintptr_t)cls;o->ref1=1u;}logmsg("[BENDER_JNI] GetStaticFieldID class=%s %s %s -> %p\n",bender_jni_class_name(cls),name?name:"",sig?sig:"",(void*)o);return o;}
static uint32_t bender_utf8_decode_one(const unsigned char **pp){
    const unsigned char*p=*pp;uint32_t cp=0xFFFDu;if(!p||!*p)return 0;
    if(p[0]<0x80u){cp=p[0];p+=1;}
    else if((p[0]&0xE0u)==0xC0u&&p[1]){cp=((uint32_t)(p[0]&0x1Fu)<<6)|(uint32_t)(p[1]&0x3Fu);p+=2;}
    else if((p[0]&0xF0u)==0xE0u&&p[1]&&p[2]){cp=((uint32_t)(p[0]&0x0Fu)<<12)|((uint32_t)(p[1]&0x3Fu)<<6)|(uint32_t)(p[2]&0x3Fu);p+=3;}
    else if((p[0]&0xF8u)==0xF0u&&p[1]&&p[2]&&p[3]){cp=((uint32_t)(p[0]&0x07u)<<18)|((uint32_t)(p[1]&0x3Fu)<<12)|((uint32_t)(p[2]&0x3Fu)<<6)|(uint32_t)(p[3]&0x3Fu);p+=4;}
    else p+=1;
    *pp=p;return cp;
}
static bjsize bender_utf8_utf16_len(const char*s){if(!s)return 0;const unsigned char*p=(const unsigned char*)s;bjsize n=0;while(*p){uint32_t cp=bender_utf8_decode_one(&p);n+=(cp>0xFFFFu)?2:1;}return n;}
static bjsize bender_utf8_to_utf16(const char*s,bjchar*out,bjsize cap){if(!s||!out||cap<=0)return 0;const unsigned char*p=(const unsigned char*)s;bjsize n=0;while(*p&&n<cap){uint32_t cp=bender_utf8_decode_one(&p);if(cp<=0xFFFFu){out[n++]=(bjchar)cp;}else{cp-=0x10000u;if(n+2>cap)break;out[n++]=(bjchar)(0xD800u+(cp>>10));out[n++]=(bjchar)(0xDC00u+(cp&0x3FFu));}}return n;}
static size_t bender_utf16_to_utf8(const bjchar*in,bjsize len,char*out,size_t cap){size_t n=0;if(!in||!out||cap==0)return 0;for(bjsize i=0;i<len;i++){uint32_t cp=in[i];if(cp>=0xD800u&&cp<=0xDBFFu&&i+1<len){uint32_t lo=in[i+1];if(lo>=0xDC00u&&lo<=0xDFFFu){cp=0x10000u+((cp-0xD800u)<<10)+(lo-0xDC00u);i++;}}unsigned char tmp[4];size_t k=0;if(cp<0x80u){tmp[k++]=(unsigned char)cp;}else if(cp<0x800u){tmp[k++]=(unsigned char)(0xC0u|(cp>>6));tmp[k++]=(unsigned char)(0x80u|(cp&0x3Fu));}else if(cp<0x10000u){tmp[k++]=(unsigned char)(0xE0u|(cp>>12));tmp[k++]=(unsigned char)(0x80u|((cp>>6)&0x3Fu));tmp[k++]=(unsigned char)(0x80u|(cp&0x3Fu));}else{tmp[k++]=(unsigned char)(0xF0u|(cp>>18));tmp[k++]=(unsigned char)(0x80u|((cp>>12)&0x3Fu));tmp[k++]=(unsigned char)(0x80u|((cp>>6)&0x3Fu));tmp[k++]=(unsigned char)(0x80u|(cp&0x3Fu));}if(n+k>=cap)break;for(size_t j=0;j<k;j++)out[n++]=(char)tmp[j];}out[n]=0;return n;}
static void *jni_NewStringUTF(BEnv*env,const char*s){(void)env;BObj*o=new_obj(3,s?s:"",NULL);if(o)logmsg("[BENDER_JNI_STRING][NEW_UTF8] obj=%p utf8_len=%zu utf16_len=%d text=%.96s\n",(void*)o,s?strlen(s):0u,(int)bender_utf8_utf16_len(s?s:""),s?s:"");return o;}
static void *jni_NewString(BEnv*env,const bjchar*chars,bjsize len){(void)env;if(len<0||(!chars&&len)){g_jni_exception=1;return NULL;}char tmp[1024];bender_utf16_to_utf8(chars,len,tmp,sizeof(tmp));BObj*o=new_obj(3,tmp,NULL);if(o&&!o->u16){o->u16_len=len;if(len>0){o->u16=(bjchar*)calloc((size_t)len+1u,sizeof(bjchar));if(o->u16)memcpy(o->u16,chars,(size_t)len*sizeof(bjchar));}}logmsg("[BENDER_JNI_STRING][NEW_UTF16] obj=%p utf16_len=%d utf8=%.96s\n",(void*)o,(int)len,tmp);return o;}
static bjsize jni_GetStringLength(BEnv*env,void*str){(void)env;BObj*o=(BObj*)str;bjsize n=(o&&o->kind==3)?bender_utf8_utf16_len(o->a):0;++g_jni_call_count;set_checkpoint("JNI GetStringLength str=%p len=%d",str,(int)n);logmsg("[BENDER_JNI_STRING][LENGTH_UTF16] obj=%p len=%d text=%.96s\n",str,(int)n,(o&&o->kind==3)?o->a:"");return n;}
static const bjchar *jni_GetStringChars(BEnv*env,void*str,bjboolean*copy){(void)env;if(copy)*copy=0;BObj*o=(BObj*)str;if(!o||o->kind!=3)return NULL;if(!o->u16){bjsize n=bender_utf8_utf16_len(o->a);o->u16=(bjchar*)calloc((size_t)n+1u,sizeof(bjchar));if(!o->u16)return NULL;o->u16_len=bender_utf8_to_utf16(o->a,o->u16,n);if(copy)*copy=1;}++g_jni_call_count;set_checkpoint("JNI GetStringChars str=%p len=%d",str,(int)o->u16_len);logmsg("[BENDER_JNI_STRING][CHARS_UTF16] obj=%p len=%d ptr=%p text=%.96s\n",str,(int)o->u16_len,(void*)o->u16,o->a);return o->u16;}
static void jni_ReleaseStringChars(BEnv*env,void*str,const bjchar*chars){(void)env;(void)str;(void)chars;++g_jni_call_count;}
static void jni_GetStringRegion(BEnv*env,void*str,bjsize start,bjsize len,bjchar*buf){if(!buf){g_jni_exception=1;return;}const bjchar*src=jni_GetStringChars(env,str,NULL);bjsize total=jni_GetStringLength(env,str);if(!src||start<0||len<0||start>total||len>total-start){g_jni_exception=1;return;}memcpy(buf,src+(size_t)start,(size_t)len*sizeof(bjchar));logmsg("[BENDER_JNI_STRING][REGION_UTF16] obj=%p start=%d len=%d\n",str,(int)start,(int)len);}
static void jni_GetStringUTFRegion(BEnv*env,void*str,bjsize start,bjsize len,char*buf){if(!buf){g_jni_exception=1;return;}const bjchar*src=jni_GetStringChars(env,str,NULL);bjsize total=jni_GetStringLength(env,str);if(!src||start<0||len<0||start>total||len>total-start){g_jni_exception=1;return;}bender_utf16_to_utf8(src+(size_t)start,len,buf,(size_t)len*4u+1u);logmsg("[BENDER_JNI_STRING][REGION_UTF8] obj=%p start=%d len=%d\n",str,(int)start,(int)len);}
static const bjchar *jni_GetStringCritical(BEnv*env,void*str,bjboolean*copy){return jni_GetStringChars(env,str,copy);}
static void jni_ReleaseStringCritical(BEnv*env,void*str,const bjchar*chars){jni_ReleaseStringChars(env,str,chars);}
static BArray *jni_array_cast(void *obj){BArray*a=(BArray*)obj;return(a&&a->magic==BENDER_JNI_ARRAY_MAGIC)?a:NULL;}
static BObjArray *jni_obj_array_cast(void *obj){BObjArray*a=(BObjArray*)obj;return(a&&a->magic==BENDER_JNI_OBJ_ARRAY_MAGIC)?a:NULL;}
static BObjArray *jni_NewObjectArray(BEnv*env,bjsize len,void*elementClass,void*initialElement){
    (void)env;if(len<0){g_jni_exception=1;logmsg("[BENDER_JNI_OBJ_ARRAY][ERROR] NewObjectArray invalid len=%d\n",(int)len);return NULL;}
    BObjArray*a=(BObjArray*)calloc(1,sizeof(*a));if(!a)return NULL;a->items=(void**)calloc((size_t)(len>0?len:1),sizeof(void*));if(!a->items){free(a);return NULL;}
    a->magic=BENDER_JNI_OBJ_ARRAY_MAGIC;a->length=len;BObj*ec=(BObj*)elementClass;if(ec&&ec->a[0])bender_strlcpy(a->elem_sig,ec->a,sizeof(a->elem_sig));else bender_strlcpy(a->elem_sig,"java/lang/Object",sizeof(a->elem_sig));
    for(bjsize i=0;i<len;i++){a->items[i]=initialElement;}++g_jni_call_count;set_checkpoint("JNI NewObjectArray len=%d element=%s",(int)len,a->elem_sig);logmsg("[BENDER_JNI_OBJ_ARRAY][NEW] array=%p len=%d element=%s initial=%p\n",(void*)a,(int)len,a->elem_sig,initialElement);return a;}
static void *jni_GetObjectArrayElement(BEnv*env,void*array,bjsize index){(void)env;BObjArray*a=jni_obj_array_cast(array);if(!a||index<0||index>=a->length){g_jni_exception=1;logmsg("[BENDER_JNI_OBJ_ARRAY][ERROR] GetObjectArrayElement array=%p index=%d\n",array,(int)index);return NULL;}void*o=a->items[index];++g_jni_call_count;set_checkpoint("JNI GetObjectArrayElement index=%d/%d",(int)index,(int)a->length);logmsg("[BENDER_JNI_OBJ_ARRAY][GET] array=%p index=%d -> %p\n",array,(int)index,o);return o;}
static void jni_SetObjectArrayElement(BEnv*env,void*array,bjsize index,void*value){(void)env;BObjArray*a=jni_obj_array_cast(array);if(!a||index<0||index>=a->length){g_jni_exception=1;logmsg("[BENDER_JNI_OBJ_ARRAY][ERROR] SetObjectArrayElement array=%p index=%d\n",array,(int)index);return;}a->items[index]=value;++g_jni_call_count;set_checkpoint("JNI SetObjectArrayElement index=%d/%d",(int)index,(int)a->length);logmsg("[BENDER_JNI_OBJ_ARRAY][SET] array=%p index=%d value=%p\n",array,(int)index,value);}
static BObjArray *jni_new_display_modes(BEnv*env){BObj*modeClass=new_obj(1,"android/view/Display$Mode",NULL);BObj*mode=new_obj(4,"android/view/Display$Mode","modeId=1;640x480@60");BObjArray*a=jni_NewObjectArray(env,1,modeClass,mode);logmsg("[BENDER_JNI][DISPLAY_MODES] array=%p mode=%p size=640x480 refresh=60\n",(void*)a,(void*)mode);return a;}
static BArray *jni_new_primitive_array(BEnv*env,BArrayKind kind,bjsize len,size_t elem,const char*name){(void)env;if(len<0||elem==0){g_jni_exception=1;logmsg("[BENDER_JNI_ARRAY][ERROR] %s invalid len=%d elem=%zu\n",name?name:"array",(int)len,elem);return NULL;}size_t n=(size_t)len;if(n>SIZE_MAX/elem||n*elem>(64u*1024u*1024u)){g_jni_exception=1;logmsg("[BENDER_JNI_ARRAY][ERROR] %s excessive len=%d elem=%zu\n",name?name:"array",(int)len,elem);return NULL;}BArray*a=(BArray*)calloc(1,sizeof(*a));if(!a)return NULL;size_t bytes=n*elem;a->data=(unsigned char*)calloc(bytes?bytes:1u,1u);if(!a->data){free(a);return NULL;}a->magic=BENDER_JNI_ARRAY_MAGIC;a->kind=(uint32_t)kind;a->elem_size=(uint32_t)elem;a->length=len;++g_jni_call_count;set_checkpoint("JNI %s len=%d bytes=%zu",name?name:"NewArray",(int)len,bytes);logmsg("[BENDER_JNI_ARRAY][NEW] %s array=%p data=%p len=%d elem=%zu bytes=%zu\n",name?name:"NewArray",(void*)a,(void*)a->data,(int)len,elem,bytes);return a;}
static bjsize jni_GetArrayLength(BEnv*env,void*array){(void)env;BArray*a=jni_array_cast(array);BObjArray*oa=jni_obj_array_cast(array);bjsize n=a?a->length:(oa?oa->length:0);++g_jni_call_count;set_checkpoint("JNI GetArrayLength array=%p len=%d",array,(int)n);logmsg("[BENDER_JNI_ARRAY][LENGTH] array=%p len=%d primitive=%d object=%d\n",array,(int)n,a?1:0,oa?1:0);return n;}
static void *jni_NewBooleanArray(BEnv*env,bjsize len){return jni_new_primitive_array(env,BARR_BOOLEAN,len,sizeof(uint8_t),"NewBooleanArray");}
static void *jni_NewByteArray(BEnv*env,bjsize len){return jni_new_primitive_array(env,BARR_BYTE,len,sizeof(int8_t),"NewByteArray");}
static void *jni_NewCharArray(BEnv*env,bjsize len){return jni_new_primitive_array(env,BARR_CHAR,len,sizeof(uint16_t),"NewCharArray");}
static void *jni_NewShortArray(BEnv*env,bjsize len){return jni_new_primitive_array(env,BARR_SHORT,len,sizeof(int16_t),"NewShortArray");}
static void *jni_NewIntArray(BEnv*env,bjsize len){return jni_new_primitive_array(env,BARR_INT,len,sizeof(bjint),"NewIntArray");}
static void *jni_NewLongArray(BEnv*env,bjsize len){return jni_new_primitive_array(env,BARR_LONG,len,sizeof(bjlong),"NewLongArray");}
static void *jni_NewFloatArray(BEnv*env,bjsize len){return jni_new_primitive_array(env,BARR_FLOAT,len,sizeof(bjfloat),"NewFloatArray");}
static void *jni_NewDoubleArray(BEnv*env,bjsize len){return jni_new_primitive_array(env,BARR_DOUBLE,len,sizeof(bjdouble),"NewDoubleArray");}
static void *jni_array_elements(BEnv*env,void*array,bjboolean*isCopy,BArrayKind kind,const char*name){(void)env;BArray*a=jni_array_cast(array);if(isCopy)*isCopy=0;if(!a||a->kind!=(uint32_t)kind){g_jni_exception=1;set_checkpoint("JNI %s invalid array=%p",name?name:"GetArrayElements",array);logmsg("[BENDER_JNI_ARRAY][ERROR] %s invalid array=%p kind=%u expected=%u\n",name?name:"GetArrayElements",array,a?a->kind:0u,(unsigned)kind);return NULL;}++g_jni_call_count;set_checkpoint("JNI %s array=%p data=%p len=%d",name?name:"GetArrayElements",array,(void*)a->data,(int)a->length);logmsg("[BENDER_JNI_ARRAY][ELEMENTS] %s array=%p data=%p len=%d\n",name?name:"GetArrayElements",array,(void*)a->data,(int)a->length);return a->data;}
static uint8_t *jni_GetBooleanArrayElements(BEnv*env,void*a,bjboolean*c){return(uint8_t*)jni_array_elements(env,a,c,BARR_BOOLEAN,"GetBooleanArrayElements");}
static int8_t *jni_GetByteArrayElements(BEnv*env,void*a,bjboolean*c){return(int8_t*)jni_array_elements(env,a,c,BARR_BYTE,"GetByteArrayElements");}
static uint16_t *jni_GetCharArrayElements(BEnv*env,void*a,bjboolean*c){return(uint16_t*)jni_array_elements(env,a,c,BARR_CHAR,"GetCharArrayElements");}
static int16_t *jni_GetShortArrayElements(BEnv*env,void*a,bjboolean*c){return(int16_t*)jni_array_elements(env,a,c,BARR_SHORT,"GetShortArrayElements");}
static bjint *jni_GetIntArrayElements(BEnv*env,void*a,bjboolean*c){return(bjint*)jni_array_elements(env,a,c,BARR_INT,"GetIntArrayElements");}
static bjlong *jni_GetLongArrayElements(BEnv*env,void*a,bjboolean*c){return(bjlong*)jni_array_elements(env,a,c,BARR_LONG,"GetLongArrayElements");}
static bjfloat *jni_GetFloatArrayElements(BEnv*env,void*a,bjboolean*c){return(bjfloat*)jni_array_elements(env,a,c,BARR_FLOAT,"GetFloatArrayElements");}
static bjdouble *jni_GetDoubleArrayElements(BEnv*env,void*a,bjboolean*c){return(bjdouble*)jni_array_elements(env,a,c,BARR_DOUBLE,"GetDoubleArrayElements");}
static void jni_release_array_elements(BEnv*env,void*array,void*elems,bjint mode,const char*name){(void)env;BArray*a=jni_array_cast(array);++g_jni_call_count;set_checkpoint("JNI %s array=%p elems=%p mode=%d",name?name:"ReleaseArrayElements",array,elems,(int)mode);logmsg("[BENDER_JNI_ARRAY][RELEASE] %s array=%p elems=%p mode=%d valid=%d\n",name?name:"ReleaseArrayElements",array,elems,(int)mode,a?1:0);}
#define DEF_RELEASE_ARRAY(fn) static void jni_Release##fn##ArrayElements(BEnv*env,void*a,void*e,bjint m){jni_release_array_elements(env,a,e,m,"Release" #fn "ArrayElements");}
DEF_RELEASE_ARRAY(Boolean) DEF_RELEASE_ARRAY(Byte) DEF_RELEASE_ARRAY(Char) DEF_RELEASE_ARRAY(Short) DEF_RELEASE_ARRAY(Int) DEF_RELEASE_ARRAY(Long) DEF_RELEASE_ARRAY(Float) DEF_RELEASE_ARRAY(Double)
#undef DEF_RELEASE_ARRAY
static int jni_array_region_check(BArray*a,bjsize start,bjsize len,size_t elem){if(!a||start<0||len<0)return 0;uint64_t s=(uint64_t)(uint32_t)start,l=(uint64_t)(uint32_t)len,n=(uint64_t)(uint32_t)a->length;if(s>n||l>n-s||a->elem_size!=elem)return 0;return 1;}
static void jni_get_array_region(BEnv*env,void*array,bjsize start,bjsize len,void*buf,size_t elem,BArrayKind kind,const char*name){(void)env;BArray*a=jni_array_cast(array);if(!buf||!jni_array_region_check(a,start,len,elem)||a->kind!=(uint32_t)kind){g_jni_exception=1;logmsg("[BENDER_JNI_ARRAY][ERROR] %s array=%p start=%d len=%d\n",name,array,(int)start,(int)len);return;}memcpy(buf,a->data+(size_t)start*elem,(size_t)len*elem);++g_jni_call_count;}
static void jni_set_array_region(BEnv*env,void*array,bjsize start,bjsize len,const void*buf,size_t elem,BArrayKind kind,const char*name){(void)env;BArray*a=jni_array_cast(array);if(!buf||!jni_array_region_check(a,start,len,elem)||a->kind!=(uint32_t)kind){g_jni_exception=1;logmsg("[BENDER_JNI_ARRAY][ERROR] %s array=%p start=%d len=%d\n",name,array,(int)start,(int)len);return;}memcpy(a->data+(size_t)start*elem,buf,(size_t)len*elem);++g_jni_call_count;}
#define DEF_REGION(T,ctype,kind) static void jni_Get##T##ArrayRegion(BEnv*e,void*a,bjsize s,bjsize l,ctype*b){jni_get_array_region(e,a,s,l,b,sizeof(ctype),kind,"Get" #T "ArrayRegion");} static void jni_Set##T##ArrayRegion(BEnv*e,void*a,bjsize s,bjsize l,const ctype*b){jni_set_array_region(e,a,s,l,b,sizeof(ctype),kind,"Set" #T "ArrayRegion");}
DEF_REGION(Boolean,uint8_t,BARR_BOOLEAN) DEF_REGION(Byte,int8_t,BARR_BYTE) DEF_REGION(Char,uint16_t,BARR_CHAR) DEF_REGION(Short,int16_t,BARR_SHORT) DEF_REGION(Int,bjint,BARR_INT) DEF_REGION(Long,bjlong,BARR_LONG) DEF_REGION(Float,bjfloat,BARR_FLOAT) DEF_REGION(Double,bjdouble,BARR_DOUBLE)
#undef DEF_REGION
static const char *jni_ret_sig(const BObj*m){if(!m)return "";const char*p=strrchr(m->b,')');return p?p+1:"";}
static int bender_jni_ctor_needs_two_args(void*cls,BObj*m){const char*cn=bender_jni_class_name(cls);if(cn&&!strcmp(cn,"java/lang/String")&&m&&m->b[0]&&!strcmp(m->b,"([BLjava/lang/String;)V"))return 1;if(m&&m->b[0]&&!strcmp(m->b,"(Landroid/os/Looper;Landroid/os/Handler$Callback;)V"))return 1;if(cn&&!strcmp(cn,"com/google/unity/ump/UnityConsentForm"))return 1;return 0;}
static void *jni_new_object_common(BEnv*env,void*cls,void*mid,const char*variant,void*arg0,void*arg1){(void)env;BObj*m=(BObj*)mid;++g_jni_call_count;const char*cn=bender_jni_class_name(cls);logmsg("[BENDER_JNI][CALL] NewObject%s cls=%p class=%s method=%s sig=%s\n",variant?variant:"",cls,cn?cn:"?",m?m->a:"?",m?m->b:"?");if(cn&&!strcmp(cn,"java/lang/String")&&m&&m->b[0]&&!strcmp(m->b,"([BLjava/lang/String;)V")){BArray*ba=jni_array_cast(arg0);if(ba&&ba->kind==BARR_BYTE&&ba->elem_size==1u){size_t n=(size_t)(ba->length>0?ba->length:0);char*tmp=(char*)calloc(n+1u,1u);if(!tmp)return NULL;if(n)memcpy(tmp,ba->data,n);void*js=jni_NewStringUTF(env,tmp);logmsg("[BENDER_JAVA_VALUE][STRING_FROM_BYTES] len=%zu charset=%s text=%.96s\n",n,(arg1&&((BObj*)arg1)->kind==3)?((BObj*)arg1)->a:"UTF-8",tmp);free(tmp);return js;}}if(m&&m->b[0]&&!strcmp(m->b,"(Landroid/os/Looper;Landroid/os/Handler$Callback;)V")){BObj*h=new_obj(4,"android/os/Handler","main-handler");if(h){h->ref0=(uintptr_t)arg0;h->ref1=(uintptr_t)arg1;}logmsg("[BENDER_JNI][HANDLER] new handler=%p looper=%p callback=%p\n",(void*)h,arg0,arg1);return h;}if(cn&&!strcmp(cn,"com/google/unity/ump/UnityConsentForm")){
        void*held_cb=arg1?jni_NewGlobalRef(env,arg1):NULL;
        BObj*f=new_obj(4,"com/google/unity/ump/UnityConsentForm","offline-not-required");
        if(f){f->ref0=(uintptr_t)arg0;f->ref1=(uintptr_t)held_cb;f->flags|=UINT32_C(0x20000000);}
        else if(held_cb)jni_DeleteGlobalRef(env,held_cb);
        logmsg("[BENDER_UMP][FORM_NEW] source=fix108 form=%p activity=%p callback_local=%p callback_held=%p policy=offline-not-required semantic=Java-field-retains-callback code_patch=0 forced_completion=0\n",(void*)f,arg0,arg1,held_cb);
        if(held_cb)logmsg("[BENDER_UMP][FORM_CALLBACK_HOLD] source=fix108 form=%p callback=%p ownership=JNI-global-ref semantic=UnityConsentForm-callback-field code_patch=0 forced_completion=0\n",(void*)f,held_cb);
        return f;
    }return cls?cls:new_obj(4,"java/lang/Object",NULL);}
static int bender_is_firebase_result_ctor_2757(void*cls,BObj*m){
    const char*cn=bender_jni_class_name(cls);
    return cn&&m&&!strcmp(cn,"com/google/firebase/app/internal/cpp/JniResultCallback")&&!strcmp(m->a,"<init>")&&!strcmp(m->b,"(Lcom/google/android/gms/tasks/Task;JJ)V");
}
static void*bender_firebase_result_ctor_2757(BEnv*env,void*cls,void*mid,const char*variant,void*task_obj,bjlong callback_fn,bjlong callback_data){
    (void)cls;(void)mid;
    BObj*task=bender_known_bobj(task_obj);
    BObj*cb=new_obj(4,"com/google/firebase/app/internal/cpp/JniResultCallback","completed-by-bender");
    if(cb){cb->ref0=(uintptr_t)task_obj;cb->ref1=(uintptr_t)callback_fn;cb->i64=(int64_t)callback_data;}
    const char*source=(task&&task->kind==4&&task->b[0])?task->b:"unknown-task";
    unsigned flags=(task&&task->kind==4)?task->flags:2u;
    bjboolean success=(bjboolean)(flags==1u);
    bjboolean cancelled=(bjboolean)(flags==3u);
    void*result=(task&&success)?(void*)task->ref0:NULL;
    void*message=NULL;
    if(!success&&!cancelled){
        char msg[256];snprintf(msg,sizeof(msg),"BENDER R36S: Android Google Task unavailable (%.160s)",source);
        message=jni_NewStringUTF(env,msg);
    }
    logmsg("[BENDER_FIREBASE_TASK][JNICALLBACK] variant=%s source=%s task=%p callback=%p native=%p callbackFn=0x%llx callbackData=0x%llx success=%u cancelled=%u result=%p\n",
           variant?variant:"",source,task_obj,(void*)cb,(void*)g_firebase_jni_result_fn_2757,
           (unsigned long long)(uint64_t)callback_fn,(unsigned long long)(uint64_t)callback_data,
           (unsigned)success,(unsigned)cancelled,result);
    if(g_firebase_jni_result_fn_2757){
        int saved_exc=g_jni_exception;void*saved_obj=g_jni_exception_obj;g_jni_exception=0;g_jni_exception_obj=NULL;
        g_firebase_jni_result_fn_2757(env,cb,result,success,cancelled,message,callback_fn,callback_data);
        int cb_exc=g_jni_exception;g_jni_exception=saved_exc;g_jni_exception_obj=saved_obj;
        ++g_firebase_task_completed_2757;if(!success)++g_firebase_task_failed_2757;
        logmsg("[BENDER_FIREBASE_TASK][COMPLETE] n=%lu source=%s success=%u cancelled=%u callback_exception=%d outer_exception=%d\n",
               g_firebase_task_completed_2757,source,(unsigned)success,(unsigned)cancelled,cb_exc,g_jni_exception);
    }else{
        logmsg("[BENDER_FIREBASE_TASK][COMPLETE][DEFERRED] source=%s reason=nativeOnResult-not-captured\n",source);
    }
    return cb?cb:(cls?cls:new_obj(4,"java/lang/Object",NULL));
}
static void *jni_NewObject(BEnv*env,void*cls,void*mid,...){BObj*m=(BObj*)mid;void*a0=NULL,*a1=NULL;va_list ap;va_start(ap,mid);if(bender_is_firebase_result_ctor_2757(cls,m)){void*task=va_arg(ap,void*);bjlong fn=va_arg(ap,bjlong),data=va_arg(ap,bjlong);va_end(ap);return bender_firebase_result_ctor_2757(env,cls,mid,"",task,fn,data);}if(bender_jni_ctor_needs_two_args(cls,m)){a0=va_arg(ap,void*);a1=va_arg(ap,void*);}va_end(ap);return jni_new_object_common(env,cls,mid,"",a0,a1);}
static void *jni_NewObjectV(BEnv*env,void*cls,void*mid,va_list ap){BObj*m=(BObj*)mid;void*a0=NULL,*a1=NULL;va_list cp;va_copy(cp,ap);if(bender_is_firebase_result_ctor_2757(cls,m)){void*task=va_arg(cp,void*);bjlong fn=va_arg(cp,bjlong),data=va_arg(cp,bjlong);va_end(cp);return bender_firebase_result_ctor_2757(env,cls,mid,"V",task,fn,data);}if(bender_jni_ctor_needs_two_args(cls,m)){a0=va_arg(cp,void*);a1=va_arg(cp,void*);}va_end(cp);return jni_new_object_common(env,cls,mid,"V",a0,a1);}
static void *jni_NewObjectA(BEnv*env,void*cls,void*mid,const void*args){BObj*m=(BObj*)mid;void*a0=NULL,*a1=NULL;if(args&&bender_is_firebase_result_ctor_2757(cls,m)){const uint64_t*v=(const uint64_t*)args;return bender_firebase_result_ctor_2757(env,cls,mid,"A",(void*)(uintptr_t)v[0],(bjlong)(int64_t)v[1],(bjlong)(int64_t)v[2]);}if(args&&bender_jni_ctor_needs_two_args(cls,m)){const uint64_t*v=(const uint64_t*)args;a0=(void*)(uintptr_t)v[0];a1=(void*)(uintptr_t)v[1];}return jni_new_object_common(env,cls,mid,"A",a0,a1);}
static bjsize jni_GetStringUTFLength(BEnv*env,void*str){(void)env;BObj*o=(BObj*)str;return(o&&o->kind==3)?(bjsize)strlen(o->a):0;}
static const char *jni_GetStringUTFChars(BEnv*env,void*str,bjboolean*copy){(void)env;if(copy)*copy=0;BObj*o=(BObj*)str;return(o&&o->kind==3)?o->a:"";}
static void jni_ReleaseStringUTFChars(BEnv*env,void*str,const char*c){(void)env;(void)str;(void)c;}
typedef bjint(*BenderFmodGetInfoFnFix5)(BEnv*,void*,bjint);
typedef bjint(*BenderFmodProcessFnFix5)(BEnv*,void*,void*);
static BenderFmodGetInfoFnFix5 g_bender_fmod_get_info_fn_fix5;
static BenderFmodProcessFnFix5 g_bender_fmod_process_fn_fix5;
static void*g_bender_fmod_class_fix5;
static volatile int g_bender_fmod_running_fix5;
static volatile int g_bender_fmod_stop_fix5;
static volatile int g_bender_fmod_start_requested_fix5;
static pthread_t g_bender_fmod_thread_fix5;
static unsigned long g_bender_fmod_blocks_fix5;
static unsigned long g_bender_fmod_bytes_fix5;
static unsigned long g_bender_fmod_empty_fix5;
static void*bender_fmod_thread_main_fix5(void*unused);
static int bender_fmod_audio_start_fix5(const char*reason);
static void bender_fmod_audio_stop_fix5(const char*reason);
static void *jni_NewDirectByteBuffer(BEnv*env,void*address,bjlong capacity){(void)env;if(!address||capacity<0)return NULL;BObj*o=new_obj(4,"java/nio/DirectByteBuffer","direct");if(o){o->ref0=(uintptr_t)address;o->i64=(int64_t)capacity;}logmsg("[BENDER_JNI_DIRECT_BUFFER][NEW] obj=%p address=%p capacity=%lld\n",(void*)o,address,(long long)capacity);return o;}
static void *jni_GetDirectBufferAddress(BEnv*env,void*buffer){(void)env;BObj*o=bender_known_bobj(buffer);void*p=(o&&o->kind==4&&!strcmp(o->a,"java/nio/DirectByteBuffer"))?(void*)o->ref0:NULL;return p;}
static bjlong jni_GetDirectBufferCapacity(BEnv*env,void*buffer){(void)env;BObj*o=bender_known_bobj(buffer);return(o&&o->kind==4&&!strcmp(o->a,"java/nio/DirectByteBuffer"))?(bjlong)o->i64:(bjlong)-1;}
typedef void(*BenderUnitySendMessageFn2758)(BEnv*,void*,void*,void*,void*);
static BenderUnitySendMessageFn2758 g_unity_native_send_message_fn_2758;
static void *g_clevertap_plugin_instance_2758;
static unsigned long g_clevertap_unity_message_count_2758;
static int g_clevertap_bootstrap_sent_2758;
/* BENDER FIX99 - Unity TouchScreenKeyboard/native soft-input bridge. */
typedef void (*BenderUnitySetInputStringFnFix99)(BEnv*,void*,void*);
typedef void (*BenderUnitySetInputSelectionFnFix99)(BEnv*,void*,bjint,bjint);
typedef void (*BenderUnitySetKeyboardVisibleFnFix99)(BEnv*,void*,bjboolean);
typedef void (*BenderUnitySoftInputClosedFnFix99)(BEnv*,void*);
static BenderUnitySetInputStringFnFix99 g_bender_set_input_string_fix99;
static BenderUnitySetInputSelectionFnFix99 g_bender_set_input_selection_fix99;
static BenderUnitySetKeyboardVisibleFnFix99 g_bender_set_keyboard_visible_fix99;
static BenderUnitySoftInputClosedFnFix99 g_bender_soft_input_closed_fix99;
static void *g_bender_soft_input_owner_fix99;
/* BENDER FIX100 - private pooled jstring + reusable UTF-8 byte[] for Unity soft input. */
static BObj *g_bender_soft_jstring_fix100;
static BArray *g_bender_soft_utf8_bytes_fix100;
static void *bender_soft_string_getbytes_fix100(BEnv*env,void*obj){
    BObj*s=bender_known_bobj(obj);
    if(!s||s!=g_bender_soft_jstring_fix100||s->kind!=3||strcmp(s->b,"fix100-private")!=0)return NULL;
    size_t n=strlen(s->a);if(n>767u)n=767u;
    if(!g_bender_soft_utf8_bytes_fix100){
        g_bender_soft_utf8_bytes_fix100=(BArray*)jni_NewByteArray(env,767);
        if(!g_bender_soft_utf8_bytes_fix100||!g_bender_soft_utf8_bytes_fix100->data){
            logmsg("[BENDER_SOFT_KEYBOARD] source=fix100 action=string-getBytes error=array-allocation-failed object=%p\n",obj);
            return NULL;
        }
    }
    g_bender_soft_utf8_bytes_fix100->length=(bjsize)n;
    if(n)memcpy(g_bender_soft_utf8_bytes_fix100->data,s->a,n);
    if(n<767u)g_bender_soft_utf8_bytes_fix100->data[n]=0;
    logmsg("[BENDER_SOFT_KEYBOARD] source=fix100 action=string-getBytes bytes=%zu charset=UTF-8 object=%p array=%p cached=1\n",n,obj,(void*)g_bender_soft_utf8_bytes_fix100);
    return g_bender_soft_utf8_bytes_fix100;
}
static bjint jni_RegisterNatives(BEnv*env,void*clazz,const BJNINativeMethod*methods,bjint n){(void)env;BObj*c=(BObj*)clazz;const char*cn=c?c->a:"?";logmsg("[BENDER_JNI] RegisterNatives class=%s count=%d\n",cn,n);for(int i=0;i<n;i++){const char*mn=methods[i].name?methods[i].name:"";const char*ms=methods[i].signature?methods[i].signature:"";logmsg("[BENDER_JNI][NATIVE] class=%s %s %s fn=%p\n",cn,mn,ms,methods[i].fnPtr);
 if(c&&!strcmp(c->a,"org/fmod/FMODAudioDevice")){
     g_bender_fmod_class_fix5=clazz;
     if(!strcmp(mn,"fmodGetInfo")&&!strcmp(ms,"(I)I")){g_bender_fmod_get_info_fn_fix5=(BenderFmodGetInfoFnFix5)methods[i].fnPtr;logmsg("[BENDER_FMOD_AUDIO][CAPTURE] fmodGetInfo=%p\n",methods[i].fnPtr);}
     else if(!strcmp(mn,"fmodProcess")&&!strcmp(ms,"(Ljava/nio/ByteBuffer;)I")){g_bender_fmod_process_fn_fix5=(BenderFmodProcessFnFix5)methods[i].fnPtr;logmsg("[BENDER_FMOD_AUDIO][CAPTURE] fmodProcess=%p\n",methods[i].fnPtr);if(g_bender_fmod_start_requested_fix5&&!g_bender_fmod_running_fix5)bender_fmod_audio_start_fix5("native-captured-after-start");}
 }
if(!strcmp(mn,"invoke")&&!strcmp(ms,"(JLjava/lang/Class;Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;")){g_jnibridge_invoke_fn=(BenderJNIBridgeInvokeFn)methods[i].fnPtr;g_jnibridge_class=clazz;logmsg("[BENDER_JNI][JNIBRIDGE] captured invoke=%p class=%p className=%s\n",methods[i].fnPtr,clazz,cn);}
if(c&&!strcmp(c->a,"com/unity3d/player/ReflectionHelper")&&!strcmp(mn,"nativeProxyInvoke")&&!strcmp(ms,"(JLjava/lang/String;[Ljava/lang/Object;)Ljava/lang/Object;")){g_reflection_proxy_invoke_fn=(BenderReflectionProxyInvokeFn)methods[i].fnPtr;g_reflection_helper_class=clazz;logmsg("[BENDER_JNI][REFLECTION_PROXY] captured nativeProxyInvoke=%p class=%p className=%s\n",methods[i].fnPtr,clazz,cn);}
if(c&&!strcmp(c->a,"com/unity3d/player/UnityCoreAssetPacksStatusCallbacks")&&!strcmp(mn,"nativeStatusQueryResult")&&!strcmp(ms,"(Ljava/lang/String;II)V")){g_unity_asset_pack_status_fn=(BenderUnityAssetPackStatusFn)methods[i].fnPtr;g_unity_asset_pack_status_class=clazz;logmsg("[BENDER_PAD][CAPTURE] nativeStatusQueryResult=%p class=%p\n",methods[i].fnPtr,clazz);}
if(c&&!strcmp(c->a,"com/google/firebase/app/internal/cpp/JniResultCallback")&&!strcmp(mn,"nativeOnResult")&&!strcmp(ms,"(Ljava/lang/Object;ZZLjava/lang/String;JJ)V")){g_firebase_jni_result_fn_2757=(BenderFirebaseJniResultFn2757)methods[i].fnPtr;g_firebase_jni_result_class_2757=clazz;logmsg("[BENDER_FIREBASE_TASK][CAPTURE] nativeOnResult=%p class=%p abi=Object-Z-Z-String-J-J\n",methods[i].fnPtr,clazz);}
if(c&&strcmp(c->a,"com/unity3d/player/NativeLoader")==0){if(strcmp(mn,"load")==0)g_native_load_fn=methods[i].fnPtr;if(strcmp(mn,"unload")==0)g_native_unload_fn=methods[i].fnPtr;}
/* FIX68: Unity 6 removed the trailing int from nativeInjectEvent's Java signature.
   Capture either ABI before the per-family lifecycle chain so input remains generic. */
if(c&&(!strcmp(c->a,"com/unity3d/player/UnityPlayer")||!strcmp(c->a,"com/unity3d/player/UnityPlayerForActivityOrService"))&&
   !strcmp(mn,"nativeInjectEvent")&&
   (!strcmp(ms,"(Landroid/view/InputEvent;)Z")||!strcmp(ms,"(Landroid/view/InputEvent;I)Z"))){
    g_unity_native_inject_event_fn=methods[i].fnPtr;
    g_unity_player_class=clazz;
    g_bender_unity_inject_abi_fix68=!strcmp(ms,"(Landroid/view/InputEvent;)Z")?1:2;
    logmsg("[BENDER_INPUT][HOOK_FIX68] family=%s nativeInjectEvent=%p sig=%s abi=%s class=%p code_patch=0 forced_completion=0\n",
           c->a,methods[i].fnPtr,ms,g_bender_unity_inject_abi_fix68==1?"unity6-inputevent":"legacy-inputevent-int",clazz);
}
if(c&&strcmp(c->a,"com/unity3d/player/UnityPlayer")==0){
if(strcmp(mn,"initJni")==0)g_unity_init_jni_fn=methods[i].fnPtr;
else if(strcmp(mn,"nativePause")==0){g_unity_native_pause_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayer method=nativePause fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeResume")==0){g_unity_native_resume_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayer method=nativeResume fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeDone")==0){g_unity_native_done_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayer method=nativeDone fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeRender")==0){g_unity_native_render_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayer method=nativeRender fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeRecreateGfxState")==0){g_unity_native_recreate_gfx_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayer method=nativeRecreateGfxState fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeFocusChanged")==0){g_unity_native_focus_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayer method=nativeFocusChanged fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeSendSurfaceChangedEvent")==0){g_unity_native_surface_changed_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayer method=nativeSendSurfaceChangedEvent fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeInjectEvent")==0){/* FIX68 captured above with ABI-aware signature handling. */}
else if(strcmp(mn,"nativeUnitySendMessage")==0&&!strcmp(ms,"(Ljava/lang/String;Ljava/lang/String;[B)V")){g_unity_native_send_message_fn_2758=(BenderUnitySendMessageFn2758)methods[i].fnPtr;g_unity_player_class=clazz;logmsg("[BENDER_UNITY_MESSAGE][CAPTURE] nativeUnitySendMessage=%p class=%p\n",methods[i].fnPtr,clazz);}
}
if(c&&strcmp(c->a,"com/unity3d/player/UnityPlayerForActivityOrService")==0){
if(!strcmp(mn,"nativeSetInputString")&&!strcmp(ms,"(Ljava/lang/String;)V")){g_bender_set_input_string_fix99=(BenderUnitySetInputStringFnFix99)methods[i].fnPtr;g_bender_soft_input_owner_fix99=clazz;logmsg("[BENDER_SOFT_KEYBOARD] source=fix99 action=capture method=nativeSetInputString fn=%p class=%p\n",methods[i].fnPtr,clazz);}
else if(!strcmp(mn,"nativeSetInputSelection")&&!strcmp(ms,"(II)V")){g_bender_set_input_selection_fix99=(BenderUnitySetInputSelectionFnFix99)methods[i].fnPtr;g_bender_soft_input_owner_fix99=clazz;logmsg("[BENDER_SOFT_KEYBOARD] source=fix99 action=capture method=nativeSetInputSelection fn=%p class=%p\n",methods[i].fnPtr,clazz);}
else if(!strcmp(mn,"nativeSetKeyboardIsVisible")&&!strcmp(ms,"(Z)V")){g_bender_set_keyboard_visible_fix99=(BenderUnitySetKeyboardVisibleFnFix99)methods[i].fnPtr;g_bender_soft_input_owner_fix99=clazz;logmsg("[BENDER_SOFT_KEYBOARD] source=fix99 action=capture method=nativeSetKeyboardIsVisible fn=%p class=%p\n",methods[i].fnPtr,clazz);}
else if(!strcmp(mn,"nativeSoftInputClosed")&&!strcmp(ms,"()V")){g_bender_soft_input_closed_fix99=(BenderUnitySoftInputClosedFnFix99)methods[i].fnPtr;g_bender_soft_input_owner_fix99=clazz;logmsg("[BENDER_SOFT_KEYBOARD] source=fix99 action=capture method=nativeSoftInputClosed fn=%p class=%p\n",methods[i].fnPtr,clazz);}

if(strcmp(mn,"nativePause")==0){g_unity_native_pause_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayerForActivityOrService method=nativePause fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeResume")==0){g_unity_native_resume_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayerForActivityOrService method=nativeResume fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeDone")==0){g_unity_native_done_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayerForActivityOrService method=nativeDone fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeRender")==0){g_unity_native_render_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayerForActivityOrService method=nativeRender fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeRecreateGfxState")==0){g_unity_native_recreate_gfx_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayerForActivityOrService method=nativeRecreateGfxState fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeFocusChanged")==0){g_unity_native_focus_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayerForActivityOrService method=nativeFocusChanged fn=%p\n",methods[i].fnPtr);}
else if(strcmp(mn,"nativeSendSurfaceChangedEvent")==0){g_unity_native_surface_changed_fn=methods[i].fnPtr;logmsg("[BENDER_UNITY_LIFECYCLE][CAPTURE] family=UnityPlayerForActivityOrService method=nativeSendSurfaceChangedEvent fn=%p\n",methods[i].fnPtr);}
}}return 0;}
static bjint jni_GetJavaVM(BEnv*env,BVM**out){(void)env;if(out)*out=&g_vm;return 0;}
static const char *jni_obj_string(void *o){BObj*b=(BObj*)o;return(b&&b->kind==3)?b->a:NULL;}
static const char *jni_file_path(void *o){BObj*b=(BObj*)o;return(b&&b->kind==4&&b->a[0]=='/')?b->a:NULL;}
static void *jni_new_file(BEnv*env,const char*path){(void)env;BObj*o=new_obj(4,path?path:"", "java/io/File");logmsg("[BENDER_JNI][FILE] new path=%s -> %p\n",path?path:"",(void*)o);return o;}

/* ---------------- BENDER APKS/PAD/LOCALE V2.7.50 ---------------- */
static int bender_join_parts_2749(char*out,size_t cap,const char*a,const char*b,const char*c,const char*d){
    const char*parts[4]={a,b,c,d};size_t need=1u;
    if(!out||cap==0u)return 0;
    for(int i=0;i<4;i++){if(parts[i]){size_t n=strlen(parts[i]);if(n>SIZE_MAX-need)return 0;need+=n;}}
    if(need>cap){out[0]=0;return 0;}
    size_t o=0u;
    for(int i=0;i<4;i++){if(parts[i]){size_t n=strlen(parts[i]);if(n){memcpy(out+o,parts[i],n);o+=n;}}}
    out[o]=0;return 1;
}
static int bender_path_is_dir_2749(const char*p){struct stat st;return p&&stat(p,&st)==0&&S_ISDIR(st.st_mode);}
static int bender_path_is_file_2749(const char*p){struct stat st;return p&&stat(p,&st)==0&&S_ISREG(st.st_mode);}
static int bender_dir_has_file_2749(const char*p){
    DIR*d=opendir(p);if(!d)return 0;struct dirent*de;int found=0;
    while((de=readdir(d))){if(de->d_name[0]=='.'&&(de->d_name[1]==0||(de->d_name[1]=='.'&&de->d_name[2]==0)))continue;found=1;break;}
    closedir(d);return found;
}
static int bender_safe_pack_name_2749(const char*pack){
    if(!pack||!*pack||strstr(pack,"..")||strchr(pack,'/')||strchr(pack,'\\'))return 0;
    size_t n=strnlen(pack,256u);return n>0u&&n<256u;
}
static int bender_split_apk_exists_2749(const char*pack){
    if(!bender_safe_pack_name_2749(pack))return 0;
    char dirp[1536];if(!bender_join_parts_2749(dirp,sizeof(dirp),g_guest_app_dir,"/apk_splits",NULL,NULL))return 0;
    DIR*d=opendir(dirp);if(!d)return 0;struct dirent*de;int found=0;
    char want1[320],want2[320];
    if(!bender_join_parts_2749(want1,sizeof(want1),"split_",pack,".apk",NULL) ||
       !bender_join_parts_2749(want2,sizeof(want2),pack,".apk",NULL,NULL)){closedir(d);return 0;}
    while((de=readdir(d))){
        if(!strcasecmp(de->d_name,want1)||!strcasecmp(de->d_name,want2)){found=1;break;}
    }
    closedir(d);return found;
}
static int bender_pad_pack_path_2749(const char*pack,char*out,size_t cap,const char**source){
    if(source){*source="missing";}if(out&&cap){out[0]=0;}
    if(!bender_safe_pack_name_2749(pack)||!out||!cap)return 0;
    char p[1536];
    if(bender_join_parts_2749(p,sizeof(p),g_guest_app_dir,"/unity_payload/asset_packs/",pack,"/assets") &&
       bender_path_is_dir_2749(p)&&bender_dir_has_file_2749(p)){
        bender_strlcpy(out,p,cap);if(source)*source="extracted-pack";return 1;
    }
    if(!strcmp(pack,"UnityStreamingAssetsPack")||!strcmp(pack,"UnityStreamingAssetPack")){
        const char*rels[3]={"/unity_payload/assets/StreamingAssets","/unity_payload/assets/bin/Data/StreamingAssets","/unity_payload/assets/bin/data/StreamingAssets"};
        for(int i=0;i<3;i++){
            if(bender_join_parts_2749(p,sizeof(p),g_guest_app_dir,rels[i],NULL,NULL) &&
               bender_path_is_dir_2749(p)&&bender_dir_has_file_2749(p)){
                bender_strlcpy(out,p,cap);if(source)*source="streaming-global-fallback";return 1;
            }
        }
        /* FIX11: many Unity Android games keep shipped AssetBundles directly under
         * base.apk!/assets/Bundles instead of a named UnityStreamingAssetsPack split.
         * Treat the materialized assets root as the pack root only when Bundles really
         * exists and contains content. This also upgrades reuse/FAST-SSH payloads made
         * by older Builder versions without fabricating files. */
        char bundles_dir[1536],assets_root[1536];
        if(bender_join_parts_2749(bundles_dir,sizeof(bundles_dir),g_guest_app_dir,"/unity_payload/assets/Bundles",NULL,NULL) &&
           bender_path_is_dir_2749(bundles_dir)&&bender_dir_has_file_2749(bundles_dir) &&
           bender_join_parts_2749(assets_root,sizeof(assets_root),g_guest_app_dir,"/unity_payload/assets",NULL,NULL)){
            bender_strlcpy(out,assets_root,cap);if(source)*source="embedded-base-bundles";return 1;
        }
    }
    if(bender_split_apk_exists_2749(pack) &&
       bender_join_parts_2749(p,sizeof(p),g_guest_app_dir,"/unity_payload/assets",NULL,NULL) &&
       bender_path_is_dir_2749(p)&&bender_dir_has_file_2749(p)){
        bender_strlcpy(out,p,cap);if(source)*source="split-apk-global-fallback";return 1;
    }
    return 0;
}
static void bender_locale_values_2749(char*lang,size_t lcap,char*country,size_t ccap,char*tag,size_t tcap,char*legacy,size_t xcap){
    const char*raw=getenv("BENDER_ANDROID_LOCALE");if(!raw||!*raw)raw="en-US";
    char l[16]={0},c[16]={0};size_t li=0,ci=0;int second=0;
    for(size_t i=0;raw[i]&&i<63u;i++){
        unsigned char ch=(unsigned char)raw[i];
        if(ch=='-'||ch=='_'){second=1;continue;}
        if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')))continue;
        if(!second&&li+1u<sizeof(l))l[li++]=(char)ch;
        else if(second&&ci+1u<sizeof(c))c[ci++]=(char)ch;
    }
    if(!li){l[0]='e';l[1]='n';l[2]=0;li=2;}
    for(size_t i=0;i<li;i++)if(l[i]>='A'&&l[i]<='Z')l[i]=(char)(l[i]-'A'+'a');
    if(!ci){c[0]='U';c[1]='S';c[2]=0;ci=2;}
    for(size_t i=0;i<ci;i++)if(c[i]>='a'&&c[i]<='z')c[i]=(char)(c[i]-'a'+'A');
    if(lang&&lcap)bender_strlcpy(lang,l,lcap);
    if(country&&ccap)bender_strlcpy(country,c,ccap);
    if(tag&&tcap){if(!bender_join_parts_2749(tag,tcap,l,"-",c,NULL))bender_strlcpy(tag,"en-US",tcap);}
    if(legacy&&xcap){if(!bender_join_parts_2749(legacy,xcap,l,"_",c,NULL))bender_strlcpy(legacy,"en_US",xcap);}
}
static BObjArray*bender_split_source_dirs_2749(BEnv*env){
    char dirp[1536];if(!bender_join_parts_2749(dirp,sizeof(dirp),g_guest_app_dir,"/apk_splits",NULL,NULL))return NULL;
    DIR*d=opendir(dirp);if(!d){logmsg("[BENDER_APK_SPLITS][FIELD] dir=%s -> NULL errno=%d\n",dirp,errno);return NULL;}
    int count=0;struct dirent*de;
    while((de=readdir(d))){
        size_t n=strlen(de->d_name);
        if(n<5u||strcasecmp(de->d_name+n-4u,".apk"))continue;
        if(!strcasecmp(de->d_name,"base.apk"))continue;
        if(count<128)count++;
    }
    if(count<=0){closedir(d);logmsg("[BENDER_APK_SPLITS][FIELD] dir=%s count=0 -> NULL\n",dirp);return NULL;}
    rewinddir(d);BObj*strcls=new_obj(1,"java/lang/String",NULL);BObjArray*a=jni_NewObjectArray(env,count,strcls,NULL);
    if(!a){closedir(d);return NULL;}
    int idx=0;
    while(idx<count&&(de=readdir(d))){
        size_t n=strlen(de->d_name);
        if(n<5u||strcasecmp(de->d_name+n-4u,".apk"))continue;
        if(!strcasecmp(de->d_name,"base.apk"))continue;
        char p[1536];if(!bender_join_parts_2749(p,sizeof(p),dirp,"/",de->d_name,NULL))continue;
        void*js=jni_NewStringUTF(env,p);jni_SetObjectArrayElement(env,a,idx,js);
        logmsg("[BENDER_APK_SPLITS][FIELD_ITEM] index=%d path=%s exists=%d\n",idx,p,bender_path_is_file_2749(p));idx++;
    }
    closedir(d);
    if(idx!=count)a->length=idx;
    logmsg("[BENDER_APK_SPLITS][FIELD] splitSourceDirs count=%d array=%p\n",idx,(void*)a);
    return a;
}
static void bender_apks_diag_2749(void){
    char dirp[1536];int splits=0;
    if(bender_join_parts_2749(dirp,sizeof(dirp),g_guest_app_dir,"/apk_splits",NULL,NULL)){
        DIR*d=opendir(dirp);if(d){struct dirent*de;while((de=readdir(d))){size_t n=strlen(de->d_name);if(n>=5u&&!strcasecmp(de->d_name+n-4u,".apk")){splits++;logmsg("[BENDER_APK_SPLITS][STARTUP] file=%s\n",de->d_name);}}closedir(d);}
    }
    char loc_l[16],loc_c[16],loc_tag[40],loc_legacy[40];bender_locale_values_2749(loc_l,sizeof(loc_l),loc_c,sizeof(loc_c),loc_tag,sizeof(loc_tag),loc_legacy,sizeof(loc_legacy));
    char pp[1536];const char*src=NULL;int data=bender_pad_pack_path_2749("UnityDataAssetPack",pp,sizeof(pp),&src);
    logmsg("[BENDER_PAD][STARTUP] splits=%d dataPack=%d dataSource=%s dataPath=%s locale=%s\n",splits,data,src?src:"?",data?pp:"(missing)",loc_tag);
    int streaming=bender_pad_pack_path_2749("UnityStreamingAssetsPack",pp,sizeof(pp),&src);
    logmsg("[BENDER_PAD][STARTUP] streamingPack=%d streamingSource=%s streamingPath=%s\n",streaming,src?src:"?",streaming?pp:"(missing)");
}
/* ---------------- end BENDER APKS/PAD/LOCALE V2.7.50 ---------------- */

/* ---------------- BENDER REAL NETWORK BRIDGE V2.7.50 ---------------- */
typedef struct BenderNetworkState2750 {
    int online;
    int transport; /* Android TYPE_WIFI=1 / TYPE_ETHERNET=9 */
    int family;
    char ifname[64];
    char address[80];
    uint64_t checked_ns;
    unsigned long generation;
} BenderNetworkState2750;
static BenderNetworkState2750 g_bender_net_2750;
static pthread_mutex_t g_bender_net_lock_2750=PTHREAD_MUTEX_INITIALIZER;
static unsigned long g_bender_net_socket_calls_2750;
static unsigned long g_bender_net_connect_calls_2750;
static unsigned long g_bender_net_dns_calls_2750;

static uint64_t bender_net_now_ns_2750(void){struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return 0;return (uint64_t)ts.tv_sec*UINT64_C(1000000000)+(uint64_t)ts.tv_nsec;}
static int bender_net_mode_2750(void){
    const char*m=getenv("BENDER_NETWORK_MODE");if(!m||!*m||!strcasecmp(m,"AUTO"))return -1;
    if(!strcasecmp(m,"OFF")||!strcasecmp(m,"OFFLINE")||!strcmp(m,"0"))return 0;
    if(!strcasecmp(m,"ON")||!strcasecmp(m,"ONLINE")||!strcmp(m,"1"))return 1;
    return -1;
}
static int bender_net_transport_from_name_2750(const char*n){
    const char*forced=getenv("BENDER_NETWORK_TRANSPORT");
    if(forced&&*forced){if(!strcasecmp(forced,"ETHERNET")||!strcasecmp(forced,"ETH"))return 9;if(!strcasecmp(forced,"WIFI")||!strcasecmp(forced,"WI-FI"))return 1;}
    if(n&&(!strncasecmp(n,"eth",3)||!strncasecmp(n,"en",2)||!strncasecmp(n,"usb",3)))return 9;
    return 1;
}
static int bender_network_refresh_2750(int force){
    uint64_t now=bender_net_now_ns_2750();
    pthread_mutex_lock(&g_bender_net_lock_2750);
    if(!force&&g_bender_net_2750.checked_ns&&now&&now-g_bender_net_2750.checked_ns<UINT64_C(1000000000)){
        int r=g_bender_net_2750.online;pthread_mutex_unlock(&g_bender_net_lock_2750);return r;
    }
    BenderNetworkState2750 ns;memset(&ns,0,sizeof(ns));ns.checked_ns=now;ns.transport=1;ns.generation=g_bender_net_2750.generation+1ul;
    int mode=bender_net_mode_2750();
    if(mode==0){bender_strlcpy(ns.ifname,"forced-offline",sizeof(ns.ifname));bender_strlcpy(ns.address,"none",sizeof(ns.address));}
    else {
        struct ifaddrs*ifs=NULL;
        if(getifaddrs(&ifs)==0){
            for(struct ifaddrs*it=ifs;it;it=it->ifa_next){
                if(!it->ifa_name||!it->ifa_addr)continue;
                if(!(it->ifa_flags&IFF_UP)||(it->ifa_flags&IFF_LOOPBACK))continue;
                int fam=it->ifa_addr->sa_family;if(fam!=AF_INET&&fam!=AF_INET6)continue;
                char host[80]={0};
                const void*src=NULL;
                if(fam==AF_INET)src=&((struct sockaddr_in*)it->ifa_addr)->sin_addr;
                else src=&((struct sockaddr_in6*)it->ifa_addr)->sin6_addr;
                if(!inet_ntop(fam,src,host,sizeof(host)))bender_strlcpy(host,"unknown",sizeof(host));
                /* Prefer IPv4 when both are present because most Android games still do. */
                if(!ns.online||fam==AF_INET){
                    ns.online=1;ns.family=fam;ns.transport=bender_net_transport_from_name_2750(it->ifa_name);
                    bender_strlcpy(ns.ifname,it->ifa_name,sizeof(ns.ifname));bender_strlcpy(ns.address,host,sizeof(ns.address));
                    if(fam==AF_INET)break;
                }
            }
            freeifaddrs(ifs);
        }
        if(mode==1&&!ns.online){ns.online=1;ns.transport=bender_net_transport_from_name_2750(NULL);ns.family=AF_UNSPEC;bender_strlcpy(ns.ifname,"forced-online",sizeof(ns.ifname));bender_strlcpy(ns.address,"host-stack",sizeof(ns.address));}
    }
    int changed=(ns.online!=g_bender_net_2750.online)||(ns.transport!=g_bender_net_2750.transport)||strcmp(ns.ifname,g_bender_net_2750.ifname)||strcmp(ns.address,g_bender_net_2750.address);
    g_bender_net_2750=ns;
    if(force||changed)logmsg("[BENDER_NETWORK][STATE] mode=%s online=%d transport=%s(%d) interface=%s address=%s family=%d generation=%lu\n",mode==0?"OFFLINE":(mode==1?"ONLINE":"AUTO"),ns.online,ns.transport==9?"ETHERNET":"WIFI",ns.transport,ns.ifname[0]?ns.ifname:"none",ns.address[0]?ns.address:"none",ns.family,ns.generation);
    int r=ns.online;pthread_mutex_unlock(&g_bender_net_lock_2750);return r;
}
static int bender_network_online_2750(void){return bender_network_refresh_2750(0);}
static int bender_network_type_2750(void){bender_network_refresh_2750(0);return g_bender_net_2750.transport?g_bender_net_2750.transport:1;}
static const char*bender_network_type_name_2750(void){return bender_network_type_2750()==9?"ETHERNET":"WIFI";}
static int bender_network_unity_reachability_2750(void){int on=bender_network_online_2750();int r=on?2:0;logmsg("[BENDER_NETWORK][UNITY_REACHABILITY] online=%d -> %d (%s)\n",on,r,r==2?"ReachableViaLocalAreaNetwork":"NotReachable");return r;}
static void bender_network_diag_2750(void){
    int on=bender_network_refresh_2750(1);const char*cert=getenv("SSL_CERT_FILE");const char*cdir=getenv("SSL_CERT_DIR");
    logmsg("[BENDER_NETWORK][STARTUP] online=%d transport=%s interface=%s address=%s SSL_CERT_FILE=%s SSL_CERT_DIR=%s native_sockets=host-glibc\n",on,bender_network_type_name_2750(),g_bender_net_2750.ifname[0]?g_bender_net_2750.ifname:"none",g_bender_net_2750.address[0]?g_bender_net_2750.address:"none",cert&&*cert?cert:"(unset)",cdir&&*cdir?cdir:"(unset)");
}

static void bender_network_preflight_2755(void){
    const char*host=getenv("BENDER_NETWORK_PREFLIGHT_HOST");if(!host||!*host)host="connectivitycheck.gstatic.com";
    struct addrinfo hints,*res=NULL;memset(&hints,0,sizeof(hints));hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;
    uint64_t t0=bender_net_now_ns_2750();int grc=getaddrinfo(host,"443",&hints,&res);uint64_t t1=bender_net_now_ns_2750();
    logmsg("[BENDER_NETWORK][PREFLIGHT_DNS] host=%s rc=%d msg=%s ms=%llu\n",host,grc,grc?gai_strerror(grc):"OK",(unsigned long long)((t1-t0)/UINT64_C(1000000)));
    int ok=0,last_errno=0;char remote[128]="(none)";unsigned remote_port=443;
    if(grc==0){
        for(struct addrinfo*it=res;it;it=it->ai_next){
            if(it->ai_family!=AF_INET&&it->ai_family!=AF_INET6)continue;
            int fd=socket(it->ai_family,it->ai_socktype?it->ai_socktype:SOCK_STREAM,it->ai_protocol);if(fd<0){last_errno=errno;continue;}
            int fl=fcntl(fd,F_GETFL,0);if(fl>=0)(void)fcntl(fd,F_SETFL,fl|O_NONBLOCK);
            int rc=connect(fd,it->ai_addr,it->ai_addrlen);
            if(rc==0)ok=1;
            else if(errno==EINPROGRESS||errno==EWOULDBLOCK){struct pollfd pfd={fd,POLLOUT,0};int pr=poll(&pfd,1,3500);if(pr>0){int soerr=0;socklen_t sl=sizeof(soerr);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&soerr,&sl)==0&&soerr==0)ok=1;else last_errno=soerr?soerr:errno;}else last_errno=(pr==0)?ETIMEDOUT:errno;}
            else last_errno=errno;
            if(it->ai_family==AF_INET){const struct sockaddr_in*a=(const struct sockaddr_in*)it->ai_addr;inet_ntop(AF_INET,&a->sin_addr,remote,sizeof(remote));remote_port=(unsigned)ntohs(a->sin_port);}
            else {const struct sockaddr_in6*a=(const struct sockaddr_in6*)it->ai_addr;inet_ntop(AF_INET6,&a->sin6_addr,remote,sizeof(remote));remote_port=(unsigned)ntohs(a->sin6_port);}
            close(fd);if(ok)break;
        }
    }
    if(res){freeaddrinfo(res);}
    uint64_t t2=bender_net_now_ns_2750();
    logmsg("[BENDER_NETWORK][PREFLIGHT_TCP] host=%s remote=%s:%u ok=%d errno=%d ms=%llu\n",host,remote,remote_port,ok,last_errno,(unsigned long long)((t2-t1)/UINT64_C(1000000)));
}
static void bender_firebase_diag_2755(void){
    logmsg("[BENDER_FIREBASE][CONFIG] source=%s package=%s appId=%s project=%s sender=%s database=%s bucket=%s api_key_present=%d\n",
        BENDER_FIREBASE_SOURCE[0]?BENDER_FIREBASE_SOURCE:"(missing)",BENDER_APP_PACKAGE,
        BENDER_FIREBASE_APPLICATION_ID[0]?BENDER_FIREBASE_APPLICATION_ID:"(missing)",
        BENDER_FIREBASE_PROJECT_ID[0]?BENDER_FIREBASE_PROJECT_ID:"(missing)",
        BENDER_FIREBASE_GCM_SENDER_ID[0]?BENDER_FIREBASE_GCM_SENDER_ID:"(missing)",
        BENDER_FIREBASE_DATABASE_URL[0]?BENDER_FIREBASE_DATABASE_URL:"(missing)",
        BENDER_FIREBASE_STORAGE_BUCKET[0]?BENDER_FIREBASE_STORAGE_BUCKET:"(missing)",
        BENDER_FIREBASE_API_KEY[0]?1:0);
}
static int bender_net_socket_2750(int domain,int type,int protocol){
    unsigned long n=++g_bender_net_socket_calls_2750;int fd=socket(domain,type,protocol);if(n<=20ul||fd<0)logmsg("[BENDER_NETWORK][SOCKET] n=%lu domain=%d type=0x%x protocol=%d -> fd=%d errno=%d\n",n,domain,type,protocol,fd,fd<0?errno:0);return fd;
}
/*
 * Bionic and glibc are both Linux ABIs, but LP64 struct addrinfo is NOT
 * layout-compatible: Bionic stores ai_canonname before ai_addr while glibc
 * stores ai_addr before ai_canonname. Returning a glibc list directly to an
 * Android guest makes ai_addr point at the canonical-name string. The next
 * connect() then sees bytes such as "11" from an IPv4 string as sa_family
 * (0x3131 = 12593), exactly what Dead Trigger 2 exposed on R36S.
 */
typedef struct BionicAddrInfo2752 {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    uint32_t __pad0;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    struct BionicAddrInfo2752 *ai_next;
} BionicAddrInfo2752;
_Static_assert(sizeof(BionicAddrInfo2752)==48,"Bionic arm64 addrinfo must be 48 bytes");

static int bender_ai_flags_bionic_to_host_2752(int f){
    int h=0;
    if(f&0x00000001)h|=AI_PASSIVE;
    if(f&0x00000002)h|=AI_CANONNAME;
    if(f&0x00000004)h|=AI_NUMERICHOST;
#ifdef AI_NUMERICSERV
    if(f&0x00000008)h|=AI_NUMERICSERV;
#endif
#ifdef AI_ALL
    if(f&0x00000100)h|=AI_ALL;
#endif
#ifdef AI_ADDRCONFIG
    if(f&0x00000400)h|=AI_ADDRCONFIG;
#endif
#ifdef AI_V4MAPPED
    if(f&0x00000800)h|=AI_V4MAPPED;
#endif
    return h;
}
static int bender_ai_flags_host_to_bionic_2752(int f){
    int b=0;
    if(f&AI_PASSIVE)b|=0x00000001;
    if(f&AI_CANONNAME)b|=0x00000002;
    if(f&AI_NUMERICHOST)b|=0x00000004;
#ifdef AI_NUMERICSERV
    if(f&AI_NUMERICSERV)b|=0x00000008;
#endif
#ifdef AI_ALL
    if(f&AI_ALL)b|=0x00000100;
#endif
#ifdef AI_ADDRCONFIG
    if(f&AI_ADDRCONFIG)b|=0x00000400;
#endif
#ifdef AI_V4MAPPED
    if(f&AI_V4MAPPED)b|=0x00000800;
#endif
    return b;
}
static int bender_eai_host_to_bionic_2752(int rc){
    if(rc==0)return 0;
#ifdef EAI_ADDRFAMILY
    if(rc==EAI_ADDRFAMILY)return 1;
#endif
    if(rc==EAI_AGAIN)return 2;
    if(rc==EAI_BADFLAGS)return 3;
    if(rc==EAI_FAIL)return 4;
    if(rc==EAI_FAMILY)return 5;
    if(rc==EAI_MEMORY)return 6;
#ifdef EAI_NODATA
    if(rc==EAI_NODATA)return 7;
#endif
    if(rc==EAI_NONAME)return 8;
    if(rc==EAI_SERVICE)return 9;
    if(rc==EAI_SOCKTYPE)return 10;
    if(rc==EAI_SYSTEM)return 11;
#ifdef EAI_OVERFLOW
    if(rc==EAI_OVERFLOW)return 14;
#endif
    return 4;
}
static const char*bender_net_gai_strerror_2752(int rc){
    switch(rc){case 0:return "Success";case 1:return "address family for hostname not supported";case 2:return "temporary failure in name resolution";case 3:return "invalid value for ai_flags";case 4:return "non-recoverable failure in name resolution";case 5:return "ai_family not supported";case 6:return "memory allocation failure";case 7:return "no address associated with hostname";case 8:return "hostname or service not known";case 9:return "service not supported for socket type";case 10:return "socket type not supported";case 11:return "system error";case 12:return "invalid hints";case 13:return "resolved protocol is unknown";case 14:return "argument buffer overflow";default:return "unknown getaddrinfo error";}
}
static void bender_net_freeaddrinfo_2752(BionicAddrInfo2752*ai){
    unsigned freed=0;
    while(ai){BionicAddrInfo2752*n=ai->ai_next;free(ai->ai_canonname);free(ai->ai_addr);free(ai);ai=n;freed++;}
    if(freed)logmsg("[BENDER_NETWORK][FREEADDRINFO] nodes=%u abi=bionic-lp64\n",freed);
}
static int bender_net_connect_2750(int fd,const struct sockaddr*sa,socklen_t sl){
    char host[96]="?";unsigned port=0;int fam=sa?(int)sa->sa_family:-1;
    if(sa&&fam==AF_INET&&sl>=sizeof(struct sockaddr_in)){const struct sockaddr_in*a=(const struct sockaddr_in*)sa;inet_ntop(AF_INET,&a->sin_addr,host,sizeof(host));port=(unsigned)ntohs(a->sin_port);}
    else if(sa&&fam==AF_INET6&&sl>=sizeof(struct sockaddr_in6)){const struct sockaddr_in6*a=(const struct sockaddr_in6*)sa;inet_ntop(AF_INET6,&a->sin6_addr,host,sizeof(host));port=(unsigned)ntohs(a->sin6_port);}
    unsigned long n=++g_bender_net_connect_calls_2750;
    if(sa&&fam!=AF_INET&&fam!=AF_INET6){
        const unsigned char*r=(const unsigned char*)sa;
        logmsg("[BENDER_NETWORK][CONNECT_ABI_ERROR] n=%lu fd=%d family=%d len=%u raw=%02x%02x%02x%02x%02x%02x%02x%02x action=reject\n",n,fd,fam,(unsigned)sl,r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7]);
        errno=EAFNOSUPPORT;return -1;
    }
    int rc=connect(fd,sa,sl);int e=rc<0?errno:0;
    if(n<=60ul||rc<0)logmsg("[BENDER_NETWORK][CONNECT] n=%lu fd=%d family=%d remote=%s:%u rc=%d errno=%d abi=bionic-sockaddr-linux-compatible\n",n,fd,fam,host,port,rc,e);
    return rc;
}
static int bender_net_getaddrinfo_2750(const char*node,const char*service,const BionicAddrInfo2752*guest_hints,BionicAddrInfo2752**guest_res){
    unsigned long n=++g_bender_net_dns_calls_2750;
    if(guest_res)*guest_res=NULL;
    struct addrinfo hh;struct addrinfo*hp=NULL;const struct addrinfo*hptr=NULL;
    memset(&hh,0,sizeof(hh));
    if(guest_hints){hh.ai_flags=bender_ai_flags_bionic_to_host_2752(guest_hints->ai_flags);hh.ai_family=guest_hints->ai_family;hh.ai_socktype=guest_hints->ai_socktype;hh.ai_protocol=guest_hints->ai_protocol;hptr=&hh;}
    int hrc=getaddrinfo(node,service,hptr,&hp);int brc=bender_eai_host_to_bionic_2752(hrc);
    unsigned out_count=0;
    if(hrc==0&&guest_res){
        BionicAddrInfo2752*head=NULL,*tail=NULL;
        for(struct addrinfo*it=hp;it;it=it->ai_next){
            BionicAddrInfo2752*g=(BionicAddrInfo2752*)calloc(1,sizeof(*g));
            if(!g){bender_net_freeaddrinfo_2752(head);head=NULL;brc=6;break;}
            g->ai_flags=bender_ai_flags_host_to_bionic_2752(it->ai_flags);g->ai_family=it->ai_family;g->ai_socktype=it->ai_socktype;g->ai_protocol=it->ai_protocol;g->ai_addrlen=it->ai_addrlen;
            if(it->ai_canonname){g->ai_canonname=strdup(it->ai_canonname);if(!g->ai_canonname){free(g);bender_net_freeaddrinfo_2752(head);head=NULL;brc=6;break;}}
            if(it->ai_addr&&it->ai_addrlen){g->ai_addr=(struct sockaddr*)malloc(it->ai_addrlen);if(!g->ai_addr){free(g->ai_canonname);free(g);bender_net_freeaddrinfo_2752(head);head=NULL;brc=6;break;}memcpy(g->ai_addr,it->ai_addr,it->ai_addrlen);}
            if(!head)head=g;else tail->ai_next=g;tail=g;out_count++;
        }
        if(brc==0)*guest_res=head;
    }
    if(hp)freeaddrinfo(hp);
    if(n<=60ul||brc)logmsg("[BENDER_NETWORK][DNS] n=%lu host=%s service=%s family=%d socktype=%d host_rc=%d bionic_rc=%d results=%u abi=glibc-to-bionic-lp64 msg=%s\n",n,node?node:"(null)",service?service:"(null)",guest_hints?guest_hints->ai_family:0,guest_hints?guest_hints->ai_socktype:0,hrc,brc,out_count,brc?bender_net_gai_strerror_2752(brc):"OK");
    return brc;
}
/* ---------------- end BENDER REAL NETWORK BRIDGE V2.7.50 ---------------- */

/* ---------------- BENDER Android identity / URI / SharedPreferences V2.7.56 ---------------- */
static void *jni_call_object_common_arg(BEnv*env,void*obj,void*mid,const char*variant,void*arg0);
static bjint jni_call_int_common_arg(BEnv*env,void*obj,void*mid,const char*variant,bjint arg0);
#define BENDER_PREF_MAX_2756 512
typedef struct BenderPref2756 {int used;char type;char file[160];char key[384];char value[1024];} BenderPref2756;
static BenderPref2756 g_bender_prefs_2756[BENDER_PREF_MAX_2756];
static pthread_mutex_t g_bender_prefs_mu_2756=PTHREAD_MUTEX_INITIALIZER;
static int g_bender_prefs_loaded_2756;

static uint64_t bender_fnv1a64_2756(const char*s){uint64_t h=UINT64_C(1469598103934665603);const unsigned char*p=(const unsigned char*)(s?s:"");while(*p){h^=(uint64_t)*p++;h*=UINT64_C(1099511628211);}return h;}
static const char*bender_android_id_2756(void){static char id[17];if(!id[0]){uint64_t h=bender_fnv1a64_2756("BENDER-R36S-ANDROID-ID");const unsigned char*p=(const unsigned char*)BENDER_APP_PACKAGE;while(*p){h^=(uint64_t)*p++;h*=UINT64_C(1099511628211);}snprintf(id,sizeof(id),"%016" PRIx64,h?h:UINT64_C(0x6b656e6465723635));}return id;}
static int bender_hexval_2756(int c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static void bender_hexenc_2756(const char*in,char*out,size_t cap){static const char hx[]="0123456789abcdef";size_t n=0;if(!out||!cap)return;for(const unsigned char*p=(const unsigned char*)(in?in:"");*p&&n+2u<cap;p++){out[n++]=hx[*p>>4];out[n++]=hx[*p&15u];}out[n]=0;}
static void bender_hexdec_2756(const char*in,char*out,size_t cap){size_t n=0;if(!out||!cap)return;const char*p=in?in:"";while(p[0]&&p[1]&&n+1u<cap){int a=bender_hexval_2756((unsigned char)p[0]),b=bender_hexval_2756((unsigned char)p[1]);if(a<0||b<0)break;out[n++]=(char)((a<<4)|b);p+=2;}out[n]=0;}
static void bender_pref_path_2756(char*out,size_t cap){snprintf(out,cap,"%s/bender_shared_prefs.tsv",g_guest_files_dir[0]?g_guest_files_dir:".");}
static void bender_prefs_load_locked_2756(void){if(g_bender_prefs_loaded_2756)return;g_bender_prefs_loaded_2756=1;char path[1536];bender_pref_path_2756(path,sizeof(path));FILE*f=fopen(path,"rb");if(!f){logmsg("[BENDER_PREFS][LOAD] path=%s state=new\n",path);return;}char line[4096];int count=0;while(fgets(line,sizeof(line),f)){char*save=NULL;char*t=strtok_r(line,"|\r\n",&save),*hf=strtok_r(NULL,"|\r\n",&save),*hk=strtok_r(NULL,"|\r\n",&save),*hv=strtok_r(NULL,"|\r\n",&save);if(!t||!hf||!hk||!hv)continue;for(int i=0;i<BENDER_PREF_MAX_2756;i++)if(!g_bender_prefs_2756[i].used){BenderPref2756*e=&g_bender_prefs_2756[i];e->used=1;e->type=t[0]?t[0]:'S';bender_hexdec_2756(hf,e->file,sizeof(e->file));bender_hexdec_2756(hk,e->key,sizeof(e->key));bender_hexdec_2756(hv,e->value,sizeof(e->value));count++;break;}}fclose(f);logmsg("[BENDER_PREFS][LOAD] path=%s entries=%d\n",path,count);}
static void bender_prefs_save_locked_2756(void){char path[1536],tmp[1600];bender_pref_path_2756(path,sizeof(path));snprintf(tmp,sizeof(tmp),"%s.tmp",path);FILE*f=fopen(tmp,"wb");if(!f){logmsg("[BENDER_PREFS][SAVE][ERROR] path=%s errno=%d\n",tmp,errno);return;}char hf[321],hk[769],hv[2049];int count=0;for(int i=0;i<BENDER_PREF_MAX_2756;i++){BenderPref2756*e=&g_bender_prefs_2756[i];if(!e->used)continue;bender_hexenc_2756(e->file,hf,sizeof(hf));bender_hexenc_2756(e->key,hk,sizeof(hk));bender_hexenc_2756(e->value,hv,sizeof(hv));fprintf(f,"%c|%s|%s|%s\n",e->type?e->type:'S',hf,hk,hv);count++;}fclose(f);if(rename(tmp,path)!=0){unlink(path);(void)rename(tmp,path);}logmsg("[BENDER_PREFS][SAVE] path=%s entries=%d\n",path,count);}
static int bender_pref_find_locked_2756(const char*file,const char*key){for(int i=0;i<BENDER_PREF_MAX_2756;i++){BenderPref2756*e=&g_bender_prefs_2756[i];if(e->used&&!strcmp(e->file,file?file:"")&&!strcmp(e->key,key?key:""))return i;}return -1;}
static void bender_pref_set_2756(const char*file,const char*key,char type,const char*value){if(!key)return;pthread_mutex_lock(&g_bender_prefs_mu_2756);bender_prefs_load_locked_2756();int idx=bender_pref_find_locked_2756(file,key);if(idx<0)for(int i=0;i<BENDER_PREF_MAX_2756;i++)if(!g_bender_prefs_2756[i].used){idx=i;g_bender_prefs_2756[i].used=1;break;}if(idx>=0){BenderPref2756*e=&g_bender_prefs_2756[idx];e->type=type;bender_strlcpy(e->file,file?file:"default",sizeof(e->file));bender_strlcpy(e->key,key,sizeof(e->key));bender_strlcpy(e->value,value?value:"",sizeof(e->value));bender_prefs_save_locked_2756();logmsg("[BENDER_PREFS][PUT] file=%s key=%.120s type=%c value_len=%zu\n",e->file,e->key,e->type,strlen(e->value));}pthread_mutex_unlock(&g_bender_prefs_mu_2756);}
static int bender_pref_get_2756(const char*file,const char*key,char*type,char*out,size_t cap){int found=0;pthread_mutex_lock(&g_bender_prefs_mu_2756);bender_prefs_load_locked_2756();int idx=bender_pref_find_locked_2756(file,key);if(idx>=0){BenderPref2756*e=&g_bender_prefs_2756[idx];if(type)*type=e->type;if(out&&cap)bender_strlcpy(out,e->value,cap);found=1;}pthread_mutex_unlock(&g_bender_prefs_mu_2756);return found;}
static int bender_pref_contains_2756(const char*file,const char*key){return bender_pref_get_2756(file,key,NULL,NULL,0);}
static void bender_pref_remove_2756(const char*file,const char*key){pthread_mutex_lock(&g_bender_prefs_mu_2756);bender_prefs_load_locked_2756();int idx=bender_pref_find_locked_2756(file,key);if(idx>=0){memset(&g_bender_prefs_2756[idx],0,sizeof(g_bender_prefs_2756[idx]));bender_prefs_save_locked_2756();}pthread_mutex_unlock(&g_bender_prefs_mu_2756);}
static const char*bender_pref_file_2756(void*obj){BObj*bo=bender_known_bobj(obj);if(!bo)return "default";if(!strcmp(bo->a,"android/content/SharedPreferences$Editor")&&bo->ref0)bo=bender_known_bobj((void*)bo->ref0);if(bo&&bo->b[0])return bo->b;return "default";}
static int bender_uri_allowed_2756(unsigned char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='!'||c=='.'||c=='~'||c=='\''||c=='('||c==')'||c=='*';}
static void*bender_uri_encode_2756(BEnv*env,void*arg){const char*s=jni_obj_string(arg);if(!s)return NULL;size_t n=strlen(s),cap=n*3u+1u;char*out=(char*)malloc(cap);if(!out)return NULL;static const char hx[]="0123456789ABCDEF";size_t o=0;for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)s[i];if(bender_uri_allowed_2756(c))out[o++]=(char)c;else{out[o++]='%';out[o++]=hx[c>>4];out[o++]=hx[c&15u];}}out[o]=0;void*r=jni_NewStringUTF(env,out);logmsg("[BENDER_URI][ENCODE] in_len=%zu out_len=%zu in=%.80s out=%.120s\n",n,o,s,out);free(out);return r;}
static void*bender_uri_decode_2756(BEnv*env,void*arg){const char*s=jni_obj_string(arg);if(!s)return NULL;size_t n=strlen(s);char*out=(char*)malloc(n+1u);if(!out)return NULL;size_t o=0;for(size_t i=0;i<n;i++){if(s[i]=='%'&&i+2u<n){int a=bender_hexval_2756((unsigned char)s[i+1]),b=bender_hexval_2756((unsigned char)s[i+2]);if(a>=0&&b>=0){out[o++]=(char)((a<<4)|b);i+=2;continue;}}out[o++]=s[i];}out[o]=0;void*r=jni_NewStringUTF(env,out);logmsg("[BENDER_URI][DECODE] in_len=%zu out_len=%zu in=%.120s out=%.80s\n",n,o,s,out);free(out);return r;}
static int bender_obj_is_2756(void*obj,const char*klass){BObj*bo=bender_known_bobj(obj);return bo&&bo->kind==4&&!strcmp(bo->a,klass);}
static int bender_objmethod_two_ptr_2756(BObj*m){return m&&((!strcmp(m->a,"getString")&&!strcmp(m->b,"(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"))||(!strcmp(m->a,"putString")&&!strcmp(m->b,"(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;")));}
static int bender_objmethod_ptr_num_2756(BObj*m){return m&&((!strcmp(m->a,"putInt")&&!strcmp(m->b,"(Ljava/lang/String;I)Landroid/content/SharedPreferences$Editor;"))||(!strcmp(m->a,"putBoolean")&&!strcmp(m->b,"(Ljava/lang/String;Z)Landroid/content/SharedPreferences$Editor;"))||(!strcmp(m->a,"putLong")&&!strcmp(m->b,"(Ljava/lang/String;J)Landroid/content/SharedPreferences$Editor;")));}
static int bender_objmethod_one_ptr_2756(BObj*m){return m&&((!strcmp(m->a,"getSharedPreferences")&&!strncmp(m->b,"(Ljava/lang/String;I)",20))||(!strcmp(m->a,"getInstallerPackageName")&&!strcmp(m->b,"(Ljava/lang/String;)Ljava/lang/String;"))||(!strcmp(m->a,"remove")&&!strcmp(m->b,"(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;")));}
static void*bender_jni_call_object_2756(BEnv*env,void*obj,void*mid,const char*variant,void*arg0,void*arg1,int64_t num1,int has_num1){BObj*m=(BObj*)mid;const char*name=m?m->a:"?";
    if(m&&!strcmp(name,"getSharedPreferences")){const char*file=jni_obj_string(arg0);BObj*p=new_obj(4,"android/content/SharedPreferences",file&&*file?file:"default");logmsg("[BENDER_PREFS][OPEN] file=%s obj=%p\n",p?p->b:"default",(void*)p);return p;}
    if(m&&!strcmp(name,"edit")&&bender_obj_is_2756(obj,"android/content/SharedPreferences")){BObj*e=new_obj(4,"android/content/SharedPreferences$Editor",bender_pref_file_2756(obj));if(e)e->ref0=(uintptr_t)obj;logmsg("[BENDER_PREFS][EDIT] file=%s editor=%p\n",bender_pref_file_2756(obj),(void*)e);return e;}
    if(m&&!strcmp(name,"getString")&&bender_obj_is_2756(obj,"android/content/SharedPreferences")){const char*key=jni_obj_string(arg0),*def=jni_obj_string(arg1);char val[1024],type=0;if(key&&bender_pref_get_2756(bender_pref_file_2756(obj),key,&type,val,sizeof(val))){logmsg("[BENDER_PREFS][GET_STRING] file=%s key=%.120s hit=1 len=%zu\n",bender_pref_file_2756(obj),key,strlen(val));return jni_NewStringUTF(env,val);}logmsg("[BENDER_PREFS][GET_STRING] file=%s key=%.120s hit=0 default_len=%zu\n",bender_pref_file_2756(obj),key?key:"(null)",def?strlen(def):0u);return arg1;}
    if(m&&!strcmp(name,"putString")&&bender_obj_is_2756(obj,"android/content/SharedPreferences$Editor")){const char*key=jni_obj_string(arg0),*val=jni_obj_string(arg1);if(key)bender_pref_set_2756(bender_pref_file_2756(obj),key,'S',val?val:"");return obj;}
    if(m&&!strcmp(name,"putInt")&&bender_obj_is_2756(obj,"android/content/SharedPreferences$Editor")&&has_num1){const char*key=jni_obj_string(arg0);char v[64];snprintf(v,sizeof(v),"%d",(int)num1);if(key)bender_pref_set_2756(bender_pref_file_2756(obj),key,'I',v);return obj;}
    if(m&&!strcmp(name,"putBoolean")&&bender_obj_is_2756(obj,"android/content/SharedPreferences$Editor")&&has_num1){const char*key=jni_obj_string(arg0);if(key)bender_pref_set_2756(bender_pref_file_2756(obj),key,'B',num1?"1":"0");return obj;}
    if(m&&!strcmp(name,"putLong")&&bender_obj_is_2756(obj,"android/content/SharedPreferences$Editor")&&has_num1){const char*key=jni_obj_string(arg0);char v[64];snprintf(v,sizeof(v),"%" PRId64,num1);if(key)bender_pref_set_2756(bender_pref_file_2756(obj),key,'J',v);return obj;}
    if(m&&!strcmp(name,"remove")&&bender_obj_is_2756(obj,"android/content/SharedPreferences$Editor")){const char*key=jni_obj_string(arg0);if(key)bender_pref_remove_2756(bender_pref_file_2756(obj),key);return obj;}
    if(m&&!strcmp(name,"getInstallerPackageName")){logmsg("[BENDER_PACKAGE][INSTALLER] package=%s -> com.android.vending\n",jni_obj_string(arg0)?jni_obj_string(arg0):BENDER_APP_PACKAGE);return jni_NewStringUTF(env,"com.android.vending");}
    return jni_call_object_common_arg(env,obj,mid,variant,arg0);
}
static bjint bender_jni_call_int_2756(BEnv*env,void*obj,void*mid,const char*variant,void*key_obj,bjint def,bjint legacy_arg){BObj*m=(BObj*)mid;if(m&&!strcmp(m->a,"getInt")&&!strcmp(m->b,"(Ljava/lang/String;I)I")&&bender_obj_is_2756(obj,"android/content/SharedPreferences")){const char*key=jni_obj_string(key_obj);char val[128],type=0;if(key&&bender_pref_get_2756(bender_pref_file_2756(obj),key,&type,val,sizeof(val))){long x=strtol(val,NULL,10);logmsg("[BENDER_PREFS][GET_INT] file=%s key=%.120s hit=1 value=%ld\n",bender_pref_file_2756(obj),key,x);return (bjint)x;}logmsg("[BENDER_PREFS][GET_INT] file=%s key=%.120s hit=0 default=%d\n",bender_pref_file_2756(obj),key?key:"(null)",(int)def);return def;}return jni_call_int_common_arg(env,obj,mid,variant,legacy_arg);}
/* ---------------- end BENDER Android identity / URI / SharedPreferences V2.7.56 ---------------- */
/* V2.7.61: coherent Firebase Remote Config/Auth compatibility state. */
static void *g_remote_config_instance_2761;
static void *g_firebase_auth_instance_2761;
static void *g_firebase_user_instance_2761;
static void *g_pad_wrapper_real_instance_2761;
static unsigned long g_remote_config_task_count_2761;
static unsigned long g_auth_token_count_2761;
static const char*bender_auth_uid_2761(void){static char uid[96];if(!uid[0])snprintf(uid,sizeof(uid),"r36s_%s",bender_android_id_2756());return uid;}
static void*bender_java_boolean_2761(int value){BObj*b=new_obj(4,"java/lang/Boolean","value");if(b)b->i64=value?1:0;return b;}
static void*bender_auth_token_result_2761(BEnv*env){
    char token[384];const char*uid=bender_auth_uid_2761();
    snprintf(token,sizeof(token),"bender.local.%s.%s",BENDER_FIREBASE_PROJECT_ID[0]?BENDER_FIREBASE_PROJECT_ID:"no-project",uid);
    BObj*r=new_obj(4,"com/google/firebase/auth/GetTokenResult","bender-local-token-result");
    if(r)r->ref0=(uintptr_t)jni_NewStringUTF(env,token);
    ++g_auth_token_count_2761;
    logmsg("[BENDER_FIREBASE_AUTH][TOKEN_RESULT] n=%lu uid=%s project=%s result=%p policy=synthetic-local-not-server-signed\n",g_auth_token_count_2761,uid,BENDER_FIREBASE_PROJECT_ID[0]?BENDER_FIREBASE_PROJECT_ID:"(missing)",(void*)r);
    return r;
}
typedef struct BSdlAudioSpecFix5 {int freq;uint16_t format;uint8_t channels;uint8_t silence;uint16_t samples;uint16_t padding;uint32_t size;void(*callback)(void*,uint8_t*,int);void*userdata;} BSdlAudioSpecFix5;
typedef uint32_t BenderSdlAudioDeviceFix5;
static void*g_bender_sdl_audio_lib_fix5;
static int(*g_sdl_InitSubSystem_fix5)(uint32_t);
static BenderSdlAudioDeviceFix5(*g_sdl_OpenAudioDevice_fix5)(const char*,int,const BSdlAudioSpecFix5*,BSdlAudioSpecFix5*,int);
static void(*g_sdl_CloseAudioDevice_fix5)(BenderSdlAudioDeviceFix5);
static void(*g_sdl_PauseAudioDevice_fix5)(BenderSdlAudioDeviceFix5,int);
static int(*g_sdl_QueueAudio_fix5)(BenderSdlAudioDeviceFix5,const void*,uint32_t);
static uint32_t(*g_sdl_GetQueuedAudioSize_fix5)(BenderSdlAudioDeviceFix5);
static void(*g_sdl_ClearQueuedAudio_fix5)(BenderSdlAudioDeviceFix5);
static const char*(*g_sdl_GetError_fix5)(void);
static BenderSdlAudioDeviceFix5 g_bender_sdl_audio_dev_fix5;
/* FIX68: once org/fmod/AudioDevice is initialized, its byte[]/short[] PCM is the
   authoritative producer. The older FMODAudioDevice producer keeps the SDL device
   alive but pauses its own queueing to avoid mixing two streams. */
static volatile int g_bender_fix68_android_audio_takeover;
static unsigned long g_bender_fix68_audio_blocks;
static unsigned long g_bender_fix68_audio_bytes;
static unsigned long g_bender_fix68_audio_bad_arrays;
static int g_bender_fix68_audio_init_args[4];
static int g_bender_audio_rate_fix5=48000,g_bender_audio_frames_fix5=512,g_bender_audio_channels_fix5=2;
static BObj*g_bender_audio_rate_key_fix5,*g_bender_audio_frames_key_fix5,*g_bender_audio_rate_value_fix5,*g_bender_audio_frames_value_fix5,*g_bender_audio_product_fix5,*g_bender_audio_device_obj_fix5;
static BObjArray*g_bender_audio_devices_fix5;
static BArray*g_bender_audio_rates_fix5,*g_bender_audio_channels_fix5_arr,*g_bender_audio_encodings_fix5;
static int bender_audio_env_int_fix5(const char*n,int defv,int lo,int hi){const char*v=getenv(n);if(!v||!*v)return defv;long x=strtol(v,NULL,10);if(x<lo)x=lo;if(x>hi)x=hi;return(int)x;}
static void bender_audio_refresh_cfg_fix5(void){g_bender_audio_rate_fix5=bender_audio_env_int_fix5("BENDER_AUDIO_SAMPLE_RATE",48000,8000,192000);g_bender_audio_frames_fix5=bender_audio_env_int_fix5("BENDER_AUDIO_FRAMES_PER_BUFFER",512,64,8192);g_bender_audio_channels_fix5=bender_audio_env_int_fix5("BENDER_AUDIO_CHANNELS",2,1,2);}
static void*bender_audio_cached_string_fix5(BEnv*env,BObj**slot,const char*s){if(!*slot)*slot=(BObj*)jni_NewStringUTF(env,s);return *slot;}
static BArray*bender_audio_cached_int_array_fix5(BEnv*env,BArray**slot,int value){if(!*slot){*slot=(BArray*)jni_NewIntArray(env,1);if(*slot&&(*slot)->data)((bjint*)(*slot)->data)[0]=(bjint)value;}return *slot;}
static void*bender_audio_property_fix5(BEnv*env,const char*key){bender_audio_refresh_cfg_fix5();char tmp[32];if(key&&strstr(key,"OUTPUT_SAMPLE_RATE")){snprintf(tmp,sizeof(tmp),"%d",g_bender_audio_rate_fix5);return bender_audio_cached_string_fix5(env,&g_bender_audio_rate_value_fix5,tmp);}if(key&&strstr(key,"OUTPUT_FRAMES_PER_BUFFER")){snprintf(tmp,sizeof(tmp),"%d",g_bender_audio_frames_fix5);return bender_audio_cached_string_fix5(env,&g_bender_audio_frames_value_fix5,tmp);}return NULL;}
static BObjArray*bender_audio_devices_fix5(BEnv*env){if(!g_bender_audio_device_obj_fix5)g_bender_audio_device_obj_fix5=new_obj(4,"android/media/AudioDeviceInfo","BENDER R36S speaker");if(!g_bender_audio_devices_fix5){BObj*cls=(BObj*)jni_FindClass(env,"android/media/AudioDeviceInfo");g_bender_audio_devices_fix5=jni_NewObjectArray(env,1,cls,g_bender_audio_device_obj_fix5);}return g_bender_audio_devices_fix5;}
static int bender_fmod_load_sdl_audio_fix5(void){if(g_bender_sdl_audio_lib_fix5)return g_sdl_OpenAudioDevice_fix5&&g_sdl_QueueAudio_fix5;const char*req=getenv("BENDER_SDL_LIBRARY");const char*names[4]={req,"libSDL2-2.0.so.0","libSDL2.so",NULL};for(int i=0;i<3&&!g_bender_sdl_audio_lib_fix5;i++){if(!names[i]||!*names[i])continue;g_bender_sdl_audio_lib_fix5=dlopen(names[i],RTLD_NOW|RTLD_LOCAL);}if(!g_bender_sdl_audio_lib_fix5){logmsg("[BENDER_FMOD_AUDIO][SDL][FAIL] dlopen SDL2: %s\n",dlerror());return 0;}
#define BENDER_AUDIO_SYM_FIX5(dst,name) do{*(void**)(&dst)=dlsym(g_bender_sdl_audio_lib_fix5,name);}while(0)
 BENDER_AUDIO_SYM_FIX5(g_sdl_InitSubSystem_fix5,"SDL_InitSubSystem");BENDER_AUDIO_SYM_FIX5(g_sdl_OpenAudioDevice_fix5,"SDL_OpenAudioDevice");BENDER_AUDIO_SYM_FIX5(g_sdl_CloseAudioDevice_fix5,"SDL_CloseAudioDevice");BENDER_AUDIO_SYM_FIX5(g_sdl_PauseAudioDevice_fix5,"SDL_PauseAudioDevice");BENDER_AUDIO_SYM_FIX5(g_sdl_QueueAudio_fix5,"SDL_QueueAudio");BENDER_AUDIO_SYM_FIX5(g_sdl_GetQueuedAudioSize_fix5,"SDL_GetQueuedAudioSize");BENDER_AUDIO_SYM_FIX5(g_sdl_ClearQueuedAudio_fix5,"SDL_ClearQueuedAudio");BENDER_AUDIO_SYM_FIX5(g_sdl_GetError_fix5,"SDL_GetError");
#undef BENDER_AUDIO_SYM_FIX5
 return g_sdl_InitSubSystem_fix5&&g_sdl_OpenAudioDevice_fix5&&g_sdl_CloseAudioDevice_fix5&&g_sdl_PauseAudioDevice_fix5&&g_sdl_QueueAudio_fix5&&g_sdl_GetQueuedAudioSize_fix5;}
static void*bender_fmod_thread_main_fix5(void*unused){(void)unused;bender_audio_refresh_cfg_fix5();int info0=0,info1=0,info2=0,info3=0,info4=0;if(g_bender_fmod_get_info_fn_fix5&&g_bender_fmod_class_fix5){info0=g_bender_fmod_get_info_fn_fix5(&g_env,g_bender_fmod_class_fix5,0);info1=g_bender_fmod_get_info_fn_fix5(&g_env,g_bender_fmod_class_fix5,1);info2=g_bender_fmod_get_info_fn_fix5(&g_env,g_bender_fmod_class_fix5,2);info3=g_bender_fmod_get_info_fn_fix5(&g_env,g_bender_fmod_class_fix5,3);info4=g_bender_fmod_get_info_fn_fix5(&g_env,g_bender_fmod_class_fix5,4);if(info0>=8000&&info0<=192000)g_bender_audio_rate_fix5=info0;if(info1>=64&&info1<=8192)g_bender_audio_frames_fix5=info1;if(info4==1||info4==2)g_bender_audio_channels_fix5=info4;}
 logmsg("[BENDER_FMOD_AUDIO][INFO] fmodGetInfo=[%d,%d,%d,%d,%d] selected=%dHz frames=%d channels=%d java_semantics=1\n",info0,info1,info2,info3,info4,g_bender_audio_rate_fix5,g_bender_audio_frames_fix5,g_bender_audio_channels_fix5);
 if(!g_bender_fmod_process_fn_fix5||!bender_fmod_load_sdl_audio_fix5()){g_bender_fmod_running_fix5=0;return NULL;}if(g_sdl_InitSubSystem_fix5(0x10u)!=0){logmsg("[BENDER_FMOD_AUDIO][SDL][FAIL] InitSubSystem error=%s\n",g_sdl_GetError_fix5?g_sdl_GetError_fix5():"?");g_bender_fmod_running_fix5=0;return NULL;}
 BSdlAudioSpecFix5 want,have;memset(&want,0,sizeof(want));memset(&have,0,sizeof(have));want.freq=g_bender_audio_rate_fix5;want.format=0x8010u;want.channels=(uint8_t)g_bender_audio_channels_fix5;want.samples=(uint16_t)g_bender_audio_frames_fix5;g_bender_sdl_audio_dev_fix5=g_sdl_OpenAudioDevice_fix5(NULL,0,&want,&have,0);if(!g_bender_sdl_audio_dev_fix5){logmsg("[BENDER_FMOD_AUDIO][SDL][FAIL] OpenAudioDevice want=%dHz/%dch/%dframes error=%s\n",want.freq,(int)want.channels,(int)want.samples,g_sdl_GetError_fix5?g_sdl_GetError_fix5():"?");g_bender_fmod_running_fix5=0;return NULL;}
 size_t bytes=(size_t)g_bender_audio_frames_fix5*(size_t)g_bender_audio_channels_fix5*2u;unsigned char*pcm=(unsigned char*)calloc(bytes?bytes:1u,1u);void*bb=pcm?jni_NewDirectByteBuffer(&g_env,pcm,(bjlong)bytes):NULL;if(!pcm||!bb){if(pcm)free(pcm);g_sdl_CloseAudioDevice_fix5(g_bender_sdl_audio_dev_fix5);g_bender_sdl_audio_dev_fix5=0;g_bender_fmod_running_fix5=0;return NULL;}g_sdl_PauseAudioDevice_fix5(g_bender_sdl_audio_dev_fix5,0);logmsg("[BENDER_FMOD_AUDIO][STARTED] backend=SDL2 queue device=%u have=%dHz/%dch/%dframes format=0x%x block_bytes=%zu java_semantics=1\n",g_bender_sdl_audio_dev_fix5,have.freq,(int)have.channels,(int)have.samples,(unsigned)have.format,bytes);
 while(!g_bender_fmod_stop_fix5){if(g_bender_fix68_android_audio_takeover){usleep(2000);continue;}uint32_t q=g_sdl_GetQueuedAudioSize_fix5(g_bender_sdl_audio_dev_fix5);if(q>bytes*8u){usleep(2000);continue;}if(g_bender_fmod_get_info_fn_fix5&&g_bender_fmod_class_fix5){bjint ready=g_bender_fmod_get_info_fn_fix5(&g_env,g_bender_fmod_class_fix5,3);if(ready!=1){++g_bender_fmod_empty_fix5;usleep(2000);continue;}}memset(pcm,0,bytes);bjint rc=g_bender_fmod_process_fn_fix5(&g_env,g_bender_fmod_class_fix5,bb);/* FMODAudioDevice.run() no APK original ignora o retorno de fmodProcess e sempre escreve o DirectByteBuffer no AudioTrack. */unsigned int fix81_peak=0u,fix81_nonzero=0u;size_t fix81_samples=bytes/2u;const int16_t*fix81_s=(const int16_t*)pcm;for(size_t fix81_i=0;fix81_i<fix81_samples;fix81_i++){int fix81_v=(int)fix81_s[fix81_i];unsigned int fix81_a=(unsigned int)(fix81_v<0?-fix81_v:fix81_v);if(fix81_a>fix81_peak)fix81_peak=fix81_a;if(fix81_v)fix81_nonzero++;}if(g_bender_fmod_blocks_fix5<3ul||((g_bender_fmod_blocks_fix5+1ul)%1200ul)==0ul)logmsg("[BENDER_FMOD_AUDIO][PCM_ACTIVITY] source=fix81 next_block=%lu peak_s16=%u nonzero=%u/%zu process_rc=%d policy=measure-only\n",g_bender_fmod_blocks_fix5+1ul,fix81_peak,fix81_nonzero,fix81_samples,(int)rc);if(g_sdl_QueueAudio_fix5(g_bender_sdl_audio_dev_fix5,pcm,(uint32_t)bytes)==0){++g_bender_fmod_blocks_fix5;g_bender_fmod_bytes_fix5+=(unsigned long)bytes;}else{logmsg("[BENDER_FMOD_AUDIO][QUEUE][WARN] error=%s\n",g_sdl_GetError_fix5?g_sdl_GetError_fix5():"?");usleep(2000);}if(g_bender_fmod_blocks_fix5<=3ul||g_bender_fmod_blocks_fix5%1200ul==0ul)logmsg("[BENDER_FMOD_AUDIO][PCM] blocks=%lu bytes=%lu queued=%u process_rc=%d policy=java-ignore-return\n",g_bender_fmod_blocks_fix5,g_bender_fmod_bytes_fix5,q,(int)rc);}
 if(g_sdl_ClearQueuedAudio_fix5){g_sdl_ClearQueuedAudio_fix5(g_bender_sdl_audio_dev_fix5);}g_sdl_PauseAudioDevice_fix5(g_bender_sdl_audio_dev_fix5,1);g_sdl_CloseAudioDevice_fix5(g_bender_sdl_audio_dev_fix5);g_bender_sdl_audio_dev_fix5=0;free(pcm);g_bender_fmod_running_fix5=0;logmsg("[BENDER_FMOD_AUDIO][STOPPED] blocks=%lu bytes=%lu inactive=%lu\n",g_bender_fmod_blocks_fix5,g_bender_fmod_bytes_fix5,g_bender_fmod_empty_fix5);return NULL;}
static int bender_fmod_audio_start_fix5(const char*reason){g_bender_fmod_start_requested_fix5=1;if(g_bender_fmod_running_fix5)return 1;if(!g_bender_fmod_process_fn_fix5){logmsg("[BENDER_FMOD_AUDIO][START][PENDING] reason=%s fmodProcess-not-captured\n",reason?reason:"?");return 0;}g_bender_fmod_stop_fix5=0;g_bender_fmod_running_fix5=1;int rc=pthread_create(&g_bender_fmod_thread_fix5,NULL,bender_fmod_thread_main_fix5,NULL);if(rc){g_bender_fmod_running_fix5=0;logmsg("[BENDER_FMOD_AUDIO][START][FAIL] pthread rc=%d\n",rc);return 0;}logmsg("[BENDER_FMOD_AUDIO][START] reason=%s thread=created\n",reason?reason:"?");return 1;}
static void bender_fmod_audio_stop_fix5(const char*reason){g_bender_fmod_start_requested_fix5=0;if(!g_bender_fmod_running_fix5)return;g_bender_fmod_stop_fix5=1;if(!pthread_equal(pthread_self(),g_bender_fmod_thread_fix5))pthread_join(g_bender_fmod_thread_fix5,NULL);logmsg("[BENDER_FMOD_AUDIO][STOP] reason=%s\n",reason?reason:"?");}
static int bender_fix68_is_fmod_audio_write(BObj*m){
    if(!m||strcmp(m->a,"write"))return 0;
    const char*cls=bender_jni_class_name((void*)m->ref0);
    if(!cls||!strstr(cls,"org/fmod/AudioDevice"))return 0;
    return !strcmp(m->b,"([BI)V")||!strcmp(m->b,"([SI)V");
}
static int bender_fix68_is_fmod_audio_init(BObj*m){
    if(!m||strcmp(m->a,"init")||strcmp(m->b,"(IIII)Z"))return 0;
    const char*cls=bender_jni_class_name((void*)m->ref0);
    return cls&&strstr(cls,"org/fmod/AudioDevice");
}
static void bender_fix68_record_audio_init(int a0,int a1,int a2,int a3,const char*variant){
    g_bender_fix68_audio_init_args[0]=a0;g_bender_fix68_audio_init_args[1]=a1;
    g_bender_fix68_audio_init_args[2]=a2;g_bender_fix68_audio_init_args[3]=a3;
    logmsg("[BENDER_FMOD_AUDIO][INIT_FIX68] variant=%s args=%d,%d,%d,%d sdl_device=%u current=%dHz/%dch/%dframes policy=android-pcm-takeover code_patch=0 forced_completion=0\n",
           variant?variant:"",a0,a1,a2,a3,g_bender_sdl_audio_dev_fix5,g_bender_audio_rate_fix5,g_bender_audio_channels_fix5,g_bender_audio_frames_fix5);
}
static void bender_fix68_fmod_audio_write(void*array,bjint requested,const char*sig,const char*variant){
    BArray*a=jni_array_cast(array);
    unsigned long call=++g_fmod_audio_write_count;
    if(!a||!a->data||a->length<0||!sig){
        ++g_bender_fix68_audio_bad_arrays;
        if(call<=8ul||call%600ul==0ul)logmsg("[BENDER_FMOD_AUDIO][WRITE_FIX68][WARN] call=%lu variant=%s array=%p requested=%d valid=0 bad=%lu\n",call,variant?variant:"",array,(int)requested,g_bender_fix68_audio_bad_arrays);
        return;
    }
    size_t elems=(size_t)a->length;
    size_t bytes=0u;
    if(!strcmp(sig,"([BI)V")){
        if(a->kind!=BARR_BYTE||a->elem_size!=1u){++g_bender_fix68_audio_bad_arrays;return;}
        if(requested>0&&(size_t)requested<elems)elems=(size_t)requested;
        bytes=elems;
    }else if(!strcmp(sig,"([SI)V")){
        if(a->kind!=BARR_SHORT||a->elem_size!=sizeof(int16_t)){++g_bender_fix68_audio_bad_arrays;return;}
        if(requested>0&&(size_t)requested<elems)elems=(size_t)requested;
        if(elems>SIZE_MAX/sizeof(int16_t))return;
        bytes=elems*sizeof(int16_t);
    }else return;
    g_bender_fix68_android_audio_takeover=1;
    if(!bytes||bytes>UINT32_MAX||!g_bender_sdl_audio_dev_fix5||!g_sdl_QueueAudio_fix5){
        if(call<=8ul||call%600ul==0ul)logmsg("[BENDER_FMOD_AUDIO][WRITE_FIX68][WARN] call=%lu variant=%s sig=%s bytes=%zu requested=%d device=%u queue_fn=%p action=not-queued\n",call,variant?variant:"",sig,bytes,(int)requested,g_bender_sdl_audio_dev_fix5,(void*)g_sdl_QueueAudio_fix5);
        return;
    }
    uint32_t queued=g_sdl_GetQueuedAudioSize_fix5?g_sdl_GetQueuedAudioSize_fix5(g_bender_sdl_audio_dev_fix5):0u;
    int waits=0;
    uint32_t high=(uint32_t)bytes*8u;
    while(g_sdl_GetQueuedAudioSize_fix5&&queued>high&&waits<50){usleep(1000);queued=g_sdl_GetQueuedAudioSize_fix5(g_bender_sdl_audio_dev_fix5);waits++;}
    int rc=g_sdl_QueueAudio_fix5(g_bender_sdl_audio_dev_fix5,a->data,(uint32_t)bytes);
    if(rc==0){++g_bender_fix68_audio_blocks;g_bender_fix68_audio_bytes+=(unsigned long)bytes;}
    if(call<=8ul||call%600ul==0ul||rc!=0)logmsg("[BENDER_FMOD_AUDIO][WRITE_FIX68] call=%lu variant=%s sig=%s array=%p requested=%d array_len=%d bytes=%zu device=%u queued_before=%u waits=%d rc=%d blocks=%lu total_bytes=%lu policy=pcm-to-sdl\n",call,variant?variant:"",sig,array,(int)requested,(int)a->length,bytes,g_bender_sdl_audio_dev_fix5,queued,waits,rc,g_bender_fix68_audio_blocks,g_bender_fix68_audio_bytes);
}
static void*g_pad_download_listener_2765;
static unsigned long g_pad_download_listener_regs_2765;

/* BENDER V2.7.120 — FIX71 Android TrustManager / X509 host system CA bridge. */
#define BENDER_FIX71_MAX_CA_CERTS_2843 512
static int g_bender_fix43_ca_loaded_2843;
static int g_bender_fix43_tmf_init_2843;
static unsigned g_bender_fix43_ca_count_2843;
static size_t g_bender_fix43_ca_der_bytes_2843;
static char g_bender_fix43_ca_path_2843[512];
static BObj *g_bender_fix43_tmf_2843;
static BObj *g_bender_fix43_x509_tm_2843;
static BObjArray *g_bender_fix43_managers_2843;
static BObjArray *g_bender_fix43_issuers_2843;

static int bender_fix43_b64val_2843(unsigned char c){
    if(c>='A'&&c<='Z')return (int)(c-'A');
    if(c>='a'&&c<='z')return (int)(c-'a')+26;
    if(c>='0'&&c<='9')return (int)(c-'0')+52;
    if(c=='+')return 62;
    if(c=='/')return 63;
    return -1;
}
static int bender_fix43_decode_b64_2843(const char*src,size_t n,unsigned char**out,size_t*out_n){
    if(out){*out=NULL;}
    if(out_n){*out_n=0;}
    if(!src||!out||!out_n)return 0;
    size_t cap=(n/4u)*3u+4u;unsigned char*buf=(unsigned char*)malloc(cap?cap:4u);if(!buf)return 0;
    uint32_t acc=0;int bits=0;size_t used=0;
    for(size_t i=0;i<n;i++){
        unsigned char ch=(unsigned char)src[i];if(ch=='=')break;
        int v=bender_fix43_b64val_2843(ch);
        if(v<0){if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;free(buf);return 0;}
        acc=(acc<<6)|(uint32_t)v;bits+=6;
        if(bits>=8){bits-=8;if(used>=cap){free(buf);return 0;}buf[used++]=(unsigned char)((acc>>bits)&0xffu);}
    }
    if(!used){free(buf);return 0;}*out=buf;*out_n=used;return 1;
}
static const char*bender_fix43_select_ca_path_2843(void){
    const char*e=getenv("SSL_CERT_FILE");if(e&&*e&&access(e,R_OK)==0)return e;
    static const char*paths[]={"/etc/ssl/certs/ca-certificates.crt","/etc/pki/tls/certs/ca-bundle.crt","/etc/ssl/cert.pem",NULL};
    for(size_t i=0;paths[i];i++)if(access(paths[i],R_OK)==0)return paths[i];
    return NULL;
}
static int bender_fix43_load_ca_2843(BEnv*env){
    if(g_bender_fix43_ca_loaded_2843)return (int)g_bender_fix43_ca_count_2843;
    g_bender_fix43_ca_loaded_2843=1;
    const char*path=bender_fix43_select_ca_path_2843();
    if(path){size_t n=strnlen(path,sizeof(g_bender_fix43_ca_path_2843)-1u);memcpy(g_bender_fix43_ca_path_2843,path,n);g_bender_fix43_ca_path_2843[n]=0;}
    BObj*cert_cls=(BObj*)jni_FindClass(env,"java/security/cert/X509Certificate");
    if(!path){g_bender_fix43_issuers_2843=jni_NewObjectArray(env,0,cert_cls,NULL);logmsg("[BENDER_TLS_TRUST][LOAD][ERROR] source=fix71 path=(none) reason=no-readable-system-ca verification=enabled\n");return 0;}
    FILE*f=fopen(path,"r");
    if(!f){g_bender_fix43_issuers_2843=jni_NewObjectArray(env,0,cert_cls,NULL);logmsg("[BENDER_TLS_TRUST][LOAD][ERROR] source=fix71 path=%s errno=%d reason=open-failed verification=enabled\n",path,errno);return 0;}
    void*certs[BENDER_FIX71_MAX_CA_CERTS_2843];memset(certs,0,sizeof(certs));
    char line[8192];char*b64=NULL;size_t b64_len=0,b64_cap=0;int in_cert=0;
    while(fgets(line,sizeof(line),f)){
        if(strstr(line,"-----BEGIN CERTIFICATE-----")){in_cert=1;b64_len=0;continue;}
        if(strstr(line,"-----END CERTIFICATE-----")){
            if(in_cert&&b64_len&&g_bender_fix43_ca_count_2843<BENDER_FIX71_MAX_CA_CERTS_2843){
                unsigned char*der=NULL;size_t der_n=0;
                if(bender_fix43_decode_b64_2843(b64,b64_len,&der,&der_n)&&der_n>0&&der_n<1048576u){
                    void*ba=jni_NewByteArray(env,(bjsize)der_n);BArray*arr=jni_array_cast(ba);
                    if(arr&&arr->data&&arr->elem_size==1u&&(size_t)arr->length==der_n){
                        memcpy(arr->data,der,der_n);BObj*cert=new_obj(4,"java/security/cert/X509Certificate","system-ca");
                        if(cert){cert->ref0=(uintptr_t)ba;cert->i64=(int64_t)g_bender_fix43_ca_count_2843;certs[g_bender_fix43_ca_count_2843++]=cert;g_bender_fix43_ca_der_bytes_2843+=der_n;
                            if(g_bender_fix43_ca_count_2843<=4u)logmsg("[BENDER_TLS_TRUST][CERT] source=fix71 index=%u der_bytes=%zu path=%s\n",g_bender_fix43_ca_count_2843-1u,der_n,path);
                        }
                    }
                }
                free(der);
            }
            in_cert=0;b64_len=0;continue;
        }
        if(in_cert){
            size_t ln=strlen(line);for(size_t i=0;i<ln;i++){
                unsigned char ch=(unsigned char)line[i];if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')continue;
                if(b64_len+1u>=b64_cap){size_t nc=b64_cap?b64_cap*2u:8192u;if(nc>262144u){in_cert=0;b64_len=0;break;}char*nb=(char*)realloc(b64,nc);if(!nb){in_cert=0;b64_len=0;break;}b64=nb;b64_cap=nc;}
                if(in_cert)b64[b64_len++]=(char)ch;
            }
        }
    }
    free(b64);fclose(f);
    g_bender_fix43_issuers_2843=jni_NewObjectArray(env,(bjsize)g_bender_fix43_ca_count_2843,cert_cls,NULL);
    if(g_bender_fix43_issuers_2843){for(unsigned i=0;i<g_bender_fix43_ca_count_2843;i++)g_bender_fix43_issuers_2843->items[i]=certs[i];}
    logmsg("[BENDER_TLS_TRUST][LOAD] source=fix71 path=%s pem_certs=%u der_bytes=%zu verification=enabled trust_source=host-system-ca\n",path,g_bender_fix43_ca_count_2843,g_bender_fix43_ca_der_bytes_2843);
    return (int)g_bender_fix43_ca_count_2843;
}
static BObjArray*bender_fix43_get_managers_2843(BEnv*env){
    (void)bender_fix43_load_ca_2843(env);
    if(!g_bender_fix43_x509_tm_2843){g_bender_fix43_x509_tm_2843=new_obj(4,"javax/net/ssl/X509TrustManager","host-system-ca");if(g_bender_fix43_x509_tm_2843)g_bender_fix43_x509_tm_2843->ref0=(uintptr_t)g_bender_fix43_issuers_2843;}
    if(!g_bender_fix43_managers_2843){BObj*tm_cls=(BObj*)jni_FindClass(env,"javax/net/ssl/TrustManager");g_bender_fix43_managers_2843=jni_NewObjectArray(env,g_bender_fix43_x509_tm_2843?1:0,tm_cls,NULL);if(g_bender_fix43_managers_2843&&g_bender_fix43_x509_tm_2843)g_bender_fix43_managers_2843->items[0]=g_bender_fix43_x509_tm_2843;}
    logmsg("[BENDER_TLS_TRUST][MANAGERS] source=fix71 count=%d ca_count=%u initialized=%d verification=enabled\n",g_bender_fix43_managers_2843?(int)g_bender_fix43_managers_2843->length:0,g_bender_fix43_ca_count_2843,g_bender_fix43_tmf_init_2843);
    return g_bender_fix43_managers_2843;
}
static void*bender_fix43_static_object_2843(BEnv*env,void*cls,BObj*m,const char*variant,int*handled){
    if(handled){*handled=0;}
    if(!m)return NULL;
    const char*cn=bender_jni_class_name(cls);
    if(strcmp(cn,"javax/net/ssl/TrustManagerFactory"))return NULL;
    if(!strcmp(m->a,"getDefaultAlgorithm")&&!strcmp(m->b,"()Ljava/lang/String;")){if(handled)*handled=1;logmsg("[BENDER_TLS_TRUST][DEFAULT_ALGORITHM] source=fix71 value=PKIX variant=%s semantic=android-security-property\n",variant?variant:"?");return jni_NewStringUTF(env,"PKIX");}
    if(!strcmp(m->a,"getInstance")&&strstr(m->b,"Ljavax/net/ssl/TrustManagerFactory;")){if(handled)*handled=1;if(!g_bender_fix43_tmf_2843)g_bender_fix43_tmf_2843=new_obj(4,"javax/net/ssl/TrustManagerFactory","PKIX");logmsg("[BENDER_TLS_TRUST][FACTORY] source=fix71 algorithm=PKIX factory=%p variant=%s verification=enabled\n",(void*)g_bender_fix43_tmf_2843,variant?variant:"?");return g_bender_fix43_tmf_2843;}
    return NULL;
}
static void*bender_fix43_object_2843(BEnv*env,void*obj,BObj*m,const char*variant,int*handled){
    if(handled){*handled=0;}
    if(!m)return NULL;
    (void)variant;
    const char*cn=bender_jni_class_name((void*)m->ref0);
    if(!strcmp(cn,"javax/net/ssl/TrustManagerFactory")&&!strcmp(m->a,"getTrustManagers")){
        if(handled){*handled=1;}
        return bender_fix43_get_managers_2843(env);
    }
    if(!strcmp(cn,"javax/net/ssl/X509TrustManager")&&!strcmp(m->a,"getAcceptedIssuers")){
        if(handled){*handled=1;}
        (void)bender_fix43_load_ca_2843(env);
        logmsg("[BENDER_TLS_TRUST][ISSUERS] source=fix71 count=%u path=%s verification=enabled\n",g_bender_fix43_ca_count_2843,g_bender_fix43_ca_path_2843[0]?g_bender_fix43_ca_path_2843:"(none)");
        return g_bender_fix43_issuers_2843;
    }
    if((!strcmp(cn,"java/security/cert/X509Certificate")||!strcmp(cn,"java/security/cert/Certificate"))&&!strcmp(m->a,"getEncoded")){
        BObj*cert=bender_known_bobj(obj);
        if(handled){*handled=1;}
        if(cert&&cert->kind==4&&cert->ref0){if(cert->i64<4)logmsg("[BENDER_TLS_TRUST][CERT_ENCODED] source=fix71 index=%lld bytes_array=%p verification=enabled\n",(long long)cert->i64,(void*)cert->ref0);return (void*)cert->ref0;}logmsg("[BENDER_TLS_TRUST][CERT_ENCODED][ERROR] source=fix71 cert=%p reason=missing-der\n",obj);return jni_NewByteArray(env,0);
    }
    return NULL;
}
static void bender_fix43_void_2843(BEnv*env,void*obj,BObj*m,const char*variant){
    (void)env;if(!m)return;const char*cn=bender_jni_class_name((void*)m->ref0);
    if(!strcmp(cn,"javax/net/ssl/TrustManagerFactory")&&!strcmp(m->a,"init")&&strstr(m->b,"Ljava/security/KeyStore;")){
        g_bender_fix43_tmf_init_2843=1;logmsg("[BENDER_TLS_TRUST][INIT] source=fix71 factory=%p keyStore=default-system source_material=SSL_CERT_FILE variant=%s verification=enabled\n",obj,variant?variant:"?");
    }
    if(!strcmp(cn,"javax/net/ssl/X509TrustManager")&&(!strcmp(m->a,"checkServerTrusted")||!strcmp(m->a,"checkClientTrusted"))){
        g_jni_exception=1;g_jni_exception_obj=new_obj(4,"java/security/cert/CertificateException","unsupported-direct-trust-check");
        logmsg("[BENDER_TLS_TRUST][DIRECT_CHECK][BLOCKED] source=fix71 method=%s reason=no-accept-all-trust-manager verification=enabled\n",m->a);
    }
}

static int bender_reliance_push_payload_methodid_match_fix103(BObj*m){
    if(!m||m->kind!=2||(int)m->ref1!=1)return 0;
    const char*decl=bender_jni_class_name((void*)m->ref0);const char*sig=m->b;
    if(!decl||strcmp(decl,"com/reliancegames/plugins/pushnotification/RGPushNotification"))return 0;
    if(strcmp(m->a,"getSavedPushPayloadData"))return 0;
    return !strcmp(sig,"(Lcom/unity3d/player/UnityPlayerForActivityOrService;)Ljava/lang/String;") ||
           !strcmp(sig,"(Landroid/content/Context;)Ljava/lang/String;");
}
/* ULTRAKILL fix -L: dispara os listeners de dialog capturados (onClick com o
 * AlertDialog como 'dialog' e depois onCancel) e limpa o estado. Idempotente:
 * age so se houver pendencia. Chamado de show() e, como ultima linha de
 * defesa, de bender_pthread_cond_wait() - porque o libunity dorme nesse cond
 * wait ate alguem responder o dialog, e aqui nao existe UI Android. */
static void bender_dialog_flush(const char*reason){
    const char*ad=getenv("BENDER_DIALOG_AUTODISMIS");
    if(ad&&!strcmp(ad,"0"))return;
    if(!g_dlg_pending&&!g_dlg_listener_n)return;
    int n=g_dlg_listener_n; g_dlg_listener_n=0; g_dlg_pending=0;
    ++g_dlg_autodismiss_count;
    if(!n)return;
    void*dlgobj=g_dlg_shown_obj?g_dlg_shown_obj:(void*)new_obj(4,"android/app/AlertDialog",NULL);
    g_dlg_shown_obj=dlgobj;
    for(int q=0;q<n;q++){
        void*lp=g_dlg_listener[q]; g_dlg_listener[q]=NULL;
        if(!lp)continue;
        logmsg("[BENDER_JNI][DIALOG][AUTODISMIS] reason=%s n=%d/%d proxy=%p -> onClick(dialog)\n",reason?reason:"?",q+1,n,lp);
        bender_jni_proxy_dispatch(lp,"onClick",dlgobj);
        logmsg("[BENDER_JNI][DIALOG][AUTODISMIS] reason=%s proxy=%p -> onCancel(dialog)\n",reason?reason:"?",lp);
        bender_jni_proxy_dispatch(lp,"onCancel",dlgobj);
        logmsg("[BENDER_JNI][DIALOG][AUTODISMIS] reason=%s proxy=%p -> onDismiss(dialog)\n",reason?reason:"?",lp);
        bender_jni_proxy_dispatch(lp,"onDismiss",dlgobj);
    }
}
static void *jni_call_object_common_arg(BEnv*env,void*obj,void*mid,const char*variant,void*arg0){BObj*m=(BObj*)mid;++g_jni_call_count;const char*name=m?m->a:"?";const char*sig=m?m->b:"?";const char*ret=jni_ret_sig(m);set_checkpoint("JNI CallObjectMethod%s method=%s sig=%s",variant?variant:"",name,sig);logmsg("[BENDER_JNI][CALL] Object%s obj=%p method=%s sig=%s\n",variant?variant:"",obj,name,sig);
if(m&&!strcmp(name,"getBytes")&&!strcmp(sig,"(Ljava/lang/String;)[B")){
    void*soft_bytes_fix100=bender_soft_string_getbytes_fix100(env,obj);
    if(soft_bytes_fix100)return soft_bytes_fix100;
}
{int handled2843=0;void*rv2843=bender_fix43_object_2843(env,obj,m,variant,&handled2843);if(handled2843)return rv2843;}
if(m&&!strcmp(name,"getProperty")&&!strcmp(bender_jni_class_name((void*)m->ref0),"android/media/AudioManager")){const char*k=jni_obj_string(arg0);void*v=bender_audio_property_fix5(env,k);logmsg("[BENDER_AUDIO_MANAGER][PROPERTY] key=%s -> %s\n",k?k:"(null)",v?jni_obj_string(v):"(null)");return v;}
 if(m&&!strcmp(name,"getDevices")&&!strcmp(bender_jni_class_name((void*)m->ref0),"android/media/AudioManager")){logmsg("[BENDER_AUDIO_MANAGER][DEVICES] flags=%ld -> builtin-speaker\n",(long)(intptr_t)arg0);return bender_audio_devices_fix5(env);}
 if(m&&!strcmp(name,"getProductName")&&!strcmp(bender_jni_class_name((void*)m->ref0),"android/media/AudioDeviceInfo")){return bender_audio_cached_string_fix5(env,&g_bender_audio_product_fix5,"BENDER R36S Audio");}
 if(m&&!strcmp(name,"getSampleRates")&&!strcmp(bender_jni_class_name((void*)m->ref0),"android/media/AudioDeviceInfo")){bender_audio_refresh_cfg_fix5();return bender_audio_cached_int_array_fix5(env,&g_bender_audio_rates_fix5,g_bender_audio_rate_fix5);}
 if(m&&!strcmp(name,"getChannelCounts")&&!strcmp(bender_jni_class_name((void*)m->ref0),"android/media/AudioDeviceInfo")){return bender_audio_cached_int_array_fix5(env,&g_bender_audio_channels_fix5_arr,2);}
 if(m&&!strcmp(name,"getEncodings")&&!strcmp(bender_jni_class_name((void*)m->ref0),"android/media/AudioDeviceInfo")){return bender_audio_cached_int_array_fix5(env,&g_bender_audio_encodings_fix5,2);}
 if(m&&!strcmp(name,"getClass")){void*cls=jni_GetObjectClass(env,obj);logmsg("[BENDER_JNI][REFLECTION][GET_CLASS] obj=%p -> %p class=%s\n",obj,cls,bender_jni_class_name(cls));return cls;}
if(m&&!strcmp(name,"getDebugMessage")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"com/android/billingclient/api/BillingResult")){logmsg("[BENDER_BILLING][DEBUG_MESSAGE] obj=%p -> OK\n",obj);return jni_NewStringUTF(env,"OK");}}
if(m&&!strcmp(name,"registerDownloadStatusListener")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/unity3d/player/PlayAssetDeliveryUnityWrapper")){
    g_pad_download_listener_2765=arg0;++g_pad_download_listener_regs_2765;
    BObj*token=new_obj(4,"com/unity3d/player/AssetPackDownloadListenerToken","registered");
    logmsg("[BENDER_PAD][LISTENER_REGISTER] n=%lu wrapper=%p callback=%p callbackClass=%s token=%p action=capture-only\n",g_pad_download_listener_regs_2765,obj,arg0,bender_jni_class_name(arg0),(void*)token);
    return token;
}
if(m&&!strcmp(name,"getCurrentUser")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/auth/FirebaseAuth")){
    if(!g_firebase_user_instance_2761)g_firebase_user_instance_2761=new_obj(4,"com/google/firebase/auth/FirebaseUser","bender-anonymous-user");
    logmsg("[BENDER_FIREBASE_AUTH][USER] getCurrentUser -> %p uid=%s policy=persistent-local-user\n",g_firebase_user_instance_2761,bender_auth_uid_2761());return g_firebase_user_instance_2761;
}
if(m&&!strcmp(name,"getUid")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/auth/FirebaseUser")){
    logmsg("[BENDER_FIREBASE_AUTH][UID] -> %s\n",bender_auth_uid_2761());return jni_NewStringUTF(env,bender_auth_uid_2761());
}
if(m&&!strcmp(name,"getToken")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/auth/GetTokenResult")){
    BObj*r=bender_known_bobj(obj);void*s=r?(void*)r->ref0:NULL;if(!s){void*nr=bender_auth_token_result_2761(env);BObj*nro=bender_known_bobj(nr);s=nro?(void*)nro->ref0:NULL;}
    logmsg("[BENDER_FIREBASE_AUTH][TOKEN_STRING] result_obj=%p token=%p policy=synthetic-local-not-server-signed\n",obj,s);return s;
}
if(m&&!strcmp(name,"getString")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfig")){
    logmsg("[BENDER_REMOTE_CONFIG][GET] type=string key=%s -> empty policy=local-default-fallback\n",jni_obj_string(arg0)?jni_obj_string(arg0):"(unavailable)");return jni_NewStringUTF(env,"");
}
if(m&&!strcmp(name,"getValue")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfig")){
    BObj*v=new_obj(4,"com/google/firebase/remoteconfig/FirebaseRemoteConfigValue",jni_obj_string(arg0)?jni_obj_string(arg0):"");
    logmsg("[BENDER_REMOTE_CONFIG][GET] type=value key=%s -> %p source=DEFAULT\n",jni_obj_string(arg0)?jni_obj_string(arg0):"(unavailable)",(void*)v);return v;
}
if(m&&!strcmp(name,"asString")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfigValue")){
    BObj*vo=bender_known_bobj(obj);logmsg("[BENDER_REMOTE_CONFIG][VALUE] asString key=%s -> empty\n",vo?vo->b:"");return jni_NewStringUTF(env,"");
}
if(m&&!strcmp(name,"asByteArray")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfigValue")){
    void*a=jni_NewByteArray(env,0);logmsg("[BENDER_REMOTE_CONFIG][VALUE] asByteArray -> len=0 array=%p\n",a);return a;
}
if(m&&!strcmp(name,"getApplicationContext")){
    BObj*ctx=new_obj(4,"android/content/Context","application-context");
    logmsg("[BENDER_ANDROID_CONTEXT][TYPED] source=%s obj=%p -> context=%p class=android/content/Context variant=%s\n",bender_jni_class_name(obj),obj,(void*)ctx,variant?variant:"");
    return ctx;
}
if(m&&strcmp(name,"toLanguageTag")==0){char l[16],c[16],tag[40],legacy[40];bender_locale_values_2749(l,sizeof(l),c,sizeof(c),tag,sizeof(tag),legacy,sizeof(legacy));logmsg("[BENDER_JNI][LOCALE] toLanguageTag -> %s obj=%p\n",tag,obj);return jni_NewStringUTF(env,tag);}
if(m&&!strcmp(name,"getName")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==1&&bo->a[0]){char cn[1024];bender_strlcpy(cn,bo->a,sizeof(cn));for(size_t i=0;cn[i];i++)if(cn[i]=='/')cn[i]='.';logmsg("[BENDER_JNI][CLASS_GET_NAME] class=%p internal=%s java=%s\n",obj,bo->a,cn);return jni_NewStringUTF(env,cn);}if(bo&&bo->kind==4&&!strcmp(bo->a,"java/lang/reflect/Method")){logmsg("[BENDER_JNI][REFLECT_METHOD] getName method=%p -> %s\n",obj,bo->b);return jni_NewStringUTF(env,bo->b);}}
if(m&&!strcmp(name,"getDeclaringClass")){BObj*bo=(BObj*)obj;if(bo&&bo->kind==4&&!strcmp(bo->a,"java/lang/reflect/Method")){logmsg("[BENDER_JNI][REFLECT_METHOD] getDeclaringClass method=%p -> %p\n",obj,(void*)bo->ref0);return (void*)bo->ref0;}}
if(m&&!strcmp(name,"getReturnType")){BObj*bo=(BObj*)obj;if(bo&&bo->kind==4&&!strcmp(bo->a,"java/lang/reflect/Method")){const char*rt=(bo->flags==1u)?"boolean":"void";BObj*rc=new_obj(1,rt,NULL);logmsg("[BENDER_JNI][REFLECT_METHOD] getReturnType method=%s -> %s class=%p\n",bo->b,rt,(void*)rc);return rc;}}
if(m&&!strcmp(name,"getParameterTypes")){BObj*bo=(BObj*)obj;if(bo&&bo->kind==4&&!strcmp(bo->a,"java/lang/reflect/Method")){BObj*pc=new_obj(1,(bo->flags==1u)?"android/os/Message":"long",NULL);BObjArray*pa=jni_NewObjectArray(env,1,pc,pc);logmsg("[BENDER_JNI][REFLECT_METHOD] getParameterTypes method=%s -> array=%p param=%s\n",bo->b,(void*)pa,pc?pc->a:"?");return pa;}}
if(m&&strcmp(name,"getLanguage")==0){char l[16],c[16],tag[40],legacy[40];bender_locale_values_2749(l,sizeof(l),c,sizeof(c),tag,sizeof(tag),legacy,sizeof(legacy));logmsg("[BENDER_JNI][LOCALE] getLanguage -> %s obj=%p\n",l,obj);return jni_NewStringUTF(env,l);}
if(m&&strcmp(name,"getCountry")==0){char l[16],c[16],tag[40],legacy[40];bender_locale_values_2749(l,sizeof(l),c,sizeof(c),tag,sizeof(tag),legacy,sizeof(legacy));logmsg("[BENDER_JNI][LOCALE] getCountry -> %s obj=%p\n",c,obj);return jni_NewStringUTF(env,c);}
if(m&&strcmp(name,"toString")==0){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==3){const char*sv=jni_obj_string(obj);logmsg("[BENDER_PROXY_VALUE][STRING] toString obj=%p value=%s -> self\n",obj,sv?sv:"");return obj;}if(bo&&bo->kind==4&&!strcmp(bo->a,"java/util/Locale")){char l[16],c[16],tag[40],legacy[40];bender_locale_values_2749(l,sizeof(l),c,sizeof(c),tag,sizeof(tag),legacy,sizeof(legacy));logmsg("[BENDER_JNI][LOCALE] toString -> %s obj=%p\n",legacy,obj);return jni_NewStringUTF(env,legacy);}if(bo&&bo->kind==4&&!strcmp(bo->a,"java/lang/reflect/Method")){const char*s=(bo->flags==1u)?"public abstract boolean android.os.Handler$Callback.handleMessage(android.os.Message)":((bo->flags==2u)?"public abstract void android.view.Choreographer$FrameCallback.doFrame(long)":"public abstract void java.lang.Runnable.run()");logmsg("[BENDER_JNI][REFLECT_METHOD] toString method=%p name=%s flags=%u -> %s\n",obj,bo->b,bo->flags,s);return jni_NewStringUTF(env,s);}if(bo&&bo->kind==4&&(strstr(bo->a,"Exception")||strstr(bo->a,"Error")||strstr(bo->a,"Throwable"))){char tmp[1536];char clsbuf[1024];bender_strlcpy(clsbuf,bo->a,sizeof(clsbuf));for(char*p=clsbuf;*p;p++)if(*p=='/')*p='.';snprintf(tmp,sizeof(tmp),"%s%s%s",clsbuf,bo->b[0]?": ":"",bo->b);logmsg("[BENDER_JNI][EXCEPTION][TOSTRING] obj=%p -> %s\n",obj,tmp);return jni_NewStringUTF(env,tmp);}if(obj&&!bo){logmsg("[BENDER_JNI][OBJECT][TOSTRING][FOREIGN] obj=%p -> safe placeholder\n",obj);return jni_NewStringUTF(env,"java.lang.Object@bender");}}
if(m&&m->ref0&&strstr(bender_jni_class_name((void*)m->ref0),"com/google/firebase/FirebaseOptions")){
    const char*fv=NULL;
    if(!strcmp(name,"getApiKey"))fv=BENDER_FIREBASE_API_KEY;
    else if(!strcmp(name,"getApplicationId"))fv=BENDER_FIREBASE_APPLICATION_ID;
    else if(!strcmp(name,"getDatabaseUrl"))fv=BENDER_FIREBASE_DATABASE_URL;
    else if(!strcmp(name,"getGcmSenderId"))fv=BENDER_FIREBASE_GCM_SENDER_ID;
    else if(!strcmp(name,"getStorageBucket"))fv=BENDER_FIREBASE_STORAGE_BUCKET;
    else if(!strcmp(name,"getProjectId"))fv=BENDER_FIREBASE_PROJECT_ID;
    if(fv){logmsg("[BENDER_FIREBASE][OPTIONS] method=%s present=%d value=%s\n",name,fv[0]?1:0,!strcmp(name,"getApiKey")?(fv[0]?"<present>":"<missing>"):(fv[0]?fv:"<missing>"));return jni_NewStringUTF(env,fv);}
}
if(m&&strcmp(name,"getPackageName")==0)return jni_NewStringUTF(env,BENDER_APP_PACKAGE);
if(m&&strcmp(name,"getPackageCodePath")==0){logmsg("[BENDER_APK][JNI] getPackageCodePath -> %s\n",g_guest_base_apk);return jni_NewStringUTF(env,g_guest_base_apk);}
if(m&&strcmp(name,"getFilesDir")==0)return jni_new_file(env,g_guest_files_dir);
if(m&&strcmp(name,"getExternalFilesDir")==0)return jni_new_file(env,g_guest_external_files_dir);
if(m&&strcmp(name,"getCacheDir")==0)return jni_new_file(env,g_guest_cache_dir);
if(m&&strcmp(name,"getObbDir")==0){logmsg("[BENDER_JNI][OBB] getObbDir -> %s\n",g_guest_obb_dir);return jni_new_file(env,g_guest_obb_dir);}
if(m&&strcmp(name,"getObbDirs")==0){BObj*fc=new_obj(1,"java/io/File",NULL);BObj*fo=(BObj*)jni_new_file(env,g_guest_obb_dir);BObjArray*a=jni_NewObjectArray(env,1,fc,fo);logmsg("[BENDER_JNI][OBB] getObbDirs -> array=%p file=%p path=%s\n",(void*)a,(void*)fo,g_guest_obb_dir);return a;}
if(m&&strcmp(name,"getExternalFilesDirs")==0){BObj*fc=new_obj(1,"java/io/File",NULL);BObj*fo=(BObj*)jni_new_file(env,g_guest_external_files_dir);return jni_NewObjectArray(env,1,fc,fo);}
if(m&&strcmp(name,"getExternalStorageState")==0)return jni_NewStringUTF(env,"mounted");
if(m&&(!strcmp(name,"getAbsolutePath")||!strcmp(name,"getPath")||!strcmp(name,"toString"))){const char*p=jni_file_path(obj);if(p)return jni_NewStringUTF(env,p);if(!strcmp(name,"getPath")||!strcmp(name,"getAbsolutePath"))return jni_NewStringUTF(env,g_guest_app_dir);}
if(m&&strcmp(name,"findLibrary")==0){
    const char*lib=jni_obj_string(arg0); char cand[2304]; cand[0]=0;
    if(lib&&*lib){
        if(!strncmp(lib,"lib",3)&&strstr(lib,".so"))snprintf(cand,sizeof(cand),"%s/%s",g_guest_lib_dir,lib);
        else snprintf(cand,sizeof(cand),"%s/lib%s.so",g_guest_lib_dir,lib);
        if(access(cand,R_OK)==0){
            unsigned char eh4[4]={0,0,0,0};
            int efd=(int)syscall(SYS_openat,AT_FDCWD,cand,O_RDONLY|O_CLOEXEC,0);
            ssize_t ern=-1; if(efd>=0){ern=(ssize_t)syscall(SYS_read,efd,eh4,sizeof(eh4));syscall(SYS_close,efd);}
            if(ern==4&&eh4[0]==0x7f&&eh4[1]=='E'&&eh4[2]=='L'&&eh4[3]=='F'){
                logmsg("[BENDER_JNI][FIND_LIBRARY] name=%s -> %s elf=1\n",lib,cand);return jni_NewStringUTF(env,cand);
            }
            logmsg("[BENDER_JNI][FIND_LIBRARY][REJECT_NON_ELF] name=%s path=%s read=%zd magic=%02x%02x%02x%02x -> NULL\n",lib,cand,ern,(unsigned)eh4[0],(unsigned)eh4[1],(unsigned)eh4[2],(unsigned)eh4[3]);
            return NULL;
        }
    }
    logmsg("[BENDER_JNI][FIND_LIBRARY] name=%s -> NULL (nao encontrado)\n",lib?lib:"(null)"); return NULL;
}
if(m&&(!strcmp(name,"setTitle")||!strcmp(name,"setMessage"))){const char*s=jni_obj_string(arg0);logmsg("[BENDER_JNI][DIALOG] %s=%s\n",name,s?s:"(non-string/null)");return obj?obj:new_obj(4,ret,NULL);}
/* ULTRAKILL fix -L: o repack mobile abre AlertDialog ("Error / Not enough
 * storage space to install required resources.") e o libunity FICA esperando o
 * callback do listener. Sem tela Android esse toque nunca chega -> a thread de
 * render dorme em pthread_cond_wait para sempre (log: show() ->
 * BENDER_UI_THREAD RETURN -> BENDER_SYNC WAIT_BEGIN libunity.so+0x2e38ac).
 * ATENCAO (aprendido no log de 09:37): o 'obj' que o Unity passa nem sempre e
 * reconhecivel como AlertDialog$Builder por bender_jni_class_name(), entao a
 * captura e pelo NOME do metodo - que so aparece em builder de dialog.
 * BENDER_DIALOG_AUTODISMIS=0 desliga. */
{
    int is_dlg_m = m && (!strncmp(name,"setOn",5) || !strcmp(name,"setPositiveButton")
                         || !strcmp(name,"setNegativeButton") || !strcmp(name,"setNeutralButton"));
    if(is_dlg_m && arg0){
        if(g_dlg_listener_n<6)g_dlg_listener[g_dlg_listener_n++]=arg0;
        logmsg("[BENDER_JNI][DIALOG] %s capturado proxy=%p (total=%d)\n",name,arg0,g_dlg_listener_n);
        return obj?obj:new_obj(4,ret,NULL);
    }
    if(m&&!strcmp(name,"show")&&obj){
        BObj*dlg=new_obj(4,"android/app/AlertDialog",NULL);
        g_dlg_shown_obj=dlg;
        logmsg("[BENDER_JNI][DIALOG] show() com %d listener(s); auto-dismiss=%d\n",
               g_dlg_listener_n,(!getenv("BENDER_DIALOG_AUTODISMIS")||strcmp(getenv("BENDER_DIALOG_AUTODISMIS"),"0")));
        bender_dialog_flush("show");
        /* DEPOIS do flush (que limpa os flags): o wait do libunity vem a seguir,
         * entao deixa armado tanto o redispacho quanto a liberacao por tempo. */
        g_dlg_pending=1;
        g_dlg_release_armed=1;
        return dlg;
    }
    if(m&&(!strcmp(name,"dismiss")||!strcmp(name,"hide"))){ g_dlg_pending=0; g_dlg_listener_n=0; }
}
if(m&&strcmp(name,"getDefault")==0&&ret&&strcmp(ret,"Ljava/util/Locale;")==0){
    char l[16],c[16],tag[40],legacy[40];bender_locale_values_2749(l,sizeof(l),c,sizeof(c),tag,sizeof(tag),legacy,sizeof(legacy));
    BObj*loc=new_obj(4,"java/util/Locale",tag);
    logmsg("[BENDER_JNI][LOCALE] getDefault -> %p tag=%s\n",(void*)loc,tag);
    return loc;
}
if(m&&!strcmp(name,"getSystemService")){
    const char*svc=jni_obj_string(arg0);BObj*svc_cls=bender_known_bobj(arg0);const char*type="android/app/Service";
    if(!svc&&svc_cls&&svc_cls->kind==1&&svc_cls->a[0])type=svc_cls->a;
    else if(svc&&!strcmp(svc,"media_router"))type="android/media/MediaRouter";
    else if(svc&&!strcmp(svc,"display"))type="android/hardware/display/DisplayManager";
    else if(svc&&!strcmp(svc,"power"))type="android/os/PowerManager";
    else if(svc&&!strcmp(svc,"audio"))type="android/media/AudioManager";
    else if(svc&&!strcmp(svc,"sensor"))type="android/hardware/SensorManager";
    else if(svc&&!strcmp(svc,"vibrator"))type="android/os/Vibrator";
    else if(svc&&!strcmp(svc,"connectivity"))type="android/net/ConnectivityManager";
    else if(svc&&!strcmp(svc,"window"))type="android/view/WindowManager";
    else if(svc&&!strcmp(svc,"input_method"))type="android/view/inputmethod/InputMethodManager";
    else if(svc&&!strcmp(svc,"input"))type="android/hardware/input/InputManager";
    else if(svc&&!strcmp(svc,"phone"))type="android/telephony/TelephonyManager";
    else if(svc&&!strcmp(svc,"notification"))type="android/app/NotificationManager";
    else if(svc&&!strcmp(svc,"uimode"))type="android/app/UiModeManager";
    BObj*service=new_obj(4,type,svc?svc:(svc_cls&&svc_cls->kind==1?svc_cls->a:""));
    logmsg("[BENDER_JNI][SERVICE] getSystemService locator=%s class_arg=%s -> %s obj=%p\n",svc?svc:"(class)",(svc_cls&&svc_cls->kind==1)?svc_cls->a:"(none)",type,(void*)service);return service;
}
if(m&&!strcmp(name,"getActiveNetworkInfo")){if(!bender_network_online_2750()){logmsg("[BENDER_NETWORK][ANDROID] getActiveNetworkInfo -> NULL offline\n");return NULL;}BObj*n=new_obj(4,"android/net/NetworkInfo",bender_network_type_name_2750());if(n){n->i64=bender_network_type_2750();n->flags=1u;}logmsg("[BENDER_NETWORK][ANDROID] getActiveNetworkInfo -> %p type=%s(%d) connected=1\n",(void*)n,bender_network_type_name_2750(),bender_network_type_2750());return n;}
if(m&&(!strcmp(name,"getNetworkInfo")||!strcmp(name,"getNetworkInfoForNetwork"))){if(!bender_network_online_2750())return NULL;BObj*n=new_obj(4,"android/net/NetworkInfo",bender_network_type_name_2750());if(n){n->i64=bender_network_type_2750();n->flags=1u;}return n;}
if(m&&!strcmp(name,"getActiveNetwork")){if(!bender_network_online_2750())return NULL;BObj*n=new_obj(4,"android/net/Network","active");if(n){n->i64=1;}logmsg("[BENDER_NETWORK][ANDROID] getActiveNetwork -> %p\n",(void*)n);return n;}
if(m&&!strcmp(name,"getNetworkCapabilities")){if(!bender_network_online_2750())return NULL;BObj*n=new_obj(4,"android/net/NetworkCapabilities",bender_network_type_name_2750());if(n){n->i64=bender_network_type_2750();n->flags=1u;}logmsg("[BENDER_NETWORK][ANDROID] getNetworkCapabilities -> %p transport=%s\n",(void*)n,bender_network_type_name_2750());return n;}
if(m&&!strcmp(name,"getLinkProperties")){if(!bender_network_online_2750())return NULL;return new_obj(4,"android/net/LinkProperties",g_bender_net_2750.ifname);}
if(m&&!strcmp(name,"getTypeName")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"android/net/NetworkInfo"))return jni_NewStringUTF(env,bender_network_type_name_2750());}
if(m&&!strcmp(name,"getSubtypeName")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"android/net/NetworkInfo"))return jni_NewStringUTF(env,"");}
if(m&&!strcmp(name,"getNetworkProxySettings")){logmsg("[BENDER_NETWORK][PROXY] getNetworkProxySettings -> empty (direct host connection)\n");return jni_NewStringUTF(env,"");}
if(m&&!strcmp(name,"getSelectedRoute")){BObj*route=new_obj(4,"android/media/MediaRouter$RouteInfo","default-route");logmsg("[BENDER_JNI][MEDIA_ROUTER] getSelectedRoute -> %p\n",(void*)route);return route;}
if(m&&!strcmp(name,"getPresentationDisplay")){logmsg("[BENDER_JNI][MEDIA_ROUTER] getPresentationDisplay -> NULL (no external presentation display)\n");return NULL;}
if(m&&!strcmp(name,"getAssetPackPath")){const char*pack=jni_obj_string(arg0);char pp[1536];const char*src=NULL;if(bender_pad_pack_path_2749(pack,pp,sizeof(pp),&src)){logmsg("[BENDER_PAD][PATH] pack=%s -> %s storage=%s\n",pack?pack:"(unknown)",pp,src?src:"?");return jni_NewStringUTF(env,pp);}logmsg("[BENDER_PAD][PATH] pack=%s -> NULL storage=MISSING\n",pack?pack:"(unknown)");return NULL;}
if(m&&!strcmp(name,"getDisplay")){BObj*d=new_obj(4,"android/view/Display","displayId=0");logmsg("[BENDER_JNI][DISPLAY] getDisplay -> %p id=0\n",(void*)d);return d;}
if(m&&!strcmp(name,"getSupportedModes"))return jni_new_display_modes(env);
if(m&&!strcmp(name,"getMaximumWindowMetrics"))return new_obj(4,"android/view/WindowMetrics","640x480");
if(m&&!strcmp(name,"getBounds"))return new_obj(4,"android/graphics/Rect","0,0,640,480");
if(m&&!strcmp(name,"getResources"))return new_obj(4,"android/content/res/Resources",NULL);
if(m&&!strcmp(name,"getConfiguration"))return new_obj(4,"android/content/res/Configuration",NULL);
if(m&&!strcmp(name,"getLooper")){BObj*l=new_obj(4,"android/os/Looper","main");logmsg("[BENDER_JNI][LOOPER] getLooper -> %p\n",(void*)l);return l;}
if(m&&!strcmp(name,"obtainMessage")){BObj*msg=new_obj(4,"android/os/Message","pending");if(msg){msg->ref0=(uintptr_t)obj;msg->i64=(int64_t)(intptr_t)arg0;}logmsg("[BENDER_JNI][MESSAGE] obtainMessage handler=%p what=%lld -> %p\n",obj,(long long)(msg?msg->i64:0),(void*)msg);return msg;}
if(ret&&!strcmp(ret,"Lcom/google/android/gms/tasks/Task;")){
    BObj*t=new_obj(4,"com/google/android/gms/tasks/Task",name?name:"Task");
    if(!t)return NULL;
    ++g_firebase_task_created_2757;
    if(!strcmp(name,"getToken")){
        char tok[160];snprintf(tok,sizeof(tok),"bender-r36s-%s-fcm",bender_android_id_2756());
        t->flags=1u;t->ref0=(uintptr_t)jni_NewStringUTF(env,tok);
        logmsg("[BENDER_FIREBASE_TASK][CREATE] n=%lu source=%s state=SUCCESS result=synthetic-local-token token_len=%zu\n",g_firebase_task_created_2757,name,strlen(tok));
    }else if(!strcmp(name,"deleteToken")||!strcmp(name,"subscribeToTopic")||!strcmp(name,"unsubscribeFromTopic")){
        t->flags=1u;t->ref0=0;
        logmsg("[BENDER_FIREBASE_TASK][CREATE] n=%lu source=%s state=SUCCESS result=void policy=local-no-push\n",g_firebase_task_created_2757,name);
    }else if(!strcmp(name,"getAppInstanceId")){
        char iid[128];snprintf(iid,sizeof(iid),"bender-app-%s",bender_android_id_2756());
        t->flags=1u;t->ref0=(uintptr_t)jni_NewStringUTF(env,iid);
        logmsg("[BENDER_FIREBASE_TASK][CREATE] n=%lu source=%s state=SUCCESS result=synthetic-instance-id\n",g_firebase_task_created_2757,name);
    }else if(!strcmp(name,"getSessionId")){
        BObj*lv=new_obj(4,"java/lang/Long","value");if(lv)lv->i64=(int64_t)(bender_fnv1a64_2756(BENDER_APP_PACKAGE)&INT64_MAX);
        t->flags=1u;t->ref0=(uintptr_t)lv;
        logmsg("[BENDER_FIREBASE_TASK][CREATE] n=%lu source=%s state=SUCCESS result=synthetic-session-id\n",g_firebase_task_created_2757,name);
    }else if(!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfig")&&
             (!strcmp(name,"setConfigSettingsAsync")||!strcmp(name,"setDefaultsAsync")||!strcmp(name,"fetch")||!strcmp(name,"ensureInitialized")||!strcmp(name,"activate")||!strcmp(name,"fetchAndActivate"))){
        ++g_remote_config_task_count_2761;t->flags=1u;t->ref0=0;
        if(!strcmp(name,"ensureInitialized"))t->ref0=(uintptr_t)new_obj(4,"com/google/firebase/remoteconfig/FirebaseRemoteConfigInfo","bender-local-info");
        else if(!strcmp(name,"activate")||!strcmp(name,"fetchAndActivate"))t->ref0=(uintptr_t)bender_java_boolean_2761(1);
        logmsg("[BENDER_REMOTE_CONFIG][TASK] n=%lu method=%s state=SUCCESS result=%p policy=local-defaults-no-cloud-fetch\n",g_remote_config_task_count_2761,name,(void*)t->ref0);
        logmsg("[BENDER_FIREBASE_TASK][CREATE] n=%lu source=%s state=SUCCESS result=remote-config-local\n",g_firebase_task_created_2757,name);
    }else if(!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/auth/FirebaseUser")&&!strcmp(name,"getIdToken")){
        t->flags=1u;t->ref0=(uintptr_t)bender_auth_token_result_2761(env);
        logmsg("[BENDER_FIREBASE_TASK][CREATE] n=%lu source=%s state=SUCCESS result=GetTokenResult policy=synthetic-local-not-server-signed\n",g_firebase_task_created_2757,name);
    }else{
        t->flags=2u;t->ref0=0;
        logmsg("[BENDER_FIREBASE_TASK][CREATE] n=%lu source=%s state=FAILURE policy=complete-dont-hang\n",g_firebase_task_created_2757,name);
    }
    return t;
}
if(bender_reliance_push_payload_methodid_match_fix103(m)){
    const char*decl=bender_jni_class_name((void*)m->ref0);
    void*empty_json_array=jni_NewStringUTF(env,"[]");
    logmsg("[BENDER_RELIANCE_PUSH][METHODID_GUARD] source=fix103 action=saved-payload-empty-json-array declared_class=%s method=%s sig=%s static=%lld result=[] policy=mirror-original-gson-empty-list variant=%s caller_obj=%p\n",
           decl?decl:"",m->a,m->b,(long long)m->ref1,variant?variant:"?",obj);
    return empty_json_array;
}
if(ret&&strcmp(ret,"Ljava/lang/String;")==0)return jni_NewStringUTF(env,"BENDER");
if(ret&&ret[0]==91&&ret[1]==76){logmsg("[BENDER_JNI_OBJ_ARRAY][GENERIC] method=%s sig=%s -> empty object array\n",name,sig);return jni_NewObjectArray(env,0,NULL,NULL);}
if(ret&&ret[0]==76)return new_obj(4,ret,NULL);
return NULL;}
static void *jni_CallObjectMethod(BEnv*env,void*obj,void*mid,...){BObj*m=(BObj*)mid;void*arg0=NULL,*arg1=NULL;int64_t n1=0;int hn=0;va_list ap;va_start(ap,mid);if(bender_objmethod_two_ptr_2756(m)){arg0=va_arg(ap,void*);arg1=va_arg(ap,void*);}else if(bender_objmethod_ptr_num_2756(m)){arg0=va_arg(ap,void*);if(!strcmp(m->a,"putLong"))n1=(int64_t)va_arg(ap,bjlong);else n1=(int64_t)va_arg(ap,int);hn=1;}else if(bender_objmethod_one_ptr_2756(m)|| (m&&(!strcmp(m->a,"findLibrary")||!strcmp(m->a,"setTitle")||!strcmp(m->a,"setMessage")||!strcmp(m->a,"getSystemService")||!strcmp(m->a,"getAssetPackPath")||!strcmp(m->a,"registerDownloadStatusListener")||!strcmp(m->a,"getProperty"))))arg0=va_arg(ap,void*);else if(m&&(!strcmp(m->a,"obtainMessage")||!strcmp(m->a,"getDevices")))arg0=(void*)(intptr_t)va_arg(ap,int);va_end(ap);return bender_jni_call_object_2756(env,obj,mid,"",arg0,arg1,n1,hn);}
static void *jni_CallObjectMethodV(BEnv*env,void*obj,void*mid,va_list ap){BObj*m=(BObj*)mid;void*arg0=NULL,*arg1=NULL;int64_t n1=0;int hn=0;va_list cp;va_copy(cp,ap);if(bender_objmethod_two_ptr_2756(m)){arg0=va_arg(cp,void*);arg1=va_arg(cp,void*);}else if(bender_objmethod_ptr_num_2756(m)){arg0=va_arg(cp,void*);if(!strcmp(m->a,"putLong"))n1=(int64_t)va_arg(cp,bjlong);else n1=(int64_t)va_arg(cp,int);hn=1;}else if(bender_objmethod_one_ptr_2756(m)||(m&&(!strcmp(m->a,"findLibrary")||!strcmp(m->a,"setTitle")||!strcmp(m->a,"setMessage")||!strcmp(m->a,"getSystemService")||!strcmp(m->a,"getAssetPackPath")||!strcmp(m->a,"registerDownloadStatusListener")||!strcmp(m->a,"getProperty")||!strcmp(m->a,"obtainMessage")||!strcmp(m->a,"getDevices")))){if(m&&(!strcmp(m->a,"obtainMessage")||!strcmp(m->a,"getDevices")))arg0=(void*)(intptr_t)va_arg(cp,int);else arg0=va_arg(cp,void*);}
    /* ULTRAKILL fix -L: os listeners de dialog. A lista acima e um whitelist e
     * setOnCancelListener/setPositiveButton NAO estavam nela -> arg0 ficava
     * NULL, o probe nao capturava o listener e o auto-dismiss nao tinha o que
     * disparar (log: 'show() com 0 listener(s)' + cond wait eterno).
     * setXButton leva (int which, listener) -> o ponteiro e o SEGUNDO arg. */
    else if(m&&!strncmp(m->a,"setOn",3)){arg0=va_arg(cp,void*);}
    else if(m&&(!strcmp(m->a,"setPositiveButton")||!strcmp(m->a,"setNegativeButton")||!strcmp(m->a,"setNeutralButton"))){(void)va_arg(cp,int);arg0=va_arg(cp,void*);}
    va_end(cp);return bender_jni_call_object_2756(env,obj,mid,"V",arg0,arg1,n1,hn);}
static void *jni_CallObjectMethodA(BEnv*env,void*obj,void*mid,const void*args){BObj*m=(BObj*)mid;void*arg0=NULL,*arg1=NULL;int64_t n1=0;int hn=0;if(args&&m){const uint64_t*v=(const uint64_t*)args;if(bender_objmethod_two_ptr_2756(m)){arg0=(void*)(uintptr_t)v[0];arg1=(void*)(uintptr_t)v[1];}else if(bender_objmethod_ptr_num_2756(m)){arg0=(void*)(uintptr_t)v[0];n1=!strcmp(m->a,"putLong")?(int64_t)v[1]:(int64_t)(int32_t)v[1];hn=1;}else if(bender_objmethod_one_ptr_2756(m)||!strcmp(m->a,"findLibrary")||!strcmp(m->a,"setTitle")||!strcmp(m->a,"setMessage")||!strcmp(m->a,"getSystemService")||!strcmp(m->a,"getAssetPackPath")||!strcmp(m->a,"registerDownloadStatusListener")||!strcmp(m->a,"getProperty"))arg0=(void*)(uintptr_t)v[0];else if(!strcmp(m->a,"obtainMessage")||!strcmp(m->a,"getDevices"))arg0=(void*)(intptr_t)(int32_t)v[0];else if(!strncmp(m->a,"setOn",3))arg0=(void*)(uintptr_t)v[0];else if(!strcmp(m->a,"setPositiveButton")||!strcmp(m->a,"setNegativeButton")||!strcmp(m->a,"setNeutralButton"))arg0=(void*)(uintptr_t)v[1];}return bender_jni_call_object_2756(env,obj,mid,"A",arg0,arg1,n1,hn);}
static void *g_firebase_crashlytics_instance_2759;
static void *g_firebase_crashlytics_ndk_instance_2759;
static void *g_firebase_crash_core_2759;
static void *g_firebase_data_arbiter_2759;
static int bender_env_bool_2759(const char*name,int fallback){const char*v=getenv(name);if(!v||!*v)return fallback;if(!strcasecmp(v,"0")||!strcasecmp(v,"false")||!strcasecmp(v,"off")||!strcasecmp(v,"no"))return 0;if(!strcasecmp(v,"1")||!strcasecmp(v,"true")||!strcasecmp(v,"on")||!strcasecmp(v,"yes"))return 1;return fallback;}
static int bender_firebase_data_collection_2759(void){return bender_env_bool_2759("BENDER_FIREBASE_DATA_COLLECTION",1);}
static int bender_crashlytics_data_collection_2759(void){int base=bender_firebase_data_collection_2759();return bender_env_bool_2759("BENDER_CRASHLYTICS_COLLECTION",base);}
static int g_applovin_initialized_2762;
static void *g_applovin_background_callback_2762;
static unsigned long g_pad_completed_2762;
static int g_pad_data_complete_2764;
static int g_pad_stream_complete_2764;
static void bender_pad_mark_complete_2764(const char*pack,int available){
    if(!pack)return;
    if(!strcmp(pack,"UnityDataAssetPack"))g_pad_data_complete_2764=available?1:0;
    else if(!strcmp(pack,"UnityStreamingAssetsPack"))g_pad_stream_complete_2764=available?1:0;
}
static unsigned long __attribute__((unused)) bender_pad_completed_2764(void){return (unsigned long)(g_pad_data_complete_2764?1:0)+(unsigned long)(g_pad_stream_complete_2764?1:0);}
static int g_pad_embedded_stream_refresh_done_2811;
static unsigned long g_pad_embedded_stream_refresh_count_2811;
static void __attribute__((unused)) bender_pad_refresh_installed_2811(void){
    if(g_pad_embedded_stream_refresh_done_2811)return;
    g_pad_embedded_stream_refresh_done_2811=1;
    char pp[1536];const char*src=NULL;
    int data=bender_pad_pack_path_2749("UnityDataAssetPack",pp,sizeof(pp),&src);
    bender_pad_mark_complete_2764("UnityDataAssetPack",data);
    const char*data_src=src?src:"missing";
    char data_src_copy[64];bender_strlcpy(data_src_copy,data_src,sizeof(data_src_copy));
    int stream=bender_pad_pack_path_2749("UnityStreamingAssetsPack",pp,sizeof(pp),&src);
    bender_pad_mark_complete_2764("UnityStreamingAssetsPack",stream);
    ++g_pad_embedded_stream_refresh_count_2811;
    logmsg("[BENDER_PAD_EMBEDDED_STREAM][REFRESH] n=%lu data=%d dataSource=%s stream=%d streamSource=%s streamPath=%s policy=real-files-only\n",g_pad_embedded_stream_refresh_count_2811,data,data_src_copy,stream,src?src:"missing",stream?pp:"(missing)");
}
static unsigned long g_mcprime_dlopen_requests_2762;
static unsigned long g_mcservices_dlopen_requests_2762;
static int __attribute__((unused)) g_football_native_autobootstrap_2763;
static void*g_billing_client_2765;
static bjboolean jni_call_boolean_common_arg(BEnv*env,void*obj,void*mid,const char*variant,void*arg0){(void)env;BObj*m=(BObj*)mid;const char*name=m?m->a:"?";bjboolean result=0;const char*mcls=m?bender_jni_class_name((void*)m->ref0):"";
    if(m&&!strcmp(mcls,"org/fmod/FMODAudioDevice")&&!strcmp(name,"isRunning")){result=g_bender_fmod_running_fix5?1:0;logmsg("[BENDER_FMOD_AUDIO][IS_RUNNING] -> %u\n",(unsigned)result);}
    else if(m&&!strcmp(mcls,"android/media/AudioDeviceInfo")&&!strcmp(name,"isSink")){result=1;}
    else if(m&&!strcmp(mcls,"android/media/AudioDeviceInfo")&&!strcmp(name,"isSource")){result=0;}
    else if(m&&!strcmp(name,"isReady")&&!strcmp(m->b,"()Z")&&obj==g_billing_client_2765){result=1;logmsg("[BENDER_BILLING][IS_READY] obj=%p -> 1 policy=connection-ready\n",obj);}
    if(m&&!strcmp(name,"booleanValue")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"java/lang/Boolean")){result=bo->i64?1:0;logmsg("[BENDER_PROXY_VALUE][BOOL] obj=%p value=%u\n",obj,(unsigned)result);}}
    else if(m&&!strcmp(name,"contains")&&bender_obj_is_2756(obj,"android/content/SharedPreferences")){const char*key=jni_obj_string(arg0);const char*pf=bender_pref_file_2756(obj);int found=(key&&bender_pref_contains_2756(pf,key));int dt2_intro=(key&&!strcmp(key,"Intro")&&((pf&&strstr(pf,"deadtrigger2"))||strstr(BENDER_APP_PACKAGE,"deadtrigger2")));if(dt2_intro){if(!found)bender_pref_set_2756(pf,key,'B',"1");result=1;logmsg("[BENDER_VIDEO_INTRO_SKIP] source=fix80 package=%s prefs=%s key=%s previous=%d result=contains-true action=persist-intro-complete policy=dead-trigger2-synthetic-video-only\n",BENDER_APP_PACKAGE,pf?pf:"default",key,found);}else result=(bjboolean)found;logmsg("[BENDER_PREFS][CONTAINS] file=%s key=%.120s -> %u\n",pf?pf:"default",key?key:"(null)",(unsigned)result);}
    else if(m&&!strcmp(name,"commit")&&bender_obj_is_2756(obj,"android/content/SharedPreferences$Editor")){result=1;logmsg("[BENDER_PREFS][COMMIT] file=%s -> 1\n",bender_pref_file_2756(obj));}
    else if(m&&!strcmp(name,"equals")){const char*a=jni_obj_string(obj);const char*b=jni_obj_string(arg0);result=(a&&b&&!strcmp(a,b))?1:0;}
    else if(m&&!strcmp(name,"isInitialized")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/applovin/mediation/unity/MaxUnityPlugin")){result=g_applovin_initialized_2762?1:0;logmsg("[BENDER_APPLOVIN][CHECK_INIT] class=MaxUnityPlugin state=%d callback=%p -> %u\n",g_applovin_initialized_2762,g_applovin_background_callback_2762,(unsigned)result);}
    else if(m&&!strcmp(name,"isConnected")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/movingeye/network/NetworkMonitor")){result=bender_network_online_2750()?1:0;logmsg("[BENDER_NETWORK_MONITOR][IS_CONNECTED] class=com/movingeye/network/NetworkMonitor online=%u -> %u\n",(unsigned)result,(unsigned)result);}
    else if(m&&(!strcmp(name,"checkInit")||!strcmp(name,"checkInitState")||!strcmp(name,"isInitialized"))&&strstr(bender_jni_class_name((void*)m->ref0),"org/fmod/")){result=g_fmod_java_init_state?1:0;logmsg("[BENDER_FMOD_JAVA][CHECK_INIT] class=%s method=%s state=%d -> %u\n",bender_jni_class_name((void*)m->ref0),name,g_fmod_java_init_state,(unsigned)result);}
    else if(m&&!strcmp(name,"supportsLowLatency")){result=0;logmsg("[BENDER_FMOD_JAVA] supportsLowLatency -> 0 policy=silent-bootstrap\n");}
    else if(m&&!strcmp(name,"supportsAAudio")){result=0;logmsg("[BENDER_FMOD_JAVA] supportsAAudio -> 0 policy=silent-bootstrap\n");}
    else if(m&&!strcmp(name,"supportsSpatial")){result=0;logmsg("[BENDER_FMOD_JAVA] supportsSpatial -> 0\n");}
    else if(m&&!strcmp(name,"init")&&!strcmp(m->b,"(IIII)Z")&&strstr(bender_jni_class_name((void*)m->ref0),"org/fmod/AudioDevice")){g_fmod_audio_device_init_state=1;g_bender_fix68_android_audio_takeover=1;if(g_bender_sdl_audio_dev_fix5&&g_sdl_ClearQueuedAudio_fix5)g_sdl_ClearQueuedAudio_fix5(g_bender_sdl_audio_dev_fix5);result=1;logmsg("[BENDER_FMOD_AUDIO][INIT] obj=%p state=1 policy=fix68-pcm-to-sdl discard_pcm=0 device=%u -> 1\n",obj,g_bender_sdl_audio_dev_fix5);}
    else if(m&&!strcmp(name,"isDataCollectionDefaultEnabled")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/FirebaseApp")){result=(bjboolean)bender_firebase_data_collection_2759();logmsg("[BENDER_FIREBASE][DATA_COLLECTION] method=isDataCollectionDefaultEnabled -> %u policy=android-default-enabled env=BENDER_FIREBASE_DATA_COLLECTION\n",(unsigned)result);}
    else if(m&&!strcmp(name,"isAutomaticDataCollectionEnabled")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/crashlytics/internal/common/DataCollectionArbiter")){result=(bjboolean)bender_crashlytics_data_collection_2759();logmsg("[BENDER_CRASHLYTICS][DATA_COLLECTION] method=isAutomaticDataCollectionEnabled -> %u firebase_default=%d env=BENDER_CRASHLYTICS_COLLECTION\n",(unsigned)result,bender_firebase_data_collection_2759());}
    else if(m&&!strcmp(name,"isAnonymous")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/auth/FirebaseUser")){result=1;logmsg("[BENDER_FIREBASE_AUTH][ANONYMOUS] -> 1\n");}
    else if(m&&(!strcmp(name,"getBoolean")||!strcmp(name,"asBoolean"))&&(!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfig")||!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfigValue"))){result=0;logmsg("[BENDER_REMOTE_CONFIG][GET] type=boolean method=%s -> 0 source=DEFAULT\n",name);}
    else if(m&&!strcmp(name,"didCrashOnPreviousExecution")&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/crashlytics/FirebaseCrashlytics")){result=0;logmsg("[BENDER_CRASHLYTICS][PREVIOUS_CRASH] -> 0 policy=fresh-host-session\n");}
    else if(m&&!strcmp(name,"canRequestAds")){result=1;logmsg("[BENDER_UMP][CAN_REQUEST_ADS] -> 1 policy=offline-not-required\n");}
    else if(m&&!strcmp(name,"isConsentFormAvailable")){result=0;logmsg("[BENDER_UMP][FORM_AVAILABLE] -> 0 policy=offline-not-required\n");}
    else if(m&&!strcmp(name,"playCoreApiMissing")){const char*pc=getenv("BENDER_PLAYCORE_MISSING");int miss=(pc&&(!strcmp(pc,"1")||!strcmp(pc,"true")))?1:0;result=miss;logmsg("[BENDER_PAD][PLAY_CORE] apiMissing=%d policy=%s\n",miss,miss?"play-services-abenteue-rasado":"synthetic-installed-packs");}
    else if(m&&(!strcmp(name,"isConnected")||!strcmp(name,"isConnectedOrConnecting")||!strcmp(name,"isAvailable"))){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"android/net/NetworkInfo")){result=bender_network_online_2750()?1:0;logmsg("[BENDER_NETWORK][ANDROID] %s NetworkInfo -> %u\n",name,(unsigned)result);}}
    else if(m&&(!strcmp(name,"hasCapability")||!strcmp(name,"hasTransport"))){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"android/net/NetworkCapabilities")){result=bender_network_online_2750()?1:0;logmsg("[BENDER_NETWORK][ANDROID] %s NetworkCapabilities transport=%s -> %u\n",name,bender_network_type_name_2750(),(unsigned)result);}}
    else if(m&&(!strcmp(name,"isActiveNetworkMetered")||!strcmp(name,"isMetered"))){result=0;logmsg("[BENDER_NETWORK][ANDROID] %s -> 0\n",name);}
    else if(m&&(!strcmp(name,"exists")||!strcmp(name,"canRead")||!strcmp(name,"canWrite")||!strcmp(name,"isDirectory"))){const char*p=jni_file_path(obj);if(p){if(!strcmp(name,"exists"))result=(access(p,F_OK)==0);else if(!strcmp(name,"canRead"))result=(access(p,R_OK)==0);else if(!strcmp(name,"canWrite"))result=(access(p,W_OK)==0);else {struct stat st;result=(stat(p,&st)==0&&S_ISDIR(st.st_mode));}}}
    ++g_jni_call_count;set_checkpoint("JNI CallBooleanMethod%s method=%s sig=%s",variant?variant:"",name,m?m->b:"?");logmsg("[BENDER_JNI][CALL] Boolean%s obj=%p method=%s sig=%s -> %u\n",variant?variant:"",obj,name,m?m->b:"?",(unsigned)result);return result;}
static bjboolean jni_call_boolean_common(BEnv*env,void*obj,void*mid,const char*variant){return jni_call_boolean_common_arg(env,obj,mid,variant,NULL);}
static bjboolean jni_CallBooleanMethod(BEnv*env,void*obj,void*mid,...){BObj*m=(BObj*)mid;void*arg0=NULL;va_list ap;va_start(ap,mid);if(bender_fix68_is_fmod_audio_init(m)){int a0=va_arg(ap,int),a1=va_arg(ap,int),a2=va_arg(ap,int),a3=va_arg(ap,int);bender_fix68_record_audio_init(a0,a1,a2,a3,"");}else if(m&&(!strcmp(m->a,"equals")||(!strcmp(m->a,"contains")&&!strcmp(m->b,"(Ljava/lang/String;)Z"))))arg0=va_arg(ap,void*);va_end(ap);return jni_call_boolean_common_arg(env,obj,mid,"",arg0);}
static bjboolean jni_CallBooleanMethodV(BEnv*env,void*obj,void*mid,va_list ap){BObj*m=(BObj*)mid;void*arg0=NULL;if(bender_fix68_is_fmod_audio_init(m)){va_list cp;va_copy(cp,ap);int a0=va_arg(cp,int),a1=va_arg(cp,int),a2=va_arg(cp,int),a3=va_arg(cp,int);va_end(cp);bender_fix68_record_audio_init(a0,a1,a2,a3,"V");}else if(m&&(!strcmp(m->a,"equals")||(!strcmp(m->a,"contains")&&!strcmp(m->b,"(Ljava/lang/String;)Z")))){va_list cp;va_copy(cp,ap);arg0=va_arg(cp,void*);va_end(cp);}return jni_call_boolean_common_arg(env,obj,mid,"V",arg0);}
static bjboolean jni_CallBooleanMethodA(BEnv*env,void*obj,void*mid,const void*args){BObj*m=(BObj*)mid;void*arg0=NULL;if(args&&bender_fix68_is_fmod_audio_init(m)){const uint64_t*v=(const uint64_t*)args;bender_fix68_record_audio_init((int)(uint32_t)v[0],(int)(uint32_t)v[1],(int)(uint32_t)v[2],(int)(uint32_t)v[3],"A");}else if(args&&m&&(!strcmp(m->a,"equals")||(!strcmp(m->a,"contains")&&!strcmp(m->b,"(Ljava/lang/String;)Z"))))arg0=(void*)(uintptr_t)(*(const uintptr_t*)args);return jni_call_boolean_common_arg(env,obj,mid,"A",arg0);}
static bjint jni_call_int_common_arg(BEnv*env,void*obj,void*mid,const char*variant,bjint arg0){(void)env;BObj*m=(BObj*)mid;const char*name=m?m->a:"?";bjint result=0;const char*mcls=m?bender_jni_class_name((void*)m->ref0):"";
    if(m&&!strcmp(mcls,"android/media/AudioManager")&&!strcmp(name,"getStreamVolume")){result=12;logmsg("[BENDER_AUDIO_MANAGER][VOLUME] stream=%d current=%d/15\n",(int)arg0,(int)result);}
    else if(m&&!strcmp(mcls,"android/media/AudioManager")&&!strcmp(name,"getStreamMaxVolume")){result=15;}
    else if(m&&!strcmp(mcls,"android/media/AudioManager")&&!strcmp(name,"getStreamMinVolume")){result=0;}
    else if(m&&!strcmp(mcls,"android/media/AudioDeviceInfo")&&!strcmp(name,"getId")){result=1;}
    else if(m&&!strcmp(mcls,"android/media/AudioDeviceInfo")&&!strcmp(name,"getType")){result=2;}
    else if(!strcmp(name,"intValue")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&(!strcmp(bo->a,"java/lang/Integer")||!strcmp(bo->a,"java/lang/Short")||!strcmp(bo->a,"java/lang/Byte")||!strcmp(bo->a,"java/lang/Character"))){result=(bjint)bo->i64;logmsg("[BENDER_PROXY_VALUE][INT] class=%s obj=%p value=%d\n",bo->a,obj,(int)result);}}
    else if(!strcmp(name,"getResponseCode")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"com/android/billingclient/api/BillingResult")){result=(bjint)bo->i64;logmsg("[BENDER_BILLING][RESULT_CODE] obj=%p -> %d\n",obj,(int)result);}}
    else if(!strcmp(name,"length")){BObj*bo=(BObj*)obj;if(bo&&bo->kind==3)result=bender_utf8_utf16_len(bo->a);}
    else if(!strcmp(name,"width")||!strcmp(name,"getWidth"))result=bender_render_scale_guest_width_2768();
    else if(!strcmp(name,"height")||!strcmp(name,"getHeight"))result=bender_render_scale_guest_height_2768();
    else if(!strcmp(name,"getDisplayId")||!strcmp(name,"getRotation"))result=0;
    else if(!strcmp(name,"getState"))result=2;
    else if(!strcmp(name,"getModeId"))result=1;
    else if(!strcmp(name,"getPhysicalWidth"))result=bender_render_scale_guest_width_2768();
    else if(!strcmp(name,"getPhysicalHeight"))result=bender_render_scale_guest_height_2768();
    else if(!strcmp(name,"getOutputSampleRate")){result=48000;logmsg("[BENDER_FMOD_JAVA] getOutputSampleRate -> %d\n",(int)result);}
    else if(!strcmp(name,"getOutputBlockSize")){result=512;logmsg("[BENDER_FMOD_JAVA] getOutputBlockSize -> %d\n",(int)result);}
    else if(!strcmp(name,"getConsentStatus")){result=1;logmsg("[BENDER_UMP][CONSENT_STATUS] -> NOT_REQUIRED(1) policy=offline-not-required\n");}
    else if(!strcmp(name,"getNetworkConnectivity")){result=(bjint)bender_network_unity_reachability_2750();}
    else if(!strcmp(name,"getType")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"android/net/NetworkInfo"))result=(bjint)bender_network_type_2750();}
    else if(!strcmp(name,"getSubtype")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"android/net/NetworkInfo"))result=0;}
    else {BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"android/view/KeyEvent")){
        if(!strcmp(name,"getAction"))result=(bjint)bo->ref0;else if(!strcmp(name,"getKeyCode"))result=(bjint)bo->ref1;else if(!strcmp(name,"getRepeatCount")||!strcmp(name,"getMetaState")||!strcmp(name,"getScanCode")||!strcmp(name,"getDeviceId")||!strcmp(name,"getFlags"))result=0;else if(!strcmp(name,"getSource"))result=(bjint)bo->flags;else if(!strcmp(name,"getUnicodeChar"))result=0;
    }else if(bo&&bo->kind==4&&!strcmp(bo->a,"android/view/MotionEvent")){
        int pc=(int)(bo->ref0?bo->ref0:1u);int raw=(int)bo->ref1;if(!strcmp(name,"getAction"))result=raw;else if(!strcmp(name,"getActionMasked"))result=raw&255;else if(!strcmp(name,"getActionIndex"))result=(raw>>8)&255;else if(!strcmp(name,"getPointerId"))result=(arg0==1&&pc>1)?bo->p1id:bo->p0id;else if(!strcmp(name,"findPointerIndex"))result=(arg0==bo->p0id)?0:((pc>1&&arg0==bo->p1id)?1:-1);else if(!strcmp(name,"getDeviceId")||!strcmp(name,"getButtonState")||!strcmp(name,"getFlags")||!strcmp(name,"getEdgeFlags"))result=0;else if(!strcmp(name,"getPointerCount"))result=pc;else if(!strcmp(name,"getToolType"))result=((bo->flags&0x01000010u)==0x01000010u)?3:1;else if(!strcmp(name,"getSource"))result=(bjint)bo->flags;
    }}
    ++g_jni_call_count;set_checkpoint("JNI CallIntMethod%s method=%s sig=%s",variant?variant:"",name,m?m->b:"?");logmsg("[BENDER_JNI][CALL] Int%s obj=%p method=%s sig=%s -> %d\n",variant?variant:"",obj,name,m?m->b:"?",(int)result);return result;}
static bjint jni_CallIntMethod(BEnv*env,void*obj,void*mid,...){BObj*m=(BObj*)mid;bjint legacy=0,def=0;void*key=NULL;va_list ap;va_start(ap,mid);if(m&&!strcmp(m->a,"getInt")&&!strcmp(m->b,"(Ljava/lang/String;I)I")){key=va_arg(ap,void*);def=va_arg(ap,bjint);}else if(m&&(!strcmp(m->a,"getPointerId")||!strcmp(m->a,"findPointerIndex")||!strcmp(m->a,"getToolType")||!strcmp(m->a,"getStreamVolume")||!strcmp(m->a,"getStreamMaxVolume")||!strcmp(m->a,"getStreamMinVolume")))legacy=va_arg(ap,bjint);va_end(ap);return bender_jni_call_int_2756(env,obj,mid,"",key,def,legacy);}
static bjint jni_CallIntMethodV(BEnv*env,void*obj,void*mid,va_list ap){BObj*m=(BObj*)mid;bjint legacy=0,def=0;void*key=NULL;va_list cp;va_copy(cp,ap);if(m&&!strcmp(m->a,"getInt")&&!strcmp(m->b,"(Ljava/lang/String;I)I")){key=va_arg(cp,void*);def=va_arg(cp,bjint);}else if(m&&(!strcmp(m->a,"getPointerId")||!strcmp(m->a,"findPointerIndex")||!strcmp(m->a,"getToolType")||!strcmp(m->a,"getStreamVolume")||!strcmp(m->a,"getStreamMaxVolume")||!strcmp(m->a,"getStreamMinVolume")))legacy=va_arg(cp,bjint);va_end(cp);return bender_jni_call_int_2756(env,obj,mid,"V",key,def,legacy);}
static bjint jni_CallIntMethodA(BEnv*env,void*obj,void*mid,const void*args){BObj*m=(BObj*)mid;bjint legacy=0,def=0;void*key=NULL;if(args&&m){const uint64_t*v=(const uint64_t*)args;if(!strcmp(m->a,"getInt")&&!strcmp(m->b,"(Ljava/lang/String;I)I")){key=(void*)(uintptr_t)v[0];def=(bjint)(int32_t)v[1];}else if(!strcmp(m->a,"getPointerId")||!strcmp(m->a,"findPointerIndex")||!strcmp(m->a,"getToolType")||!strcmp(m->a,"getStreamVolume")||!strcmp(m->a,"getStreamMaxVolume")||!strcmp(m->a,"getStreamMinVolume"))legacy=(bjint)(int32_t)v[0];}return bender_jni_call_int_2756(env,obj,mid,"A",key,def,legacy);}
static bjint jni_call_int_common(BEnv*env,void*obj,void*mid,const char*variant){return jni_call_int_common_arg(env,obj,mid,variant,0);}
static bjlong bender_elapsed_realtime_ms_2762(void){
    struct timespec ts;memset(&ts,0,sizeof(ts));
#ifdef CLOCK_BOOTTIME
    if(clock_gettime(CLOCK_BOOTTIME,&ts)!=0)
#endif
    {if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return 1;}
    bjlong ms=(bjlong)ts.tv_sec*1000+(bjlong)(ts.tv_nsec/1000000L);
    return ms>0?ms:1;
}
static bjlong jni_call_long_common(BEnv*env,void*obj,void*mid,const char*variant){(void)env;BObj*m=(BObj*)mid;const char*name=m?m->a:"?";bjlong result=0;
    if(m&&(!strcmp(name,"getLong")||!strcmp(name,"asLong"))&&(!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfig")||!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfigValue")))logmsg("[BENDER_REMOTE_CONFIG][GET] type=long method=%s -> 0 source=DEFAULT\n",name);
    if(m&&!strcmp(name,"elapsedRealtime")&&!strcmp(bender_jni_class_name((void*)m->ref0),"android/os/SystemClock")){result=bender_elapsed_realtime_ms_2762();static unsigned long n2762;unsigned long n=++n2762;if(n<=6ul||n%300ul==0ul)logmsg("[BENDER_SYSTEM_CLOCK] elapsedRealtime call=%lu -> %" PRId64 " ms source=CLOCK_BOOTTIME/MONOTONIC\n",n,(int64_t)result);}
    else if(!strcmp(name,"longValue")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"java/lang/Long")){result=(bjlong)bo->i64;logmsg("[BENDER_PROXY_VALUE][LONG] obj=%p value=%" PRId64 "\n",obj,(int64_t)result);}}
    else if(!strcmp(name,"getAppVsyncOffsetNanos"))result=BENDER_DISPLAY_VSYNC_OFFSET_NS;
    else if(!strcmp(name,"getPresentationDeadlineNanos"))result=BENDER_DISPLAY_PRESENTATION_DEADLINE_NS;
    else {BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&(!strcmp(bo->a,"android/view/KeyEvent")||!strcmp(bo->a,"android/view/MotionEvent"))){if(!strcmp(name,"getEventTime"))result=(bjlong)bo->i64;else if(!strcmp(name,"getDownTime"))result=(bjlong)(bo->i64b?bo->i64b:bo->i64);}}
    ++g_jni_call_count;set_checkpoint("JNI CallLongMethod%s method=%s sig=%s",variant?variant:"",name,m?m->b:"?");logmsg("[BENDER_JNI][CALL] Long%s obj=%p method=%s sig=%s -> %" PRId64 "\n",variant?variant:"",obj,name,m?m->b:"?",(int64_t)result);return result;}
static bjlong jni_CallLongMethod(BEnv*env,void*obj,void*mid,...){return jni_call_long_common(env,obj,mid,"");}
static bjlong jni_CallLongMethodV(BEnv*env,void*obj,void*mid,va_list ap){(void)ap;return jni_call_long_common(env,obj,mid,"V");}
static bjlong jni_CallLongMethodA(BEnv*env,void*obj,void*mid,const void*args){(void)args;return jni_call_long_common(env,obj,mid,"A");}
static bjfloat jni_call_float_common_arg(BEnv*env,void*obj,void*mid,const char*variant,bjint arg0){(void)env;BObj*m=(BObj*)mid;const char*name=m?m->a:"?";bjfloat result=0.0f;if(!strcmp(name,"getRefreshRate"))result=BENDER_DISPLAY_REFRESH_HZ;
    else {BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"android/view/MotionEvent")){int pc=(int)(bo->ref0?bo->ref0:1u);if(!strcmp(name,"getX")||!strcmp(name,"getRawX"))result=(arg0==1&&pc>1)?bo->f2:bo->f0;else if(!strcmp(name,"getY")||!strcmp(name,"getRawY"))result=(arg0==1&&pc>1)?bo->f3:bo->f1;else if(!strcmp(name,"getPressure")||!strcmp(name,"getSize"))result=1.0f;else if(!strcmp(name,"getAxisValue")){switch(arg0){case 0:result=bo->f0;break;case 1:result=bo->f1;break;case 11:case 12:result=bo->f2;break;case 13:case 14:result=bo->f3;break;case 17:result=bo->f4;break;case 18:result=bo->f5;break;case 15:result=bo->f6;break;case 16:result=bo->f7;break;default:result=0.0f;}}else if(!strcmp(name,"getTouchMajor")||!strcmp(name,"getTouchMinor")||!strcmp(name,"getToolMajor")||!strcmp(name,"getToolMinor")||!strcmp(name,"getOrientation"))result=0.0f;}}
    ++g_jni_call_count;set_checkpoint("JNI CallFloatMethod%s method=%s sig=%s",variant?variant:"",name,m?m->b:"?");logmsg("[BENDER_JNI][CALL] Float%s obj=%p method=%s sig=%s -> %.3f\n",variant?variant:"",obj,name,m?m->b:"?",(double)result);return result;}
static int bender_float_method_has_int_arg(BObj*m){return m&&m->b[0]=='('&&m->b[1]=='I'&&(!strcmp(m->a,"getX")||!strcmp(m->a,"getY")||!strcmp(m->a,"getPressure")||!strcmp(m->a,"getSize")||!strcmp(m->a,"getAxisValue"));}
static bjfloat jni_CallFloatMethod(BEnv*env,void*obj,void*mid,...){BObj*m=(BObj*)mid;bjint a0=0;va_list ap;va_start(ap,mid);if(bender_float_method_has_int_arg(m))a0=va_arg(ap,bjint);va_end(ap);return jni_call_float_common_arg(env,obj,mid,"",a0);}
static bjfloat jni_CallFloatMethodV(BEnv*env,void*obj,void*mid,va_list ap){BObj*m=(BObj*)mid;bjint a0=0;if(bender_float_method_has_int_arg(m)){va_list cp;va_copy(cp,ap);a0=va_arg(cp,bjint);va_end(cp);}return jni_call_float_common_arg(env,obj,mid,"V",a0);}
static bjfloat jni_CallFloatMethodA(BEnv*env,void*obj,void*mid,const void*args){BObj*m=(BObj*)mid;bjint a0=0;if(args&&bender_float_method_has_int_arg(m))a0=*(const bjint*)args;return jni_call_float_common_arg(env,obj,mid,"A",a0);}
static bjfloat jni_call_float_common(BEnv*env,void*obj,void*mid,const char*variant){return jni_call_float_common_arg(env,obj,mid,variant,0);}
static bjdouble jni_call_double_common(BEnv*env,void*obj,void*mid,const char*variant){(void)env;BObj*m=(BObj*)mid;bjdouble result=0.0;if(m&&(!strcmp(m->a,"getDouble")||!strcmp(m->a,"asDouble"))&&(!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfig")||!strcmp(bender_jni_class_name((void*)m->ref0),"com/google/firebase/remoteconfig/FirebaseRemoteConfigValue")))logmsg("[BENDER_REMOTE_CONFIG][GET] type=double method=%s -> 0 source=DEFAULT\n",m->a);++g_jni_call_count;set_checkpoint("JNI CallDoubleMethod%s method=%s sig=%s",variant?variant:"",m?m->a:"?",m?m->b:"?");logmsg("[BENDER_JNI][CALL] Double%s obj=%p method=%s sig=%s -> %.6f\n",variant?variant:"",obj,m?m->a:"?",m?m->b:"?",result);return result;}
static bjdouble jni_CallDoubleMethod(BEnv*env,void*obj,void*mid,...){return jni_call_double_common(env,obj,mid,"");}
static bjdouble jni_CallDoubleMethodV(BEnv*env,void*obj,void*mid,va_list ap){(void)ap;return jni_call_double_common(env,obj,mid,"V");}
static bjdouble jni_CallDoubleMethodA(BEnv*env,void*obj,void*mid,const void*args){(void)args;return jni_call_double_common(env,obj,mid,"A");}
static void *bender_jni_proxy_dispatch(void*proxy,const char*method_name,void*arg0){
    BObj*p=(BObj*)proxy;if(!p||p->kind!=4||strcmp(p->a,"bitter/jnibridge/Proxy")){logmsg("[BENDER_JNI][PROXY_DISPATCH][SKIP] invalid proxy=%p method=%s\n",proxy,method_name?method_name:"?");return NULL;}
    int is_reflection_proxy=((p->flags&4u)!=0u)||!strcmp(p->b,"reflection-androidjavaproxy");
    int is_handler=(method_name&&!strcmp(method_name,"handleMessage"));
    int is_frame=(method_name&&!strcmp(method_name,"doFrame"));
    int is_surface_frame=(method_name&&!strcmp(method_name,"onFrameAvailable"));
    int is_ump_failure=(method_name&&(!strcmp(method_name,"onConsentInfoUpdateFailure")||!strcmp(method_name,"onConsentFormDismissed")));
    /* ULTRAKILL fix -L: 'run' e o metodo de um Runnable despachado na UI thread
     * (BENDER_UI_THREAD). Precisa de declaring class propria, senao o
     * JNIBridge.invoke do libunity procura 'run' na interface errada. */
    int is_runnable=(method_name&&!strcmp(method_name,"run"));
    int callback_argc=(arg0||is_handler||is_frame||is_surface_frame||is_ump_failure)?1:0;
    if(is_reflection_proxy){
        if(!g_reflection_proxy_invoke_fn||!g_reflection_helper_class){logmsg("[BENDER_JNI][REFLECTION_PROXY][SKIP] nativeProxyInvoke-not-captured proxy=%p cookie=0x%llx method=%s\n",proxy,(unsigned long long)(uint64_t)p->i64,method_name?method_name:"?");return NULL;}
        BObjArray*args=jni_NewObjectArray(&g_env,callback_argc,NULL,arg0);
        void*name_obj=jni_NewStringUTF(&g_env,method_name?method_name:"callback");
        unsigned long n=__atomic_add_fetch(&g_reflection_proxy_dispatch_count,1ul,__ATOMIC_RELAXED);
        set_checkpoint("ReflectionHelper.nativeProxyInvoke begin n=%lu method=%s cookie=0x%llx",n,method_name?method_name:"?",(unsigned long long)(uint64_t)p->i64);
        logmsg("[BENDER_JNI][REFLECTION_PROXY][BEGIN] n=%lu proxy=%p cookie=0x%llx interface=%p class=%s method=%s argc=%d args=%p invoke=%p\n",n,proxy,(unsigned long long)(uint64_t)p->i64,(void*)p->ref1,bender_jni_class_name((void*)p->ref1),method_name?method_name:"?",callback_argc,(void*)args,(void*)g_reflection_proxy_invoke_fn);
        int saved_exc=g_jni_exception;void*saved_exc_obj=g_jni_exception_obj;g_jni_exception=0;g_jni_exception_obj=NULL;
        void*ret=g_reflection_proxy_invoke_fn(&g_env,g_reflection_helper_class,(bjlong)p->i64,name_obj,args);
        int cb_exc=g_jni_exception;void*cb_exc_obj=g_jni_exception_obj;BObj*cb_e=bender_known_bobj(cb_exc_obj);
        if(cb_exc)logmsg("[BENDER_JNI][REFLECTION_PROXY][EXCEPTION_ISOLATED] n=%lu method=%s obj=%p class=%s msg=%s\n",n,method_name?method_name:"?",cb_exc_obj,cb_e?cb_e->a:"(unknown)",cb_e?cb_e->b:"");
        g_jni_exception=saved_exc;g_jni_exception_obj=saved_exc_obj;
        logmsg("[BENDER_JNI][REFLECTION_PROXY][RETURN] n=%lu method=%s ret=%p callback_exception=%d outer_exception=%d\n",n,method_name?method_name:"?",ret,cb_exc,g_jni_exception);
        set_checkpoint("ReflectionHelper.nativeProxyInvoke return n=%lu method=%s",n,method_name?method_name:"?");
        return ret;
    }
    if(!g_jnibridge_invoke_fn){logmsg("[BENDER_JNI][JNIBRIDGE_DISPATCH][SKIP] invoke-not-captured proxy=%p cookie=0x%llx method=%s\n",proxy,(unsigned long long)(uint64_t)p->i64,method_name?method_name:"?");return NULL;}
    void*decl=(void*)p->ref1;
    BObjArray*ifaces=jni_obj_array_cast((void*)p->ref0);
    const char*want_iface=NULL;
    if(is_handler)want_iface="android/os/Handler$Callback";
    else if(is_frame)want_iface="android/view/Choreographer$FrameCallback";
    else if(is_surface_frame)want_iface="android/graphics/SurfaceTexture$OnFrameAvailableListener";
    else if(is_runnable)want_iface="java/lang/Runnable";
    int want_found=0;
    if(ifaces&&want_iface){for(bjsize ii=0;ii<ifaces->length;ii++){BObj*ic=bender_known_bobj(ifaces->items[ii]);if(ic&&ic->kind==1&&!strcmp(ic->a,want_iface)){decl=ifaces->items[ii];want_found=1;break;}}}
    /* ULTRAKILL fix -L (causa do hang no 1o frame, evidenciado no log de 04/11):
     * este proxy nasceu de um Dialog do jogo, entao a lista de interfaces dele e
     * [DialogInterface$OnClickListener, OnKeyListener, OnCancelListener,
     * CompoundButton$OnCheckedChangeListener, java/lang/Runnable] - e o que as
     * comparacoes SAME_OBJECT do log percorre. Sem forcar java/lang/Runnable como
     * declaring class do metodo 'run', o JNIBridge.invoke do PROPRIO libunity
     * procurava 'run' em DialogInterface$OnClickListener e lancarava
     *   java.lang.NoSuchMethodError: public abstract void java.lang.Runnable.run()
     * (EXATAMENTE o par EXCEPTION_ISOLATED + BENDER_SYNC][WAIT_BEGIN
     * kind=cond module=libunity.so offset=0x2e38ac do unity_probe.log: o runnable
     * nunca rodava de verdade e a thread de render ficava no cond wait). */
    if(is_runnable&&!want_found){
        static void*uk_runnable_cls=NULL;
        if(!uk_runnable_cls)uk_runnable_cls=new_obj(1,"java/lang/Runnable",NULL);
        decl=uk_runnable_cls;
        logmsg("[BENDER_JNI][JNIBRIDGE_DISPATCH][RUNNABLE_FIX] method=run decl forcado p/ java/lang/Runnable proxy=%p\n",proxy);
    }
    if(!decl)decl=g_jnibridge_class;
    logmsg("[BENDER_JNI][JNIBRIDGE_DISPATCH][DECL] method=%s interface=%s decl=%p\n",method_name?method_name:"?",want_iface?want_iface:"default",decl);
    const char*callback_sig="()V";
    if(is_handler)callback_sig="(Landroid/os/Message;)Z";
    else if(is_frame)callback_sig="(J)V";
    else if(is_surface_frame)callback_sig="(Landroid/graphics/SurfaceTexture;)V";
    else if(is_ump_failure)callback_sig="(Lcom/google/android/ump/FormError;)V";
    /* ULTRAKILL fix -L: os callbacks de dialog precisam da assinatura real, senao
     * o JNIBridge.do_dispatch do libunity nao casa o metodo com o peer C++. */
    else if(method_name&&!strcmp(method_name,"onClick"))callback_sig="(Landroid/content/DialogInterface;I)V";
    else if(method_name&&(!strcmp(method_name,"onCancel")||!strcmp(method_name,"onDismiss")))callback_sig="(Landroid/content/DialogInterface;)V";
    BObj**rm_slot=is_frame?&g_jnibridge_cached_rm_frame:(is_handler?&g_jnibridge_cached_rm_handler:NULL);
    BObjArray**args_slot=is_frame?&g_jnibridge_cached_args_frame:(is_handler?&g_jnibridge_cached_args_handler:NULL);
    BObj*rm=rm_slot?__atomic_load_n(rm_slot,__ATOMIC_ACQUIRE):NULL;
    if(!rm){BObj*created=new_obj(4,"java/lang/reflect/Method",method_name?method_name:"callback");if(created){created->ref0=(uintptr_t)decl;created->ref1=(uintptr_t)bender_jni_intern_method(decl,method_name?method_name:"callback",callback_sig,0,"ProxyCallback");if(is_handler)created->flags=1u;else if(is_frame)created->flags=2u;}if(rm_slot)__atomic_store_n(rm_slot,created,__ATOMIC_RELEASE);rm=created;}
    if(rm){rm->ref0=(uintptr_t)decl;if(!rm->ref1)rm->ref1=(uintptr_t)bender_jni_intern_method(decl,method_name?method_name:"callback",callback_sig,0,"ProxyCallback");if(is_handler)rm->flags=1u;else if(is_frame)rm->flags=2u;}
    BObjArray*args=args_slot?__atomic_load_n(args_slot,__ATOMIC_ACQUIRE):NULL;
    if(!args){args=jni_NewObjectArray(&g_env,callback_argc,NULL,arg0);if(args_slot)__atomic_store_n(args_slot,args,__ATOMIC_RELEASE);}
    else if(args->length>0)args->items[0]=arg0;
    unsigned long n=__atomic_add_fetch(&g_jnibridge_dispatch_count,1ul,__ATOMIC_RELAXED);
    set_checkpoint("JNIBridge.invoke begin n=%lu method=%s cookie=0x%llx",n,method_name?method_name:"?",(unsigned long long)(uint64_t)p->i64);
    uintptr_t cookie=(uintptr_t)(uint64_t)p->i64,vt=0,vfn=0;if(cookie){vt=*(uintptr_t*)cookie;if(vt)vfn=*(uintptr_t*)(vt+0x10u);}Module*vmod=bender_module_for_runtime_addr(vfn);
    logmsg("[BENDER_JNI][JNIBRIDGE_COOKIE] cookie=%p vtable=%p virtual_invoke=%p module=%s offset=0x%llx\n",(void*)cookie,(void*)vt,(void*)vfn,vmod?vmod->name:"host-or-unknown",(unsigned long long)(vmod?(vfn-vmod->bias):0u));
    logmsg("[BENDER_JNI][JNIBRIDGE_DISPATCH][BEGIN] n=%lu proxy=%p cookie=0x%llx decl=%p method=%s arg0=%p args=%p invoke=%p\n",n,proxy,(unsigned long long)(uint64_t)p->i64,decl,method_name?method_name:"?",arg0,(void*)args,(void*)g_jnibridge_invoke_fn);
    int saved_exc=g_jni_exception;void*saved_exc_obj=g_jni_exception_obj;g_jni_exception=0;g_jni_exception_obj=NULL;
    void*ret=g_jnibridge_invoke_fn(&g_env,g_jnibridge_class,(bjlong)p->i64,decl,rm,args);
    int cb_exc=g_jni_exception;void*cb_exc_obj=g_jni_exception_obj;BObj*cb_e=bender_known_bobj(cb_exc_obj);
    if(cb_exc)logmsg("[BENDER_JNI][JNIBRIDGE_DISPATCH][EXCEPTION_ISOLATED] n=%lu method=%s obj=%p class=%s msg=%s\n",n,method_name?method_name:"?",cb_exc_obj,cb_e?cb_e->a:"(unknown)",cb_e?cb_e->b:"");
    g_jni_exception=saved_exc;g_jni_exception_obj=saved_exc_obj;
    logmsg("[BENDER_JNI][JNIBRIDGE_DISPATCH][RETURN] n=%lu method=%s ret=%p callback_exception=%d outer_exception=%d\n",n,method_name?method_name:"?",ret,cb_exc,g_jni_exception);set_checkpoint("JNIBridge.invoke return n=%lu method=%s",n,method_name?method_name:"?");return ret;
}
static void bender_surface_frame_dispatch_fix76(const char*reason){
    unsigned count=g_bender_surface_frame_proxy_count_fix76;
    if(count>4u)count=4u;
    if(!count){logmsg("[BENDER_MEDIANDK][SURFACE_FRAME][SKIP] source=fix76 reason=no-listener trigger=%s\n",reason?reason:"?");return;}
    if(!g_bender_surface_texture_fix76)g_bender_surface_texture_fix76=new_obj(4,"android/graphics/SurfaceTexture","bender-synthetic-video-frame");
    unsigned long n=__atomic_add_fetch(&g_bender_surface_frame_dispatch_count_fix76,1ul,__ATOMIC_RELAXED);
    logmsg("[BENDER_MEDIANDK][SURFACE_FRAME] source=fix76 n=%lu listeners=%u texture=%p trigger=%s\n",n,count,(void*)g_bender_surface_texture_fix76,reason?reason:"?");
    for(unsigned i=0;i<count;i++){
        void*p=g_bender_surface_frame_proxies_fix76[i];
        if(!p)continue;
        void*ret=bender_jni_proxy_dispatch(p,"onFrameAvailable",g_bender_surface_texture_fix76);
        logmsg("[BENDER_MEDIANDK][SURFACE_FRAME_RETURN] source=fix76 n=%lu slot=%u proxy=%p ret=%p outer_exception=%d\n",n,i,p,ret,g_jni_exception);
    }
}
static void bender_pad_status_dispatch(void*pack_obj,void*callback_obj){const char*pack=jni_obj_string(pack_obj);if(!pack)pack="(unknown)";unsigned long n=++g_pad_status_dispatch_count;if(!g_unity_asset_pack_status_fn){logmsg("[BENDER_PAD][STATUS][SKIP] n=%lu pack=%s callback=%p reason=nativeStatusQueryResult-not-captured\n",n,pack,callback_obj);return;}char pp[1536];const char*src=NULL;int available=bender_pad_pack_path_2749(pack,pp,sizeof(pp),&src);bender_pad_mark_complete_2764(pack,available);int status=available?4:8;void*name_obj=pack_obj?pack_obj:jni_NewStringUTF(&g_env,pack);set_checkpoint("PAD status pack=%s status=%d",pack,status);logmsg("[BENDER_PAD][STATUS] n=%lu pack=%s status=%s(%d) error=NO_ERROR(0) callback=%p native=%p source=%s path=%s\n",n,pack,available?"COMPLETED":"NOT_INSTALLED",status,callback_obj,(void*)g_unity_asset_pack_status_fn,src?src:"missing",available?pp:"(missing)");int saved_exc=g_jni_exception;void*saved_obj=g_jni_exception_obj;g_jni_exception=0;g_jni_exception_obj=NULL;g_unity_asset_pack_status_fn(&g_env,callback_obj,name_obj,status,0);int cb_exc=g_jni_exception;g_jni_exception=saved_exc;g_jni_exception_obj=saved_obj;logmsg("[BENDER_PAD][STATUS_RETURN] n=%lu pack=%s callback_exception=%d outer_exception=%d\n",n,pack,cb_exc,g_jni_exception);}
static void bender_unity_message_2758(BEnv*env,const char*game_object,const char*method,const char*payload,const char*source){
    if(!g_unity_native_send_message_fn_2758){logmsg("[BENDER_UNITY_MESSAGE][SKIP] source=%s method=%s reason=native-not-captured\n",source?source:"?",method?method:"?");return;}
    if(!game_object)game_object="AndroidCallbackHandler";
    if(!method)method="";
    if(!payload)payload="";
    void*go=jni_NewStringUTF(env,game_object);void*mn=jni_NewStringUTF(env,method);size_t n=strlen(payload);
    void*ba=jni_NewByteArray(env,(bjint)n);int8_t*dst=jni_GetByteArrayElements(env,ba,NULL);if(dst&&n)memcpy(dst,payload,n);
    ++g_clevertap_unity_message_count_2758;
    logmsg("[BENDER_UNITY_MESSAGE][SEND] n=%lu source=%s object=%s method=%s bytes=%zu native=%p\n",g_clevertap_unity_message_count_2758,source?source:"?",game_object,method,n,(void*)g_unity_native_send_message_fn_2758);
    int saved_exc=g_jni_exception;void*saved_obj=g_jni_exception_obj;g_jni_exception=0;g_jni_exception_obj=NULL;
    g_unity_native_send_message_fn_2758(env,g_unity_player_class,go,mn,ba);
    int cb_exc=g_jni_exception;g_jni_exception=saved_exc;g_jni_exception_obj=saved_obj;
    logmsg("[BENDER_UNITY_MESSAGE][RETURN] method=%s callback_exception=%d outer_exception=%d\n",method,cb_exc,g_jni_exception);
}
/* [BENDER_BILLING_MADFINGER][SIG_COMPAT] source=fix73 canonical+javareflection-dotted */
static int bender_madfinger_billing_connect_sig_fix73(const char*sig){
    int canonical=sig&&!strcmp(sig,"(Lcom/madfingergames/billing/UnityCallback;)V");
    int reflected=sig&&!strcmp(sig,"(Lcom.madfingergames.billing.UnityCallback;)V");
    return canonical||reflected;
}
/* [BENDER_BILLING_MADFINGER][QUERY_COMPAT] source=fix74 read-only-empty-results */
static int bender_madfinger_billing_query_sku_sig_fix74(BObj*m){
    if(!m||strcmp(m->a,"querySkuDetails")||strcmp(bender_jni_class_name((void*)m->ref0),"com/madfingergames/billing/BillingManager"))return 0;
    return !strcmp(m->b,"([Ljava/lang/String;Ljava/lang/String;Lcom/madfingergames/billing/UnityCallback;)V")||
           !strcmp(m->b,"([Ljava/lang/String;Ljava/lang/String;Lcom.madfingergames.billing.UnityCallback;)V");
}
static int bender_madfinger_billing_query_purchases_sig_fix74(BObj*m){
    if(!m||strcmp(m->a,"queryPurchases")||strcmp(bender_jni_class_name((void*)m->ref0),"com/madfingergames/billing/BillingManager"))return 0;
    return !strcmp(m->b,"(Ljava/lang/String;Lcom/madfingergames/billing/UnityCallback;)V")||
           !strcmp(m->b,"(Ljava/lang/String;Lcom.madfingergames.billing.UnityCallback;)V");
}
static void*bender_madfinger_billing_empty_list_fix74(const char*kind){
    BObj*list=new_obj(4,"java/util/ArrayList",kind?kind:"empty-billing-list");
    if(list){list->ref0=0;list->ref1=0;list->i64=0;}
    return list;
}
static void*bender_madfinger_billing_onresult_fix72(void*proxy,int response_code,void*payload){
    BObj*p=bender_known_bobj(proxy);
    if(!p||p->kind!=4||strcmp(p->a,"bitter/jnibridge/Proxy")){
        logmsg("[BENDER_BILLING_MADFINGER][CALLBACK][SKIP] source=fix72 proxy=%p reason=invalid-proxy\n",proxy);
        return NULL;
    }
    if(!g_reflection_proxy_invoke_fn||!g_reflection_helper_class){
        logmsg("[BENDER_BILLING_MADFINGER][CALLBACK][SKIP] source=fix72 proxy=%p reason=reflection-nativeProxyInvoke-missing\n",proxy);
        return NULL;
    }
    BObj*boxed=new_obj(4,"java/lang/Integer","value");
    if(boxed)boxed->i64=(int64_t)response_code;
    BObjArray*args=jni_NewObjectArray(&g_env,2,NULL,NULL);
    if(!boxed||!args){
        logmsg("[BENDER_BILLING_MADFINGER][CALLBACK][SKIP] source=fix72 proxy=%p reason=args-allocation responseCode=%d\n",proxy,response_code);
        return NULL;
    }
    jni_SetObjectArrayElement(&g_env,args,0,boxed);
    jni_SetObjectArrayElement(&g_env,args,1,payload);
    void*name_obj=jni_NewStringUTF(&g_env,"onResult");
    int saved_exc=g_jni_exception;void*saved_exc_obj=g_jni_exception_obj;
    g_jni_exception=0;g_jni_exception_obj=NULL;
    set_checkpoint("Madfinger Billing UnityCallback.onResult responseCode=%d",response_code);
    logmsg("[BENDER_BILLING_MADFINGER][CALLBACK][BEGIN] source=fix72 proxy=%p cookie=0x%llx responseCode=%d payload=%p argc=2 policy=connection-only-no-purchase-emulation\n",proxy,(unsigned long long)(uint64_t)p->i64,response_code,payload);
    void*ret=g_reflection_proxy_invoke_fn(&g_env,g_reflection_helper_class,(bjlong)p->i64,name_obj,args);
    int cb_exc=g_jni_exception;void*cb_exc_obj=g_jni_exception_obj;BObj*cb_e=bender_known_bobj(cb_exc_obj);
    if(cb_exc)logmsg("[BENDER_BILLING_MADFINGER][CALLBACK][EXCEPTION_ISOLATED] source=fix72 obj=%p class=%s msg=%s\n",cb_exc_obj,cb_e?cb_e->a:"(unknown)",cb_e?cb_e->b:"");
    g_jni_exception=saved_exc;g_jni_exception_obj=saved_exc_obj;
    logmsg("[BENDER_BILLING_MADFINGER][CALLBACK][RETURN] source=fix72 proxy=%p responseCode=%d ret=%p callback_exception=%d outer_exception=%d\n",proxy,response_code,ret,cb_exc,g_jni_exception);
    return ret;
}
static void bender_clevertap_bootstrap_2758(BEnv*env,const char*trigger){
    if(g_clevertap_bootstrap_sent_2758){logmsg("[BENDER_CLEVERTAP][BOOTSTRAP] trigger=%s action=already-sent\n",trigger?trigger:"?");return;}
    g_clevertap_bootstrap_sent_2758=1;
    char id[96],json[192];snprintf(id,sizeof(id),"bender-%s",bender_android_id_2756());snprintf(json,sizeof(json),"{\"CleverTapID\":\"%s\"}",id);
    logmsg("[BENDER_CLEVERTAP][BOOTSTRAP] trigger=%s id=%s policy=local-profile-ready\n",trigger?trigger:"?",id);
    bender_unity_message_2758(env,"AndroidCallbackHandler","CleverTapProfileInitializedCallback",json,"clevertap-profile-init");
    bender_unity_message_2758(env,"AndroidCallbackHandler","CleverTapInitCleverTapIdCallback",json,"clevertap-id-init");
}
static void jni_call_void_common(BEnv*env,void*obj,void*mid,const char*variant,void*arg0){(void)env;BObj*m=(BObj*)mid;++g_jni_call_count;const char*name=m?m->a:"?";int fix68_audio_write=bender_fix68_is_fmod_audio_write(m);if(!fix68_audio_write){set_checkpoint("JNI CallVoidMethod%s method=%s sig=%s",variant?variant:"",name,m?m->b:"?");logmsg("[BENDER_JNI][CALL] Void%s obj=%p method=%s sig=%s arg0=%p\n",variant?variant:"",obj,name,m?m->b:"?",arg0);}
if(m)bender_fix43_void_2843(env,obj,m,variant);
if(m&&!strcmp(bender_jni_class_name((void*)m->ref0),"org/fmod/FMODAudioDevice")&&!strcmp(name,"start")){bender_fmod_audio_start_fix5("FMODAudioDevice.start");}
 else if(m&&!strcmp(bender_jni_class_name((void*)m->ref0),"org/fmod/FMODAudioDevice")&&(!strcmp(name,"stop")||!strcmp(name,"close"))){bender_fmod_audio_stop_fix5(name);}
 else if(m&&bender_madfinger_billing_query_sku_sig_fix74(m)){
    void*payload=bender_madfinger_billing_empty_list_fix74("sku-details-empty");
    logmsg("[BENDER_BILLING_MADFINGER][QUERY_SKU] source=fix74 variant=%s sig=%s sig_style=%s callback=%p payload=%p responseCode=0 items=0 policy=read-only-empty-catalog-no-purchase-emulation\n",variant?variant:"",m->b,strchr(m->b,'.')?"reflection-dotted":"jni-canonical",arg0,payload);
    if(arg0)bender_madfinger_billing_onresult_fix72(arg0,0,payload);
    else logmsg("[BENDER_BILLING_MADFINGER][QUERY_SKU][WARN] source=fix74 callback=NULL action=no-dispatch\n");
}
else if(m&&bender_madfinger_billing_query_purchases_sig_fix74(m)){
    void*payload=bender_madfinger_billing_empty_list_fix74("purchases-empty");
    logmsg("[BENDER_BILLING_MADFINGER][QUERY_PURCHASES] source=fix74 variant=%s sig=%s sig_style=%s callback=%p payload=%p responseCode=0 items=0 policy=read-only-empty-owned-list-no-purchase-emulation\n",variant?variant:"",m->b,strchr(m->b,'.')?"reflection-dotted":"jni-canonical",arg0,payload);
    if(arg0)bender_madfinger_billing_onresult_fix72(arg0,0,payload);
    else logmsg("[BENDER_BILLING_MADFINGER][QUERY_PURCHASES][WARN] source=fix74 callback=NULL action=no-dispatch\n");
}
else if(m&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/madfingergames/billing/BillingManager")&&!strcmp(name,"connect")&&bender_madfinger_billing_connect_sig_fix73(m->b)){
    logmsg("[BENDER_BILLING_MADFINGER][CONNECT] source=fix73 class=com/madfingergames/billing/BillingManager sig=%s sig_style=%s callback=%p responseCode=0 policy=connection-only-no-purchase-emulation\n",m->b,strchr(m->b,'.')?"reflection-dotted":"jni-canonical",arg0);
    if(arg0)bender_madfinger_billing_onresult_fix72(arg0,0,NULL);
    else logmsg("[BENDER_BILLING_MADFINGER][CONNECT][WARN] source=fix72 callback=NULL action=no-dispatch\n");
}
else if(m&&!strcmp(name,"startConnection")&&strstr(m->b,"BillingClientStateListener")){
    g_billing_client_2765=obj;BObj*br=new_obj(4,"com/android/billingclient/api/BillingResult","OK");if(br)br->i64=0;
    logmsg("[BENDER_BILLING][START_CONNECTION] client=%p listener=%p result=%p responseCode=0 policy=connection-ready-no-purchase-emulation\n",obj,arg0,(void*)br);
    if(arg0){set_checkpoint("BillingClient.onBillingSetupFinished listener=%p",arg0);bender_jni_proxy_dispatch(arg0,"onBillingSetupFinished",br);}
}
else if(m&&!strcmp(name,"endConnection")&&obj==g_billing_client_2765){g_billing_client_2765=NULL;logmsg("[BENDER_BILLING][END_CONNECTION] client=%p action=clear-ready-state\n",obj);}
else if(m&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/movingeye/network/NetworkMonitor")&&(!strcmp(name,"start")||!strcmp(name,"stop"))){logmsg("[BENDER_NETWORK_MONITOR][%s] online=%d policy=host-network-state\n",!strcmp(name,"start")?"START":"STOP",bender_network_online_2750());}
else if(m&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/applovin/mediation/unity/MaxUnityPlugin")&&!strcmp(name,"setBackgroundCallback")){g_applovin_background_callback_2762=arg0;g_applovin_initialized_2762=1;logmsg("[BENDER_APPLOVIN][INIT] method=setBackgroundCallback callback=%p state=1 policy=synthetic-sdk-ready\n",arg0);}
else if(m&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/applovin/mediation/unity/MaxUnityPlugin")&&(!strcmp(name,"initializeSdk")||!strcmp(name,"initialize"))){g_applovin_initialized_2762=1;logmsg("[BENDER_APPLOVIN][INIT] method=%s state=1 policy=synthetic-sdk-ready\n",name);}
else if(m&&!strcmp(name,"init")&&strstr(bender_jni_class_name((void*)m->ref0),"org/fmod/FMOD")){g_fmod_java_context=arg0?arg0:g_unity_activity_instance;g_fmod_java_init_state=1;logmsg("[BENDER_FMOD_JAVA][INIT] context=%p activity=%p state=1 variant=%s\n",g_fmod_java_context,g_unity_activity_instance,variant?variant:"");}
else if(m&&!strcmp(name,"close")&&strstr(bender_jni_class_name((void*)m->ref0),"org/fmod/FMOD")){g_fmod_java_context=NULL;g_fmod_java_init_state=0;logmsg("[BENDER_FMOD_JAVA][CLOSE] state=0 variant=%s\n",variant?variant:"");}
else if(bender_fix68_is_fmod_audio_write(m)){/* FIX68: args decoded and PCM queued by the CallVoid wrapper. */}
else if(m&&!strcmp(name,"close")&&strstr(bender_jni_class_name((void*)m->ref0),"org/fmod/AudioDevice")){g_fmod_audio_device_init_state=0;g_bender_fix68_android_audio_takeover=0;logmsg("[BENDER_FMOD_AUDIO][CLOSE] obj=%p state=0 writes=%lu fix68_blocks=%lu fix68_bytes=%lu\n",obj,g_fmod_audio_write_count,g_bender_fix68_audio_blocks,g_bender_fix68_audio_bytes);}
else if(m&&!strcmp(name,"kill")){g_jni_kill_requested=1;logmsg("[BENDER_JNI][KILL_REQUEST] UnityPlayer.kill() solicitado; interceptando encerramento Android. render_jmp=%d\n",(int)g_render_jmp_active);if(g_render_jmp_active)siglongjmp(g_render_jmp,1);}
else if(m&&!strcmp(name,"sendToTarget")){BObj*msg=(BObj*)obj;if(msg&&msg->kind==4&&!strcmp(msg->a,"android/os/Message")){msg->flags|=1u;BObj*h=(BObj*)msg->ref0;void*cb=(h&&h->kind==4&&!strcmp(h->a,"android/os/Handler"))?(void*)h->ref1:NULL;logmsg("[BENDER_JNI][MESSAGE] sendToTarget msg=%p target=%p what=%lld callback=%p action=dispatch-handler-callback\n",obj,(void*)msg->ref0,(long long)msg->i64,cb);if(cb)bender_jni_proxy_dispatch(cb,"handleMessage",msg);else logmsg("[BENDER_JNI][MESSAGE][WARN] target sem callback; mantendo noop msg=%p\n",obj);}else logmsg("[BENDER_JNI][MESSAGE] sendToTarget unknown msg=%p action=noop\n",obj);}
else if(m&&!strcmp(name,"postFrameCallback")){bender_jni_choreographer_ensure_worker();__atomic_store_n(&g_jni_choreographer_pending_cb,arg0,__ATOMIC_RELEASE);unsigned long n=__atomic_add_fetch(&g_jni_choreographer_post_count,1ul,__ATOMIC_RELAXED);logmsg("[BENDER_JNI][CHOREOGRAPHER][POST] n=%lu choreographer=%p callback=%p action=queue-async-vsync-thread\n",n,obj,arg0);set_checkpoint("Choreographer.postFrameCallback queued async n=%lu callback=%p",n,arg0);}
else if(m&&!strcmp(name,"removeFrameCallback")){void*cur=__atomic_load_n(&g_jni_choreographer_pending_cb,__ATOMIC_ACQUIRE);if(!arg0||cur==arg0)__atomic_store_n(&g_jni_choreographer_pending_cb,NULL,__ATOMIC_RELEASE);logmsg("[BENDER_JNI][CHOREOGRAPHER][REMOVE] choreographer=%p callback=%p pending=%p\n",obj,arg0,__atomic_load_n(&g_jni_choreographer_pending_cb,__ATOMIC_ACQUIRE));}
else if(m&&!strcmp(name,"runOnUiThread")){if(arg0){logmsg("[BENDER_UI_THREAD][RUN] activity=%p runnable=%p policy=inline-reflection-proxy\n",obj,arg0);set_checkpoint("Activity.runOnUiThread dispatch runnable=%p",arg0);bender_jni_proxy_dispatch(arg0,"run",NULL);logmsg("[BENDER_UI_THREAD][RETURN] activity=%p runnable=%p exception=%d fatal=%d\n",obj,arg0,g_jni_exception,g_jni_fatal_count);}else logmsg("[BENDER_UI_THREAD][WARN] runOnUiThread recebeu runnable NULL\n");}
else if(m&&(!strcmp(name,"loadAndShowConsentFormIfRequired")||!strcmp(name,"showPrivacyOptionsForm"))){
        BObj*f=bender_known_bobj(obj);
        void*cb=(f&&f->kind==4&&!strcmp(f->a,"com/google/unity/ump/UnityConsentForm"))?(void*)f->ref1:NULL;
        int held=(f&&((f->flags&UINT32_C(0x20000000))!=0u));
        BObj*cb_obj=cb?bender_known_bobj(cb):NULL;
        int cb_valid=(cb_obj&&cb_obj->kind==4&&!strcmp(cb_obj->a,"bitter/jnibridge/Proxy"));
        logmsg("[BENDER_UMP][FORM_COMPLETE] source=fix108 method=%s form=%p callback=%p callback_valid=%d held=%d policy=offline-not-required action=onConsentFormDismissed-null semantic=plugin-owned-completion code_patch=0 forced_completion=0\n",name,obj,cb,cb_valid,held);
        if(cb&&cb_valid){
            set_checkpoint("UMP form complete callback=%p valid=1 held=%d",cb,held);
            logmsg("[BENDER_UMP][FORM_CALLBACK_DISPATCH] source=fix108 form=%p callback=%p valid=1 held=%d method=onConsentFormDismissed arg=FormError-null code_patch=0 forced_completion=0\n",obj,cb,held);
            bender_jni_proxy_dispatch(cb,"onConsentFormDismissed",NULL);
        }else if(cb){
            logmsg("[BENDER_UMP][FORM_CALLBACK_DISPATCH][SKIP] source=fix108 form=%p callback=%p valid=0 held=%d reason=retained-callback-not-proxy code_patch=0 forced_completion=0\n",obj,cb,held);
        }else{
            logmsg("[BENDER_UMP][FORM_COMPLETE][WARN] source=fix108 callback_ausente form=%p held=%d code_patch=0 forced_completion=0\n",obj,held);
        }
        if(f){f->ref1=0;f->flags&=~UINT32_C(0x20000000);}
        if(cb&&held){
            jni_DeleteGlobalRef(env,cb);
            logmsg("[BENDER_UMP][FORM_CALLBACK_RELEASE] source=fix108 form=%p callback=%p ownership=JNI-global-ref action=release-after-one-shot-dispatch code_patch=0 forced_completion=0\n",obj,cb);
        }
    }
}
static int bender_jni_void_arg0_method(BObj*m){return m&&(!strcmp(m->a,"postFrameCallback")||!strcmp(m->a,"removeFrameCallback")||!strcmp(m->a,"runOnUiThread")||(!strcmp(m->a,"startConnection")&&strstr(m->b,"BillingClientStateListener")));}

/* ---------------- BENDER PAD DOWNLOAD CALLBACK V2.7.54 ---------------- */
static void bender_pad_download_proxy_invoke_2751(void*proxy,const char*method_name,BObjArray*args){
    BObj*p=bender_known_bobj(proxy);
    if(!p||p->kind!=4||strcmp(p->a,"bitter/jnibridge/Proxy")||!g_reflection_proxy_invoke_fn||!g_reflection_helper_class){
        logmsg("[BENDER_PAD][DOWNLOAD_CALLBACK][SKIP] proxy=%p method=%s invoke=%p helper=%p\n",proxy,method_name?method_name:"?",(void*)g_reflection_proxy_invoke_fn,g_reflection_helper_class);return;
    }
    void*name_obj=jni_NewStringUTF(&g_env,method_name?method_name:"onStatusUpdate");
    int saved_exc=g_jni_exception;void*saved_obj=g_jni_exception_obj;g_jni_exception=0;g_jni_exception_obj=NULL;
    logmsg("[BENDER_PAD][DOWNLOAD_CALLBACK][BEGIN] proxy=%p cookie=0x%llx method=%s argc=%d\n",proxy,(unsigned long long)(uint64_t)p->i64,method_name?method_name:"?",args?(int)args->length:0);
    (void)g_reflection_proxy_invoke_fn(&g_env,g_reflection_helper_class,(bjlong)p->i64,name_obj,args);
    int cb_exc=g_jni_exception;g_jni_exception=saved_exc;g_jni_exception_obj=saved_obj;
    logmsg("[BENDER_PAD][DOWNLOAD_CALLBACK][RETURN] proxy=%p method=%s callback_exception=%d outer_exception=%d\n",proxy,method_name?method_name:"?",cb_exc,g_jni_exception);
}
static BObj*bender_box_i32_2751(int v){BObj*o=new_obj(4,"java/lang/Integer","value");if(o)o->i64=(int64_t)v;return o;}
static BObj*bender_box_i64_2751(int64_t v){BObj*o=new_obj(4,"java/lang/Long","value");if(o)o->i64=v;return o;}
static void bender_pad_download_status_dispatch_2751(void*packs_obj,void*callback_obj){
    BObjArray*packs=jni_obj_array_cast(packs_obj);
    if(!packs||!callback_obj){logmsg("[BENDER_PAD][DOWNLOAD][SKIP] packs=%p callback=%p reason=invalid-args\n",packs_obj,callback_obj);return;}
    logmsg("[BENDER_PAD][DOWNLOAD] requested=%d callback=%p policy=local-universal-apk-packs\n",(int)packs->length,callback_obj);
    for(bjsize i=0;i<packs->length;i++){
        void*pack_obj=packs->items[i];const char*pack=jni_obj_string(pack_obj);if(!pack)pack="(unknown)";
        char pp[1536];const char*src=NULL;int available=bender_pad_pack_path_2749(pack,pp,sizeof(pp),&src);bender_pad_mark_complete_2764(pack,available);
        int status=available?4:5; /* COMPLETED / FAILED */
        int error=available?0:-2; int progress=available?100:0;
        int64_t done=available?1:0,total=1;
        BObjArray*args=jni_NewObjectArray(&g_env,6,NULL,NULL);
        if(!args){logmsg("[BENDER_PAD][DOWNLOAD][ERROR] pack=%s reason=args-allocation\n",pack);continue;}
        jni_SetObjectArrayElement(&g_env,args,0,pack_obj?pack_obj:jni_NewStringUTF(&g_env,pack));
        jni_SetObjectArrayElement(&g_env,args,1,bender_box_i32_2751(status));
        jni_SetObjectArrayElement(&g_env,args,2,bender_box_i64_2751(done));
        jni_SetObjectArrayElement(&g_env,args,3,bender_box_i64_2751(total));
        jni_SetObjectArrayElement(&g_env,args,4,bender_box_i32_2751(error));
        jni_SetObjectArrayElement(&g_env,args,5,bender_box_i32_2751(progress));
        if(available)++g_pad_completed_2762;
        logmsg("[BENDER_PAD][DOWNLOAD_STATUS] pack=%s status=%s(%d) bytes=%lld/%lld error=%d progress=%d source=%s path=%s\n",pack,available?"COMPLETED":"FAILED",status,(long long)done,(long long)total,error,progress,src?src:"missing",available?pp:"(missing)");
        logmsg("[BENDER_PAD][DOWNLOAD_PROXY_VALUES] pack=%s status=%d bytes=%lld total=%lld error=%d progress=%d\n",pack,status,(long long)done,(long long)total,error,progress);
        bender_pad_download_proxy_invoke_2751(callback_obj,"onStatusUpdate",args);
    }
}
/* ---------------- end BENDER PAD DOWNLOAD CALLBACK V2.7.54 ---------------- */
static void jni_CallVoidMethod(BEnv*env,void*obj,void*mid,...){BObj*m=(BObj*)mid;void*arg0=NULL,*ump_success=NULL,*pad_pack=NULL,*pad_cb=NULL,*pad_download_packs=NULL,*pad_download_cb=NULL,*audio_arr=NULL;bjint audio_count=0;int audio_write=bender_fix68_is_fmod_audio_write(m);va_list ap;va_start(ap,mid);if(audio_write){audio_arr=va_arg(ap,void*);audio_count=(bjint)va_arg(ap,int);arg0=audio_arr;}else if(bender_jni_void_arg0_method(m))arg0=va_arg(ap,void*);else if(m&&!strcmp(m->a,"requestConsentInfoUpdate")){(void)va_arg(ap,void*);(void)va_arg(ap,void*);ump_success=va_arg(ap,void*);(void)va_arg(ap,void*);}else if(m&&!strcmp(m->a,"getAssetPackState")){pad_pack=va_arg(ap,void*);pad_cb=va_arg(ap,void*);}else if(m&&!strcmp(m->a,"downloadAssetPacks")){pad_download_packs=va_arg(ap,void*);pad_download_cb=va_arg(ap,void*);}va_end(ap);jni_call_void_common(env,obj,mid,"",arg0);if(audio_write)bender_fix68_fmod_audio_write(audio_arr,audio_count,m->b,"");if(ump_success){logmsg("[BENDER_UMP][CONSENT_UPDATE] policy=offline-not-required success_proxy=%p action=callback\n",ump_success);bender_jni_proxy_dispatch(ump_success,"onConsentInfoUpdateSuccess",NULL);}if(pad_pack)bender_pad_status_dispatch(pad_pack,pad_cb);if(pad_download_packs)bender_pad_download_status_dispatch_2751(pad_download_packs,pad_download_cb);}
static void jni_CallVoidMethodV(BEnv*env,void*obj,void*mid,va_list ap){BObj*m=(BObj*)mid;void*arg0=NULL,*ump_success=NULL,*pad_pack=NULL,*pad_cb=NULL,*pad_download_packs=NULL,*pad_download_cb=NULL,*audio_arr=NULL;bjint audio_count=0;int audio_write=bender_fix68_is_fmod_audio_write(m);if(audio_write){va_list cp;va_copy(cp,ap);audio_arr=va_arg(cp,void*);audio_count=(bjint)va_arg(cp,int);va_end(cp);arg0=audio_arr;}else if(bender_jni_void_arg0_method(m)){va_list cp;va_copy(cp,ap);arg0=va_arg(cp,void*);va_end(cp);}else if(m&&!strcmp(m->a,"requestConsentInfoUpdate")){va_list cp;va_copy(cp,ap);(void)va_arg(cp,void*);(void)va_arg(cp,void*);ump_success=va_arg(cp,void*);(void)va_arg(cp,void*);va_end(cp);}else if(m&&!strcmp(m->a,"getAssetPackState")){va_list cp;va_copy(cp,ap);pad_pack=va_arg(cp,void*);pad_cb=va_arg(cp,void*);va_end(cp);}else if(m&&!strcmp(m->a,"downloadAssetPacks")){va_list cp;va_copy(cp,ap);pad_download_packs=va_arg(cp,void*);pad_download_cb=va_arg(cp,void*);va_end(cp);}jni_call_void_common(env,obj,mid,"V",arg0);if(audio_write)bender_fix68_fmod_audio_write(audio_arr,audio_count,m->b,"V");if(ump_success){logmsg("[BENDER_UMP][CONSENT_UPDATE] policy=offline-not-required success_proxy=%p action=callback\n",ump_success);bender_jni_proxy_dispatch(ump_success,"onConsentInfoUpdateSuccess",NULL);}if(pad_pack)bender_pad_status_dispatch(pad_pack,pad_cb);if(pad_download_packs)bender_pad_download_status_dispatch_2751(pad_download_packs,pad_download_cb);}
static void jni_CallVoidMethodA(BEnv*env,void*obj,void*mid,const void*args){BObj*m=(BObj*)mid;void*arg0=NULL,*ump_success=NULL,*pad_pack=NULL,*pad_cb=NULL,*pad_download_packs=NULL,*pad_download_cb=NULL,*audio_arr=NULL;bjint audio_count=0;int audio_write=bender_fix68_is_fmod_audio_write(m);if(args&&audio_write){const uint64_t*v=(const uint64_t*)args;audio_arr=(void*)(uintptr_t)v[0];audio_count=(bjint)(int32_t)(uint32_t)v[1];arg0=audio_arr;}else if(args&&bender_jni_void_arg0_method(m))arg0=(void*)(uintptr_t)(*(const uintptr_t*)args);else if(args&&m&&!strcmp(m->a,"requestConsentInfoUpdate")){const uint64_t*v=(const uint64_t*)args;ump_success=(void*)(uintptr_t)v[2];}else if(args&&m&&!strcmp(m->a,"getAssetPackState")){const uint64_t*v=(const uint64_t*)args;pad_pack=(void*)(uintptr_t)v[0];pad_cb=(void*)(uintptr_t)v[1];}else if(args&&m&&!strcmp(m->a,"downloadAssetPacks")){const uint64_t*v=(const uint64_t*)args;pad_download_packs=(void*)(uintptr_t)v[0];pad_download_cb=(void*)(uintptr_t)v[1];}jni_call_void_common(env,obj,mid,"A",arg0);if(audio_write)bender_fix68_fmod_audio_write(audio_arr,audio_count,m->b,"A");if(ump_success){logmsg("[BENDER_UMP][CONSENT_UPDATE] policy=offline-not-required success_proxy=%p action=callback\n",ump_success);bender_jni_proxy_dispatch(ump_success,"onConsentInfoUpdateSuccess",NULL);}if(pad_pack)bender_pad_status_dispatch(pad_pack,pad_cb);if(pad_download_packs)bender_pad_download_status_dispatch_2751(pad_download_packs,pad_download_cb);}

static void bender_jni_choreographer_dispatch_cb(void*cb,unsigned long tick,const char*source){
    if(!cb)return;
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);int64_t ns=(int64_t)ts.tv_sec*INT64_C(1000000000)+(int64_t)ts.tv_nsec;
    BObj*boxed=__atomic_load_n(&g_jni_choreographer_cached_long,__ATOMIC_ACQUIRE);
    if(!boxed){boxed=new_obj(4,"java/lang/Long","frameTimeNanos");__atomic_store_n(&g_jni_choreographer_cached_long,boxed,__ATOMIC_RELEASE);}
    if(boxed)boxed->i64=ns;
    unsigned long n=__atomic_add_fetch(&g_jni_choreographer_dispatch_count,1ul,__ATOMIC_RELAXED);
    logmsg("[BENDER_JNI][CHOREOGRAPHER][DOFRAME_DISPATCH] n=%lu tick=%lu source=%s callback=%p frameTimeNanos=%lld boxed=%p tid=%ld\n",n,tick,source?source:"?",cb,(long long)ns,(void*)boxed,(long)syscall(SYS_gettid));
    set_checkpoint("Choreographer.doFrame dispatch n=%lu tick=%lu source=%s",n,tick,source?source:"?");
    bender_jni_proxy_dispatch(cb,"doFrame",boxed);
}
static void *bender_jni_choreographer_worker_main(void*arg){
    (void)arg;install_crash_handlers();
    unsigned interval_us=16666u;const char*e=getenv("BENDER_CHOREOGRAPHER_INTERVAL_US");if(e){long v=strtol(e,NULL,10);if(v>=4000&&v<=100000)interval_us=(unsigned)v;}
    logmsg("[BENDER_JNI][CHOREOGRAPHER][ASYNC_THREAD_START] tid=%ld interval_us=%u refresh=%.2fHz\n",(long)syscall(SYS_gettid),interval_us,(double)BENDER_DISPLAY_REFRESH_HZ);
    unsigned long last_render_serial=__atomic_load_n(&g_bender_progress_serial,__ATOMIC_RELAXED);unsigned stale=0;
    while(!__atomic_load_n(&g_jni_choreographer_worker_stop,__ATOMIC_ACQUIRE)){
        usleep(interval_us);
        if(__atomic_load_n(&g_jni_choreographer_worker_stop,__ATOMIC_ACQUIRE))break;
        unsigned long rs=__atomic_load_n(&g_bender_progress_serial,__ATOMIC_RELAXED);
        if(rs!=last_render_serial){last_render_serial=rs;stale=0;}
        void*cb=__atomic_exchange_n(&g_jni_choreographer_pending_cb,NULL,__ATOMIC_ACQ_REL);
        if(!cb)continue;
        unsigned long tick=__atomic_add_fetch(&g_jni_choreographer_worker_ticks,1ul,__ATOMIC_RELAXED);
        bender_jni_choreographer_dispatch_cb(cb,tick,"async-thread");
        rs=__atomic_load_n(&g_bender_progress_serial,__ATOMIC_RELAXED);
        if(rs==last_render_serial){stale++;if(stale==120u){logmsg("[BENDER_JNI][CHOREOGRAPHER][ASYNC_STALE] callbacks_without_render_progress=%u pending=%p action=pause-until-render-progress\n",stale,__atomic_load_n(&g_jni_choreographer_pending_cb,__ATOMIC_ACQUIRE));}}
        else{last_render_serial=rs;stale=0;}
        while(stale>=120u&&!__atomic_load_n(&g_jni_choreographer_worker_stop,__ATOMIC_ACQUIRE)){
            usleep(interval_us);rs=__atomic_load_n(&g_bender_progress_serial,__ATOMIC_RELAXED);if(rs!=last_render_serial){last_render_serial=rs;stale=0;logmsg("[BENDER_JNI][CHOREOGRAPHER][ASYNC_RESUME] render_serial=%lu\n",rs);break;}
        }
    }
    logmsg("[BENDER_JNI][CHOREOGRAPHER][ASYNC_THREAD_STOP] tid=%ld ticks=%lu posts=%lu dispatches=%lu\n",(long)syscall(SYS_gettid),g_jni_choreographer_worker_ticks,g_jni_choreographer_post_count,g_jni_choreographer_dispatch_count);
    return NULL;
}
static void bender_jni_choreographer_ensure_worker(void){
    if(__atomic_load_n(&g_jni_choreographer_worker_started,__ATOMIC_ACQUIRE))return;
    int expected=0;if(!__atomic_compare_exchange_n(&g_jni_choreographer_worker_started,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
    __atomic_store_n(&g_jni_choreographer_worker_stop,0,__ATOMIC_RELEASE);
    int rc=pthread_create(&g_jni_choreographer_worker,NULL,bender_jni_choreographer_worker_main,NULL);
    if(rc!=0){__atomic_store_n(&g_jni_choreographer_worker_started,0,__ATOMIC_RELEASE);logmsg("[BENDER_JNI][CHOREOGRAPHER][ASYNC_THREAD_ERROR] pthread_create rc=%d\n",rc);}
}
static void bender_jni_choreographer_stop_worker(void){
    if(!__atomic_load_n(&g_jni_choreographer_worker_started,__ATOMIC_ACQUIRE))return;
    __atomic_store_n(&g_jni_choreographer_worker_stop,1,__ATOMIC_RELEASE);
    (void)pthread_join(g_jni_choreographer_worker,NULL);
    __atomic_store_n(&g_jni_choreographer_worker_started,0,__ATOMIC_RELEASE);
}
static void __attribute__((unused)) bender_jni_choreographer_pump(unsigned frame){
    if(__atomic_load_n(&g_jni_choreographer_worker_started,__ATOMIC_ACQUIRE))return;
    void*cb=__atomic_exchange_n(&g_jni_choreographer_pending_cb,NULL,__ATOMIC_ACQ_REL);if(!cb)return;
    bender_jni_choreographer_dispatch_cb(cb,(unsigned long)frame,"frame-loop-fallback");
}

static void *jni_GetObjectField(BEnv*env,void*obj,void*fid){BObj*f=(BObj*)fid;++g_jni_call_count;const char*name=f?f->a:"?";const char*sig=f?f->b:"";logmsg("[BENDER_JNI][FIELD] Object obj=%p field=%s sig=%s\n",obj,name,sig);
/* Android Context service constants are Strings even if reflection widened the signature to Object. */
if(!strcmp(name,"CONNECTIVITY_SERVICE"))return jni_NewStringUTF(env,"connectivity");
if(!strcmp(name,"DISPLAY_SERVICE"))return jni_NewStringUTF(env,"display");
if(!strcmp(name,"POWER_SERVICE"))return jni_NewStringUTF(env,"power");
if(!strcmp(name,"ACTIVITY_SERVICE"))return jni_NewStringUTF(env,"activity");
if(!strcmp(name,"AUDIO_SERVICE"))return jni_NewStringUTF(env,"audio");
if(!strcmp(name,"SENSOR_SERVICE"))return jni_NewStringUTF(env,"sensor");
if(!strcmp(name,"VIBRATOR_SERVICE"))return jni_NewStringUTF(env,"vibrator");
if(!strcmp(name,"WINDOW_SERVICE"))return jni_NewStringUTF(env,"window");
if(!strcmp(name,"INPUT_METHOD_SERVICE"))return jni_NewStringUTF(env,"input_method");
if(!strcmp(name,"INPUT_SERVICE"))return jni_NewStringUTF(env,"input");
if(!strcmp(name,"TELEPHONY_SERVICE"))return jni_NewStringUTF(env,"phone");
if(!strcmp(name,"NOTIFICATION_SERVICE"))return jni_NewStringUTF(env,"notification");
if(!strcmp(name,"MEDIA_ROUTER_SERVICE"))return jni_NewStringUTF(env,"media_router");
if(!strcmp(name,"UI_MODE_SERVICE"))return jni_NewStringUTF(env,"uimode");
if(!strcmp(name,"core")&&!strcmp(sig,"Lcom/google/firebase/crashlytics/internal/common/CrashlyticsCore;")){
    if(!g_firebase_crash_core_2759)g_firebase_crash_core_2759=new_obj(4,"com/google/firebase/crashlytics/internal/common/CrashlyticsCore","bender-crash-core");
    logmsg("[BENDER_CRASHLYTICS][FIELD] core parent=%p -> %p class=CrashlyticsCore\n",obj,g_firebase_crash_core_2759);return g_firebase_crash_core_2759;
}
if(!strcmp(name,"dataCollectionArbiter")&&!strcmp(sig,"Lcom/google/firebase/crashlytics/internal/common/DataCollectionArbiter;")){
    if(!g_firebase_data_arbiter_2759)g_firebase_data_arbiter_2759=new_obj(4,"com/google/firebase/crashlytics/internal/common/DataCollectionArbiter","bender-data-collection-arbiter");
    logmsg("[BENDER_CRASHLYTICS][FIELD] dataCollectionArbiter parent=%p -> %p class=DataCollectionArbiter\n",obj,g_firebase_data_arbiter_2759);return g_firebase_data_arbiter_2759;
}
if(sig&&strcmp(sig,"Ljava/lang/String;")==0){
    if(!strcmp(name,"MANUFACTURER"))return jni_NewStringUTF(env,"BENDER");
    if(!strcmp(name,"MODEL"))return jni_NewStringUTF(env,"R36S");
    if(!strcmp(name,"RELEASE"))return jni_NewStringUTF(env,"11");
    if(!strcmp(name,"versionName"))return jni_NewStringUTF(env,BENDER_APP_VERSION_NAME);
    if(!strcmp(name,"MEDIA_MOUNTED"))return jni_NewStringUTF(env,"mounted");
    if(!strcmp(name,"MEDIA_ROUTER_SERVICE"))return jni_NewStringUTF(env,"media_router");
    if(!strcmp(name,"DISPLAY_SERVICE"))return jni_NewStringUTF(env,"display");
    if(!strcmp(name,"POWER_SERVICE"))return jni_NewStringUTF(env,"power");
    if(!strcmp(name,"ACTIVITY_SERVICE"))return jni_NewStringUTF(env,"activity");
    if(!strcmp(name,"AUDIO_SERVICE"))return jni_NewStringUTF(env,"audio");
    if(!strcmp(name,"SENSOR_SERVICE"))return jni_NewStringUTF(env,"sensor");
    if(!strcmp(name,"VIBRATOR_SERVICE"))return jni_NewStringUTF(env,"vibrator");
    if(!strcmp(name,"CONNECTIVITY_SERVICE"))return jni_NewStringUTF(env,"connectivity");
    if(!strcmp(name,"WINDOW_SERVICE"))return jni_NewStringUTF(env,"window");
    if(!strcmp(name,"INPUT_METHOD_SERVICE"))return jni_NewStringUTF(env,"input_method");
    if(!strcmp(name,"TELEPHONY_SERVICE"))return jni_NewStringUTF(env,"phone");
    if(!strcmp(name,"NOTIFICATION_SERVICE"))return jni_NewStringUTF(env,"notification");
    if(!strcmp(name,"sourceDir")||!strcmp(name,"publicSourceDir"))return jni_NewStringUTF(env,g_guest_base_apk);
    if(!strcmp(name,"nativeLibraryDir"))return jni_NewStringUTF(env,g_guest_lib_dir);
    if(!strcmp(name,"dataDir"))return jni_NewStringUTF(env,g_guest_data_dir);
    return jni_NewStringUTF(env,"BENDER");
}
if(!strcmp(name,"splitSourceDirs")||!strcmp(name,"splitPublicSourceDirs"))return bender_split_source_dirs_2749(env);
return obj?obj:new_obj(4,sig,NULL);}
static void *jni_GetStaticObjectField(BEnv*env,void*cls,void*fid){
    BObj*f=bender_known_bobj(fid);const char*fcls=(f&&f->ref0)?bender_jni_class_name((void*)f->ref0):bender_jni_class_name(cls);
    if(f&&f->a[0]&&!strcmp(fcls,"android/media/AudioManager")&&!strcmp(f->a,"PROPERTY_OUTPUT_SAMPLE_RATE")){return bender_audio_cached_string_fix5(env,&g_bender_audio_rate_key_fix5,"android.media.property.OUTPUT_SAMPLE_RATE");}
    if(f&&f->a[0]&&!strcmp(fcls,"android/media/AudioManager")&&!strcmp(f->a,"PROPERTY_OUTPUT_FRAMES_PER_BUFFER")){return bender_audio_cached_string_fix5(env,&g_bender_audio_frames_key_fix5,"android.media.property.OUTPUT_FRAMES_PER_BUFFER");}
    if(f&&f->a[0]&&!strcmp(f->a,"currentActivity")){
        if(!g_unity_activity_instance)g_unity_activity_instance=new_obj(4,"com/unity3d/player/UnityPlayerForActivityOrService","currentActivity");
        logmsg("[BENDER_JNI][CURRENT_ACTIVITY] class=%p field=%p sig=%s -> activity=%p\n",cls,fid,f->b,(void*)g_unity_activity_instance);
        return g_unity_activity_instance;
    }
    return jni_GetObjectField(env,cls,fid);
}
static bjint jni_get_int_field_common(BEnv*env,void*obj,void*fid,const char*variant){(void)env;BObj*f=(BObj*)fid;const char*name=f?f->a:"?";const char*fcls=(f&&f->ref0)?bender_jni_class_name((void*)f->ref0):bender_jni_class_name(obj);bjint v=0;
    if(!strcmp(name,"what")){BObj*bo=bender_known_bobj(obj);if(bo&&bo->kind==4&&!strcmp(bo->a,"android/os/Message")){v=(bjint)bo->i64;logmsg("[BENDER_JNI][MESSAGE] GetIntField what msg=%p stored=%lld -> %d\n",obj,(long long)bo->i64,(int)v);}}
    else if(!strcmp(name,"SDK_INT")){const char*e=getenv("BENDER_ANDROID_SDK_INT");v=(e&&*e)?(bjint)strtol(e,NULL,10):(bjint)BENDER_APP_MIN_SDK;if(v<(bjint)BENDER_APP_MIN_SDK)v=(bjint)BENDER_APP_MIN_SDK;if(v<21)v=21;}
    else if(!strcmp(name,"targetSdkVersion"))v=BENDER_APP_TARGET_SDK;
    else if(!strcmp(name,"minSdkVersion"))v=BENDER_APP_MIN_SDK;
    else if(!strcmp(name,"versionCode"))v=BENDER_APP_VERSION_CODE;
    else if(!strcmp(name,"widthPixels"))v=bender_render_scale_guest_width_2768();
    else if(!strcmp(name,"heightPixels"))v=bender_render_scale_guest_height_2768();
    else if(!strcmp(name,"densityDpi"))v=BENDER_DISPLAY_DENSITY_DPI;
    else if(!strcmp(fcls,"android/media/MediaCodecInfo$CodecCapabilities")&&!strcmp(name,"COLOR_FormatYUV420Planar"))v=19;
    else if(!strcmp(fcls,"android/media/MediaCodecInfo$CodecCapabilities")&&!strcmp(name,"COLOR_FormatYUV420PackedPlanar"))v=20;
    else if(!strcmp(fcls,"android/media/MediaCodecInfo$CodecCapabilities")&&!strcmp(name,"COLOR_FormatYUV420SemiPlanar"))v=21;
    else if(!strcmp(fcls,"android/media/MediaCodecInfo$CodecCapabilities")&&!strcmp(name,"COLOR_FormatYUV420PackedSemiPlanar"))v=39;
    else if(!strcmp(fcls,"android/media/MediaCodecInfo$CodecCapabilities")&&!strcmp(name,"COLOR_FormatSurface"))v=0x7F000789;
    else if(!strcmp(fcls,"android/media/MediaCodecInfo$CodecCapabilities")&&!strcmp(name,"COLOR_FormatYUV420Flexible"))v=0x7F420888;
    else if(!strcmp(name,"GET_DEVICES_OUTPUTS"))v=2;
    else if(!strcmp(name,"STREAM_MUSIC"))v=3;
    else if(!strcmp(name,"MODE_NORMAL"))v=0;
    else if(!strcmp(name,"TYPE_BUILTIN_SPEAKER"))v=2;
    else if(!strcmp(name,"ENCODING_PCM_16BIT"))v=2;
    else if(!strcmp(name,"BUTTON_POSITIVE"))v=-1;
    else if(!strcmp(name,"BUTTON_NEGATIVE"))v=-2;
    else if(!strcmp(name,"ROUTE_TYPE_LIVE_VIDEO"))v=2;
    else if(!strcmp(name,"TYPE_MOBILE"))v=0;
    else if(!strcmp(name,"TYPE_WIFI"))v=1;
    else if(!strcmp(name,"TYPE_ETHERNET"))v=9;
    else if(!strcmp(name,"NET_CAPABILITY_INTERNET"))v=12;
    else if(!strcmp(name,"NET_CAPABILITY_VALIDATED"))v=16;
    else if(!strcmp(name,"TRANSPORT_CELLULAR"))v=0;
    else if(!strcmp(name,"TRANSPORT_WIFI"))v=1;
    else if(!strcmp(name,"TRANSPORT_ETHERNET"))v=3;
    if(fcls&&!strcmp(fcls,"android/media/MediaCodecInfo$CodecCapabilities")&&!strncmp(name,"COLOR_",6))logmsg("[BENDER_MEDIANDK][COLOR_FORMAT] source=fix77 class=%s field=%s value=%d hex=0x%x variant=%s\n",fcls,name,(int)v,(unsigned)v,variant?variant:"");
    ++g_jni_call_count; set_checkpoint("JNI Get%sIntField field=%s value=%d",variant?variant:"",name,(int)v);
    logmsg("[BENDER_JNI][FIELD] Int%s obj=%p field=%s -> %d\n",variant?variant:"",obj,name,(int)v); return v;}
static bjint jni_GetIntField(BEnv*env,void*obj,void*fid){return jni_get_int_field_common(env,obj,fid,"");}
static bjint jni_GetStaticIntField(BEnv*env,void*cls,void*fid){return jni_get_int_field_common(env,cls,fid,"Static");}
static bjlong jni_GetLongField(BEnv*env,void*obj,void*fid){(void)env;BObj*f=(BObj*)fid;bjlong v=0;++g_jni_call_count;set_checkpoint("JNI GetLongField field=%s",f?f->a:"?");logmsg("[BENDER_JNI][FIELD] Long obj=%p field=%s -> %" PRId64 "\n",obj,f?f->a:"?",(int64_t)v);return v;}
static bjlong jni_GetStaticLongField(BEnv*env,void*obj,void*fid){return jni_GetLongField(env,obj,fid);}
static bjfloat jni_GetFloatField(BEnv*env,void*obj,void*fid){(void)env;BObj*f=(BObj*)fid;bjfloat v=0.0f;++g_jni_call_count;set_checkpoint("JNI GetFloatField field=%s",f?f->a:"?");logmsg("[BENDER_JNI][FIELD] Float obj=%p field=%s -> %.3f\n",obj,f?f->a:"?",(double)v);return v;}
static bjfloat jni_GetStaticFloatField(BEnv*env,void*obj,void*fid){return jni_GetFloatField(env,obj,fid);}
static bjdouble jni_GetDoubleField(BEnv*env,void*obj,void*fid){(void)env;BObj*f=(BObj*)fid;bjdouble v=0.0;++g_jni_call_count;set_checkpoint("JNI GetDoubleField field=%s",f?f->a:"?");logmsg("[BENDER_JNI][FIELD] Double obj=%p field=%s -> %.6f\n",obj,f?f->a:"?",v);return v;}
static bjdouble jni_GetStaticDoubleField(BEnv*env,void*obj,void*fid){return jni_GetDoubleField(env,obj,fid);}
static int bender_reliance_push_payload_match_fix102(void*cls,void*mid){
    BObj*m=(BObj*)mid;const char*cn=bender_jni_class_name(cls);const char*sig=m?m->b:NULL;
    if(!m||!cn||!sig)return 0;
    if(strcmp(cn,"com/reliancegames/plugins/pushnotification/RGPushNotification"))return 0;
    if(strcmp(m->a,"getSavedPushPayloadData"))return 0;
    return !strcmp(sig,"(Lcom/unity3d/player/UnityPlayerForActivityOrService;)Ljava/lang/String;") ||
           !strcmp(sig,"(Landroid/content/Context;)Ljava/lang/String;");
}
static void *bender_reliance_push_payload_fix102(BEnv*env,void*cls,void*mid,const char*variant){
    if(!bender_reliance_push_payload_match_fix102(cls,mid))return NULL;
    void*empty_json_array=jni_NewStringUTF(env,"[]");
    BObj*m=(BObj*)mid;
    logmsg("[BENDER_RELIANCE_PUSH] source=fix102 action=saved-payload-empty-json-array class=%s method=%s sig=%s result=[] policy=mirror-original-gson-empty-list variant=%s\n",
           bender_jni_class_name(cls),m?m->a:"?",m?m->b:"?",variant?variant:"?");
    return empty_json_array;
}
static int bender_reliance_push_payload_match_fix104(void*cls,void*mid){
    BObj*m=bender_known_bobj(mid);if(!m||m->kind!=2)return 0;
    if(strcmp(m->a,"getSavedPushPayloadData"))return 0;
    if(strcmp(m->b,"(Lcom/unity3d/player/UnityPlayerForActivityOrService;)Ljava/lang/String;") &&
       strcmp(m->b,"(Landroid/content/Context;)Ljava/lang/String;"))return 0;
    const char*call_cls=bender_jni_class_name(cls);
    const char*decl_cls=bender_jni_class_name((void*)m->ref0);
    return (call_cls&&!strcmp(call_cls,"com/reliancegames/plugins/pushnotification/RGPushNotification")) ||
           (decl_cls&&!strcmp(decl_cls,"com/reliancegames/plugins/pushnotification/RGPushNotification"));
}
static void *bender_reliance_push_payload_fix104(BEnv*env,void*cls,void*mid,const char*variant){
    BObj*m=bender_known_bobj(mid);
    if(m&&!strcmp(m->a,"getSavedPushPayloadData")){
        logmsg("[BENDER_RELIANCE_PUSH][FINAL_TRACE] source=fix104 call_class=%s declared_class=%s method=%s sig=%s ref1=%llu variant=%s\n",
               bender_jni_class_name(cls),bender_jni_class_name((void*)m->ref0),m->a,m->b,
               (unsigned long long)m->ref1,variant?variant:"?");
    }
    if(!bender_reliance_push_payload_match_fix104(cls,mid))return NULL;
    void*empty_json_array=jni_NewStringUTF(env,"[]");
    logmsg("[BENDER_RELIANCE_PUSH][FINAL_FENCE] source=fix104 action=saved-payload-empty-json-array call_class=%s declared_class=%s method=%s sig=%s result=[] policy=mirror-original-gson-empty-list variant=%s\n",
           bender_jni_class_name(cls),m?bender_jni_class_name((void*)m->ref0):"",m?m->a:"?",m?m->b:"?",variant?variant:"?");
    return empty_json_array;
}
static int bender_jni_sig_classsep_equiv_fix105(const char*actual,const char*expected){
    if(!actual||!expected)return 0;
    int in_ref=0;
    while(*actual&&*expected){
        unsigned char a=(unsigned char)*actual,b=(unsigned char)*expected;
        if(a==b){
            if(a=='L')in_ref=1;
            else if(a==';')in_ref=0;
            actual++;expected++;continue;
        }
        if(in_ref&&((a=='.'&&b=='/')||(a=='/'&&b=='.'))){actual++;expected++;continue;}
        return 0;
    }
    return *actual=='\0'&&*expected=='\0';
}
static int bender_reliance_push_payload_match_fix105(void*cls,void*mid){
    BObj*m=bender_known_bobj(mid);if(!m||m->kind!=2)return 0;
    if(strcmp(m->a,"getSavedPushPayloadData"))return 0;
    if(!bender_jni_sig_classsep_equiv_fix105(m->b,"(Lcom/unity3d/player/UnityPlayerForActivityOrService;)Ljava/lang/String;")&&
       !bender_jni_sig_classsep_equiv_fix105(m->b,"(Landroid/content/Context;)Ljava/lang/String;"))return 0;
    const char*call_cls=bender_jni_class_name(cls);
    const char*decl_cls=bender_jni_class_name((void*)m->ref0);
    return (call_cls&&!strcmp(call_cls,"com/reliancegames/plugins/pushnotification/RGPushNotification"))||
           (decl_cls&&!strcmp(decl_cls,"com/reliancegames/plugins/pushnotification/RGPushNotification"));
}
static void *bender_reliance_push_payload_fix105(BEnv*env,void*cls,void*mid,const char*variant){
    BObj*m=bender_known_bobj(mid);
    if(m&&!strcmp(m->a,"getSavedPushPayloadData")){
        int canonical=bender_jni_sig_classsep_equiv_fix105(m->b,"(Lcom/unity3d/player/UnityPlayerForActivityOrService;)Ljava/lang/String;");
        logmsg("[BENDER_JNI_SIGNATURE][CLASS_SEP_TRACE] source=fix105 method=%s actual_sig=%s canonical_unityplayer_match=%d variant=%s\n",
               m->a,m->b,canonical,variant?variant:"?");
    }
    if(!bender_reliance_push_payload_match_fix105(cls,mid))return NULL;
    void*empty_json_array=jni_NewStringUTF(env,"[]");
    logmsg("[BENDER_RELIANCE_PUSH][SIG_NORM_FENCE] source=fix105 action=saved-payload-empty-json-array call_class=%s declared_class=%s method=%s actual_sig=%s result=[] policy=class-separator-normalized-exact-contract variant=%s\n",
           bender_jni_class_name(cls),m?bender_jni_class_name((void*)m->ref0):"",m?m->a:"?",m?m->b:"?",variant?variant:"?");
    return empty_json_array;
}
static void *bender_jni_static_object_args(BEnv*env,void*cls,void*mid,const char*variant,void*arg0,bjlong larg0,void*arg1){
    BObj*m=(BObj*)mid;const char*name=m?m->a:"?";const char*cn2756=bender_jni_class_name(cls);
    {void*fix105=bender_reliance_push_payload_fix105(env,cls,mid,variant);if(fix105)return fix105;}
    {void*fix104=bender_reliance_push_payload_fix104(env,cls,mid,variant);if(fix104)return fix104;}
    {void*fix102=bender_reliance_push_payload_fix102(env,cls,mid,variant);if(fix102)return fix102;}
    {int handled2843=0;void*rv2843=bender_fix43_static_object_2843(env,cls,m,variant,&handled2843);if(handled2843)return rv2843;}
    if(m&&!strcmp(name,"getString")&&!strcmp(cn2756,"android/provider/Settings$Secure")){const char*key=jni_obj_string(arg1);const char*id=bender_android_id_2756();logmsg("[BENDER_ANDROID_ID] Settings.Secure.getString key=%s -> %s\n",key?key:"(unknown)",id);return jni_NewStringUTF(env,id);}
    if(m&&!strcmp(cn2756,"android/net/Uri")&&!strcmp(name,"encode"))return bender_uri_encode_2756(env,arg0);
    if(m&&!strcmp(cn2756,"android/net/Uri")&&!strcmp(name,"decode"))return bender_uri_decode_2756(env,arg0);
    if(m&&!strcmp(name,"forName")){
        const char*src=jni_obj_string(arg0);if(src&&*src){char cn[1024];size_t n=strlen(src);if(n>=sizeof(cn))n=sizeof(cn)-1u;for(size_t i=0;i<n;i++)cn[i]=(src[i]=='.')?'/':src[i];cn[n]=0;BObj*k=new_obj(1,cn,NULL);set_checkpoint("JNI Class.forName class=%s",cn);logmsg("[BENDER_JNI][CLASS_FOR_NAME] %s -> %p\n",cn,(void*)k);return k;}
    }
    if(m&&!strcmp(name,"obtain")&&!strcmp(bender_jni_class_name(cls),"android/view/MotionEvent")&&arg0){
        BObj*copy=bender_input_event_clone(arg0,"android/view/MotionEvent");
        logmsg("[BENDER_INPUT][MOTION_OBTAIN] variant=%s src=%p dst=%p\n",variant?variant:"?",arg0,(void*)copy);
        return copy;
    }
    if(m&&!strcmp(name,"getDeviceIds")&&!strcmp(bender_jni_class_name(cls),"android/view/InputDevice")){
    void*a=jni_NewIntArray(env,0);
    logmsg("[BENDER_INPUT_DEVICE][IDS] class=android/view/InputDevice -> int[] len=0 array=%p variant=%s\n",a,variant?variant:"");
    return a;
}
if(m&&!strcmp(name,"getInstance")&&!strcmp(bender_jni_class_name(cls),"com/google/firebase/crashlytics/FirebaseCrashlytics")){
    if(!g_firebase_crashlytics_instance_2759)g_firebase_crashlytics_instance_2759=new_obj(4,"com/google/firebase/crashlytics/FirebaseCrashlytics","bender-crashlytics-instance");
    logmsg("[BENDER_CRASHLYTICS][INSTANCE] -> %p class=FirebaseCrashlytics variant=%s collection=%d\n",g_firebase_crashlytics_instance_2759,variant?variant:"",bender_crashlytics_data_collection_2759());return g_firebase_crashlytics_instance_2759;
}
if(m&&!strcmp(name,"getInstance")&&!strcmp(bender_jni_class_name(cls),"com/google/firebase/crashlytics/ndk/FirebaseCrashlyticsNdk")){
    if(!g_firebase_crashlytics_ndk_instance_2759)g_firebase_crashlytics_ndk_instance_2759=new_obj(4,"com/google/firebase/crashlytics/ndk/FirebaseCrashlyticsNdk","bender-crashlytics-ndk-instance");
    logmsg("[BENDER_CRASHLYTICS][NDK_INSTANCE] -> %p class=FirebaseCrashlyticsNdk variant=%s\n",g_firebase_crashlytics_ndk_instance_2759,variant?variant:"");return g_firebase_crashlytics_ndk_instance_2759;
}
if(m&&!strcmp(name,"getInstance")&&!strcmp(bender_jni_class_name(cls),"com/clevertap/unity/CleverTapUnityPlugin")){
    if(!g_clevertap_plugin_instance_2758)g_clevertap_plugin_instance_2758=new_obj(4,"com/clevertap/unity/CleverTapUnityPlugin","bender-clevertap-instance");
    logmsg("[BENDER_CLEVERTAP][INSTANCE] context=%p -> plugin=%p class=com/clevertap/unity/CleverTapUnityPlugin variant=%s\n",arg0,g_clevertap_plugin_instance_2758,variant?variant:"");
    return g_clevertap_plugin_instance_2758;
}
if(m&&!strcmp(name,"getInstance")&&!strcmp(m->b,"()Landroid/view/Choreographer;")){
        if(!g_jni_choreographer_instance)g_jni_choreographer_instance=new_obj(4,"android/view/Choreographer","singleton");
        logmsg("[BENDER_JNI][CHOREOGRAPHER] getInstance -> %p variant=%s\n",g_jni_choreographer_instance,variant?variant:"");
        return g_jni_choreographer_instance;
    }
    if(m&&!strcmp(name,"newInterfaceProxy")){
        BObj*p=new_obj(4,"bitter/jnibridge/Proxy","native-interface-proxy");BObjArray*ia=jni_obj_array_cast(arg1);
        if(p){p->i64=(int64_t)larg0;p->ref0=(uintptr_t)arg1;p->ref1=(uintptr_t)((ia&&ia->length>0)?ia->items[0]:NULL);}
        if(p&&ia){
            for(bjsize ii=0;ii<ia->length;ii++){
                const char*icn=bender_jni_class_name(ia->items[ii]);
                if(icn&&!strcmp(icn,"android/graphics/SurfaceTexture$OnFrameAvailableListener")){
                    int duplicate=0;for(unsigned jj=0;jj<g_bender_surface_frame_proxy_count_fix76&&jj<4u;jj++)if(g_bender_surface_frame_proxies_fix76[jj]==p)duplicate=1;
                    if(!duplicate&&g_bender_surface_frame_proxy_count_fix76<4u){unsigned slot=g_bender_surface_frame_proxy_count_fix76++;g_bender_surface_frame_proxies_fix76[slot]=p;logmsg("[BENDER_MEDIANDK][SURFACE_LISTENER_CAPTURE] source=fix76 slot=%u proxy=%p cookie=0x%llx interface=%s variant=%s\n",slot,(void*)p,(unsigned long long)(uint64_t)larg0,icn,variant?variant:"");}
                    break;
                }
            }
        }
        logmsg("[BENDER_JNI][JNIBRIDGE_PROXY] new proxy=%p cookie=0x%llx interfaces=%p count=%d primary=%p variant=%s\n",(void*)p,(unsigned long long)(uint64_t)larg0,arg1,ia?(int)ia->length:-1,p?(void*)p->ref1:NULL,variant?variant:"");
        return p;
    }
    if(m&&!strcmp(name,"newProxyInstance")){
        BObj*p=new_obj(4,"bitter/jnibridge/Proxy","reflection-androidjavaproxy");
        if(p){p->i64=(int64_t)larg0;p->ref0=0;p->ref1=(uintptr_t)arg1;p->flags|=4u;}
        logmsg("[BENDER_JNI][ANDROID_JAVA_PROXY][NEW] proxy=%p cookie=0x%llx interface=%p class=%s variant=%s dispatch=ReflectionHelper.nativeProxyInvoke\n",(void*)p,(unsigned long long)(uint64_t)larg0,arg1,bender_jni_class_name(arg1),variant?variant:"");
        return p;
    }
    if(m&&!strcmp(name,"fromResource")&&strstr(bender_jni_class_name(cls),"com/google/firebase/FirebaseOptions")){
        if(!BENDER_FIREBASE_APPLICATION_ID[0]){logmsg("[BENDER_FIREBASE][FROM_RESOURCE] source=missing -> NULL\n");return NULL;}
        BObj*fo=new_obj(4,"com/google/firebase/FirebaseOptions","apk-config");logmsg("[BENDER_FIREBASE][FROM_RESOURCE] source=%s -> %p\n",BENDER_FIREBASE_SOURCE,(void*)fo);return fo;
    }
    if(m&&!strcmp(name,"getActivity")&&strstr(bender_jni_class_name(cls),"org/fmod/FMOD")){logmsg("[BENDER_FMOD_JAVA] getActivity -> %p\n",g_fmod_java_context);return g_fmod_java_context;}
    if(m&&!strcmp(name,"getAssetManager")&&strstr(bender_jni_class_name(cls),"org/fmod/FMOD")){BObj*am=new_obj(4,"android/content/res/AssetManager","bender-assets");logmsg("[BENDER_FMOD_JAVA] getAssetManager -> %p\n",(void*)am);return am;}
    if(m&&!strcmp(name,"getAudioDevices")&&strstr(bender_jni_class_name(cls),"org/fmod/FMOD")){BObj*dc=new_obj(1,"android/media/AudioDeviceInfo",NULL);BObjArray*a=jni_NewObjectArray(env,0,dc,NULL);logmsg("[BENDER_FMOD_JAVA] getAudioDevices -> empty array=%p\n",(void*)a);return a;}
    if(m&&!strcmp(name,"getSignatureHash")&&!strcmp(m->b,"()[B")&&!strcmp(bender_jni_class_name(cls),"com/madfingergames/utils/Signature")&&strstr(BENDER_APP_PACKAGE,"deadtrigger2")){
        static const unsigned char bender_dt2_signature_md5_fix83[16]={
            0x9f,0x55,0x52,0x18,0xa6,0xee,0x3a,0xd4,
            0x8e,0x0d,0x6c,0x8a,0x4f,0x62,0xfa,0x75
        };
        BArray*a=(BArray*)jni_NewByteArray(env,16);
        if(!a){
            logmsg("[BENDER_MADFINGER_SIGNATURE] source=fix83 action=return-signature-hash result=NULL reason=allocation-failed variant=%s\n",variant?variant:"?");
            return NULL;
        }
        memcpy(a->data,bender_dt2_signature_md5_fix83,sizeof(bender_dt2_signature_md5_fix83));
        logmsg("[BENDER_MADFINGER_SIGNATURE] source=fix83 action=return-signature-hash class=%s method=%s sig=%s variant=%s result=%p len=16 md5=9f555218a6ee3ad48e0d6c8a4f62fa75 policy=exact-apk-signature-contract\n",
               bender_jni_class_name(cls),name,m->b,variant?variant:"?",(void*)a);
        return a;
    }
    if(m&&!strcmp(name,"getInstance")&&!strcmp(bender_jni_class_name(cls),"com/google/firebase/auth/FirebaseAuth")){
        if(!g_firebase_auth_instance_2761)g_firebase_auth_instance_2761=new_obj(4,"com/google/firebase/auth/FirebaseAuth","bender-auth-instance");
        logmsg("[BENDER_FIREBASE_AUTH][INSTANCE] app=%p -> auth=%p variant=%s\n",arg0,g_firebase_auth_instance_2761,variant?variant:"");return g_firebase_auth_instance_2761;
    }
    if(m&&!strcmp(name,"getInstance")&&!strcmp(bender_jni_class_name(cls),"com/google/firebase/remoteconfig/FirebaseRemoteConfig")){
        if(!g_remote_config_instance_2761)g_remote_config_instance_2761=new_obj(4,"com/google/firebase/remoteconfig/FirebaseRemoteConfig","bender-remote-config-instance");
        logmsg("[BENDER_REMOTE_CONFIG][INSTANCE] app=%p -> remote=%p variant=%s\n",arg0,g_remote_config_instance_2761,variant?variant:"");return g_remote_config_instance_2761;
    }
    if(m&&!strcmp(name,"getInstance")&&!strcmp(m->b,"()Ljava/lang/Object;")&&!strcmp(bender_jni_class_name(cls),"com/unity3d/player/PlayAssetDeliveryUnityWrapper")&&g_pad_wrapper_real_instance_2761){
        logmsg("[BENDER_PAD][INSTANCE_REUSE] source=reflection-getInstance wrapper=%p class=%s variant=%s policy=late-only\n",g_pad_wrapper_real_instance_2761,bender_jni_class_name(g_pad_wrapper_real_instance_2761),variant?variant:"");return g_pad_wrapper_real_instance_2761;
    }
    void*rv2761=jni_call_object_common_arg(env,cls,mid,variant,arg0);
    if(m&&!strcmp(name,"init")&&!strcmp(bender_jni_class_name(cls),"com/unity3d/player/PlayAssetDeliveryUnityWrapper")&&rv2761){
        g_pad_wrapper_real_instance_2761=rv2761;
        logmsg("[BENDER_PAD][INSTANCE_CAPTURE] source=init wrapper=%p class=%s variant=%s policy=observe-only\n",rv2761,bender_jni_class_name(rv2761),variant?variant:"");
    }
    return rv2761;
}
static void *bender_jni_reflection_get_method(BEnv*env,void*target_cls,void*name_obj,void*sig_obj,int is_static,const char*variant){
    (void)env;const char*mn=jni_obj_string(name_obj);const char*ms=jni_obj_string(sig_obj);
    if(!target_cls||!mn||!*mn||!ms||!*ms){
        logmsg("[BENDER_JNI][REFLECTION_HELPER][METHOD][ERROR] variant=%s cls=%p name_obj=%p sig_obj=%p name=%s sig=%s static=%d\n",
               variant?variant:"?",target_cls,name_obj,sig_obj,mn?mn:"(null)",ms?ms:"(null)",is_static);
        return NULL;
    }
    return bender_jni_make_reflected_method(target_cls,mn,ms,is_static,"ReflectionHelper.getMethodID");
}
static void *bender_jni_reflection_get_field(BEnv*env,void*target_cls,void*name_obj,void*sig_obj,int is_static,const char*variant){
    (void)env;const char*fn=jni_obj_string(name_obj);const char*fs=jni_obj_string(sig_obj);
    if(!target_cls||!fn||!*fn||!fs||!*fs){logmsg("[BENDER_JNI][REFLECTION_HELPER][FIELD][ERROR] variant=%s cls=%p name=%s sig=%s static=%d\n",variant?variant:"?",target_cls,fn?fn:"(null)",fs?fs:"(null)",is_static);return NULL;}
    BObj*rf=new_obj(4,"java/lang/reflect/Field",fn);BObj*so=new_obj(3,fs,NULL);
    if(rf){rf->ref0=(uintptr_t)target_cls;rf->ref1=(uintptr_t)so;rf->flags=is_static?1u:0u;}
    logmsg("[BENDER_JNI][REFLECTION_HELPER][FIELD] class=%s name=%s sig=%s static=%d -> reflected=%p\n",bender_jni_class_name(target_cls),fn,fs,is_static,(void*)rf);return rf;
}
static void *bender_jni_reflection_field_signature(BEnv*env,void*field){BObj*rf=bender_known_bobj(field);BObj*so=rf?bender_known_bobj((void*)rf->ref1):NULL;const char*sig=(so&&so->kind==3&&so->a[0])?so->a:"Ljava/lang/Object;";logmsg("[BENDER_JNI][REFLECTION_HELPER][FIELD_SIGNATURE] field=%p name=%s -> %s\n",field,(rf&&rf->b[0])?rf->b:"?",sig);return jni_NewStringUTF(env,sig);}
static void *jni_CallStaticObjectMethod(BEnv*env,void*cls,void*mid,...){
    {void*fix102=bender_reliance_push_payload_fix102(env,cls,mid,"Static-direct");if(fix102)return fix102;}
    BObj*m=(BObj*)mid;void*a0=NULL,*a1=NULL;bjlong l0=0;va_list ap;va_start(ap,mid);
    if(m&&!strcmp(m->a,"getMethodID")&&strstr(bender_jni_class_name(cls),"ReflectionHelper")){
        void*tc=va_arg(ap,void*);void*no=va_arg(ap,void*);void*so=va_arg(ap,void*);int st=va_arg(ap,int);va_end(ap);
        return bender_jni_reflection_get_method(env,tc,no,so,st?1:0,"Static");
    }
    if(m&&!strcmp(m->a,"getFieldID")&&strstr(bender_jni_class_name(cls),"ReflectionHelper")){void*tc=va_arg(ap,void*);void*no=va_arg(ap,void*);void*so=va_arg(ap,void*);int st=va_arg(ap,int);va_end(ap);return bender_jni_reflection_get_field(env,tc,no,so,st?1:0,"Static");}
    if(m&&!strcmp(m->a,"getFieldSignature")&&strstr(bender_jni_class_name(cls),"ReflectionHelper")){void*fo=va_arg(ap,void*);va_end(ap);return bender_jni_reflection_field_signature(env,fo);}
    if(m&&!strcmp(m->a,"getString")&&!strcmp(bender_jni_class_name(cls),"android/provider/Settings$Secure")){a0=va_arg(ap,void*);a1=va_arg(ap,void*);}else if(m&&(!strcmp(bender_jni_class_name(cls),"android/net/Uri"))&&(!strcmp(m->a,"encode")||!strcmp(m->a,"decode"))){a0=va_arg(ap,void*);}else if(m&&(!strcmp(m->a,"forName")||(!strcmp(m->a,"obtain")&&!strcmp(bender_jni_class_name(cls),"android/view/MotionEvent")))){a0=va_arg(ap,void*);}else if(m&&!strcmp(m->a,"getInstance")&&!strcmp(m->b,"(Landroid/content/Context;)Ljava/lang/Object;")){a0=va_arg(ap,void*);}else if(m&&!strcmp(m->a,"newInterfaceProxy")){l0=va_arg(ap,bjlong);a1=va_arg(ap,void*);}else if(m&&!strcmp(m->a,"newProxyInstance")){a0=va_arg(ap,void*);l0=va_arg(ap,bjlong);a1=va_arg(ap,void*);}va_end(ap);
    return bender_jni_static_object_args(env,cls,mid,"Static",a0,l0,a1);
}
static void *jni_CallStaticObjectMethodV(BEnv*env,void*cls,void*mid,va_list ap){
    {void*fix102=bender_reliance_push_payload_fix102(env,cls,mid,"StaticV-direct");if(fix102)return fix102;}
    BObj*m=(BObj*)mid;void*a0=NULL,*a1=NULL;bjlong l0=0;
    if(m&&!strcmp(m->a,"getMethodID")&&strstr(bender_jni_class_name(cls),"ReflectionHelper")){
        va_list cp;va_copy(cp,ap);void*tc=va_arg(cp,void*);void*no=va_arg(cp,void*);void*so=va_arg(cp,void*);int st=va_arg(cp,int);va_end(cp);
        return bender_jni_reflection_get_method(env,tc,no,so,st?1:0,"StaticV");
    }
    if(m&&!strcmp(m->a,"getFieldID")&&strstr(bender_jni_class_name(cls),"ReflectionHelper")){va_list cp;va_copy(cp,ap);void*tc=va_arg(cp,void*);void*no=va_arg(cp,void*);void*so=va_arg(cp,void*);int st=va_arg(cp,int);va_end(cp);return bender_jni_reflection_get_field(env,tc,no,so,st?1:0,"StaticV");}
    if(m&&!strcmp(m->a,"getFieldSignature")&&strstr(bender_jni_class_name(cls),"ReflectionHelper")){va_list cp;va_copy(cp,ap);void*fo=va_arg(cp,void*);va_end(cp);return bender_jni_reflection_field_signature(env,fo);}
    if(m&&((!strcmp(m->a,"getString")&&!strcmp(bender_jni_class_name(cls),"android/provider/Settings$Secure"))||(!strcmp(bender_jni_class_name(cls),"android/net/Uri")&&(!strcmp(m->a,"encode")||!strcmp(m->a,"decode")))||!strcmp(m->a,"forName")||!strcmp(m->a,"newInterfaceProxy")||!strcmp(m->a,"newProxyInstance")||(!strcmp(m->a,"obtain")&&!strcmp(bender_jni_class_name(cls),"android/view/MotionEvent"))||(!strcmp(m->a,"getInstance")&&!strcmp(m->b,"(Landroid/content/Context;)Ljava/lang/Object;")))){va_list cp;va_copy(cp,ap);if(!strcmp(m->a,"getString")&&!strcmp(bender_jni_class_name(cls),"android/provider/Settings$Secure")){a0=va_arg(cp,void*);a1=va_arg(cp,void*);}else if(!strcmp(bender_jni_class_name(cls),"android/net/Uri")&&(!strcmp(m->a,"encode")||!strcmp(m->a,"decode"))){a0=va_arg(cp,void*);}else if(!strcmp(m->a,"forName")||!strcmp(m->a,"obtain")||(!strcmp(m->a,"getInstance")&&!strcmp(m->b,"(Landroid/content/Context;)Ljava/lang/Object;"))){a0=va_arg(cp,void*);}else if(!strcmp(m->a,"newInterfaceProxy")){l0=va_arg(cp,bjlong);a1=va_arg(cp,void*);}else{a0=va_arg(cp,void*);l0=va_arg(cp,bjlong);a1=va_arg(cp,void*);}va_end(cp);}
    return bender_jni_static_object_args(env,cls,mid,"StaticV",a0,l0,a1);
}
static void *jni_CallStaticObjectMethodA(BEnv*env,void*cls,void*mid,const void*args){
    {void*fix105=bender_reliance_push_payload_fix105(env,cls,mid,"StaticA-sig-normalized");if(fix105)return fix105;}
    {void*fix104=bender_reliance_push_payload_fix104(env,cls,mid,"StaticA-final");if(fix104)return fix104;}
    {void*fix102=bender_reliance_push_payload_fix102(env,cls,mid,"StaticA-direct");if(fix102)return fix102;}
    BObj*m=(BObj*)mid;void*a0=NULL,*a1=NULL;bjlong l0=0;
    if(args&&m&&!strcmp(m->a,"getMethodID")&&strstr(bender_jni_class_name(cls),"ReflectionHelper")){
        const uint64_t*v=(const uint64_t*)args;
        return bender_jni_reflection_get_method(env,(void*)(uintptr_t)v[0],(void*)(uintptr_t)v[1],(void*)(uintptr_t)v[2],(int)(v[3]&0xffu),"StaticA");
    }
    if(args&&m&&!strcmp(m->a,"getFieldID")&&strstr(bender_jni_class_name(cls),"ReflectionHelper")){const uint64_t*v=(const uint64_t*)args;return bender_jni_reflection_get_field(env,(void*)(uintptr_t)v[0],(void*)(uintptr_t)v[1],(void*)(uintptr_t)v[2],(int)(v[3]&0xffu),"StaticA");}
    if(args&&m&&!strcmp(m->a,"getFieldSignature")&&strstr(bender_jni_class_name(cls),"ReflectionHelper")){const uint64_t*v=(const uint64_t*)args;return bender_jni_reflection_field_signature(env,(void*)(uintptr_t)v[0]);}
    if(args&&m&&!strcmp(m->a,"getString")&&!strcmp(bender_jni_class_name(cls),"android/provider/Settings$Secure")){const uint64_t*v=(const uint64_t*)args;a0=(void*)(uintptr_t)v[0];a1=(void*)(uintptr_t)v[1];}
    else if(args&&m&&!strcmp(bender_jni_class_name(cls),"android/net/Uri")&&(!strcmp(m->a,"encode")||!strcmp(m->a,"decode")))a0=(void*)(uintptr_t)(*(const uint64_t*)args);
    else if(args&&m&&(!strcmp(m->a,"forName")||(!strcmp(m->a,"obtain")&&!strcmp(bender_jni_class_name(cls),"android/view/MotionEvent"))))a0=(void*)(uintptr_t)(*(const uint64_t*)args);
    else if(args&&m&&!strcmp(m->a,"getInstance")&&!strcmp(m->b,"(Landroid/content/Context;)Ljava/lang/Object;"))a0=(void*)(uintptr_t)(*(const uint64_t*)args);
    else if(args&&m&&!strcmp(m->a,"newInterfaceProxy")){const uint64_t*v=(const uint64_t*)args;l0=(bjlong)(int64_t)v[0];a1=(void*)(uintptr_t)v[1];}
    else if(args&&m&&!strcmp(m->a,"newProxyInstance")){const uint64_t*v=(const uint64_t*)args;a0=(void*)(uintptr_t)v[0];l0=(bjlong)(int64_t)v[1];a1=(void*)(uintptr_t)v[2];}
    return bender_jni_static_object_args(env,cls,mid,"StaticA",a0,l0,a1);
}
static bjboolean jni_CallStaticBooleanMethod(BEnv*env,void*cls,void*mid,...){return jni_call_boolean_common(env,cls,mid,"Static");}
static bjboolean jni_CallStaticBooleanMethodV(BEnv*env,void*cls,void*mid,va_list ap){(void)ap;return jni_call_boolean_common(env,cls,mid,"StaticV");}
static bjboolean jni_CallStaticBooleanMethodA(BEnv*env,void*cls,void*mid,const void*args){(void)args;return jni_call_boolean_common(env,cls,mid,"StaticA");}
static bjint bender_static_int_arg_fix5(BEnv*env,void*cls,void*mid,const char*variant,void*arg0){BObj*m=(BObj*)mid;if(m&&!strcmp(bender_jni_class_name(cls),"java/lang/Integer")&&!strcmp(m->a,"parseInt")&&!strcmp(m->b,"(Ljava/lang/String;)I")){const char*s=jni_obj_string(arg0);bjint v=s?(bjint)strtol(s,NULL,10):0;logmsg("[BENDER_AUDIO_MANAGER][PARSE_INT] text=%s -> %d\n",s?s:"(null)",(int)v);return v;}return jni_call_int_common(env,cls,mid,variant);}
static bjint jni_CallStaticIntMethod(BEnv*env,void*cls,void*mid,...){BObj*m=(BObj*)mid;void*a0=NULL;va_list ap;va_start(ap,mid);if(m&&!strcmp(m->a,"parseInt")&&!strcmp(m->b,"(Ljava/lang/String;)I"))a0=va_arg(ap,void*);va_end(ap);return bender_static_int_arg_fix5(env,cls,mid,"Static",a0);}
static bjint jni_CallStaticIntMethodV(BEnv*env,void*cls,void*mid,va_list ap){BObj*m=(BObj*)mid;void*a0=NULL;if(m&&!strcmp(m->a,"parseInt")&&!strcmp(m->b,"(Ljava/lang/String;)I")){va_list cp;va_copy(cp,ap);a0=va_arg(cp,void*);va_end(cp);}return bender_static_int_arg_fix5(env,cls,mid,"StaticV",a0);}
static bjint jni_CallStaticIntMethodA(BEnv*env,void*cls,void*mid,const void*args){BObj*m=(BObj*)mid;void*a0=NULL;if(args&&m&&!strcmp(m->a,"parseInt")&&!strcmp(m->b,"(Ljava/lang/String;)I"))a0=(void*)(uintptr_t)(*(const uint64_t*)args);return bender_static_int_arg_fix5(env,cls,mid,"StaticA",a0);}
static bjlong jni_CallStaticLongMethod(BEnv*env,void*cls,void*mid,...){return jni_call_long_common(env,cls,mid,"Static");}
static bjlong jni_CallStaticLongMethodV(BEnv*env,void*cls,void*mid,va_list ap){(void)ap;return jni_call_long_common(env,cls,mid,"StaticV");}
static bjlong jni_CallStaticLongMethodA(BEnv*env,void*cls,void*mid,const void*args){(void)args;return jni_call_long_common(env,cls,mid,"StaticA");}
static bjfloat jni_CallStaticFloatMethod(BEnv*env,void*cls,void*mid,...){return jni_call_float_common(env,cls,mid,"Static");}
static bjfloat jni_CallStaticFloatMethodV(BEnv*env,void*cls,void*mid,va_list ap){(void)ap;return jni_call_float_common(env,cls,mid,"StaticV");}
static bjfloat jni_CallStaticFloatMethodA(BEnv*env,void*cls,void*mid,const void*args){(void)args;return jni_call_float_common(env,cls,mid,"StaticA");}
static bjdouble jni_CallStaticDoubleMethod(BEnv*env,void*cls,void*mid,...){return jni_call_double_common(env,cls,mid,"Static");}
static bjdouble jni_CallStaticDoubleMethodV(BEnv*env,void*cls,void*mid,va_list ap){(void)ap;return jni_call_double_common(env,cls,mid,"StaticV");}
static bjdouble jni_CallStaticDoubleMethodA(BEnv*env,void*cls,void*mid,const void*args){(void)args;return jni_call_double_common(env,cls,mid,"StaticA");}
static int bender_static_void_arg0_2762(BObj*m){return m&&((!strcmp(m->a,"init")||!strcmp(m->a,"setBackgroundCallback"))||(!strcmp(m->a,"connect")&&bender_madfinger_billing_connect_sig_fix73(m->b)&&!strcmp(bender_jni_class_name((void*)m->ref0),"com/madfingergames/billing/BillingManager")));}
static void jni_CallStaticVoidMethod(BEnv*env,void*cls,void*mid,...){BObj*m=(BObj*)mid;void*a0=NULL;va_list ap;va_start(ap,mid);if(bender_static_void_arg0_2762(m))a0=va_arg(ap,void*);else if(bender_madfinger_billing_query_sku_sig_fix74(m)){(void)va_arg(ap,void*);(void)va_arg(ap,void*);a0=va_arg(ap,void*);}else if(bender_madfinger_billing_query_purchases_sig_fix74(m)){(void)va_arg(ap,void*);a0=va_arg(ap,void*);}va_end(ap);jni_call_void_common(env,cls,mid,"Static",a0);}
static void jni_CallStaticVoidMethodV(BEnv*env,void*cls,void*mid,va_list ap){BObj*m=(BObj*)mid;void*a0=NULL;if(bender_static_void_arg0_2762(m)){va_list cp;va_copy(cp,ap);a0=va_arg(cp,void*);va_end(cp);}else if(bender_madfinger_billing_query_sku_sig_fix74(m)){va_list cp;va_copy(cp,ap);(void)va_arg(cp,void*);(void)va_arg(cp,void*);a0=va_arg(cp,void*);va_end(cp);}else if(bender_madfinger_billing_query_purchases_sig_fix74(m)){va_list cp;va_copy(cp,ap);(void)va_arg(cp,void*);a0=va_arg(cp,void*);va_end(cp);}jni_call_void_common(env,cls,mid,"StaticV",a0);}
static int bender_jni_sig_classsep_equiv_fix109(const char*actual,const char*expected){
    if(!actual||!expected)return 0;
    int in_ref=0;
    while(*actual&&*expected){
        unsigned char a=(unsigned char)*actual++,e=(unsigned char)*expected++;
        if(a=='L'&&e=='L')in_ref=1;
        if(in_ref&&((a=='.'&&e=='/')||(a=='/'&&e=='.')))continue;
        if(a!=e)return 0;
        if(a==';')in_ref=0;
    }
    return *actual==0&&*expected==0;
}
static int bender_reliance_selective_permission_match_fix109(void*cls,void*mid){
    BObj*m=bender_known_bobj(mid);if(!m)return 0;
    const char*call_class=bender_jni_class_name(cls);
    const char*decl_class=bender_jni_class_name((void*)m->ref0);
    if(strcmp(call_class,"com/reliancegames/plugins/permission/PermissionManager")&&
       strcmp(decl_class,"com/reliancegames/plugins/permission/PermissionManager"))return 0;
    if(strcmp(m->a,"askSelectivePermission"))return 0;
    if(!bender_jni_sig_classsep_equiv_fix109(m->b,"(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V")&&
       !bender_jni_sig_classsep_equiv_fix109(m->b,"(Lcom/unity3d/player/UnityPlayerForActivityOrService;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"))return 0;
    return 1;
}
static size_t bender_json_escape_fix109(const char*in,char*out,size_t cap){
    size_t n=0;if(!out||cap==0)return 0;if(!in)in="";
    while(*in&&n+1<cap){unsigned char c=(unsigned char)*in++;
        const char*esc=NULL;char tiny[7];
        if(c=='\\')esc="\\\\";else if(c=='\"')esc="\\\"";else if(c=='\n')esc="\\n";else if(c=='\r')esc="\\r";else if(c=='\t')esc="\\t";
        else if(c<0x20u){snprintf(tiny,sizeof(tiny),"\\u%04x",(unsigned)c);esc=tiny;}
        if(esc){size_t k=strlen(esc);if(n+k>=cap)break;memcpy(out+n,esc,k);n+=k;}else out[n++]=(char)c;
    }
    out[n]=0;return n;
}
static int bender_reliance_selective_permission_callback_fix109(BEnv*env,void*cls,void*mid,const void*args,const char*variant){
    if(!args||!bender_reliance_selective_permission_match_fix109(cls,mid))return 0;
    const uint64_t*v=(const uint64_t*)args;
    const char*game_object=jni_obj_string((void*)(uintptr_t)v[1]);
    const char*callback=jni_obj_string((void*)(uintptr_t)v[2]);
    const char*education=jni_obj_string((void*)(uintptr_t)v[3]);
    const char*app_settings=jni_obj_string((void*)(uintptr_t)v[4]);
    const char*permission=jni_obj_string((void*)(uintptr_t)v[5]);
    BObj*m=bender_known_bobj(mid);
    const char*call_class=bender_jni_class_name(cls);const char*decl_class=m?bender_jni_class_name((void*)m->ref0):"";
    logmsg("[BENDER_RELIANCE_PERMISSION][REQUEST] source=fix109 variant=%s call_class=%s declared_class=%s method=%s sig=%s game_object=%s callback=%s permission=%s education=%s app_settings=%s policy=non-android-runtime-permission-unavailable code_patch=0 forced_completion=0\n",
        variant?variant:"?",call_class,decl_class,m?m->a:"?",m?m->b:"?",game_object?game_object:"(null)",callback?callback:"(null)",permission?permission:"(null)",education?education:"(null)",app_settings?app_settings:"(null)");
    if(!game_object||!*game_object||!callback||!*callback||!permission||!*permission){
        logmsg("[BENDER_RELIANCE_PERMISSION][CALLBACK_SKIP] source=fix109 reason=missing-plugin-callback-arguments grant_fabricated=0 processed_fabricated=0\n");return 0;
    }
    if(!g_unity_native_send_message_fn_2758){
        logmsg("[BENDER_RELIANCE_PERMISSION][CALLBACK_SKIP] source=fix109 reason=nativeUnitySendMessage-not-captured grant_fabricated=0 processed_fabricated=0\n");return 0;
    }
    char escaped[384],json[512];bender_json_escape_fix109(permission,escaped,sizeof(escaped));
    int nw=snprintf(json,sizeof(json),"[{\"isPermissionNew\":true,\"isRequired\":false,\"name\":\"%s\"}]",escaped);
    if(nw<0||(size_t)nw>=sizeof(json)){
        logmsg("[BENDER_RELIANCE_PERMISSION][CALLBACK_SKIP] source=fix109 reason=payload-overflow grant_fabricated=0 processed_fabricated=0\n");return 0;
    }
    logmsg("[BENDER_RELIANCE_PERMISSION][CALLBACK] source=fix109 object=%s method=%s permission=%s payload=%s semantic=DEX-PermissionInfo-array-Gson-default-first-request grant=denied grant_fabricated=0 processed_via_plugin_callback=1 code_patch=0 forced_completion=0\n",game_object,callback,permission,json);
    bender_unity_message_2758(env,game_object,callback,json,"reliance-selective-permission-fix109");
    return 1;
}
static void jni_CallStaticVoidMethodA(BEnv*env,void*cls,void*mid,const void*args){BObj*m=(BObj*)mid;void*a0=NULL,*ump_dismiss=NULL;if(args&&bender_static_void_arg0_2762(m))a0=(void*)(uintptr_t)(*(const uint64_t*)args);else if(args&&bender_madfinger_billing_query_sku_sig_fix74(m)){const uint64_t*v=(const uint64_t*)args;a0=(void*)(uintptr_t)v[2];}else if(args&&bender_madfinger_billing_query_purchases_sig_fix74(m)){const uint64_t*v=(const uint64_t*)args;a0=(void*)(uintptr_t)v[1];}else if(args&&m&&!strcmp(m->a,"loadAndShowConsentFormIfRequired")){const uint64_t*v=(const uint64_t*)args;ump_dismiss=(void*)(uintptr_t)v[1];}jni_call_void_common(env,cls,mid,"StaticA",a0);bender_reliance_selective_permission_callback_fix109(env,cls,mid,args,"StaticA");if(ump_dismiss){logmsg("[BENDER_UMP][FORM] policy=not-required dismiss_proxy=%p action=callback-null-error\n",ump_dismiss);bender_jni_proxy_dispatch(ump_dismiss,"onConsentFormDismissed",NULL);}if(m&&!strcmp(m->a,"createNotificationChannel")&&!strcmp(bender_jni_class_name(cls),"com/clevertap/unity/CleverTapUnityPlugin")){bender_clevertap_bootstrap_2758(env,"createNotificationChannel");}}

static bjint vm_DestroyJavaVM(BVM*vm){(void)vm;return 0;} static bjint vm_AttachCurrentThread(BVM*vm,void**penv,void*args){(void)vm;(void)args;if(penv)*penv=&g_env;return 0;} static bjint vm_DetachCurrentThread(BVM*vm){(void)vm;return 0;} static bjint vm_GetEnv(BVM*vm,void**penv,bjint ver){(void)vm;(void)ver;if(penv)*penv=&g_env;return 0;} static bjint vm_AttachDaemon(BVM*vm,void**penv,void*args){return vm_AttachCurrentThread(vm,penv,args);}
static void __attribute__((unused)) init_fake_jni(void){for(size_t i=0;i<256;i++)g_jni_table[i]=(void*)jni_generic;memset(g_vm_table,0,sizeof(g_vm_table));g_env.functions=g_jni_table;g_vm.functions=g_vm_table;
#define JNISLOT(i,fn) g_jni_table[(i)]=(void*)(fn)
    JNISLOT(4,jni_GetVersion); JNISLOT(6,jni_FindClass); JNISLOT(7,jni_FromReflectedMethod); JNISLOT(9,jni_ToReflectedMethod); JNISLOT(13,jni_Throw); JNISLOT(14,jni_ThrowNew); JNISLOT(15,jni_ExceptionOccurred); JNISLOT(16,jni_ExceptionDescribe); JNISLOT(17,jni_ExceptionClear); JNISLOT(18,jni_FatalError);
    JNISLOT(21,jni_NewGlobalRef); JNISLOT(22,jni_DeleteGlobalRef); JNISLOT(23,jni_DeleteLocalRef); JNISLOT(24,jni_IsSameObject); JNISLOT(25,jni_NewLocalRef); JNISLOT(28,jni_NewObject); JNISLOT(29,jni_NewObjectV); JNISLOT(30,jni_NewObjectA); JNISLOT(11,jni_IsAssignableFrom); JNISLOT(31,jni_GetObjectClass); JNISLOT(32,jni_IsInstanceOf); JNISLOT(33,jni_GetMethodID);
    JNISLOT(34,jni_CallObjectMethod); JNISLOT(35,jni_CallObjectMethodV); JNISLOT(36,jni_CallObjectMethodA); JNISLOT(37,jni_CallBooleanMethod); JNISLOT(38,jni_CallBooleanMethodV); JNISLOT(39,jni_CallBooleanMethodA); JNISLOT(49,jni_CallIntMethod); JNISLOT(50,jni_CallIntMethodV); JNISLOT(51,jni_CallIntMethodA); JNISLOT(52,jni_CallLongMethod); JNISLOT(53,jni_CallLongMethodV); JNISLOT(54,jni_CallLongMethodA); JNISLOT(55,jni_CallFloatMethod); JNISLOT(56,jni_CallFloatMethodV); JNISLOT(57,jni_CallFloatMethodA); JNISLOT(58,jni_CallDoubleMethod); JNISLOT(59,jni_CallDoubleMethodV); JNISLOT(60,jni_CallDoubleMethodA); JNISLOT(61,jni_CallVoidMethod); JNISLOT(62,jni_CallVoidMethodV); JNISLOT(63,jni_CallVoidMethodA); JNISLOT(94,jni_GetFieldID); JNISLOT(95,jni_GetObjectField); JNISLOT(100,jni_GetIntField); JNISLOT(101,jni_GetLongField); JNISLOT(102,jni_GetFloatField); JNISLOT(103,jni_GetDoubleField);
    JNISLOT(113,jni_GetStaticMethodID); JNISLOT(114,jni_CallStaticObjectMethod); JNISLOT(115,jni_CallStaticObjectMethodV); JNISLOT(116,jni_CallStaticObjectMethodA); JNISLOT(117,jni_CallStaticBooleanMethod); JNISLOT(118,jni_CallStaticBooleanMethodV); JNISLOT(119,jni_CallStaticBooleanMethodA); JNISLOT(129,jni_CallStaticIntMethod); JNISLOT(130,jni_CallStaticIntMethodV); JNISLOT(131,jni_CallStaticIntMethodA); JNISLOT(132,jni_CallStaticLongMethod); JNISLOT(133,jni_CallStaticLongMethodV); JNISLOT(134,jni_CallStaticLongMethodA); JNISLOT(135,jni_CallStaticFloatMethod); JNISLOT(136,jni_CallStaticFloatMethodV); JNISLOT(137,jni_CallStaticFloatMethodA); JNISLOT(138,jni_CallStaticDoubleMethod); JNISLOT(139,jni_CallStaticDoubleMethodV); JNISLOT(140,jni_CallStaticDoubleMethodA); JNISLOT(141,jni_CallStaticVoidMethod); JNISLOT(142,jni_CallStaticVoidMethodV); JNISLOT(143,jni_CallStaticVoidMethodA); JNISLOT(144,jni_GetStaticFieldID); JNISLOT(145,jni_GetStaticObjectField); JNISLOT(150,jni_GetStaticIntField); JNISLOT(151,jni_GetStaticLongField); JNISLOT(152,jni_GetStaticFloatField); JNISLOT(153,jni_GetStaticDoubleField);
    JNISLOT(163,jni_NewString); JNISLOT(164,jni_GetStringLength); JNISLOT(165,jni_GetStringChars); JNISLOT(166,jni_ReleaseStringChars);
    JNISLOT(167,jni_NewStringUTF); JNISLOT(168,jni_GetStringUTFLength); JNISLOT(169,jni_GetStringUTFChars); JNISLOT(170,jni_ReleaseStringUTFChars);
    JNISLOT(171,jni_GetArrayLength); JNISLOT(172,jni_NewObjectArray); JNISLOT(173,jni_GetObjectArrayElement); JNISLOT(174,jni_SetObjectArrayElement); JNISLOT(175,jni_NewBooleanArray); JNISLOT(176,jni_NewByteArray); JNISLOT(177,jni_NewCharArray); JNISLOT(178,jni_NewShortArray); JNISLOT(179,jni_NewIntArray); JNISLOT(180,jni_NewLongArray); JNISLOT(181,jni_NewFloatArray); JNISLOT(182,jni_NewDoubleArray);
    JNISLOT(183,jni_GetBooleanArrayElements); JNISLOT(184,jni_GetByteArrayElements); JNISLOT(185,jni_GetCharArrayElements); JNISLOT(186,jni_GetShortArrayElements); JNISLOT(187,jni_GetIntArrayElements); JNISLOT(188,jni_GetLongArrayElements); JNISLOT(189,jni_GetFloatArrayElements); JNISLOT(190,jni_GetDoubleArrayElements);
    JNISLOT(191,jni_ReleaseBooleanArrayElements); JNISLOT(192,jni_ReleaseByteArrayElements); JNISLOT(193,jni_ReleaseCharArrayElements); JNISLOT(194,jni_ReleaseShortArrayElements); JNISLOT(195,jni_ReleaseIntArrayElements); JNISLOT(196,jni_ReleaseLongArrayElements); JNISLOT(197,jni_ReleaseFloatArrayElements); JNISLOT(198,jni_ReleaseDoubleArrayElements);
    JNISLOT(199,jni_GetBooleanArrayRegion); JNISLOT(200,jni_GetByteArrayRegion); JNISLOT(201,jni_GetCharArrayRegion); JNISLOT(202,jni_GetShortArrayRegion); JNISLOT(203,jni_GetIntArrayRegion); JNISLOT(204,jni_GetLongArrayRegion); JNISLOT(205,jni_GetFloatArrayRegion); JNISLOT(206,jni_GetDoubleArrayRegion);
    JNISLOT(207,jni_SetBooleanArrayRegion); JNISLOT(208,jni_SetByteArrayRegion); JNISLOT(209,jni_SetCharArrayRegion); JNISLOT(210,jni_SetShortArrayRegion); JNISLOT(211,jni_SetIntArrayRegion); JNISLOT(212,jni_SetLongArrayRegion); JNISLOT(213,jni_SetFloatArrayRegion); JNISLOT(214,jni_SetDoubleArrayRegion);
    JNISLOT(215,jni_RegisterNatives); JNISLOT(220,jni_GetStringRegion); JNISLOT(221,jni_GetStringUTFRegion); JNISLOT(224,jni_GetStringCritical); JNISLOT(225,jni_ReleaseStringCritical); JNISLOT(219,jni_GetJavaVM); JNISLOT(228,jni_ExceptionCheck); JNISLOT(229,jni_NewDirectByteBuffer); JNISLOT(230,jni_GetDirectBufferAddress); JNISLOT(231,jni_GetDirectBufferCapacity);
#undef JNISLOT
    g_vm_table[3]=(void*)vm_DestroyJavaVM;g_vm_table[4]=(void*)vm_AttachCurrentThread;g_vm_table[5]=(void*)vm_DetachCurrentThread;g_vm_table[6]=(void*)vm_GetEnv;g_vm_table[7]=(void*)vm_AttachDaemon;
}

/* Android/Bionic C++ lifecycle bridge.
 * Guest destructors MUST NOT be registered in host glibc: the probe unmaps
 * guest ELF images before process exit, so host __cxa_atexit would later call
 * stale/unmapped guest function pointers. We retain registrations under BENDER
 * control and suppress automatic finalize during probe phases. */
static const char *guest_module_name_for_address(const void *ptr) {
    uintptr_t p=(uintptr_t)ptr;
    for(int i=0;i<g_module_count;i++) {
        Module *m=&g_modules[i];
        if(m->reserve && p>=(uintptr_t)m->reserve && p<(uintptr_t)m->reserve+m->reserve_size) return m->name;
    }
    return "host/unknown";
}


/* BENDER guest heap guard V2.6.0 (allocator compatibility retained from V2.5.1).
 * Android/Unity allocations must not share glibc malloc metadata while we are
 * validating ABI bridges. The hard POST redzone starts after an allocator-like
 * usable chunk, not immediately after the requested byte count. This mirrors the
 * 16-byte allocation quantum normally visible to AArch64 Android allocators and
 * avoids treating writes inside allocator padding (e.g. malloc(40) -> 48 usable)
 * as a real heap overflow. Padding is still instrumented and reported. */
#define MAX_GUEST_ALLOCS 131072
#define MAX_GUEST_ACTIVE_HASH 262144u
#define MAX_GUEST_FREED_TOMBSTONES 262144u
#define MAX_GUEST_EVER_PTRS 524288u
#define GUEST_REDZONE 64u
#define GUEST_MALLOC_QUANTUM 16u
#define GUEST_PADDING_MARK 0xC3u
#define GUEST_ALLOC_MAGIC UINT64_C(0xB36E251110CA7E55)
typedef struct {
    void *map_base;
    size_t map_size;
    unsigned char *user;
    size_t user_size;      /* bytes requested by guest */
    size_t usable_size;    /* allocator-like rounded chunk */
    size_t alignment;
    uintptr_t caller;
    uint64_t magic;
    unsigned padding_reported;
    unsigned backend; /* 0=mmap guard pages, 1=fast host heap + redzones */
    int active;
} GuestAlloc;
static GuestAlloc g_guest_allocs[MAX_GUEST_ALLOCS];
static uintptr_t g_guest_active_keys[MAX_GUEST_ACTIVE_HASH];
static uint32_t g_guest_active_vals[MAX_GUEST_ACTIVE_HASH]; /* slot index + 1; key=1 is tombstone */
static uint32_t g_guest_free_slots[MAX_GUEST_ALLOCS];
static size_t g_guest_free_slot_count, g_guest_next_slot;
static uintptr_t g_guest_freed_tombstones[MAX_GUEST_FREED_TOMBSTONES];
static uintptr_t g_guest_ever_ptrs[MAX_GUEST_EVER_PTRS];
static size_t g_guest_freed_tombstone_count;
static size_t g_guest_ever_ptr_count;
static pthread_mutex_t g_guest_heap_meta_lock=PTHREAD_MUTEX_INITIALIZER;
static size_t g_guest_alloc_total, g_guest_free_total, g_guest_alloc_peak, g_guest_alloc_active;
static size_t g_guest_guard_alloc_total, g_guest_fast_alloc_total;
static int g_guest_heap_fast_mode;
static size_t g_guest_heap_corruptions, g_guest_padding_writes;
static size_t g_guest_stale_free_ignored, g_guest_interior_free_ignored, g_guest_orphan_free_ignored, g_guest_foreign_free_forwarded;
static unsigned g_guest_heap_capacity_warn_level;
static int g_guest_heap_trace_detail;

static size_t guest_norm_align(size_t a) {
    if(a<sizeof(void*)) a=sizeof(void*);
    size_t p=1; while(p<a && p<(1u<<20)) p<<=1;
    return p<a ? 0 : p;
}
static size_t guest_usable_size(size_t requested) {
    if(requested==0) requested=1;
    if(requested>SIZE_MAX-(GUEST_MALLOC_QUANTUM-1u)) return 0;
    return (requested+(GUEST_MALLOC_QUANTUM-1u))&~(size_t)(GUEST_MALLOC_QUANTUM-1u);
}
static size_t guest_active_hash(uintptr_t p){
    uint64_t x=(uint64_t)(p>>4);x^=x>>33;x*=UINT64_C(0xff51afd7ed558ccd);x^=x>>33;return (size_t)x&(MAX_GUEST_ACTIVE_HASH-1u);
}
static int guest_active_add(const void *ptr,size_t slot){
    if(!ptr||slot>=MAX_GUEST_ALLOCS)return -1;
    uintptr_t p=(uintptr_t)ptr;size_t h=guest_active_hash(p),first_tomb=SIZE_MAX;
    for(size_t n=0;n<MAX_GUEST_ACTIVE_HASH;n++){
        size_t i=(h+n)&(MAX_GUEST_ACTIVE_HASH-1u);uintptr_t k=g_guest_active_keys[i];
        if(k==p){g_guest_active_vals[i]=(uint32_t)(slot+1u);return 0;}
        if(k==1u){if(first_tomb==SIZE_MAX)first_tomb=i;continue;}
        if(k==0u){if(first_tomb!=SIZE_MAX)i=first_tomb;g_guest_active_keys[i]=p;g_guest_active_vals[i]=(uint32_t)(slot+1u);return 0;}
    }
    if(first_tomb!=SIZE_MAX){g_guest_active_keys[first_tomb]=p;g_guest_active_vals[first_tomb]=(uint32_t)(slot+1u);return 0;}
    return -1;
}
static void guest_active_remove(const void *ptr){
    if(!ptr)return;
    uintptr_t p=(uintptr_t)ptr;
    size_t h=guest_active_hash(p);
    for(size_t n=0;n<MAX_GUEST_ACTIVE_HASH;n++){
        size_t i=(h+n)&(MAX_GUEST_ACTIVE_HASH-1u);uintptr_t k=g_guest_active_keys[i];
        if(k==0u)return;
        if(k==p){g_guest_active_keys[i]=1u;g_guest_active_vals[i]=0u;return;}
    }
}
static GuestAlloc *guest_alloc_find(const void *ptr) {
    if(!ptr) return NULL;
    uintptr_t p=(uintptr_t)ptr;size_t h=guest_active_hash(p);
    for(size_t n=0;n<MAX_GUEST_ACTIVE_HASH;n++){
        size_t i=(h+n)&(MAX_GUEST_ACTIVE_HASH-1u);uintptr_t k=g_guest_active_keys[i];
        if(k==0u)break;
        if(k==p){uint32_t v=g_guest_active_vals[i];if(v&&v<=MAX_GUEST_ALLOCS){GuestAlloc*r=&g_guest_allocs[v-1u];if(r->active&&r->user==(const unsigned char*)ptr)return r;}break;}
    }
    /* Defensive fallback if the hash table ever became inconsistent. */
    for(size_t i=0;i<g_guest_next_slot&&i<MAX_GUEST_ALLOCS;i++) if(g_guest_allocs[i].active && g_guest_allocs[i].user==(const unsigned char*)ptr) return &g_guest_allocs[i];
    return NULL;
}
static GuestAlloc *guest_alloc_find_containing(const void *ptr) {
    if(!ptr) return NULL;
    uintptr_t p=(uintptr_t)ptr;
    for(size_t i=0;i<MAX_GUEST_ALLOCS;i++) if(g_guest_allocs[i].active){
        uintptr_t lo=(uintptr_t)g_guest_allocs[i].user, hi=lo+g_guest_allocs[i].usable_size;
        if(p>lo && p<hi) return &g_guest_allocs[i];
    }
    return NULL;
}
static size_t guest_freed_tombstone_hash(uintptr_t p){
    uint64_t x=(uint64_t)(p>>4);x^=x>>33;x*=UINT64_C(0xff51afd7ed558ccd);x^=x>>33;return (size_t)x&(MAX_GUEST_FREED_TOMBSTONES-1u);
}
static void guest_freed_tombstone_add(const void *ptr){
    if(!ptr)return;
    uintptr_t p=(uintptr_t)ptr;
    size_t h=guest_freed_tombstone_hash(p);
    for(size_t n=0;n<MAX_GUEST_FREED_TOMBSTONES;n++){size_t i=(h+n)&(MAX_GUEST_FREED_TOMBSTONES-1u);uintptr_t v=g_guest_freed_tombstones[i];if(v==p)return;if(v==0){g_guest_freed_tombstones[i]=p;++g_guest_freed_tombstone_count;return;}}
    logmsg("[BENDER_HEAP][TOMBSTONE_FULL] ptr=%p capacity=%u\n",ptr,(unsigned)MAX_GUEST_FREED_TOMBSTONES);
}
static int guest_freed_tombstone_has(const void *ptr){
    if(!ptr)return 0;
    uintptr_t p=(uintptr_t)ptr;
    size_t h=guest_freed_tombstone_hash(p);
    for(size_t n=0;n<MAX_GUEST_FREED_TOMBSTONES;n++){size_t i=(h+n)&(MAX_GUEST_FREED_TOMBSTONES-1u);uintptr_t v=g_guest_freed_tombstones[i];if(v==p)return 1;if(v==0)return 0;}
    return 0;
}
static size_t guest_ever_ptr_hash(uintptr_t p){
    uint64_t x=(uint64_t)(p>>4);x^=x>>29;x*=UINT64_C(0x9e3779b97f4a7c15);x^=x>>32;return (size_t)x&(MAX_GUEST_EVER_PTRS-1u);
}
static void guest_ever_ptr_add(const void *ptr){
    if(!ptr)return;
    uintptr_t p=(uintptr_t)ptr;size_t h=guest_ever_ptr_hash(p);
    for(size_t n=0;n<MAX_GUEST_EVER_PTRS;n++){
        size_t i=(h+n)&(MAX_GUEST_EVER_PTRS-1u);uintptr_t v=g_guest_ever_ptrs[i];
        if(v==p)return;
        if(v==0){g_guest_ever_ptrs[i]=p;++g_guest_ever_ptr_count;return;}
    }
    logmsg("[BENDER_HEAP][EVER_PTR_FULL] ptr=%p capacity=%u\n",ptr,(unsigned)MAX_GUEST_EVER_PTRS);
}
static int guest_ever_ptr_has(const void *ptr){
    if(!ptr)return 0;
    uintptr_t p=(uintptr_t)ptr;size_t h=guest_ever_ptr_hash(p);
    for(size_t n=0;n<MAX_GUEST_EVER_PTRS;n++){
        size_t i=(h+n)&(MAX_GUEST_EVER_PTRS-1u);uintptr_t v=g_guest_ever_ptrs[i];
        if(v==p)return 1;
        if(v==0)return 0;
    }
    return 0;
}
static void guest_heap_capacity_health_locked(void) {
    const size_t cap=(size_t)MAX_GUEST_ALLOCS;
    unsigned level=0u;
    if(g_guest_alloc_active*100u>=cap*90u)level=3u;
    else if(g_guest_alloc_active*100u>=cap*75u)level=2u;
    else if(g_guest_alloc_active*100u>=cap*50u)level=1u;
    if(level>g_guest_heap_capacity_warn_level){
        const char *name=level==3u?"CRITICAL":(level==2u?"HIGH":"NOTICE");
        g_guest_heap_capacity_warn_level=level;
        logmsg("[BENDER_HEAP][CAPACITY] active=%zu/%u peak=%zu level=%s free_slots=%zu next_slot=%zu active_hash=%u\n",g_guest_alloc_active,(unsigned)MAX_GUEST_ALLOCS,g_guest_alloc_peak,name,g_guest_free_slot_count,g_guest_next_slot,(unsigned)MAX_GUEST_ACTIVE_HASH);
    }
}
static GuestAlloc *guest_alloc_slot(void) {
    if(g_guest_free_slot_count){size_t idx=(size_t)g_guest_free_slots[--g_guest_free_slot_count];if(idx<MAX_GUEST_ALLOCS)return &g_guest_allocs[idx];}
    if(g_guest_next_slot<MAX_GUEST_ALLOCS)return &g_guest_allocs[g_guest_next_slot++];
    for(size_t i=0;i<MAX_GUEST_ALLOCS;i++) if(!g_guest_allocs[i].active) return &g_guest_allocs[i];
    return NULL;
}
static void __attribute__((unused)) guest_heap_set_fast_mode(const char *where){
    if(g_guest_heap_fast_mode)return;
    g_guest_heap_fast_mode=1;
    logmsg("[BENDER_HEAP][FAST_MODE] where=%s backend=host-malloc+redzones lookup=active-hash guard_existing=preserved\n",where?where:"?");
    set_checkpoint("heap fast mode enabled where=%s active=%zu",where?where:"?",g_guest_alloc_active);
}
static int guest_check_one(GuestAlloc *r, const char *where) {
    if(!r||!r->active||r->magic!=GUEST_ALLOC_MAGIC) return 0;
    unsigned char *pre=r->user-GUEST_REDZONE, *post=r->user+r->usable_size;
    for(size_t i=0;i<GUEST_REDZONE;i++) {
        if(pre[i]!=0xA5) {
            ++g_guest_heap_corruptions;
            logmsg("[BENDER_HEAP][CORRUPTION] where=%s side=PRE user=%p requested=%zu usable=%zu align=%zu alloc_caller=%p module=%s offset=%zu got=%02x expected=a5\n",
                   where?where:"?",r->user,r->user_size,r->usable_size,r->alignment,(void*)r->caller,
                   guest_module_name_for_address((const void*)r->caller),i,(unsigned)pre[i]);
            set_checkpoint("heap PRE corruption user=%p requested=%zu usable=%zu caller=%p",r->user,r->user_size,r->usable_size,(void*)r->caller);
            raise(SIGABRT);
            return -1;
        }
        if(post[i]!=0x5A) {
            ++g_guest_heap_corruptions;
            logmsg("[BENDER_HEAP][CORRUPTION] where=%s side=POST user=%p requested=%zu usable=%zu align=%zu alloc_caller=%p module=%s offset=%zu got=%02x expected=5a\n",
                   where?where:"?",r->user,r->user_size,r->usable_size,r->alignment,(void*)r->caller,
                   guest_module_name_for_address((const void*)r->caller),i,(unsigned)post[i]);
            set_checkpoint("heap POST corruption user=%p requested=%zu usable=%zu caller=%p",r->user,r->user_size,r->usable_size,(void*)r->caller);
            raise(SIGABRT);
            return -1;
        }
    }
    if(r->usable_size>r->user_size && !r->padding_reported) {
        size_t changed=0, first=0; unsigned first_value=0;
        for(size_t i=r->user_size;i<r->usable_size;i++) {
            if(r->user[i]!=(unsigned char)GUEST_PADDING_MARK) {
                if(!changed){first=i-r->user_size;first_value=(unsigned)r->user[i];}
                changed++;
            }
        }
        if(changed) {
            r->padding_reported=1; ++g_guest_padding_writes;
            logmsg("[BENDER_HEAP][PADDING_WRITE] where=%s user=%p requested=%zu usable=%zu padding=%zu changed=%zu first_padding_offset=%zu first_value=%02x alloc_caller=%p module=%s action=allowed\n",
                   where?where:"?",r->user,r->user_size,r->usable_size,r->usable_size-r->user_size,changed,first,first_value,
                   (void*)r->caller,guest_module_name_for_address((const void*)r->caller));
        }
    }
    return 0;
}
static int guest_heap_validate_all(const char *where) {
    int rc=0;
    pthread_mutex_lock(&g_guest_heap_meta_lock);
    for(size_t i=0;i<MAX_GUEST_ALLOCS;i++){
        if(g_guest_allocs[i].active && guest_check_one(&g_guest_allocs[i],where)!=0){rc=-1;break;}
    }
    pthread_mutex_unlock(&g_guest_heap_meta_lock);
    return rc;
}
static void *bender_guest_alloc(size_t size, size_t alignment, uintptr_t caller, int zero) {
    if(size==0) size=1;
    alignment=guest_norm_align(alignment); if(!alignment){errno=EINVAL;return NULL;}
    size_t usable=guest_usable_size(size); if(!usable){errno=ENOMEM;return NULL;}
    size_t ps=(size_t)(g_pagesize>0?g_pagesize:4096);
    unsigned backend=g_guest_heap_fast_mode?1u:0u;
    unsigned char *base=NULL,*rw=NULL;size_t total=0,data=0;
    if(usable>SIZE_MAX-(alignment+2u*GUEST_REDZONE+ps)){errno=ENOMEM;return NULL;}
    size_t need=usable+alignment+2u*GUEST_REDZONE;
    if(backend==0u){
        data=(need+ps-1u)&~(ps-1u);
        if(data>SIZE_MAX-2u*ps){errno=ENOMEM;return NULL;}
        total=data+2u*ps;
        base=(unsigned char*)mmap(NULL,total,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(base==MAP_FAILED) return NULL;
        rw=base+ps;
        if(mprotect(rw,data,PROT_READ|PROT_WRITE)!=0){int e=errno;munmap(base,total);errno=e;return NULL;}
    }else{
        total=need;
        base=(unsigned char*)malloc(total);
        if(!base){errno=ENOMEM;return NULL;}
        rw=base;data=total;
    }
    uintptr_t up=align_up((uintptr_t)(rw+GUEST_REDZONE),alignment);
    unsigned char *user=(unsigned char*)up;
    if(user+usable+GUEST_REDZONE>rw+data){if(backend)free(base);else munmap(base,total);errno=ENOMEM;return NULL;}
    memset(user-GUEST_REDZONE,0xA5,GUEST_REDZONE);
    if(zero) memset(user,0,size);
    if(usable>size) memset(user+size,GUEST_PADDING_MARK,usable-size);
    memset(user+usable,0x5A,GUEST_REDZONE);

    pthread_mutex_lock(&g_guest_heap_meta_lock);
    GuestAlloc *r=guest_alloc_slot();
    if(!r){
        size_t active_now=g_guest_alloc_active;
        pthread_mutex_unlock(&g_guest_heap_meta_lock);
        if(backend)free(base);else munmap(base,total);errno=ENOMEM;
        logmsg("[BENDER_HEAP][ERROR] tabela cheia active=%zu capacity=%u active_hash=%u\n",active_now,(unsigned)MAX_GUEST_ALLOCS,(unsigned)MAX_GUEST_ACTIVE_HASH);
        return NULL;
    }
    memset(r,0,sizeof(*r));r->map_base=base;r->map_size=total;r->user=user;r->user_size=size;r->usable_size=usable;r->alignment=alignment;r->caller=caller;r->magic=GUEST_ALLOC_MAGIC;r->backend=backend;r->active=1;
    size_t slot=(size_t)(r-g_guest_allocs);
    if(guest_active_add(user,slot)!=0){r->active=0;r->magic=0;if(backend)free(base);else munmap(base,total);pthread_mutex_unlock(&g_guest_heap_meta_lock);errno=ENOMEM;logmsg("[BENDER_HEAP][ACTIVE_HASH_FULL] ptr=%p slot=%zu\n",user,slot);return NULL;}
    guest_ever_ptr_add(user);
    ++g_guest_alloc_total;++g_guest_alloc_active;if(backend)++g_guest_fast_alloc_total;else ++g_guest_guard_alloc_total;if(g_guest_alloc_active>g_guest_alloc_peak)g_guest_alloc_peak=g_guest_alloc_active;guest_heap_capacity_health_locked();
    size_t alloc_no=g_guest_alloc_total, active_now=g_guest_alloc_active;
    pthread_mutex_unlock(&g_guest_heap_meta_lock);

    if(alloc_no<=24u || (alloc_no%1000u)==0u) set_checkpoint_quiet("heap alloc n=%zu requested=%zu usable=%zu caller=%p module=%s active=%zu",alloc_no,size,usable,(void*)caller,guest_module_name_for_address((const void*)caller),active_now);
    if(g_guest_heap_trace_detail || alloc_no<=24 || (alloc_no%1000u)==0u)
        logmsg("[BENDER_HEAP] alloc n=%zu user=%p requested=%zu usable=%zu padding=%zu align=%zu backend=%s caller=%p module=%s active=%zu\n",alloc_no,user,size,usable,usable-size,alignment,backend?"fast":"guard",(void*)caller,guest_module_name_for_address((const void*)caller),active_now);
    return user;
}
static void *bender_malloc(size_t size){return bender_guest_alloc(size,16,(uintptr_t)__builtin_return_address(0),0);}
static void *bender_calloc(size_t n,size_t s){if(s&&n>SIZE_MAX/s){errno=ENOMEM;return NULL;}return bender_guest_alloc(n*s,16,(uintptr_t)__builtin_return_address(0),1);}
static size_t bender_malloc_usable_size(const void *ptr){
    size_t out=0;pthread_mutex_lock(&g_guest_heap_meta_lock);GuestAlloc*r=guest_alloc_find(ptr);if(r)out=r->usable_size;pthread_mutex_unlock(&g_guest_heap_meta_lock);return out;
}
static void bender_free(void *ptr){
    if(!ptr) return;
    void *caller=__builtin_return_address(0);
    pthread_mutex_lock(&g_guest_heap_meta_lock);
    GuestAlloc*r=guest_alloc_find(ptr);
    if(!r){
        GuestAlloc*owner=guest_alloc_find_containing(ptr);
        if(owner){
            ++g_guest_interior_free_ignored;
            void *owner_user=owner->user;size_t owner_req=owner->user_size,owner_usable=owner->usable_size;
            pthread_mutex_unlock(&g_guest_heap_meta_lock);
            logmsg("[BENDER_HEAP][INTERIOR_FREE] ptr=%p owner=%p requested=%zu usable=%zu caller=%p module=%s action=ignored\n",ptr,owner_user,owner_req,owner_usable,caller,guest_module_name_for_address(caller));
            return;
        }
        if(guest_freed_tombstone_has(ptr)){
            ++g_guest_stale_free_ignored;pthread_mutex_unlock(&g_guest_heap_meta_lock);
            logmsg("[BENDER_HEAP][STALE_GUEST_FREE] ptr=%p caller=%p module=%s action=ignored reason=already-freed-by-heap-guard\n",ptr,caller,guest_module_name_for_address(caller));
            return;
        }
        if(guest_ever_ptr_has(ptr)){
            ++g_guest_orphan_free_ignored;pthread_mutex_unlock(&g_guest_heap_meta_lock);
            logmsg("[BENDER_HEAP][ORPHAN_GUEST_FREE] ptr=%p caller=%p module=%s action=ignored reason=guest-provenance-without-active-record\n",ptr,caller,guest_module_name_for_address(caller));
            return;
        }
        ++g_guest_foreign_free_forwarded;pthread_mutex_unlock(&g_guest_heap_meta_lock);
        logmsg("[BENDER_HEAP][FOREIGN_FREE] ptr=%p caller=%p module=%s -> host free\n",ptr,caller,guest_module_name_for_address(caller));free(ptr);return;
    }
    guest_check_one(r,"free");guest_freed_tombstone_add(ptr);void*base=r->map_base;size_t total=r->map_size;unsigned backend=r->backend;size_t slot=(size_t)(r-g_guest_allocs);guest_active_remove(ptr);r->active=0;r->magic=0;if(g_guest_free_slot_count<MAX_GUEST_ALLOCS)g_guest_free_slots[g_guest_free_slot_count++]=(uint32_t)slot;++g_guest_free_total;if(g_guest_alloc_active)--g_guest_alloc_active;
    pthread_mutex_unlock(&g_guest_heap_meta_lock);
    if(backend)free(base);else munmap(base,total);
}
static void *bender_realloc(void *ptr,size_t size){
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if(!ptr)return bender_guest_alloc(size,16,caller,0);
    if(size==0){bender_free(ptr);return NULL;}
    pthread_mutex_lock(&g_guest_heap_meta_lock);
    GuestAlloc*r=guest_alloc_find(ptr);
    if(!r){
        const char *why=guest_freed_tombstone_has(ptr)?"stale-guest":(guest_alloc_find_containing(ptr)?"interior-guest":(guest_ever_ptr_has(ptr)?"orphan-guest":"foreign"));
        pthread_mutex_unlock(&g_guest_heap_meta_lock);
        logmsg("[BENDER_HEAP][FOREIGN_REALLOC] ptr=%p size=%zu reason=%s blocked\n",ptr,size,why);errno=EINVAL;return NULL;
    }
    guest_check_one(r,"realloc");size_t old=r->user_size;size_t al=r->alignment;
    pthread_mutex_unlock(&g_guest_heap_meta_lock);
    void*n=bender_guest_alloc(size,al,caller,0);if(!n)return NULL;memcpy(n,ptr,old<size?old:size);bender_free(ptr);return n;
}
static void *bender_memalign(size_t alignment,size_t size){return bender_guest_alloc(size,alignment,(uintptr_t)__builtin_return_address(0),0);}
static int bender_posix_memalign(void **out,size_t alignment,size_t size){if(!out)return EINVAL;size_t a=guest_norm_align(alignment);if(!a||a!=alignment||alignment%sizeof(void*)){return EINVAL;}void*p=bender_guest_alloc(size,alignment,(uintptr_t)__builtin_return_address(0),0);if(!p)return errno==EINVAL?EINVAL:ENOMEM;*out=p;return 0;}
static char *bender_strdup(const char *s){if(!s)return NULL;size_t n=strlen(s)+1;char*p=(char*)bender_guest_alloc(n,16,(uintptr_t)__builtin_return_address(0),0);if(p)memcpy(p,s,n);return p;}
static int bender_vasprintf(char **out,const char *fmt,va_list ap){if(!out||!fmt){errno=EINVAL;return -1;}va_list a,b;va_copy(a,ap);int n=vsnprintf(NULL,0,fmt,a);va_end(a);if(n<0)return -1;char*p=(char*)bender_guest_alloc((size_t)n+1u,16,(uintptr_t)__builtin_return_address(0),0);if(!p)return -1;va_copy(b,ap);int rc=vsnprintf(p,(size_t)n+1u,fmt,b);va_end(b);if(rc<0){bender_free(p);return -1;}*out=p;return rc;}
static void guest_heap_summary(const char *where){
    guest_heap_validate_all(where);
    pthread_mutex_lock(&g_guest_heap_meta_lock);
    size_t a=g_guest_alloc_total,f=g_guest_free_total,act=g_guest_alloc_active,peak=g_guest_alloc_peak,pw=g_guest_padding_writes,hc=g_guest_heap_corruptions,sf=g_guest_stale_free_ignored,inf=g_guest_interior_free_ignored,orf=g_guest_orphan_free_ignored,ff=g_guest_foreign_free_forwarded,ts=g_guest_freed_tombstone_count,ep=g_guest_ever_ptr_count,ga=g_guest_guard_alloc_total,fa=g_guest_fast_alloc_total;
    int fm=g_guest_heap_fast_mode;
    pthread_mutex_unlock(&g_guest_heap_meta_lock);
    logmsg("[BENDER_HEAP][SUMMARY] where=%s alloc=%zu free=%zu active=%zu peak=%zu capacity=%u active_hash=%u guard_alloc=%zu fast_alloc=%zu fast_mode=%d padding_writes=%zu hard_corruptions=%zu stale_free_ignored=%zu interior_free_ignored=%zu orphan_free_ignored=%zu foreign_free_forwarded=%zu tombstones=%zu ever_ptrs=%zu\n",where?where:"?",a,f,act,peak,(unsigned)MAX_GUEST_ALLOCS,(unsigned)MAX_GUEST_ACTIVE_HASH,ga,fa,fm,pw,hc,sf,inf,orf,ff,ts,ep);
}
static int bender___cxa_atexit(void (*fn)(void *), void *arg, void *dso) {
    size_t idx=g_guest_atexit_count;
    if(idx<MAX_GUEST_ATEXIT) {
        g_guest_atexit[idx].fn=fn; g_guest_atexit[idx].arg=arg; g_guest_atexit[idx].dso=dso;
        g_guest_atexit_count++;
        if(idx<24 || ((idx+1u)%100u)==0u)
            logmsg("[BENDER_CXA] register index=%zu module=%s fn=%p arg=%p dso=%p\n",idx,guest_module_name_for_address((const void*)fn),(void*)fn,arg,dso);
        return 0;
    }
    g_guest_atexit_dropped++;
    logmsg("[BENDER_CXA][WARN] queue cheia; destructor ignorado fn=%p dso=%p\n",(void*)fn,dso);
    return 0;
}
static void bender___cxa_finalize(void *dso) {
    g_guest_finalize_calls++;
    logmsg("[BENDER_CXA] __cxa_finalize interceptado dso=%p; destructors guest mantidos sob controle do probe\n",dso);
}


/* ---------------- BENDER SDL2/KMSDRM EGL SHIM 2.7.9 ----------------
 * V2.7.6 proved that eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR,...)
 * can block indefinitely on this RK3326/DarkOS stack after gbm_create_device().
 * Do not call GBM platform-display from Unity's main thread anymore.
 *
 * V2.7.9 keeps SDL2 as the native video owner, but now prefers a real GLES3.x
 * context (falling back to GLES2) and mirrors the actual context capability into
 * EGL_RENDERABLE_TYPE/EGL_CONFORMANT instead of hard-coding ES2. It also keeps
 * the host SDL context private until guest eglCreateContext/eglMakeCurrent establish
 * guest-visible current-context state.
 * Android-facing EGL remains opaque cookies backed by that SDL window.
 * This mirrors the proven design used by native Android-game PortMaster ports:
 * Android ANativeWindow never reaches the host EGL implementation directly.
 *
 * A bounded GPU watchdog exits the probe with RC=124 if SDL/EGL bring-up ever
 * blocks, so a bad backend cannot leave the handheld on a permanent black screen.
 */
typedef void *BenderEGLDisplay;
typedef void *BenderEGLConfig;
typedef void *BenderEGLContext;
typedef void *BenderEGLSurface;
typedef void *BenderEGLClientBuffer;
typedef void *BenderEGLNativeDisplayType;
typedef void *BenderEGLNativeWindowType;
typedef int BenderEGLint;
typedef unsigned int BenderEGLBoolean;
typedef unsigned int BenderEGLenum;
#define BENDER_EGL_FALSE 0u
#define BENDER_EGL_TRUE 1u
#define BENDER_EGL_SUCCESS 0x3000
#define BENDER_EGL_NOT_INITIALIZED 0x3001
#define BENDER_EGL_BAD_ACCESS 0x3002
#define BENDER_EGL_BAD_ALLOC 0x3003
#define BENDER_EGL_BAD_ATTRIBUTE 0x3004
#define BENDER_EGL_BAD_CONFIG 0x3005
#define BENDER_EGL_BAD_CONTEXT 0x3006
#define BENDER_EGL_BAD_CURRENT_SURFACE 0x3007
#define BENDER_EGL_BAD_DISPLAY 0x3008
#define BENDER_EGL_BAD_MATCH 0x3009
#define BENDER_EGL_BAD_NATIVE_PIXMAP 0x300A
#define BENDER_EGL_BAD_NATIVE_WINDOW 0x300B
#define BENDER_EGL_BAD_PARAMETER 0x300C
#define BENDER_EGL_BAD_SURFACE 0x300D
#define BENDER_EGL_NONE 0x3038
#define BENDER_EGL_BUFFER_SIZE 0x3020
#define BENDER_EGL_ALPHA_SIZE 0x3021
#define BENDER_EGL_BLUE_SIZE 0x3022
#define BENDER_EGL_GREEN_SIZE 0x3023
#define BENDER_EGL_RED_SIZE 0x3024
#define BENDER_EGL_DEPTH_SIZE 0x3025
#define BENDER_EGL_STENCIL_SIZE 0x3026
#define BENDER_EGL_CONFIG_ID 0x3028
#define BENDER_EGL_LEVEL 0x3029
#define BENDER_EGL_NATIVE_VISUAL_ID 0x302E
#define BENDER_EGL_SAMPLES 0x3031
#define BENDER_EGL_SAMPLE_BUFFERS 0x3032
#define BENDER_EGL_SURFACE_TYPE 0x3033
#define BENDER_EGL_VENDOR 0x3053
#define BENDER_EGL_VERSION 0x3054
#define BENDER_EGL_EXTENSIONS 0x3055
#define BENDER_EGL_HEIGHT 0x3056
#define BENDER_EGL_WIDTH 0x3057
#define BENDER_EGL_DRAW 0x3059
#define BENDER_EGL_READ 0x305A
#define BENDER_EGL_RENDERABLE_TYPE 0x3040
#define BENDER_EGL_CONFORMANT 0x3042
#define BENDER_EGL_CLIENT_APIS 0x308D
#define BENDER_EGL_RGB_BUFFER 0x308E
#define BENDER_EGL_OPENGL_ES_API 0x30A0
#define BENDER_EGL_WINDOW_BIT 0x0004
#define BENDER_EGL_PBUFFER_BIT 0x0001
#define BENDER_EGL_OPENGL_ES2_BIT 0x0004
#define BENDER_EGL_OPENGL_ES3_BIT_KHR 0x0040
#define BENDER_EGL_COLOR_BUFFER_TYPE 0x303F
#define BENDER_EGL_CONFIG_CAVEAT 0x3027
#define BENDER_EGL_NATIVE_RENDERABLE 0x302D
#define BENDER_EGL_TRANSPARENT_TYPE 0x3034
#define BENDER_EGL_BIND_TO_TEXTURE_RGB 0x3039
#define BENDER_EGL_BIND_TO_TEXTURE_RGBA 0x303A
#define BENDER_EGL_MIN_SWAP_INTERVAL 0x303B
#define BENDER_EGL_MAX_SWAP_INTERVAL 0x303C
#define BENDER_EGL_MAX_PBUFFER_HEIGHT 0x302A
#define BENDER_EGL_MAX_PBUFFER_PIXELS 0x302B
#define BENDER_EGL_MAX_PBUFFER_WIDTH 0x302C
#define BENDER_EGL_CONTEXT_CLIENT_VERSION 0x3098
#define BENDER_EGL_RECORDABLE_ANDROID 0x3142

#define BENDER_EGL_DISPLAY_COOKIE ((BenderEGLDisplay)(uintptr_t)0xB277D1591u)
#define BENDER_EGL_CONFIG_COOKIE  ((BenderEGLConfig )(uintptr_t)0xB277CF6C0u)
#define BENDER_EGL_SURFACE_COOKIE ((BenderEGLSurface)(uintptr_t)0xB27757FACu)
#define BENDER_EGL_CONTEXT_COOKIE ((BenderEGLContext)(uintptr_t)0xB277C07E1u)

/* SDL2 ABI constants used without SDL headers. */
#define BENDER_SDL_INIT_VIDEO 0x00000020u
#define BENDER_SDL_INIT_JOYSTICK 0x00000200u
#define BENDER_SDL_INIT_GAMECONTROLLER 0x00002000u
#define BENDER_SDL_INIT_EVENTS 0x00004000u
#define BENDER_SDL_CONTROLLER_AXIS_LEFTX 0
#define BENDER_SDL_CONTROLLER_AXIS_LEFTY 1
#define BENDER_SDL_CONTROLLER_AXIS_RIGHTX 2
#define BENDER_SDL_CONTROLLER_AXIS_RIGHTY 3
#define BENDER_SDL_CONTROLLER_AXIS_TRIGGERLEFT 4
#define BENDER_SDL_CONTROLLER_AXIS_TRIGGERRIGHT 5
#define BENDER_SDL_CONTROLLER_BUTTON_A 0
#define BENDER_SDL_CONTROLLER_BUTTON_B 1
#define BENDER_SDL_CONTROLLER_BUTTON_X 2
#define BENDER_SDL_CONTROLLER_BUTTON_Y 3
#define BENDER_SDL_CONTROLLER_BUTTON_BACK 4
#define BENDER_SDL_CONTROLLER_BUTTON_GUIDE 5
#define BENDER_SDL_CONTROLLER_BUTTON_START 6
#define BENDER_SDL_CONTROLLER_BUTTON_LEFTSTICK 7
#define BENDER_SDL_CONTROLLER_BUTTON_RIGHTSTICK 8
#define BENDER_SDL_CONTROLLER_BUTTON_LEFTSHOULDER 9
#define BENDER_SDL_CONTROLLER_BUTTON_RIGHTSHOULDER 10
#define BENDER_SDL_CONTROLLER_BUTTON_DPAD_UP 11
#define BENDER_SDL_CONTROLLER_BUTTON_DPAD_DOWN 12
#define BENDER_SDL_CONTROLLER_BUTTON_DPAD_LEFT 13
#define BENDER_SDL_CONTROLLER_BUTTON_DPAD_RIGHT 14
#define BENDER_SDL_WINDOW_FULLSCREEN_DESKTOP 0x00001001u
#define BENDER_SDL_WINDOW_OPENGL 0x00000002u
#define BENDER_SDL_WINDOW_SHOWN 0x00000004u
#define BENDER_SDL_WINDOWPOS_CENTERED_MASK 0x2FFF0000u
#define BENDER_SDL_GL_RED_SIZE 0
#define BENDER_SDL_GL_GREEN_SIZE 1
#define BENDER_SDL_GL_BLUE_SIZE 2
#define BENDER_SDL_GL_ALPHA_SIZE 3
#define BENDER_SDL_GL_BUFFER_SIZE 4
#define BENDER_SDL_GL_DOUBLEBUFFER 5
#define BENDER_SDL_GL_DEPTH_SIZE 6
#define BENDER_SDL_GL_STENCIL_SIZE 7
#define BENDER_SDL_GL_MULTISAMPLEBUFFERS 13
#define BENDER_SDL_GL_MULTISAMPLESAMPLES 14
#define BENDER_SDL_GL_CONTEXT_MAJOR_VERSION 17
#define BENDER_SDL_GL_CONTEXT_MINOR_VERSION 18
#define BENDER_SDL_GL_CONTEXT_PROFILE_MASK 21
#define BENDER_SDL_GL_CONTEXT_PROFILE_ES 0x0004

typedef int (*BenderSDLInitSubSystemFn)(unsigned int);
typedef void (*BenderSDLQuitSubSystemFn)(unsigned int);
typedef const char *(*BenderSDLGetErrorFn)(void);
typedef const char *(*BenderSDLGetCurrentVideoDriverFn)(void);
typedef int (*BenderSDLGetNumVideoDriversFn)(void);
typedef const char *(*BenderSDLGetVideoDriverFn)(int);
typedef int (*BenderSDLSetHintFn)(const char*,const char*);
typedef int (*BenderSDLGLSetAttributeFn)(int,int);
typedef int (*BenderSDLGLGetAttributeFn)(int,int*);
typedef void *(*BenderSDLCreateWindowFn)(const char*,int,int,int,int,unsigned int);
typedef void (*BenderSDLDestroyWindowFn)(void*);
typedef void *(*BenderSDLGLCreateContextFn)(void*);
typedef void (*BenderSDLGLDeleteContextFn)(void*);
typedef int (*BenderSDLGLMakeCurrentFn)(void*,void*);
typedef void (*BenderSDLGLSwapWindowFn)(void*);
typedef void (*BenderSDLGLGetDrawableSizeFn)(void*,int*,int*);
typedef int (*BenderSDLGLSetSwapIntervalFn)(int);
typedef void *(*BenderSDLGLGetProcAddressFn)(const char*);
typedef int (*BenderSDLNumJoysticksFn)(void);
typedef int (*BenderSDLIsGameControllerFn)(int);
typedef void *(*BenderSDLGameControllerOpenFn)(int);
typedef const char *(*BenderSDLGameControllerNameFn)(void*);
typedef unsigned char (*BenderSDLGameControllerGetButtonFn)(void*,int);
typedef int16_t (*BenderSDLGameControllerGetAxisFn)(void*,int);
typedef void (*BenderSDLGameControllerUpdateFn)(void);
typedef void *(*BenderSDLJoystickOpenFn)(int);
typedef const char *(*BenderSDLJoystickNameFn)(void*);
typedef unsigned char (*BenderSDLJoystickGetButtonFn)(void*,int);
typedef int16_t (*BenderSDLJoystickGetAxisFn)(void*,int);
typedef int (*BenderSDLJoystickNumButtonsFn)(void*);
typedef void (*BenderSDLJoystickUpdateFn)(void);

static void *g_bender_sdl_host;
static const char *g_bender_sdl_host_request;
static BenderSDLInitSubSystemFn g_sdl_InitSubSystem;
static BenderSDLGetErrorFn g_sdl_GetError;
static BenderSDLGetCurrentVideoDriverFn g_sdl_GetCurrentVideoDriver;
static BenderSDLGetNumVideoDriversFn g_sdl_GetNumVideoDrivers;
static BenderSDLGetVideoDriverFn g_sdl_GetVideoDriver;
static BenderSDLSetHintFn g_sdl_SetHint;
static BenderSDLGLSetAttributeFn g_sdl_GL_SetAttribute;
static BenderSDLGLGetAttributeFn g_sdl_GL_GetAttribute;
static BenderSDLCreateWindowFn g_sdl_CreateWindow;
static BenderSDLGLCreateContextFn g_sdl_GL_CreateContext;
static BenderSDLGLMakeCurrentFn g_sdl_GL_MakeCurrent;
static BenderSDLGLSwapWindowFn g_sdl_GL_SwapWindow;
static BenderSDLGLGetDrawableSizeFn g_sdl_GL_GetDrawableSize;
static BenderSDLGLSetSwapIntervalFn g_sdl_GL_SetSwapInterval;
static BenderSDLGLGetProcAddressFn g_sdl_GL_GetProcAddress;
static BenderSDLNumJoysticksFn g_sdl_NumJoysticks;
static BenderSDLIsGameControllerFn g_sdl_IsGameController;
static BenderSDLGameControllerOpenFn g_sdl_GameControllerOpen;
static BenderSDLGameControllerNameFn g_sdl_GameControllerName;
static BenderSDLGameControllerGetButtonFn g_sdl_GameControllerGetButton;
static BenderSDLGameControllerGetAxisFn g_sdl_GameControllerGetAxis;
static BenderSDLGameControllerUpdateFn g_sdl_GameControllerUpdate;
static BenderSDLJoystickOpenFn g_sdl_JoystickOpen;
static BenderSDLJoystickNameFn g_sdl_JoystickName;
static BenderSDLJoystickGetButtonFn g_sdl_JoystickGetButton;
static BenderSDLJoystickGetAxisFn g_sdl_JoystickGetAxis;
static BenderSDLJoystickNumButtonsFn g_sdl_JoystickNumButtons;
static BenderSDLJoystickUpdateFn g_sdl_JoystickUpdate;
static void *g_bender_sdl_window;
static void *g_bender_sdl_gl;
static int g_bender_sdl_ready;
static int g_bender_sdl_draw_w=640;
static int g_bender_sdl_draw_h=480;
static int g_bender_sdl_gl_major=2;
static int g_bender_sdl_gl_minor=0;
static BenderEGLint g_bender_egl_renderable_bits=BENDER_EGL_OPENGL_ES2_BIT;
static int g_bender_egl_current; /* guest-visible current state; host SDL context may already be current */
static BenderEGLint g_bender_egl_error=BENDER_EGL_SUCCESS;
static unsigned long g_bender_egl_calls;
static unsigned long g_bender_egl_choose_calls;
static unsigned long g_bender_egl_swaps;
static volatile int g_bender_gpu_watchdog_state;
static volatile int g_bender_gpu_watchdog_started;
static char g_bender_gpu_watchdog_stage[128]="idle";
static volatile int g_bender_render_watchdog_state;
static volatile unsigned long g_bender_render_watchdog_generation;

/* Raw EGL is retained only as a fast diagnostic fallback. No in-process GBM. */
static void *g_bender_egl_host;
static const char *g_bender_egl_host_request;
static BenderEGLDisplay g_bender_raw_display;
static int g_bender_raw_initialized;
static BenderEGLint g_bender_raw_major=-1;
static BenderEGLint g_bender_raw_minor=-1;

static void bender_gpu_stage(const char *stage){
    if(!stage)stage="unknown";
    snprintf(g_bender_gpu_watchdog_stage,sizeof(g_bender_gpu_watchdog_stage),"%s",stage);
    set_checkpoint_quiet("gpu-stage %s",stage);
}
static unsigned bender_gpu_watchdog_seconds(void){
    const char *e=getenv("BENDER_GPU_WATCHDOG_SEC");
    long v=e?strtol(e,NULL,10):15;
    if(v<5)v=5;
    if(v>60)v=60;
    return (unsigned)v;
}
static void *bender_gpu_watchdog_thread(void *arg){
    (void)arg;unsigned sec=bender_gpu_watchdog_seconds();
    for(unsigned i=0;i<sec;i++){
        sleep(1);
        if(g_bender_gpu_watchdog_state==2||g_bender_gpu_watchdog_state==0)return NULL;
    }
    if(g_bender_gpu_watchdog_state==1){
        logmsg("[BENDER_GPU_WATCHDOG][TIMEOUT] stage=%s timeout=%us action=_exit(124) reason=gpu-bringup-blocked\n",g_bender_gpu_watchdog_stage,sec);
        fflush(NULL);_exit(124);
    }
    return NULL;
}
static void bender_gpu_watchdog_start(void){
    if(g_bender_gpu_watchdog_started)return;
    g_bender_gpu_watchdog_started=1;g_bender_gpu_watchdog_state=1;
    pthread_t t;int rc=pthread_create(&t,NULL,bender_gpu_watchdog_thread,NULL);
    if(rc==0){pthread_detach(t);logmsg("[BENDER_GPU_WATCHDOG][START] timeout=%us\n",bender_gpu_watchdog_seconds());}
    else{g_bender_gpu_watchdog_started=0;logmsg("[BENDER_GPU_WATCHDOG][WARN] pthread_create rc=%d; watchdog indisponivel\n",rc);}
}
static void bender_gpu_watchdog_done(const char *stage){
    bender_gpu_stage(stage);g_bender_gpu_watchdog_state=2;
    logmsg("[BENDER_GPU_WATCHDOG][DONE] stage=%s\n",stage?stage:"unknown");
}
static void bender_run_stop_signal(int sig){
    g_bender_exit_signal=(sig_atomic_t)sig;
    if(sig==SIGTERM)g_bender_exit_reason_code=g_bender_exit_combo_recent?6:1;
    else if(sig==SIGINT)g_bender_exit_reason_code=2;
    g_bender_run_stop_requested=1;
}
static void bender_install_run_stop_handlers(void){
    struct sigaction sa;memset(&sa,0,sizeof(sa));sa.sa_handler=bender_run_stop_signal;sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGTERM,&sa,NULL);(void)sigaction(SIGINT,&sa,NULL);
}

static unsigned bender_render_idle_watchdog_seconds(void){
    const char *e=getenv("BENDER_RENDER_IDLE_WATCHDOG_SEC");
    long v=e?strtol(e,NULL,10):20;
    if(v<10)v=10;
    if(v>120)v=120;
    return (unsigned)v;
}
static unsigned bender_render_hard_watchdog_seconds(void){
    const char *e=getenv("BENDER_RENDER_HARD_WATCHDOG_SEC");
    long v=e?strtol(e,NULL,10):180;
    if(v<30)v=30;
    if(v>900)v=900;
    return (unsigned)v;
}
static unsigned bender_render_absolute_watchdog_seconds(void){
    const char *e=getenv("BENDER_RENDER_ABSOLUTE_WATCHDOG_SEC");
    long v=e?strtol(e,NULL,10):420;
    if(v<120)v=120;
    if(v>1200)v=1200;
    return (unsigned)v;
}
static unsigned bender_render_extend_seconds(void){
    const char *e=getenv("BENDER_RENDER_EXTEND_SEC");
    long v=e?strtol(e,NULL,10):60;
    if(v<30)v=30;
    if(v>180)v=180;
    return (unsigned)v;
}
static void *bender_render_watchdog_thread(void *arg){
    unsigned long gen=(unsigned long)(uintptr_t)arg;
    unsigned idle_limit=bender_render_idle_watchdog_seconds();
    unsigned hard_limit=bender_render_hard_watchdog_seconds();
    unsigned absolute_limit=bender_render_absolute_watchdog_seconds();
    unsigned extend_step=bender_render_extend_seconds();
    if(absolute_limit<hard_limit)absolute_limit=hard_limit;
    unsigned idle=0,elapsed=0,last_progress_elapsed=0;
    int idle_reported=0;
    unsigned long last=__atomic_load_n(&g_bender_progress_serial,__ATOMIC_RELAXED);
    while(g_bender_render_watchdog_state&&g_bender_render_watchdog_generation==gen){
        sleep(1);++elapsed;
        if(!g_bender_render_watchdog_state||g_bender_render_watchdog_generation!=gen)return NULL;
        unsigned long now=__atomic_load_n(&g_bender_progress_serial,__ATOMIC_RELAXED);
        if(now!=last){
            last=now;idle=0;last_progress_elapsed=elapsed;idle_reported=0;
            if((elapsed%10u)==0u)logmsg("[BENDER_RENDER_WATCHDOG][PROGRESS] elapsed=%us soft_hard=%us absolute=%us serial=%lu checkpoint=%s\n",elapsed,hard_limit,absolute_limit,now,g_bender_render_checkpoint_len?g_bender_render_checkpoint:"(none)");
        }else{
            ++idle;
        }
        if(idle>=idle_limit){
            if(g_bender_continuous_run_mode){
                if(!idle_reported){
                    bender_render_request_stall_snapshot();
                    logmsg("[BENDER_RENDER_WATCHDOG][IDLE_STALL] idle=%us elapsed=%us serial=%lu checkpoint=%s action=observe-only-continuous-run\n",idle,elapsed,now,g_bender_render_checkpoint_len?g_bender_render_checkpoint:"(none)");
                    idle_reported=1;
                }
            }else{
                bender_render_request_stall_snapshot();
                logmsg("[BENDER_RENDER_WATCHDOG][IDLE_TIMEOUT] idle=%us elapsed=%us serial=%lu checkpoint=%s action=_exit(125)\n",idle,elapsed,now,g_bender_render_checkpoint_len?g_bender_render_checkpoint:"(none)");
                fflush(NULL);_exit(125);
            }
        }
        if(!g_bender_continuous_run_mode && elapsed>=absolute_limit){
            bender_render_request_stall_snapshot();
            logmsg("[BENDER_RENDER_WATCHDOG][ABSOLUTE_TIMEOUT] elapsed=%us idle=%us serial=%lu checkpoint=%s action=_exit(127)\n",elapsed,idle,now,g_bender_render_checkpoint_len?g_bender_render_checkpoint:"(none)");
            fflush(NULL);_exit(127);
        }
        if(!g_bender_continuous_run_mode && elapsed>=hard_limit){
            unsigned since_progress=elapsed-last_progress_elapsed;
            if(since_progress<=5u&&hard_limit<absolute_limit){
                unsigned old_limit=hard_limit;
                unsigned next=hard_limit+extend_step;
                if(next>absolute_limit)next=absolute_limit;
                hard_limit=next;
                logmsg("[BENDER_RENDER_WATCHDOG][EXTEND] elapsed=%us old_soft_hard=%us new_soft_hard=%us absolute=%us since_progress=%us serial=%lu checkpoint=%s\n",elapsed,old_limit,hard_limit,absolute_limit,since_progress,now,g_bender_render_checkpoint_len?g_bender_render_checkpoint:"(none)");
            }else{
                bender_render_request_stall_snapshot();
                logmsg("[BENDER_RENDER_WATCHDOG][HARD_TIMEOUT] elapsed=%us idle=%us since_progress=%us serial=%lu checkpoint=%s action=_exit(126)\n",elapsed,idle,since_progress,now,g_bender_render_checkpoint_len?g_bender_render_checkpoint:"(none)");
                fflush(NULL);_exit(126);
            }
        }
    }
    return NULL;
}
static __attribute__((unused)) void bender_render_watchdog_start(void){
    __atomic_store_n(&g_bender_render_tid,(pid_t)syscall(SYS_gettid),__ATOMIC_RELAXED);
    unsigned long gen=++g_bender_render_watchdog_generation;g_bender_render_watchdog_state=1;
    __atomic_add_fetch(&g_bender_progress_serial,1ul,__ATOMIC_RELAXED);
    pthread_t t;int rc=pthread_create(&t,NULL,bender_render_watchdog_thread,(void*)(uintptr_t)gen);
    if(rc==0){pthread_detach(t);logmsg("[BENDER_RENDER_WATCHDOG][START] mode=%s idle_timeout=%us soft_hard=%us extend=%us absolute=%us generation=%lu\n",g_bender_continuous_run_mode?"continuous-idle-only":"diagnostic",bender_render_idle_watchdog_seconds(),bender_render_hard_watchdog_seconds(),bender_render_extend_seconds(),bender_render_absolute_watchdog_seconds(),gen);}
    else{g_bender_render_watchdog_state=0;logmsg("[BENDER_RENDER_WATCHDOG][WARN] pthread_create rc=%d\n",rc);}
}
static __attribute__((unused)) void bender_render_watchdog_done(const char *reason){
    g_bender_render_watchdog_state=0;++g_bender_render_watchdog_generation;
    logmsg("[BENDER_RENDER_WATCHDOG][DONE] reason=%s\n",reason?reason:"unknown");
}

/* V2.7.24: finite post-bootstrap frame loop. The first nativeRender may spend minutes
 * initializing IL2CPP. Once it returns cleanly, run a bounded number of additional
 * frames and present the SDL backbuffer when Android Unity does not call eglSwapBuffers. */
static unsigned long g_bender_host_presents;
static __attribute__((unused)) unsigned bender_frame_loop_frames(void){
    const char *e=getenv("BENDER_FRAME_LOOP_FRAMES");
    long v=e?strtol(e,NULL,10):120;
    if(v<1)v=1;
    if(v>600)v=600;
    return (unsigned)v;
}
static __attribute__((unused)) unsigned bender_frame_loop_delay_us(void){
    const char *e=getenv("BENDER_FRAME_LOOP_DELAY_US");
    long v=e?strtol(e,NULL,10):16666;
    if(v<0)v=0;
    if(v>100000)v=100000;
    return (unsigned)v;
}

/* BENDER FIX61 - optional measured frame remainder pacing.
 * VSync/swap interval is telemetry only: eglSwapInterval success does not prove
 * that eglSwapBuffers blocks for one refresh on every R36S/KMSDRM stack. */
static unsigned long g_bender_fix61_frames_2861;
static unsigned long g_bender_fix61_sleep_frames_2861;
static unsigned long long g_bender_fix61_sleep_total_us_2861;
static unsigned long g_bender_fix61_over_budget_2861;
/* BENDER FIX69 - runtime-selectable Unity frame pacing. */
static int g_bender_fix69_frame_mode_2869=-1;
static __attribute__((unused)) int bender_fix69_frame_mode_2869(void){
    if(g_bender_fix69_frame_mode_2869>=0)return g_bender_fix69_frame_mode_2869;
    const char *m=getenv("BENDER_FRAME_PACING_MODE");
    if(m&&(!strcasecmp(m,"auto60")||!strcasecmp(m,"auto")||!strcasecmp(m,"60")||!strcasecmp(m,"60fps")))g_bender_fix69_frame_mode_2869=1;
    else if(m&&(!strcasecmp(m,"uncapped")||!strcasecmp(m,"unlimited")||!strcasecmp(m,"none")||!strcasecmp(m,"off")))g_bender_fix69_frame_mode_2869=2;
    else g_bender_fix69_frame_mode_2869=0;
    return g_bender_fix69_frame_mode_2869;
}
static __attribute__((unused)) const char *bender_fix69_frame_mode_name_2869(void){
    int m=bender_fix69_frame_mode_2869();
    return m==1?"auto60":(m==2?"uncapped":"compat");
}
static __attribute__((unused)) uint64_t bender_fix61_now_us_2861(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return 0;
    return (uint64_t)ts.tv_sec*1000000ull+(uint64_t)ts.tv_nsec/1000ull;
}
static __attribute__((unused)) unsigned bender_fix61_target_us_2861(void){
    const char *e=getenv("BENDER_FRAME_TARGET_US");
    long v=e?strtol(e,NULL,10):16666;
    if(v<1000)v=1000;
    if(v>100000)v=100000;
    return (unsigned)v;
}
static __attribute__((unused)) void bender_fix61_pace_2861(unsigned frame,uint64_t start_us){
    const unsigned target=bender_fix61_target_us_2861();
    const uint64_t now=bender_fix61_now_us_2861();
    const uint64_t elapsed=(now>=start_us&&start_us)?(now-start_us):0ull;
    unsigned sleep_us=0u;
    if(elapsed<(uint64_t)target)sleep_us=(unsigned)((uint64_t)target-elapsed);
    else ++g_bender_fix61_over_budget_2861;
    if(sleep_us){
        usleep(sleep_us);
        ++g_bender_fix61_sleep_frames_2861;
        g_bender_fix61_sleep_total_us_2861+=(unsigned long long)sleep_us;
    }
    ++g_bender_fix61_frames_2861;
    if(frame<=10u||frame%300u==0u){
        const uint64_t end_us=bender_fix61_now_us_2861();
        uint64_t total=(end_us>=start_us&&start_us)?(end_us-start_us):(elapsed+(uint64_t)sleep_us);
        double fps=total?1000000.0/(double)total:0.0;
        logmsg("[BENDER_FRAME_PACING][FRAME] source=fix61 mode=auto60 frame=%u elapsed_us=%llu target_us=%u sleep_us=%u total_us=%llu fps=%.2f policy=measured-remainder-vsync-telemetry-only code_patch=0 forced_completion=0\n",frame,(unsigned long long)elapsed,target,sleep_us,(unsigned long long)total,fps);
    }
    if(frame%600u==0u){
        unsigned long long avg_sleep=g_bender_fix61_sleep_frames_2861?(g_bender_fix61_sleep_total_us_2861/(unsigned long long)g_bender_fix61_sleep_frames_2861):0ull;
        logmsg("[BENDER_FRAME_PACING][SUMMARY] source=fix61 mode=auto60 frames=%lu sleep_frames=%lu sleep_total_us=%llu avg_sleep_us=%llu over_budget=%lu target_us=%u code_patch=0 forced_completion=0\n",g_bender_fix61_frames_2861,g_bender_fix61_sleep_frames_2861,g_bender_fix61_sleep_total_us_2861,avg_sleep,g_bender_fix61_over_budget_2861,target);
    }
}
/* [BENDER_FRAME_PACING][RUNTIME_FIX69] */
static __attribute__((unused)) void bender_fix69_frame_pace_2869(unsigned frame,uint64_t start_us,unsigned legacy_delay_us){
    const int mode=bender_fix69_frame_mode_2869();
    if(mode==1){
        bender_fix61_pace_2861(frame,start_us);
        return;
    }
    if(mode==2){
        if(frame<=10u||frame%300u==0u){
            const uint64_t now=bender_fix61_now_us_2861();
            const uint64_t elapsed=(now>=start_us&&start_us)?(now-start_us):0ull;
            double fps=elapsed?1000000.0/(double)elapsed:0.0;
            logmsg("[BENDER_FRAME_PACING][FRAME] source=fix69 mode=uncapped frame=%u elapsed_us=%llu sleep_us=0 fps=%.2f policy=no-bender-sleep runtime_select=1 code_patch=0 forced_completion=0\n",frame,(unsigned long long)elapsed,fps);
        }
        return;
    }
    if(legacy_delay_us)usleep(legacy_delay_us);
    if(frame<=10u||frame%300u==0u){
        logmsg("[BENDER_FRAME_PACING][FRAME] source=fix69 mode=compat frame=%u legacy_delay_us=%u policy=historical-post-render-sleep runtime_select=1 code_patch=0 forced_completion=0\n",frame,legacy_delay_us);
    }
}

/* ULTRAKILL fix -N: forward decl do present por framebuffer (as funcoes uk_*
 * estao mais abaixo no arquivo; sem isto o call site do frame loop nao as ve). */
static void uk_fb_present_frame(int w,int h);
static __attribute__((unused)) int bender_frame_host_present_enabled(void){
    const char *e=getenv("BENDER_FRAME_HOST_PRESENT");
    if(!e||!*e)return 1;
    return !(e[0]=='0'||e[0]=='n'||e[0]=='N'||e[0]=='f'||e[0]=='F');
}
static __attribute__((unused)) int bender_host_present_fallback(unsigned frame,const char *reason){
    static int suppression_announced;
    if(!bender_frame_host_present_enabled())return 0;
    if(g_bender_egl_swaps>0ul){if(!suppression_announced){suppression_announced=1;logmsg("[BENDER_FRAME_LOOP][HOST_PRESENT_SUPPRESSED] after_first_guest_swap=1 policy=single-present reason=avoid-double-buffer-flip\n");}return 0;}
    if(!g_bender_sdl_ready||!g_bender_egl_current||!g_sdl_GL_SwapWindow||!g_bender_sdl_window)return 0;
    g_sdl_GL_SwapWindow(g_bender_sdl_window);++g_bender_host_presents;
    logmsg("[BENDER_FRAME_LOOP][HOST_PRESENT] frame=%u host_present=%lu guest_swaps=%lu drawable=%dx%d reason=%s policy=bootstrap-only\n",frame,g_bender_host_presents,g_bender_egl_swaps,g_bender_sdl_draw_w,g_bender_sdl_draw_h,reason?reason:"no-guest-swap");
    /* ULTRAKILL fix -N (evidencia do log de 05/11 07:17: musica tocando, 200
     * host_present, guest_swaps=0 e o painel parado na lista da ES): o swap do
     * SDL apresenta a surface do SDL, mas quem desenha o jogo e a surface do
     * Unity (criada do nosso ANativeWindow fake). Entao batemos o framebuffer
     * atual no painel nos mesmos. BENDER_UK_FB=0 desliga. */
    uk_fb_present_frame(0,0);
    return 1;
}

static const char *bender_sdl_error(void){return g_sdl_GetError?g_sdl_GetError():"(SDL_GetError unavailable)";}
static void *bender_sdl_sym(const char *name){return (g_bender_sdl_host&&name)?dlsym(g_bender_sdl_host,name):NULL;}
static int bender_sdl_load(void){
    if(g_bender_sdl_host)return 1;
    const char *envlib=getenv("BENDER_SDL_LIBRARY");
    if(envlib&&envlib[0]){void *h=dlopen(envlib,RTLD_NOW|RTLD_LOCAL);if(h){g_bender_sdl_host=h;g_bender_sdl_host_request=envlib;}}
    const char *candidates[]={"libSDL2-2.0.so.0","libSDL2.so","/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0","/usr/local/lib/aarch64-linux-gnu/libSDL2-2.0.so.0",NULL};
    for(int i=0;!g_bender_sdl_host&&candidates[i];i++){
        void *h=dlopen(candidates[i],RTLD_NOW|RTLD_LOCAL);
        if(h){g_bender_sdl_host=h;g_bender_sdl_host_request=candidates[i];break;}
    }
    if(!g_bender_sdl_host){logmsg("[BENDER_SDL][LOAD][FAIL] dlerror=%s\n",dlerror());return 0;}
#define SDLREQ(dst,name) do{dst=(void*)bender_sdl_sym(name);if(!(dst)){logmsg("[BENDER_SDL][LOAD][MISSING] symbol=%s\n",name);return 0;}}while(0)
    SDLREQ(g_sdl_InitSubSystem,"SDL_InitSubSystem");
    SDLREQ(g_sdl_GetError,"SDL_GetError");
    g_sdl_GetCurrentVideoDriver=(BenderSDLGetCurrentVideoDriverFn)bender_sdl_sym("SDL_GetCurrentVideoDriver");
    g_sdl_GetNumVideoDrivers=(BenderSDLGetNumVideoDriversFn)bender_sdl_sym("SDL_GetNumVideoDrivers");
    g_sdl_GetVideoDriver=(BenderSDLGetVideoDriverFn)bender_sdl_sym("SDL_GetVideoDriver");
    g_sdl_SetHint=(BenderSDLSetHintFn)bender_sdl_sym("SDL_SetHint");
    SDLREQ(g_sdl_GL_SetAttribute,"SDL_GL_SetAttribute");
    g_sdl_GL_GetAttribute=(BenderSDLGLGetAttributeFn)bender_sdl_sym("SDL_GL_GetAttribute");
    SDLREQ(g_sdl_CreateWindow,"SDL_CreateWindow");
    SDLREQ(g_sdl_GL_CreateContext,"SDL_GL_CreateContext");
    SDLREQ(g_sdl_GL_MakeCurrent,"SDL_GL_MakeCurrent");
    SDLREQ(g_sdl_GL_SwapWindow,"SDL_GL_SwapWindow");
    SDLREQ(g_sdl_GL_GetDrawableSize,"SDL_GL_GetDrawableSize");
    g_sdl_GL_SetSwapInterval=(BenderSDLGLSetSwapIntervalFn)bender_sdl_sym("SDL_GL_SetSwapInterval");
    SDLREQ(g_sdl_GL_GetProcAddress,"SDL_GL_GetProcAddress");
    g_sdl_NumJoysticks=(BenderSDLNumJoysticksFn)bender_sdl_sym("SDL_NumJoysticks");
    g_sdl_IsGameController=(BenderSDLIsGameControllerFn)bender_sdl_sym("SDL_IsGameController");
    g_sdl_GameControllerOpen=(BenderSDLGameControllerOpenFn)bender_sdl_sym("SDL_GameControllerOpen");
    g_sdl_GameControllerName=(BenderSDLGameControllerNameFn)bender_sdl_sym("SDL_GameControllerName");
    g_sdl_GameControllerGetButton=(BenderSDLGameControllerGetButtonFn)bender_sdl_sym("SDL_GameControllerGetButton");
    g_sdl_GameControllerGetAxis=(BenderSDLGameControllerGetAxisFn)bender_sdl_sym("SDL_GameControllerGetAxis");
    g_sdl_GameControllerUpdate=(BenderSDLGameControllerUpdateFn)bender_sdl_sym("SDL_GameControllerUpdate");
    g_sdl_JoystickOpen=(BenderSDLJoystickOpenFn)bender_sdl_sym("SDL_JoystickOpen");
    g_sdl_JoystickName=(BenderSDLJoystickNameFn)bender_sdl_sym("SDL_JoystickName");
    g_sdl_JoystickGetButton=(BenderSDLJoystickGetButtonFn)bender_sdl_sym("SDL_JoystickGetButton");
    g_sdl_JoystickGetAxis=(BenderSDLJoystickGetAxisFn)bender_sdl_sym("SDL_JoystickGetAxis");
    g_sdl_JoystickNumButtons=(BenderSDLJoystickNumButtonsFn)bender_sdl_sym("SDL_JoystickNumButtons");
    g_sdl_JoystickUpdate=(BenderSDLJoystickUpdateFn)bender_sdl_sym("SDL_JoystickUpdate");
#undef SDLREQ
    Dl_info di;void *p=(void*)g_sdl_CreateWindow;
    const char *path=(p&&dladdr(p,&di)&&di.dli_fname)?di.dli_fname:"(unknown)";
    logmsg("[BENDER_SDL][LOAD][OK] request=%s handle=%p library=%s SDL_VIDEODRIVER=%s SDL_VIDEO_EGL_DRIVER=%s\n",
        g_bender_sdl_host_request?g_bender_sdl_host_request:"(none)",g_bender_sdl_host,path,
        getenv("SDL_VIDEODRIVER")?getenv("SDL_VIDEODRIVER"):"(unset)",
        getenv("SDL_VIDEO_EGL_DRIVER")?getenv("SDL_VIDEO_EGL_DRIVER"):"(unset)");
    return 1;
}
static int bender_render_scale_enabled_2768(void);
static void bender_render_scale_init_2768(void);
static void bender_render_scale_present_2768(void);
static void bender_render_scale_resume_2768(void);
static int bender_sdl_egl_init(void){
    if(g_bender_sdl_ready)return 1;
    bender_gpu_watchdog_start();bender_gpu_stage("sdl-load");
    if(!bender_sdl_load())return 0;
    bender_gpu_stage("sdl-video-init");
    if(g_sdl_SetHint){
        g_sdl_SetHint("SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS","0");
        g_sdl_SetHint("SDL_MOUSE_RELATIVE_MODE_WARP","0");
    }
    if(!getenv("SDL_VIDEODRIVER")&&g_sdl_GetNumVideoDrivers&&g_sdl_GetVideoDriver){
        int n=g_sdl_GetNumVideoDrivers();int have_mali=0,have_kmsdrm=0;
        logmsg("[BENDER_SDL][VIDEO_DRIVERS] count=%d\n",n);
        for(int i=0;i<n;i++){
            const char *vd=g_sdl_GetVideoDriver(i);
            logmsg("[BENDER_SDL][VIDEO_DRIVER] index=%d name=%s\n",i,vd?vd:"(null)");
            if(vd&&strcmp(vd,"mali")==0)have_mali=1;
            if(vd&&strcmp(vd,"kmsdrm")==0)have_kmsdrm=1;
        }
        const char *chosen=have_mali?"mali":(have_kmsdrm?"kmsdrm":NULL);
        if(chosen){setenv("SDL_VIDEODRIVER",chosen,1);logmsg("[BENDER_SDL][VIDEO_DRIVER][SELECTED] name=%s reason=capability\n",chosen);}
    }
    int rc=g_sdl_InitSubSystem(BENDER_SDL_INIT_VIDEO);
    if(rc!=0){logmsg("[BENDER_SDL][VIDEO][FAIL] rc=%d error=%s\n",rc,bender_sdl_error());return 0;}
    logmsg("[BENDER_SDL][VIDEO][OK] driver=%s\n",g_sdl_GetCurrentVideoDriver?g_sdl_GetCurrentVideoDriver():"(unknown)");

    bender_gpu_stage("sdl-gl-attributes");
    g_sdl_GL_SetAttribute(BENDER_SDL_GL_CONTEXT_PROFILE_MASK,BENDER_SDL_GL_CONTEXT_PROFILE_ES);
    /* Prefer an ES3-capable EGLConfig at SDL window creation. Context creation below
       still falls back to ES2 if the firmware rejects ES3. */
    g_sdl_GL_SetAttribute(BENDER_SDL_GL_CONTEXT_MAJOR_VERSION,3);
    g_sdl_GL_SetAttribute(BENDER_SDL_GL_CONTEXT_MINOR_VERSION,0);
    g_sdl_GL_SetAttribute(BENDER_SDL_GL_DOUBLEBUFFER,1);
    g_sdl_GL_SetAttribute(BENDER_SDL_GL_RED_SIZE,8);
    g_sdl_GL_SetAttribute(BENDER_SDL_GL_GREEN_SIZE,8);
    g_sdl_GL_SetAttribute(BENDER_SDL_GL_BLUE_SIZE,8);
    g_sdl_GL_SetAttribute(BENDER_SDL_GL_ALPHA_SIZE,8);
    g_sdl_GL_SetAttribute(BENDER_SDL_GL_DEPTH_SIZE,24);
    g_sdl_GL_SetAttribute(BENDER_SDL_GL_STENCIL_SIZE,8);

    bender_gpu_stage("sdl-create-window");
    unsigned flags=BENDER_SDL_WINDOW_OPENGL|BENDER_SDL_WINDOW_SHOWN|BENDER_SDL_WINDOW_FULLSCREEN_DESKTOP;
    g_bender_sdl_window=g_sdl_CreateWindow("BENDER Unity",(int)BENDER_SDL_WINDOWPOS_CENTERED_MASK,(int)BENDER_SDL_WINDOWPOS_CENTERED_MASK,640,480,flags);
    if(!g_bender_sdl_window){
        logmsg("[BENDER_SDL][WINDOW][FAIL] flags=0x%x error=%s; retry=windowed\n",flags,bender_sdl_error());
        flags=BENDER_SDL_WINDOW_OPENGL|BENDER_SDL_WINDOW_SHOWN;
        g_bender_sdl_window=g_sdl_CreateWindow("BENDER Unity",(int)BENDER_SDL_WINDOWPOS_CENTERED_MASK,(int)BENDER_SDL_WINDOWPOS_CENTERED_MASK,640,480,flags);
    }
    if(!g_bender_sdl_window){logmsg("[BENDER_SDL][WINDOW][FAIL_FINAL] error=%s\n",bender_sdl_error());return 0;}
    logmsg("[BENDER_SDL][WINDOW][OK] window=%p flags=0x%x\n",g_bender_sdl_window,flags);

    bender_gpu_stage("sdl-create-gles-context");
    const int ctx_versions[][2]={{3,2},{3,1},{3,0},{2,0}};
    for(size_t vi=0;vi<sizeof(ctx_versions)/sizeof(ctx_versions[0]);vi++){
        int maj=ctx_versions[vi][0],min=ctx_versions[vi][1];
        g_sdl_GL_SetAttribute(BENDER_SDL_GL_CONTEXT_MAJOR_VERSION,maj);
        g_sdl_GL_SetAttribute(BENDER_SDL_GL_CONTEXT_MINOR_VERSION,min);
        set_checkpoint_quiet("gpu-stage sdl-create-gles%d.%d-context",maj,min);
        g_bender_sdl_gl=g_sdl_GL_CreateContext(g_bender_sdl_window);
        logmsg("[BENDER_SDL][CONTEXT_TRY] requested=GLES%d.%d context=%p error=%s\n",maj,min,g_bender_sdl_gl,bender_sdl_error());
        if(g_bender_sdl_gl){g_bender_sdl_gl_major=maj;g_bender_sdl_gl_minor=min;break;}
    }
    if(!g_bender_sdl_gl){logmsg("[BENDER_SDL][CONTEXT][FAIL] all GLES3/GLES2 attempts failed error=%s\n",bender_sdl_error());return 0;}

    bender_gpu_stage("sdl-make-current");
    rc=g_sdl_GL_MakeCurrent(g_bender_sdl_window,g_bender_sdl_gl);
    if(rc!=0){logmsg("[BENDER_SDL][MAKE_CURRENT][FAIL] rc=%d error=%s\n",rc,bender_sdl_error());return 0;}
    if(g_sdl_GL_GetAttribute){
        int qmaj=0,qmin=0;
        if(g_sdl_GL_GetAttribute(BENDER_SDL_GL_CONTEXT_MAJOR_VERSION,&qmaj)==0&&qmaj>0)g_bender_sdl_gl_major=qmaj;
        if(g_sdl_GL_GetAttribute(BENDER_SDL_GL_CONTEXT_MINOR_VERSION,&qmin)==0&&qmin>=0)g_bender_sdl_gl_minor=qmin;
    }
    g_bender_egl_renderable_bits=BENDER_EGL_OPENGL_ES2_BIT;
    if(g_bender_sdl_gl_major>=3)g_bender_egl_renderable_bits|=BENDER_EGL_OPENGL_ES3_BIT_KHR;
    if(g_sdl_GL_GetProcAddress){
        typedef const unsigned char *(*BenderGLGetStringFn)(unsigned int);
        BenderGLGetStringFn glGetStringFn=(BenderGLGetStringFn)g_sdl_GL_GetProcAddress("glGetString");
        const unsigned char *ver=glGetStringFn?glGetStringFn(0x1F02u):NULL;
        logmsg("[BENDER_SDL][GL_VERSION] requested/backing=GLES%d.%d renderable_bits=0x%x string=%s\n",g_bender_sdl_gl_major,g_bender_sdl_gl_minor,g_bender_egl_renderable_bits,ver?(const char*)ver:"(unavailable)");
    }
    /* Host context stays current so SDL/KMSDRM owns the real EGL plumbing, but
       guest EGL must still observe 'no current context' until eglMakeCurrent. */
    g_bender_egl_current=0;
    if(g_sdl_GL_SetSwapInterval){int src=g_sdl_GL_SetSwapInterval(0);logmsg("[BENDER_SDL][SWAP_INTERVAL] requested=0 rc=%d error=%s\n",src,bender_sdl_error());}
    g_sdl_GL_GetDrawableSize(g_bender_sdl_window,&g_bender_sdl_draw_w,&g_bender_sdl_draw_h);
    if(g_bender_sdl_draw_w<=0)g_bender_sdl_draw_w=640;
    if(g_bender_sdl_draw_h<=0)g_bender_sdl_draw_h=480;
    g_bender_sdl_ready=1;
    bender_render_scale_init_2768();
    logmsg("[BENDER_SDL][CONTEXT][OK] context=%p GLES=%d.%d renderable_bits=0x%x\n",g_bender_sdl_gl,g_bender_sdl_gl_major,g_bender_sdl_gl_minor,g_bender_egl_renderable_bits);
    logmsg("[BENDER_SDL][READY] driver=%s drawable=%dx%d window=%p context=%p\n",
        g_sdl_GetCurrentVideoDriver?g_sdl_GetCurrentVideoDriver():"(unknown)",g_bender_sdl_draw_w,g_bender_sdl_draw_h,g_bender_sdl_window,g_bender_sdl_gl);
    return 1;
}

static void bender_host_egl_open(void){
    if(g_bender_egl_host)return;
    const char *env_egl=getenv("BENDER_EGL_LIBRARY");
    const char *env_sdl_egl=getenv("SDL_VIDEO_EGL_DRIVER");
    if(env_egl&&env_egl[0]){void *h=dlopen(env_egl,RTLD_LAZY|RTLD_LOCAL);if(h){g_bender_egl_host=h;g_bender_egl_host_request=env_egl;}}
    if(!g_bender_egl_host&&env_sdl_egl&&env_sdl_egl[0]){void *h=dlopen(env_sdl_egl,RTLD_LAZY|RTLD_LOCAL);if(h){g_bender_egl_host=h;g_bender_egl_host_request=env_sdl_egl;}}
    const char *candidates[]={"libEGL.so.1","libEGL.so",NULL};
    for(int i=0;!g_bender_egl_host&&candidates[i];i++){
        void *h=dlopen(candidates[i],RTLD_LAZY|RTLD_LOCAL);
        if(h){g_bender_egl_host=h;g_bender_egl_host_request=candidates[i];break;}
    }
    logmsg("[BENDER_EGL_RAW][HOST_OPEN] request=%s handle=%p\n",g_bender_egl_host_request?g_bender_egl_host_request:"(none)",g_bender_egl_host);
}
static void *bender_host_egl_sym(const char *name){bender_host_egl_open();return (g_bender_egl_host&&name)?dlsym(g_bender_egl_host,name):NULL;}
static BenderEGLint bender_host_egl_error(void){typedef BenderEGLint(*Fn)(void);Fn f=(Fn)bender_host_egl_sym("eglGetError");return f?f():0;}
static BenderEGLDisplay bender_raw_default_display(void){
    if(g_bender_raw_display)return g_bender_raw_display;
    typedef BenderEGLDisplay(*GetFn)(BenderEGLNativeDisplayType);GetFn gd=(GetFn)bender_host_egl_sym("eglGetDisplay");
    typedef BenderEGLBoolean(*InitFn)(BenderEGLDisplay,BenderEGLint*,BenderEGLint*);InitFn init=(InitFn)bender_host_egl_sym("eglInitialize");
    BenderEGLDisplay d=gd?gd(NULL):NULL;BenderEGLint maj=-1,min=-1;BenderEGLBoolean rc=(d&&init)?init(d,&maj,&min):0;BenderEGLint err=bender_host_egl_error();
    logmsg("[BENDER_EGL_RAW][DEFAULT] display=%p initialize=%u version=%d.%d err=0x%x\n",d,rc,maj,min,(unsigned)err);
    if(rc){g_bender_raw_display=d;g_bender_raw_initialized=1;g_bender_raw_major=maj;g_bender_raw_minor=min;}
    return d;
}

static BenderEGLBoolean bender_egl_fail(BenderEGLint err){g_bender_egl_error=err;return BENDER_EGL_FALSE;}
static BenderEGLDisplay bender_eglGetDisplay(BenderEGLNativeDisplayType native_display){
    (void)native_display;++g_bender_egl_calls;bender_gpu_watchdog_start();bender_gpu_stage("eglGetDisplay-sdl");
    if(bender_sdl_egl_init()){
        g_bender_egl_error=BENDER_EGL_SUCCESS;
        logmsg("[BENDER_EGL][GET_DISPLAY][SDL] call=%lu display=%p driver=%s\n",g_bender_egl_calls,BENDER_EGL_DISPLAY_COOKIE,g_sdl_GetCurrentVideoDriver?g_sdl_GetCurrentVideoDriver():"(unknown)");
        return BENDER_EGL_DISPLAY_COOKIE;
    }
    bender_gpu_stage("eglGetDisplay-raw-default");
    BenderEGLDisplay d=bender_raw_default_display();
    logmsg("[BENDER_EGL][GET_DISPLAY][FALLBACK_RAW] call=%lu display=%p initialized=%d\n",g_bender_egl_calls,d,g_bender_raw_initialized);
    return d;
}
static BenderEGLBoolean bender_eglInitialize(BenderEGLDisplay d,BenderEGLint *major,BenderEGLint *minor){
    bender_gpu_stage("eglInitialize");
    if(d==BENDER_EGL_DISPLAY_COOKIE&&g_bender_sdl_ready){if(major)*major=1;if(minor)*minor=4;g_bender_egl_error=BENDER_EGL_SUCCESS;logmsg("[BENDER_EGL][INITIALIZE][SDL] rc=1 version=1.4\n");return BENDER_EGL_TRUE;}
    if(d&&d==g_bender_raw_display&&g_bender_raw_initialized){if(major)*major=g_bender_raw_major;if(minor)*minor=g_bender_raw_minor;logmsg("[BENDER_EGL][INITIALIZE][RAW] rc=1 cached=1 version=%d.%d\n",g_bender_raw_major,g_bender_raw_minor);return BENDER_EGL_TRUE;}
    typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay,BenderEGLint*,BenderEGLint*);Fn f=(Fn)bender_host_egl_sym("eglInitialize");BenderEGLBoolean rc=f?f(d,major,minor):0;logmsg("[BENDER_EGL][INITIALIZE][RAW_DIRECT] display=%p rc=%u err=0x%x\n",d,rc,(unsigned)bender_host_egl_error());return rc;
}
static BenderEGLBoolean bender_eglBindAPI(BenderEGLenum api){
    if(g_bender_sdl_ready){logmsg("[BENDER_EGL][BIND_API][SDL] api=0x%x\n",api);return api==BENDER_EGL_OPENGL_ES_API?BENDER_EGL_TRUE:bender_egl_fail(BENDER_EGL_BAD_PARAMETER);}
    typedef BenderEGLBoolean(*Fn)(BenderEGLenum);Fn f=(Fn)bender_host_egl_sym("eglBindAPI");return f?f(api):BENDER_EGL_FALSE;
}
/* ULTRAKILL: estado do fallback pbuffer ( EGL surfaceless so ) */
static BenderEGLConfig  g_bender_pb_cfg;
static BenderEGLSurface g_bender_pb_surface;
static int              g_bender_pb_cfg_ok;

static int bender_sdl_gl_attr(int attr,int fallback);
static BenderEGLBoolean bender_eglChooseConfig(BenderEGLDisplay d,const BenderEGLint *attrs,BenderEGLConfig *configs,BenderEGLint config_size,BenderEGLint *num){
    if(d==BENDER_EGL_DISPLAY_COOKIE&&g_bender_sdl_ready){
        ++g_bender_egl_choose_calls;
        BenderEGLint req_render=0,req_surface=0;int compatible=1;
        char abuf[768];size_t apos=(size_t)snprintf(abuf,sizeof(abuf),"[BENDER_EGL][CHOOSE_CONFIG][ATTRS] call=%lu",g_bender_egl_choose_calls);
        if(attrs){
            for(int i=0;i<64;i+=2){
                BenderEGLint a=attrs[i];if(a==BENDER_EGL_NONE)break;BenderEGLint v=attrs[i+1];
                if(apos+32u<sizeof(abuf)){int an=snprintf(abuf+apos,sizeof(abuf)-apos," 0x%x=%d",a,v);if(an>0){size_t add=(size_t)an;apos+=(add<sizeof(abuf)-apos)?add:(sizeof(abuf)-apos-1u);}}
                if(a==BENDER_EGL_RENDERABLE_TYPE)req_render=v;
                else if(a==BENDER_EGL_SURFACE_TYPE)req_surface=v;
                else if(a==BENDER_EGL_RED_SIZE&&v>bender_sdl_gl_attr(BENDER_SDL_GL_RED_SIZE,8))compatible=0;
                else if(a==BENDER_EGL_GREEN_SIZE&&v>bender_sdl_gl_attr(BENDER_SDL_GL_GREEN_SIZE,8))compatible=0;
                else if(a==BENDER_EGL_BLUE_SIZE&&v>bender_sdl_gl_attr(BENDER_SDL_GL_BLUE_SIZE,8))compatible=0;
                else if(a==BENDER_EGL_ALPHA_SIZE&&v>bender_sdl_gl_attr(BENDER_SDL_GL_ALPHA_SIZE,8))compatible=0;
                else if(a==BENDER_EGL_DEPTH_SIZE&&v>bender_sdl_gl_attr(BENDER_SDL_GL_DEPTH_SIZE,24))compatible=0;
                else if(a==BENDER_EGL_STENCIL_SIZE&&v>bender_sdl_gl_attr(BENDER_SDL_GL_STENCIL_SIZE,8))compatible=0;
                else if(a==BENDER_EGL_SAMPLE_BUFFERS&&v>bender_sdl_gl_attr(BENDER_SDL_GL_MULTISAMPLEBUFFERS,0))compatible=0;
                else if(a==BENDER_EGL_SAMPLES&&v>bender_sdl_gl_attr(BENDER_SDL_GL_MULTISAMPLESAMPLES,0))compatible=0;
            }
        }
        if(apos>=sizeof(abuf)) apos=sizeof(abuf)-1;
        abuf[apos]=0;
        logmsg("%s\n",abuf);
        if(req_render&&((g_bender_egl_renderable_bits&req_render)!=req_render))compatible=0;
        if(req_surface&&(((BENDER_EGL_WINDOW_BIT|BENDER_EGL_PBUFFER_BIT)&req_surface)!=req_surface))compatible=0;
        if(num)*num=compatible?1:0;
        if(compatible&&configs&&config_size>0)configs[0]=BENDER_EGL_CONFIG_COOKIE;
        g_bender_egl_error=BENDER_EGL_SUCCESS;
        logmsg("[BENDER_EGL][CHOOSE_CONFIG][SDL] call=%lu rc=1 num=%d req_render=0x%x available=0x%x req_surface=0x%x config=%p\n",g_bender_egl_choose_calls,compatible?1:0,req_render,g_bender_egl_renderable_bits,req_surface,compatible?BENDER_EGL_CONFIG_COOKIE:NULL);
        return BENDER_EGL_TRUE;
    }
    typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay,const BenderEGLint*,BenderEGLConfig*,BenderEGLint,BenderEGLint*);Fn f=(Fn)bender_host_egl_sym("eglChooseConfig");
    if(!f)return 0;
    /* ULTRAKILL/R36S: aqui o host EGL e SURFACELESS (Mesa llvmpipe por cima
     * do glvndstack do port) - o GBM dessa firmware esta morto, tanto que o
     * SDL deles mesmo falha com "Can't window GBM/EGL surfaces". Config com
     * EGL_WINDOW_BIT = ZERO -> o Unity recebe num=0, nao tem GFX device e chama
     * UnityPlayer.kill() no primeiro nativeRender (visto no log do device).
     * Re-consulta forcando EGL_PBUFFER_BIT: pbuffer e render target completo. */
    {
        char abuf[640];size_t apos=(size_t)snprintf(abuf,sizeof(abuf),
            "[BENDER_EGL][CHOOSE_CONFIG][RAW][ATTRS] dpy=%d cfgs=%d", (int)(uintptr_t)d, (int)config_size);
        if(attrs){for(int q=0;q<64;q+=2){BenderEGLint at=attrs[q];if(at==BENDER_EGL_NONE)break;
            if(apos+24u<sizeof(abuf)){int an=snprintf(abuf+apos,sizeof(abuf)-apos," 0x%x=%d",at,attrs[q+1]);
                if(an>0){size_t add=(size_t)an;apos+=(add<sizeof(abuf)-apos-1u)?add:(sizeof(abuf)-apos-1u);}}}}
        else apos+=(size_t)snprintf(abuf+apos,sizeof(abuf)-apos," (null)");
        if(apos>=sizeof(abuf))apos=sizeof(abuf)-1;abuf[apos]=0;
        logmsg("%s configs=%p\n",abuf,(void*)configs);
    }
    BenderEGLBoolean rc=f(d,attrs,configs,config_size,num);
    int raw_num=(rc&&num)?*num:0;
    logmsg("[BENDER_EGL][CHOOSE_CONFIG][RAW] rc=%u num=%d err=0x%x\n",rc,raw_num,(unsigned)bender_host_egl_error());
    if((!rc||raw_num==0)&&!g_bender_sdl_ready){
        /* pass 1 = mesmos atributos do Unity com SURFACE_TYPE forcado em
         * PBUFFER; pass 2 = lista minima que qualquer Mesa surfaceless
         * aceita (RGBA8888 + depth24 + stencil8 + ES2|ES3, pbuffer). */
        static const BenderEGLint minimal[]={BENDER_EGL_SURFACE_TYPE,BENDER_EGL_PBUFFER_BIT,
            0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3025,24,0x3026,8,
            BENDER_EGL_RENDERABLE_TYPE,0x0044,BENDER_EGL_NONE};
        BenderEGLint pb[80];
        int n=0,i,havest=0,pass;
        for(pass=1;pass<=2;pass++){
            BenderEGLint n2=0;BenderEGLBoolean rc2;
            const BenderEGLint *req;
            if(pass==1){
                n=0;havest=0;
                if(attrs){for(i=0;i<64&&n<72;i+=2){BenderEGLint at=attrs[i];if(at==BENDER_EGL_NONE)break;
                    BenderEGLint v=attrs[i+1];
                    if(at==BENDER_EGL_SURFACE_TYPE){v=(v&~BENDER_EGL_WINDOW_BIT)|BENDER_EGL_PBUFFER_BIT;if(!v)v=BENDER_EGL_PBUFFER_BIT;havest=1;}
                    pb[n++]=at;pb[n++]=v;}}
                if(!havest){pb[n++]=BENDER_EGL_SURFACE_TYPE;pb[n++]=BENDER_EGL_PBUFFER_BIT;}
                pb[n++]=BENDER_EGL_NONE;
                req=pb;
            } else {
                req=minimal;
            }
            bender_host_egl_error();
            rc2=f(d,req,configs,config_size,&n2);
            logmsg("[BENDER_EGL][CHOOSE_CONFIG][RAW_PBUFFER] pass=%d rc=%u num=%d err=0x%x\n",
                   pass,rc2,n2,(unsigned)bender_host_egl_error());
            if(rc2&&n2>0){
                g_bender_pb_cfg=(configs&&config_size>0)?configs[0]:(BenderEGLConfig)0;
                if(!g_bender_pb_cfg)g_bender_pb_cfg=(BenderEGLConfig)(uintptr_t)1;
                g_bender_pb_cfg_ok=1;
                if(num)*num=n2;
                logmsg("[BENDER_EGL][CHOOSE_CONFIG][FORCED_PBUFFER] aceito pass=%d configs=%d primeiro=%p\n",
                       pass,n2,(void*)g_bender_pb_cfg);
                return BENDER_EGL_TRUE;
            }
        }
        logmsg("[BENDER_EGL][CHOOSE_CONFIG][FORCED_PBUFFER][FAIL] nenhuma lista pbuffer foi aceita\n");
        /* ULTRAKILL: o Mesa surfaceless comeca a devolver BAD_ATTRIBUTE ate para
         * lista minima depois que o Unity monta o GFX device. Ele so precisa de
         * UM config valido; devolvemos o pbuffer que ja achamos antes. */
        if(g_bender_pb_cfg_ok){
            if(configs&&config_size>0)configs[0]=g_bender_pb_cfg;
            if(num)*num=1;
            bender_host_egl_error();
            logmsg("[BENDER_EGL][CHOOSE_CONFIG][REUSE_CACHED_CONFIG] cfg=%p num=1\n",(void*)g_bender_pb_cfg);
            return BENDER_EGL_TRUE;
        }
    }
    if(g_bender_pb_cfg_ok&&(!rc||raw_num==0)){
        if(configs&&config_size>0)configs[0]=g_bender_pb_cfg;
        if(num)*num=1;
        bender_host_egl_error();
        logmsg("[BENDER_EGL][CHOOSE_CONFIG][REUSE_CACHED_CONFIG] (pos-crua) cfg=%p num=1\n",(void*)g_bender_pb_cfg);
        return BENDER_EGL_TRUE;
    }
    return rc;
}
static int bender_sdl_gl_attr(int attr,int fallback){int v=fallback;if(g_sdl_GL_GetAttribute&&g_sdl_GL_GetAttribute(attr,&v)==0)return v;return fallback;}
static BenderEGLBoolean bender_eglGetConfigAttrib(BenderEGLDisplay d,BenderEGLConfig c,BenderEGLint attr,BenderEGLint *value){
    if(d==BENDER_EGL_DISPLAY_COOKIE&&c==BENDER_EGL_CONFIG_COOKIE&&value&&g_bender_sdl_ready){
        switch(attr){
            case BENDER_EGL_RED_SIZE:*value=bender_sdl_gl_attr(BENDER_SDL_GL_RED_SIZE,8);break;
            case BENDER_EGL_GREEN_SIZE:*value=bender_sdl_gl_attr(BENDER_SDL_GL_GREEN_SIZE,8);break;
            case BENDER_EGL_BLUE_SIZE:*value=bender_sdl_gl_attr(BENDER_SDL_GL_BLUE_SIZE,8);break;
            case BENDER_EGL_ALPHA_SIZE:*value=bender_sdl_gl_attr(BENDER_SDL_GL_ALPHA_SIZE,8);break;
            case BENDER_EGL_DEPTH_SIZE:*value=bender_sdl_gl_attr(BENDER_SDL_GL_DEPTH_SIZE,24);break;
            case BENDER_EGL_STENCIL_SIZE:*value=bender_sdl_gl_attr(BENDER_SDL_GL_STENCIL_SIZE,8);break;
            case BENDER_EGL_SAMPLES:*value=bender_sdl_gl_attr(BENDER_SDL_GL_MULTISAMPLESAMPLES,0);break;
            case BENDER_EGL_SAMPLE_BUFFERS:*value=bender_sdl_gl_attr(BENDER_SDL_GL_MULTISAMPLEBUFFERS,0);break;
            case BENDER_EGL_BUFFER_SIZE:*value=bender_sdl_gl_attr(BENDER_SDL_GL_RED_SIZE,8)+bender_sdl_gl_attr(BENDER_SDL_GL_GREEN_SIZE,8)+bender_sdl_gl_attr(BENDER_SDL_GL_BLUE_SIZE,8)+bender_sdl_gl_attr(BENDER_SDL_GL_ALPHA_SIZE,8);break;
            case BENDER_EGL_NATIVE_VISUAL_ID:*value=0;break;
            case BENDER_EGL_CONFIG_ID:*value=1;break;
            case BENDER_EGL_LEVEL:*value=0;break;
            case BENDER_EGL_CONFIG_CAVEAT:*value=BENDER_EGL_NONE;break;
            case BENDER_EGL_NATIVE_RENDERABLE:*value=BENDER_EGL_FALSE;break;
            case BENDER_EGL_COLOR_BUFFER_TYPE:*value=BENDER_EGL_RGB_BUFFER;break;
            case BENDER_EGL_TRANSPARENT_TYPE:*value=BENDER_EGL_NONE;break;
            case BENDER_EGL_BIND_TO_TEXTURE_RGB:case BENDER_EGL_BIND_TO_TEXTURE_RGBA:*value=BENDER_EGL_FALSE;break;
            case BENDER_EGL_MIN_SWAP_INTERVAL:*value=0;break;
            case BENDER_EGL_MAX_SWAP_INTERVAL:*value=1;break;
            case BENDER_EGL_MAX_PBUFFER_WIDTH:*value=g_bender_sdl_draw_w;break;
            case BENDER_EGL_MAX_PBUFFER_HEIGHT:*value=g_bender_sdl_draw_h;break;
            case BENDER_EGL_MAX_PBUFFER_PIXELS:*value=g_bender_sdl_draw_w*g_bender_sdl_draw_h;break;
            case BENDER_EGL_RECORDABLE_ANDROID:*value=BENDER_EGL_TRUE;break;
            case BENDER_EGL_SURFACE_TYPE:*value=BENDER_EGL_WINDOW_BIT|BENDER_EGL_PBUFFER_BIT;break;
            case BENDER_EGL_RENDERABLE_TYPE:case BENDER_EGL_CONFORMANT:*value=g_bender_egl_renderable_bits;break;
            default:logmsg("[BENDER_EGL][CONFIG_ATTR][SDL][UNKNOWN] attr=0x%x\n",attr);return bender_egl_fail(BENDER_EGL_BAD_ATTRIBUTE);
        }
        logmsg("[BENDER_EGL][CONFIG_ATTR][SDL] attr=0x%x value=%d\n",attr,*value);return BENDER_EGL_TRUE;
    }
    typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay,BenderEGLConfig,BenderEGLint,BenderEGLint*);Fn f=(Fn)bender_host_egl_sym("eglGetConfigAttrib");return f?f(d,c,attr,value):0;
}
static BenderEGLSurface bender_eglCreateWindowSurface(BenderEGLDisplay d,BenderEGLConfig c,BenderEGLNativeWindowType win,const BenderEGLint *attrs){
    (void)win;(void)attrs;bender_gpu_stage("eglCreateWindowSurface");
    if(d==BENDER_EGL_DISPLAY_COOKIE&&c==BENDER_EGL_CONFIG_COOKIE&&g_bender_sdl_ready){logmsg("[BENDER_EGL][WINDOW_TO_SDL] android_window=%p -> surface=%p drawable=%dx%d\n",win,BENDER_EGL_SURFACE_COOKIE,g_bender_sdl_draw_w,g_bender_sdl_draw_h);return BENDER_EGL_SURFACE_COOKIE;}
    /* ULTRAKILL/R36S: sem GBM nao ha window surface possivel. Quando o
     * choose_config acima achou um config pbuffer, devolve um PBUFFER real do
     * tamanho que o guest pede no ANativeWindow - e o que o bridge deste port
     * faz e o que o Mesa daqui aceita (egltest: ES 3.2 swrast). */
    if(g_bender_pb_cfg_ok){
        typedef BenderEGLSurface(*Fnp)(BenderEGLDisplay,BenderEGLConfig,const BenderEGLint*);
        Fnp fp=(Fnp)bender_host_egl_sym("eglCreatePbufferSurface");
        if(fp){
            int W=g_native_window.width>0?g_native_window.width:640;
            int H=g_native_window.height>0?g_native_window.height:480;
            const BenderEGLint at[]={BENDER_EGL_WIDTH,W,BENDER_EGL_HEIGHT,H,BENDER_EGL_NONE};
            BenderEGLConfig use=c?c:g_bender_pb_cfg;
            BenderEGLSurface s=fp(d,use,at);
            logmsg("[BENDER_EGL][WINDOW_AS_PBUFFER] window surface indisponivel (GBM morto) -> pbuffer %dx%d cfg=%p surface=%p\n",
                   W,H,(void*)use,(void*)s);
            if(s){g_bender_pb_surface=s;bender_gpu_watchdog_done("eglCreateWindowSurface-as-pbuffer");return s;}
            g_bender_egl_error=bender_host_egl_error();
            logmsg("[BENDER_EGL][WINDOW_AS_PBUFFER][FAIL] err=0x%x; cai no caminho original\n",(unsigned)g_bender_egl_error);
        }
    }
    typedef BenderEGLSurface(*Fn)(BenderEGLDisplay,BenderEGLConfig,BenderEGLNativeWindowType,const BenderEGLint*);Fn f=(Fn)bender_host_egl_sym("eglCreateWindowSurface");return f?f(d,c,win,attrs):NULL;
}
static BenderEGLSurface bender_eglCreatePbufferSurface(BenderEGLDisplay d,BenderEGLConfig c,const BenderEGLint *attrs){
    (void)attrs;if(d==BENDER_EGL_DISPLAY_COOKIE&&c==BENDER_EGL_CONFIG_COOKIE&&g_bender_sdl_ready){logmsg("[BENDER_EGL][PBUFFER_TO_SDL] surface=%p\n",BENDER_EGL_SURFACE_COOKIE);return BENDER_EGL_SURFACE_COOKIE;}
    typedef BenderEGLSurface(*Fn)(BenderEGLDisplay,BenderEGLConfig,const BenderEGLint*);Fn f=(Fn)bender_host_egl_sym("eglCreatePbufferSurface");return f?f(d,c,attrs):NULL;
}
static BenderEGLContext bender_eglCreateContext(BenderEGLDisplay d,BenderEGLConfig c,BenderEGLContext share,const BenderEGLint *attrs){
    (void)share;bender_gpu_stage("eglCreateContext");
    if(d==BENDER_EGL_DISPLAY_COOKIE&&c==BENDER_EGL_CONFIG_COOKIE&&g_bender_sdl_ready){
        int req=2;char abuf[384];size_t apos=(size_t)snprintf(abuf,sizeof(abuf),"[BENDER_EGL][CREATE_CONTEXT][ATTRS]");
        if(attrs){for(int i=0;i<32;i+=2){BenderEGLint a=attrs[i];if(a==BENDER_EGL_NONE)break;BenderEGLint v=attrs[i+1];if(apos+32u<sizeof(abuf)){int an=snprintf(abuf+apos,sizeof(abuf)-apos," 0x%x=%d",a,v);if(an>0){size_t add=(size_t)an;apos+=(add<sizeof(abuf)-apos)?add:(sizeof(abuf)-apos-1u);}}if(a==BENDER_EGL_CONTEXT_CLIENT_VERSION)req=v;}}
        if(apos>=sizeof(abuf)) apos=sizeof(abuf)-1;
        abuf[apos]=0;
        logmsg("%s\n",abuf);
        if(req>=3&&g_bender_sdl_gl_major<3){g_bender_egl_error=BENDER_EGL_BAD_MATCH;logmsg("[BENDER_EGL][CREATE_CONTEXT][SDL][REJECT] requested=GLES%d backing=GLES%d.%d\n",req,g_bender_sdl_gl_major,g_bender_sdl_gl_minor);return NULL;}
        g_bender_egl_error=BENDER_EGL_SUCCESS;
        logmsg("[BENDER_EGL][CREATE_CONTEXT][SDL] requested=GLES%d context=%p backing=%p GLES=%d.%d\n",req,BENDER_EGL_CONTEXT_COOKIE,g_bender_sdl_gl,g_bender_sdl_gl_major,g_bender_sdl_gl_minor);
        return BENDER_EGL_CONTEXT_COOKIE;
    }
    typedef BenderEGLContext(*Fn)(BenderEGLDisplay,BenderEGLConfig,BenderEGLContext,const BenderEGLint*);Fn f=(Fn)bender_host_egl_sym("eglCreateContext");return f?f(d,c,share,attrs):NULL;
}
static BenderEGLBoolean bender_eglMakeCurrent(BenderEGLDisplay d,BenderEGLSurface draw,BenderEGLSurface read,BenderEGLContext ctx){
    (void)read;bender_gpu_stage("eglMakeCurrent");
    if(d==BENDER_EGL_DISPLAY_COOKIE&&g_bender_sdl_ready){
        if(!ctx||!draw){int rc=g_sdl_GL_MakeCurrent?g_sdl_GL_MakeCurrent(g_bender_sdl_window,NULL):-1;g_bender_egl_current=0;logmsg("[BENDER_EGL][MAKE_CURRENT][SDL_RELEASE] rc=%d error=%s\n",rc,bender_sdl_error());return rc==0?BENDER_EGL_TRUE:bender_egl_fail(BENDER_EGL_BAD_ACCESS);}
        if(ctx!=BENDER_EGL_CONTEXT_COOKIE||draw!=BENDER_EGL_SURFACE_COOKIE||(read&&read!=BENDER_EGL_SURFACE_COOKIE))return bender_egl_fail(BENDER_EGL_BAD_MATCH);
        int rc=g_sdl_GL_MakeCurrent?g_sdl_GL_MakeCurrent(g_bender_sdl_window,g_bender_sdl_gl):-1;
        logmsg("[BENDER_EGL][MAKE_CURRENT][SDL] rc=%d window=%p context=%p error=%s\n",rc,g_bender_sdl_window,g_bender_sdl_gl,bender_sdl_error());
        if(rc==0){g_bender_egl_current=1;bender_gpu_watchdog_done("eglMakeCurrent-sdl-ready");return BENDER_EGL_TRUE;}
        return bender_egl_fail(BENDER_EGL_BAD_ACCESS);
    }
    typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay,BenderEGLSurface,BenderEGLSurface,BenderEGLContext);Fn f=(Fn)bender_host_egl_sym("eglMakeCurrent");BenderEGLBoolean rc=f?f(d,draw,read,ctx):0;if(rc)bender_gpu_watchdog_done("eglMakeCurrent-raw-ready");return rc;
}
static BenderEGLBoolean bender_eglDestroyContext(BenderEGLDisplay d,BenderEGLContext c){if(d==BENDER_EGL_DISPLAY_COOKIE){(void)c;g_bender_egl_current=0;return BENDER_EGL_TRUE;}typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay,BenderEGLContext);Fn f=(Fn)bender_host_egl_sym("eglDestroyContext");return f?f(d,c):0;}
static BenderEGLBoolean bender_eglDestroySurface(BenderEGLDisplay d,BenderEGLSurface s){if(d==BENDER_EGL_DISPLAY_COOKIE){(void)s;return BENDER_EGL_TRUE;}typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay,BenderEGLSurface);Fn f=(Fn)bender_host_egl_sym("eglDestroySurface");return f?f(d,s):0;}
static BenderEGLContext bender_eglGetCurrentContext(void){if(g_bender_sdl_ready){BenderEGLContext r=g_bender_egl_current?BENDER_EGL_CONTEXT_COOKIE:NULL;logmsg("[BENDER_EGL][GET_CURRENT_CONTEXT][SDL] guest_current=%d -> %p\n",g_bender_egl_current,r);return r;}typedef BenderEGLContext(*Fn)(void);Fn f=(Fn)bender_host_egl_sym("eglGetCurrentContext");return f?f():NULL;}
static BenderEGLSurface bender_eglGetCurrentSurface(BenderEGLint which){if(g_bender_sdl_ready){BenderEGLSurface r=g_bender_egl_current?BENDER_EGL_SURFACE_COOKIE:NULL;logmsg("[BENDER_EGL][GET_CURRENT_SURFACE][SDL] which=0x%x guest_current=%d -> %p\n",which,g_bender_egl_current,r);return r;}typedef BenderEGLSurface(*Fn)(BenderEGLint);Fn f=(Fn)bender_host_egl_sym("eglGetCurrentSurface");return f?f(which):NULL;}
static BenderEGLDisplay bender_eglGetCurrentDisplay(void){if(g_bender_sdl_ready){BenderEGLDisplay r=g_bender_egl_current?BENDER_EGL_DISPLAY_COOKIE:NULL;logmsg("[BENDER_EGL][GET_CURRENT_DISPLAY][SDL] guest_current=%d -> %p\n",g_bender_egl_current,r);return r;}typedef BenderEGLDisplay(*Fn)(void);Fn f=(Fn)bender_host_egl_sym("eglGetCurrentDisplay");return f?f():NULL;}
static BenderEGLint bender_eglGetError(void){if(g_bender_sdl_ready){BenderEGLint e=g_bender_egl_error;g_bender_egl_error=BENDER_EGL_SUCCESS;return e;}return bender_host_egl_error();}
/* V2.7.29: GLES Render Inspector.
 * Unity obtains most GLES entrypoints dynamically. Previous logs only proved symbol
 * resolution because raw Mali pointers were returned directly. These wrappers always
 * forward to the real GLES function and only collect counters/state.
 */
typedef void (*BenderGLClearFn)(unsigned int);
typedef void (*BenderGLClearColorFn)(float,float,float,float);
typedef void (*BenderGLViewportFn)(int,int,int,int);
typedef void (*BenderGLBindFramebufferFn)(unsigned int,unsigned int);
typedef void (*BenderGLUseProgramFn)(unsigned int);
typedef void (*BenderGLDrawArraysFn)(unsigned int,int,int);
typedef void (*BenderGLDrawElementsFn)(unsigned int,int,unsigned int,const void*);
typedef void (*BenderGLDrawArraysInstancedFn)(unsigned int,int,int,int);
typedef void (*BenderGLDrawElementsInstancedFn)(unsigned int,int,unsigned int,const void*,int);
typedef void (*BenderGLDrawElementsBaseVertexFn)(unsigned int,int,unsigned int,const void*,int);
typedef void (*BenderGLDrawElementsInstancedBaseVertexFn)(unsigned int,int,unsigned int,const void*,int,int);
typedef void (*BenderGLDrawArraysIndirectFn)(unsigned int,const void*);
typedef void (*BenderGLDrawElementsIndirectFn)(unsigned int,unsigned int,const void*);
typedef void (*BenderGLBlitFramebufferFn)(int,int,int,int,int,int,int,int,unsigned int,unsigned int);
typedef void (*BenderGLFramebufferTexture2DFn)(unsigned int,unsigned int,unsigned int,unsigned int,int);
typedef void (*BenderGLGetIntegervFn)(unsigned int,int*);
typedef void (*BenderGLReadPixelsFn)(int,int,int,int,unsigned int,unsigned int,void*);

static unsigned long g_ri_clear_calls,g_ri_clear_color_calls,g_ri_viewport_calls;
static unsigned long g_ri_bind_fbo_calls,g_ri_bind_fbo_zero_calls,g_ri_use_program_calls;
static unsigned long g_ri_draw_arrays_calls,g_ri_draw_elements_calls,g_ri_draw_instanced_calls,g_ri_draw_indirect_calls;
static unsigned long g_ri_blit_calls,g_ri_fbo_attach_calls;
static unsigned int g_ri_last_fbo;
static int g_ri_viewport[4];
static float g_ri_clear_color[4];
static int g_ri_announced;
static int bender_render_inspector_enabled(void){const char*e=getenv("BENDER_RENDER_INSPECT");if(!e||!*e)return 1;return !(e[0]=='0'||e[0]=='n'||e[0]=='N'||e[0]=='f'||e[0]=='F');}
static void*bender_gl_real_proc(const char*name){void*p=NULL;if(g_sdl_GL_GetProcAddress)p=g_sdl_GL_GetProcAddress(name);if(!p)p=dlsym(RTLD_DEFAULT,name);return p;}


/* V2.7.68: optional default-framebuffer render scale.
 * Guest-visible Android/EGL surface remains the physical 640x480. Only rendering
 * directed at logical framebuffer 0 is redirected to a 75% offscreen FBO and
 * upscaled to the real framebuffer at swap. FIX2 may expose a 480x360 guest surface and map synthetic input.
 */
#define BENDER_RS_GL_FRAMEBUFFER 0x8D40u
#define BENDER_RS_GL_READ_FRAMEBUFFER 0x8CA8u
#define BENDER_RS_GL_DRAW_FRAMEBUFFER 0x8CA9u
#define BENDER_RS_GL_FRAMEBUFFER_BINDING 0x8CA6u
#define BENDER_RS_GL_READ_FRAMEBUFFER_BINDING 0x8CAAu
#define BENDER_RS_GL_VIEWPORT 0x0BA2u
#define BENDER_RS_GL_SCISSOR_BOX 0x0C10u
#define BENDER_RS_GL_SCISSOR_TEST 0x0C11u
#define BENDER_RS_GL_TEXTURE_2D 0x0DE1u
#define BENDER_RS_GL_RGBA 0x1908u
#define BENDER_RS_GL_UNSIGNED_BYTE 0x1401u
#define BENDER_RS_GL_TEXTURE_MIN_FILTER 0x2801u
#define BENDER_RS_GL_TEXTURE_MAG_FILTER 0x2800u
#define BENDER_RS_GL_TEXTURE_WRAP_S 0x2802u
#define BENDER_RS_GL_TEXTURE_WRAP_T 0x2803u
#define BENDER_RS_GL_LINEAR 0x2601u
#define BENDER_RS_GL_CLAMP_TO_EDGE 0x812Fu
#define BENDER_RS_GL_COLOR_ATTACHMENT0 0x8CE0u
#define BENDER_RS_GL_RENDERBUFFER 0x8D41u
#define BENDER_RS_GL_DEPTH24_STENCIL8 0x88F0u
#define BENDER_RS_GL_DEPTH_STENCIL_ATTACHMENT 0x821Au
#define BENDER_RS_GL_FRAMEBUFFER_COMPLETE 0x8CD5u
#define BENDER_RS_GL_COLOR_BUFFER_BIT 0x00004000u
#define BENDER_RS_GL_NO_ERROR 0u

typedef void (*BenderRSGenFramebuffersFn)(int,unsigned int*);
typedef void (*BenderRSDeleteFramebuffersFn)(int,const unsigned int*);
typedef void (*BenderRSGenTexturesFn)(int,unsigned int*);
typedef void (*BenderRSDeleteTexturesFn)(int,const unsigned int*);
typedef void (*BenderRSBindTextureFn)(unsigned int,unsigned int);
typedef void (*BenderRSTexParameteriFn)(unsigned int,unsigned int,int);
typedef void (*BenderRSTexImage2DFn)(unsigned int,int,int,int,int,int,unsigned int,unsigned int,const void*);
typedef void (*BenderRSGenRenderbuffersFn)(int,unsigned int*);
typedef void (*BenderRSDeleteRenderbuffersFn)(int,const unsigned int*);
typedef void (*BenderRSBindRenderbufferFn)(unsigned int,unsigned int);
typedef void (*BenderRSRenderbufferStorageFn)(unsigned int,unsigned int,int,int);
typedef void (*BenderRSFramebufferRenderbufferFn)(unsigned int,unsigned int,unsigned int,unsigned int);
typedef unsigned int (*BenderRSCheckFramebufferStatusFn)(unsigned int);
typedef void (*BenderRSScissorFn)(int,int,int,int);
typedef unsigned int (*BenderRSGetErrorFn)(void);
typedef unsigned char (*BenderRSIsEnabledFn)(unsigned int);
typedef void (*BenderRSEnableDisableFn)(unsigned int);

static int g_bender_rs_requested_2768;
static int g_bender_rs_active_2768;
static int g_bender_rs_failed_2768;
static int g_bender_rs_percent_2768=75;
static int g_bender_rs_physical_w_2768=640,g_bender_rs_physical_h_2768=480;
static int g_bender_rs_render_w_2768=480,g_bender_rs_render_h_2768=360;
static unsigned int g_bender_rs_fbo_2768,g_bender_rs_color_2768,g_bender_rs_depthstencil_2768;
static unsigned int g_bender_rs_logical_draw_fbo_2768,g_bender_rs_logical_read_fbo_2768;
static int g_bender_rs_logical_viewport_2768[4]={0,0,640,480};
static int g_bender_rs_logical_scissor_2768[4]={0,0,640,480};
static unsigned long g_bender_rs_present_count_2768;

static int bender_render_scale_env_on_2768(void){
    const char*e=getenv("BENDER_RENDER_SCALE_75");
    if(!e||!*e)return 0;
    return !(e[0]=='0'||e[0]=='n'||e[0]=='N'||e[0]=='f'||e[0]=='F');
}
static int bender_render_scale_enabled_2768(void){return g_bender_rs_active_2768;}
static int bender_render_scale_guest_width_2768(void){
    if(g_bender_rs_active_2768)return g_bender_rs_render_w_2768;
    if(!g_bender_rs_failed_2768&&bender_render_scale_env_on_2768())return 480;
    return BENDER_DISPLAY_WIDTH;
}
static int bender_render_scale_guest_height_2768(void){
    if(g_bender_rs_active_2768)return g_bender_rs_render_h_2768;
    if(!g_bender_rs_failed_2768&&bender_render_scale_env_on_2768())return 360;
    return BENDER_DISPLAY_HEIGHT;
}
static void bender_render_scale_map_input_2768(float*x,float*y){
    if(!x||!y||!g_bender_rs_active_2768)return;
    int gw=g_bender_rs_render_w_2768,gh=g_bender_rs_render_h_2768;
    int pw=g_bender_rs_physical_w_2768>0?g_bender_rs_physical_w_2768:BENDER_DISPLAY_WIDTH;
    int ph=g_bender_rs_physical_h_2768>0?g_bender_rs_physical_h_2768:BENDER_DISPLAY_HEIGHT;
    if(pw>0&&gw>0)*x=(*x)*(float)gw/(float)pw;
    if(ph>0&&gh>0)*y=(*y)*(float)gh/(float)ph;
    if(*x<0.0f)*x=0.0f;
    if(*y<0.0f)*y=0.0f;
    if(gw>0&&*x>(float)(gw-1))*x=(float)(gw-1);
    if(gh>0&&*y>(float)(gh-1))*y=(float)(gh-1);
}
static int bender_rs_scale_coord_2768(int v,int dst,int src){
    if(src<=0)return v;
    long long n=(long long)v*(long long)dst;
    if(n>=0)n+=(long long)src/2ll;else n-=(long long)src/2ll;
    return (int)(n/(long long)src);
}
static int bender_rs_scale_size_2768(int v,int dst,int src){
    if(v<=0||src<=0)return v;
    long long n=(long long)v*(long long)dst+(long long)src/2ll;
    int r=(int)(n/(long long)src);return r<1?1:r;
}
static unsigned int bender_rs_actual_fbo_2768(unsigned int logical){return logical==0u?g_bender_rs_fbo_2768:logical;}
static void bender_rs_cleanup_2768(void){
    BenderGLBindFramebufferFn bf=(BenderGLBindFramebufferFn)bender_gl_real_proc("glBindFramebuffer");
    BenderRSDeleteFramebuffersFn df=(BenderRSDeleteFramebuffersFn)bender_gl_real_proc("glDeleteFramebuffers");
    BenderRSDeleteTexturesFn dt=(BenderRSDeleteTexturesFn)bender_gl_real_proc("glDeleteTextures");
    BenderRSDeleteRenderbuffersFn dr=(BenderRSDeleteRenderbuffersFn)bender_gl_real_proc("glDeleteRenderbuffers");
    if(bf)bf(BENDER_RS_GL_FRAMEBUFFER,0u);
    if(g_bender_rs_depthstencil_2768&&dr)dr(1,&g_bender_rs_depthstencil_2768);
    if(g_bender_rs_color_2768&&dt)dt(1,&g_bender_rs_color_2768);
    if(g_bender_rs_fbo_2768&&df)df(1,&g_bender_rs_fbo_2768);
    g_bender_rs_depthstencil_2768=0;g_bender_rs_color_2768=0;g_bender_rs_fbo_2768=0;
}
static void bender_rs_fallback_2768(const char*reason,unsigned int detail){
    int was=g_bender_rs_active_2768;g_bender_rs_active_2768=0;g_bender_rs_failed_2768=1;bender_rs_cleanup_2768();
    logmsg("[BENDER_RENDER_SCALE][FALLBACK] requested=75%% reason=%s detail=0x%x previous_active=%d output=%dx%d action=physical-default-framebuffer\n",reason?reason:"unknown",detail,was,g_bender_rs_physical_w_2768,g_bender_rs_physical_h_2768);
}
static void bender_render_scale_init_2768(void){
    g_bender_rs_requested_2768=bender_render_scale_env_on_2768();g_bender_rs_failed_2768=0;
    if(!g_bender_rs_requested_2768){logmsg("[BENDER_RENDER_SCALE][OFF] percent=75 output=%dx%d\n",g_bender_sdl_draw_w,g_bender_sdl_draw_h);return;}
    g_bender_rs_physical_w_2768=g_bender_sdl_draw_w>0?g_bender_sdl_draw_w:640;
    g_bender_rs_physical_h_2768=g_bender_sdl_draw_h>0?g_bender_sdl_draw_h:480;
    g_bender_rs_render_w_2768=(g_bender_rs_physical_w_2768*g_bender_rs_percent_2768+50)/100;
    g_bender_rs_render_h_2768=(g_bender_rs_physical_h_2768*g_bender_rs_percent_2768+50)/100;
    if(g_bender_rs_render_w_2768<1||g_bender_rs_render_h_2768<1){bender_rs_fallback_2768("invalid-size",0u);return;}
    if(g_bender_sdl_gl_major<3){bender_rs_fallback_2768("gles3-required",(unsigned)g_bender_sdl_gl_major);return;}
    BenderRSGenFramebuffersFn genfb=(BenderRSGenFramebuffersFn)bender_gl_real_proc("glGenFramebuffers");
    BenderGLBindFramebufferFn bindfb=(BenderGLBindFramebufferFn)bender_gl_real_proc("glBindFramebuffer");
    BenderRSGenTexturesFn gent=(BenderRSGenTexturesFn)bender_gl_real_proc("glGenTextures");
    BenderRSBindTextureFn bindt=(BenderRSBindTextureFn)bender_gl_real_proc("glBindTexture");
    BenderRSTexParameteriFn texp=(BenderRSTexParameteriFn)bender_gl_real_proc("glTexParameteri");
    BenderRSTexImage2DFn texi=(BenderRSTexImage2DFn)bender_gl_real_proc("glTexImage2D");
    BenderGLFramebufferTexture2DFn fbt=(BenderGLFramebufferTexture2DFn)bender_gl_real_proc("glFramebufferTexture2D");
    BenderRSGenRenderbuffersFn genrb=(BenderRSGenRenderbuffersFn)bender_gl_real_proc("glGenRenderbuffers");
    BenderRSBindRenderbufferFn bindrb=(BenderRSBindRenderbufferFn)bender_gl_real_proc("glBindRenderbuffer");
    BenderRSRenderbufferStorageFn rbs=(BenderRSRenderbufferStorageFn)bender_gl_real_proc("glRenderbufferStorage");
    BenderRSFramebufferRenderbufferFn fbr=(BenderRSFramebufferRenderbufferFn)bender_gl_real_proc("glFramebufferRenderbuffer");
    BenderRSCheckFramebufferStatusFn chk=(BenderRSCheckFramebufferStatusFn)bender_gl_real_proc("glCheckFramebufferStatus");
    BenderGLBlitFramebufferFn blit=(BenderGLBlitFramebufferFn)bender_gl_real_proc("glBlitFramebuffer");
    BenderRSGetErrorFn ge=(BenderRSGetErrorFn)bender_gl_real_proc("glGetError");
    if(!genfb||!bindfb||!gent||!bindt||!texp||!texi||!fbt||!genrb||!bindrb||!rbs||!fbr||!chk||!blit){bender_rs_fallback_2768("missing-gles3-proc",0u);return;}
    while(ge&&ge()!=BENDER_RS_GL_NO_ERROR){}
    genfb(1,&g_bender_rs_fbo_2768);bindfb(BENDER_RS_GL_FRAMEBUFFER,g_bender_rs_fbo_2768);
    gent(1,&g_bender_rs_color_2768);bindt(BENDER_RS_GL_TEXTURE_2D,g_bender_rs_color_2768);
    texp(BENDER_RS_GL_TEXTURE_2D,BENDER_RS_GL_TEXTURE_MIN_FILTER,(int)BENDER_RS_GL_LINEAR);
    texp(BENDER_RS_GL_TEXTURE_2D,BENDER_RS_GL_TEXTURE_MAG_FILTER,(int)BENDER_RS_GL_LINEAR);
    texp(BENDER_RS_GL_TEXTURE_2D,BENDER_RS_GL_TEXTURE_WRAP_S,(int)BENDER_RS_GL_CLAMP_TO_EDGE);
    texp(BENDER_RS_GL_TEXTURE_2D,BENDER_RS_GL_TEXTURE_WRAP_T,(int)BENDER_RS_GL_CLAMP_TO_EDGE);
    texi(BENDER_RS_GL_TEXTURE_2D,0,(int)BENDER_RS_GL_RGBA,g_bender_rs_render_w_2768,g_bender_rs_render_h_2768,0,BENDER_RS_GL_RGBA,BENDER_RS_GL_UNSIGNED_BYTE,NULL);
    fbt(BENDER_RS_GL_FRAMEBUFFER,BENDER_RS_GL_COLOR_ATTACHMENT0,BENDER_RS_GL_TEXTURE_2D,g_bender_rs_color_2768,0);
    genrb(1,&g_bender_rs_depthstencil_2768);bindrb(BENDER_RS_GL_RENDERBUFFER,g_bender_rs_depthstencil_2768);
    rbs(BENDER_RS_GL_RENDERBUFFER,BENDER_RS_GL_DEPTH24_STENCIL8,g_bender_rs_render_w_2768,g_bender_rs_render_h_2768);
    fbr(BENDER_RS_GL_FRAMEBUFFER,BENDER_RS_GL_DEPTH_STENCIL_ATTACHMENT,BENDER_RS_GL_RENDERBUFFER,g_bender_rs_depthstencil_2768);
    unsigned int status=chk(BENDER_RS_GL_FRAMEBUFFER);unsigned int err=ge?ge():0u;
    if(status!=BENDER_RS_GL_FRAMEBUFFER_COMPLETE||err!=BENDER_RS_GL_NO_ERROR){bender_rs_fallback_2768("fbo-incomplete",status?status:err);return;}
    g_bender_rs_logical_draw_fbo_2768=0u;g_bender_rs_logical_read_fbo_2768=0u;
    g_bender_rs_logical_viewport_2768[0]=0;g_bender_rs_logical_viewport_2768[1]=0;g_bender_rs_logical_viewport_2768[2]=g_bender_rs_render_w_2768;g_bender_rs_logical_viewport_2768[3]=g_bender_rs_render_h_2768;
    g_bender_rs_logical_scissor_2768[0]=0;g_bender_rs_logical_scissor_2768[1]=0;g_bender_rs_logical_scissor_2768[2]=g_bender_rs_render_w_2768;g_bender_rs_logical_scissor_2768[3]=g_bender_rs_render_h_2768;
    BenderGLViewportFn vp=(BenderGLViewportFn)bender_gl_real_proc("glViewport");BenderRSScissorFn sc=(BenderRSScissorFn)bender_gl_real_proc("glScissor");
    if(vp){
        vp(0,0,g_bender_rs_render_w_2768,g_bender_rs_render_h_2768);
    }
    if(sc){
        sc(0,0,g_bender_rs_render_w_2768,g_bender_rs_render_h_2768);
    }
    g_bender_rs_active_2768=1;
    logmsg("[BENDER_RENDER_SCALE][ACTIVE] percent=75 logical=%dx%d render=%dx%d output=%dx%d fbo=%u color=%u depthstencil=%u upscale=glBlitFramebuffer-linear guest_surface=render-size input_map=physical-to-guest fallback=automatic\n",g_bender_rs_render_w_2768,g_bender_rs_render_h_2768,g_bender_rs_render_w_2768,g_bender_rs_render_h_2768,g_bender_rs_physical_w_2768,g_bender_rs_physical_h_2768,g_bender_rs_fbo_2768,g_bender_rs_color_2768,g_bender_rs_depthstencil_2768);
}
static void bender_rs_glScissor_2768(int x,int y,int w,int h){
    BenderRSScissorFn f=(BenderRSScissorFn)bender_gl_real_proc("glScissor");
    g_bender_rs_logical_scissor_2768[0]=x;g_bender_rs_logical_scissor_2768[1]=y;g_bender_rs_logical_scissor_2768[2]=w;g_bender_rs_logical_scissor_2768[3]=h;
    if(!f)return;
    if(g_bender_rs_active_2768&&g_bender_rs_logical_draw_fbo_2768==0u){if(w<=g_bender_rs_render_w_2768&&h<=g_bender_rs_render_h_2768)f(x,y,w,h);else f(bender_rs_scale_coord_2768(x,g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768),bender_rs_scale_coord_2768(y,g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768),bender_rs_scale_size_2768(w,g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768),bender_rs_scale_size_2768(h,g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768));}
    else f(x,y,w,h);
}
static void bender_rs_glGetIntegerv_2768(unsigned int pname,int*out){
    BenderGLGetIntegervFn f=(BenderGLGetIntegervFn)bender_gl_real_proc("glGetIntegerv");if(!out)return;
    if(g_bender_rs_active_2768){
        if(pname==BENDER_RS_GL_FRAMEBUFFER_BINDING){*out=(int)g_bender_rs_logical_draw_fbo_2768;return;}
        if(pname==BENDER_RS_GL_READ_FRAMEBUFFER_BINDING){*out=(int)g_bender_rs_logical_read_fbo_2768;return;}
        if(pname==BENDER_RS_GL_VIEWPORT){for(int i=0;i<4;i++)out[i]=g_bender_rs_logical_viewport_2768[i];return;}
        if(pname==BENDER_RS_GL_SCISSOR_BOX){for(int i=0;i<4;i++)out[i]=g_bender_rs_logical_scissor_2768[i];return;}
    }
    if(f)f(pname,out);
}
static void bender_render_scale_present_2768(void){
    if(!g_bender_rs_active_2768)return;
    BenderGLBindFramebufferFn bf=(BenderGLBindFramebufferFn)bender_gl_real_proc("glBindFramebuffer");BenderGLBlitFramebufferFn blit=(BenderGLBlitFramebufferFn)bender_gl_real_proc("glBlitFramebuffer");BenderGLViewportFn vp=(BenderGLViewportFn)bender_gl_real_proc("glViewport");BenderRSScissorFn sc=(BenderRSScissorFn)bender_gl_real_proc("glScissor");BenderRSGetErrorFn ge=(BenderRSGetErrorFn)bender_gl_real_proc("glGetError");BenderRSIsEnabledFn isen=(BenderRSIsEnabledFn)bender_gl_real_proc("glIsEnabled");BenderRSEnableDisableFn dis=(BenderRSEnableDisableFn)bender_gl_real_proc("glDisable");BenderRSEnableDisableFn en=(BenderRSEnableDisableFn)bender_gl_real_proc("glEnable");
    if(!bf||!blit||!vp){bender_rs_fallback_2768("present-proc-missing",0u);return;}
    while(ge&&ge()!=BENDER_RS_GL_NO_ERROR){}
    bf(BENDER_RS_GL_READ_FRAMEBUFFER,g_bender_rs_fbo_2768);bf(BENDER_RS_GL_DRAW_FRAMEBUFFER,0u);
    int was_scissor=isen?isen(BENDER_RS_GL_SCISSOR_TEST):0;
    if(was_scissor&&dis){dis(BENDER_RS_GL_SCISSOR_TEST);}
    vp(0,0,g_bender_rs_physical_w_2768,g_bender_rs_physical_h_2768);
    if(sc){sc(0,0,g_bender_rs_physical_w_2768,g_bender_rs_physical_h_2768);}
    blit(0,0,g_bender_rs_render_w_2768,g_bender_rs_render_h_2768,0,0,g_bender_rs_physical_w_2768,g_bender_rs_physical_h_2768,BENDER_RS_GL_COLOR_BUFFER_BIT,BENDER_RS_GL_LINEAR);
    if(was_scissor&&en){en(BENDER_RS_GL_SCISSOR_TEST);}
    unsigned int err=ge?ge():0u;if(err!=BENDER_RS_GL_NO_ERROR){bender_rs_fallback_2768("blit-error",err);return;}
    bf(BENDER_RS_GL_FRAMEBUFFER,0u);
    unsigned long n=++g_bender_rs_present_count_2768;if(n<=10ul||n%300ul==0ul)logmsg("[BENDER_RENDER_SCALE][PRESENT] frame=%lu render=%dx%d output=%dx%d filter=linear\n",n,g_bender_rs_render_w_2768,g_bender_rs_render_h_2768,g_bender_rs_physical_w_2768,g_bender_rs_physical_h_2768);
}
static void bender_render_scale_resume_2768(void){
    if(!g_bender_rs_active_2768)return;
    BenderGLBindFramebufferFn bf=(BenderGLBindFramebufferFn)bender_gl_real_proc("glBindFramebuffer");BenderGLViewportFn vp=(BenderGLViewportFn)bender_gl_real_proc("glViewport");BenderRSScissorFn sc=(BenderRSScissorFn)bender_gl_real_proc("glScissor");if(!bf)return;
    if(g_bender_rs_logical_draw_fbo_2768==g_bender_rs_logical_read_fbo_2768)bf(BENDER_RS_GL_FRAMEBUFFER,bender_rs_actual_fbo_2768(g_bender_rs_logical_draw_fbo_2768));else{bf(BENDER_RS_GL_READ_FRAMEBUFFER,bender_rs_actual_fbo_2768(g_bender_rs_logical_read_fbo_2768));bf(BENDER_RS_GL_DRAW_FRAMEBUFFER,bender_rs_actual_fbo_2768(g_bender_rs_logical_draw_fbo_2768));}
    if(vp){if(g_bender_rs_logical_draw_fbo_2768==0u){if(g_bender_rs_logical_viewport_2768[2]<=g_bender_rs_render_w_2768&&g_bender_rs_logical_viewport_2768[3]<=g_bender_rs_render_h_2768)vp(g_bender_rs_logical_viewport_2768[0],g_bender_rs_logical_viewport_2768[1],g_bender_rs_logical_viewport_2768[2],g_bender_rs_logical_viewport_2768[3]);else vp(bender_rs_scale_coord_2768(g_bender_rs_logical_viewport_2768[0],g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768),bender_rs_scale_coord_2768(g_bender_rs_logical_viewport_2768[1],g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768),bender_rs_scale_size_2768(g_bender_rs_logical_viewport_2768[2],g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768),bender_rs_scale_size_2768(g_bender_rs_logical_viewport_2768[3],g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768));}else vp(g_bender_rs_logical_viewport_2768[0],g_bender_rs_logical_viewport_2768[1],g_bender_rs_logical_viewport_2768[2],g_bender_rs_logical_viewport_2768[3]);}
    if(sc){if(g_bender_rs_logical_draw_fbo_2768==0u){if(g_bender_rs_logical_scissor_2768[2]<=g_bender_rs_render_w_2768&&g_bender_rs_logical_scissor_2768[3]<=g_bender_rs_render_h_2768)sc(g_bender_rs_logical_scissor_2768[0],g_bender_rs_logical_scissor_2768[1],g_bender_rs_logical_scissor_2768[2],g_bender_rs_logical_scissor_2768[3]);else sc(bender_rs_scale_coord_2768(g_bender_rs_logical_scissor_2768[0],g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768),bender_rs_scale_coord_2768(g_bender_rs_logical_scissor_2768[1],g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768),bender_rs_scale_size_2768(g_bender_rs_logical_scissor_2768[2],g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768),bender_rs_scale_size_2768(g_bender_rs_logical_scissor_2768[3],g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768));}else sc(g_bender_rs_logical_scissor_2768[0],g_bender_rs_logical_scissor_2768[1],g_bender_rs_logical_scissor_2768[2],g_bender_rs_logical_scissor_2768[3]);}
}

/* ---------------- BENDER Unity input test layer V2.7.41 ---------------- */
static int64_t bender_input_now_ms(void){struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return 0;return (int64_t)ts.tv_sec*1000ll+(int64_t)(ts.tv_nsec/1000000ll);}
static int bender_input_button(int idx){if(g_bender_input_controller&&g_sdl_GameControllerGetButton)return g_sdl_GameControllerGetButton(g_bender_input_controller,idx)?1:0;return 0;}
static int bender_input_raw_button(int idx){if(g_bender_input_joystick&&g_sdl_JoystickGetButton&&idx>=0){int n=g_sdl_JoystickNumButtons?g_sdl_JoystickNumButtons(g_bender_input_joystick):0;if(idx<n)return g_sdl_JoystickGetButton(g_bender_input_joystick,idx)?1:0;}return 0;}
static int bender_input_axis(int idx){if(g_bender_input_controller&&g_sdl_GameControllerGetAxis)return (int)g_sdl_GameControllerGetAxis(g_bender_input_controller,idx);return 0;}
static void bender_input_init(void){
    if(g_bender_input_init_attempted){return;} g_bender_input_init_attempted=1;
    const char*fr=getenv("BENDER_FN_RAW_BUTTON");if(fr&&*fr){long v=strtol(fr,NULL,10);if(v>=0&&v<64)g_bender_input_fn_raw_button=(int)v;}
    if(!g_sdl_InitSubSystem||!g_sdl_NumJoysticks){logmsg("[BENDER_INPUT][INIT][SKIP] SDL controller symbols unavailable\n");return;}
    int rc=g_sdl_InitSubSystem(BENDER_SDL_INIT_JOYSTICK|BENDER_SDL_INIT_GAMECONTROLLER|BENDER_SDL_INIT_EVENTS);
    int n=g_sdl_NumJoysticks();
    for(int i=0;i<n&&!g_bender_input_controller;i++)if(g_sdl_IsGameController&&g_sdl_IsGameController(i)&&g_sdl_GameControllerOpen)g_bender_input_controller=g_sdl_GameControllerOpen(i);
    if(n>0&&g_sdl_JoystickOpen)g_bender_input_joystick=g_sdl_JoystickOpen(0);
    const char*cn=(g_bender_input_controller&&g_sdl_GameControllerName)?g_sdl_GameControllerName(g_bender_input_controller):NULL;
    const char*jn=(g_bender_input_joystick&&g_sdl_JoystickName)?g_sdl_JoystickName(g_bender_input_joystick):NULL;
    g_bender_input_ready=(g_bender_input_controller||g_bender_input_joystick)?1:0;
    logmsg("[BENDER_INPUT][INIT] rc=%d joysticks=%d controller=%p name=%s raw=%p raw_name=%s fn=guide|raw%d nativeInjectEvent=%p scheme=FN+UP-keyboard,FN+DOWN-mouse,FN+LEFT-sticktouch,FN+L3-directtouch,FN+RIGHT-keymap\n",rc,n,g_bender_input_controller,cn?cn:"(none)",g_bender_input_joystick,jn?jn:"(none)",g_bender_input_fn_raw_button,g_unity_native_inject_event_fn);
}
static BObj*bender_input_event_snapshot(const char*cls,const BObj*src){
    pthread_mutex_lock(&g_jni_handle_lock);
    BObj*o=&g_bender_input_event_ring[g_bender_input_event_ring_next++%BENDER_INPUT_EVENT_RING_SIZE];
    if(src){*o=*src;}else{memset(o,0,sizeof(*o));}
    o->kind=4;
    if(cls&&*cls){snprintf(o->a,sizeof(o->a),"%s",cls);}
    pthread_mutex_unlock(&g_jni_handle_lock);
    return o;
}
static BObj*bender_input_event_clone(void*src,const char*expected){
    BObj*bo=bender_known_bobj(src);
    if(!bo||bo->kind!=4){logmsg("[BENDER_INPUT][CLONE][REJECT] src=%p expected=%s\n",src,expected?expected:"?");return NULL;}
    const char*cls=(expected&&*expected)?expected:bo->a;
    BObj*copy=bender_input_event_snapshot(cls,bo);
    if(copy){logmsg("[BENDER_INPUT][CLONE] src=%p dst=%p class=%s action=%lu source=0x%x x=%.1f y=%.1f\n",src,(void*)copy,copy->a,(unsigned long)copy->ref1,(unsigned)copy->flags,(double)copy->f0,(double)copy->f1);}
    return copy;
}
static BObj*bender_input_event_obj(const char*cls,BObj**slot){(void)slot;return bender_input_event_snapshot(cls,NULL);}
static int bender_input_inject_key(int action,int keycode,const char*source){
    if(!g_unity_native_inject_event_fn||!g_unity_player_class){return 0;} BObj*e=bender_input_event_obj("android/view/KeyEvent",&g_bender_input_key_event);if(!e)return 0;int64_t now=bender_input_now_ms();if(action==0||!g_bender_input_key_down_time_ms)g_bender_input_key_down_time_ms=now;e->ref0=(uintptr_t)action;e->ref1=(uintptr_t)keycode;e->i64=now;e->i64b=g_bender_input_key_down_time_ms;e->f0=e->f1=0.0f;e->flags=0x00000101u;
    bjboolean ok=bender_input_call_inject_fix68(e);unsigned long n=++g_bender_input_event_count;logmsg("[BENDER_INPUT][KEY] n=%lu event=%p action=%s keycode=%d source=%s result=%u\n",n,(void*)e,action?"UP":"DOWN",keycode,source?source:"?",(unsigned)ok);return ok?1:0;
}
static int bender_input_inject_motion(int action,float x,float y,const char*source){
    float physical_x=x,physical_y=y;bender_render_scale_map_input_2768(&x,&y);
    if(!g_unity_native_inject_event_fn||!g_unity_player_class){return 0;} BObj*e=bender_input_event_obj("android/view/MotionEvent",&g_bender_input_motion_event);if(!e)return 0;int64_t now=bender_input_now_ms();if(action==0||!g_bender_input_down_time_ms)g_bender_input_down_time_ms=now;e->ref0=0;e->ref1=(uintptr_t)action;e->i64=now;e->i64b=g_bender_input_down_time_ms;e->f0=x;e->f1=y;e->flags=0x00001002u;
    bjboolean ok=bender_input_call_inject_fix68(e);unsigned long n=++g_bender_input_event_count;if((action!=2||n<=20||n%60==0)&&g_bender_rs_active_2768)logmsg("[BENDER_RENDER_SCALE][INPUT_MAP] physical=%.0f,%.0f guest=%.0f,%.0f action=%d source=%s\n",(double)physical_x,(double)physical_y,(double)x,(double)y,action,source?source:"?");if(action!=2||n<=20||n%60==0)logmsg("[BENDER_INPUT][TOUCH] n=%lu event=%p action=%d x=%.0f y=%.0f source=%s result=%u\n",n,(void*)e,action,(double)x,(double)y,source?source:"?",(unsigned)ok);return ok?1:0;
}
static const char*bender_input_mode_title(const char*name){
    if(!name)return "MODO";
    if(!strcmp(name,"keyboard"))return "TECLADO";
    if(!strcmp(name,"mouse"))return "MOUSE";
    if(!strcmp(name,"touchmap"))return "TOQUE DIRETO";
    if(!strcmp(name,"keymap"))return "CONTROLE TECLAS";
    if(!strcmp(name,"sticktouch"))return "STICKS TOQUE";
    return "MODO";
}
static void bender_input_set_notice(const char*name,int enabled){
    snprintf(g_bender_input_notice,sizeof(g_bender_input_notice),"%s %s",bender_input_mode_title(name),enabled?"ATIVO":"DESATIVADO");
    g_bender_input_notice_until_ms=bender_input_now_ms()+3000ll;
    logmsg("[BENDER_INPUT][NOTICE] text=%s duration_ms=3000\n",g_bender_input_notice);
}
static int bender_input_notice_visible(void){return g_bender_input_notice[0]&&bender_input_now_ms()<g_bender_input_notice_until_ms;}
static void bender_input_toggle(const char*name,int*state){*state=!*state;bender_input_set_notice(name,*state);logmsg("[BENDER_INPUT][TOGGLE] %s=%s keyboard=%s mouse=%s touchmap=%s keymap=%s\n",name,*state?"ON":"OFF",g_bender_input_keyboard_active?"ON":"OFF",g_bender_input_mouse_active?"ON":"OFF",g_bender_input_touchmap_active?"ON":"OFF",g_bender_input_keymap_active?"ON":"OFF");}
#define BENDER_UNITY_KEYMAP_COUNT 24
static const char*g_bender_unity_keymap_names[BENDER_UNITY_KEYMAP_COUNT]={"A","B","X","Y","L1","R1","L2","R2","START","SELECT","L3","R3","DPAD_UP","DPAD_DOWN","DPAD_LEFT","DPAD_RIGHT","LS_LEFT","LS_RIGHT","LS_UP","LS_DOWN","RS_LEFT","RS_RIGHT","RS_UP","RS_DOWN"};
static int g_bender_unity_keymap[BENDER_UNITY_KEYMAP_COUNT]={66,111,62,61,45,33,0,0,0,0,0,0,19,20,21,22,29,32,51,47,0,0,0,0};
static int g_bender_unity_keymap_loaded;
static int g_bender_unity_keymap_reload_prev;
static void bender_cfg_trim_2744(char*s){char*p=s;while(*p==' '||*p=='\t')p++;if(p!=s)memmove(s,p,strlen(p)+1);size_t n=strlen(s);while(n&&(s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\r'||s[n-1]=='\n'))s[--n]=0;}
static void bender_unity_keymap_load_2744(void){char path[1200];snprintf(path,sizeof(path),"%s/UNITY_KEY_MAP.cfg",g_guest_app_dir);FILE*f=fopen(path,"r");if(!f){g_bender_unity_keymap_loaded=1;logmsg("[BENDER_KEYMAP][DEFAULT] file=%s not-found using-builtins\n",path);return;}char line[256];int count=0;while(fgets(line,sizeof(line),f)){bender_cfg_trim_2744(line);if(!line[0]||line[0]=='#')continue;char*eq=strchr(line,'=');if(!eq)continue;*eq++=0;bender_cfg_trim_2744(line);bender_cfg_trim_2744(eq);for(int i=0;i<BENDER_UNITY_KEYMAP_COUNT;i++){if(!strcmp(line,g_bender_unity_keymap_names[i])){g_bender_unity_keymap[i]=(int)strtol(eq,NULL,0);count++;break;}}}fclose(f);g_bender_unity_keymap_loaded=1;logmsg("[BENDER_KEYMAP][LOAD] file=%s bindings=%d\n",path,count);}
static int g_bender_mouse_speed_percent_2767=100;
static int g_bender_mouse_speed_loaded_2767;
static int g_bender_mouse_speed_prev_r1_2767;
static int g_bender_mouse_speed_prev_r2_2767;
static int bender_mouse_speed_clamp_2767(int v){if(v<50)return 50;if(v>400)return 400;return v;}
static void bender_mouse_speed_notice_2767(void){snprintf(g_bender_input_notice,sizeof(g_bender_input_notice),"MOUSE VEL %d",g_bender_mouse_speed_percent_2767);g_bender_input_notice_until_ms=bender_input_now_ms()+3000ll;}
static void bender_mouse_speed_save_2767(void){char path[1200];snprintf(path,sizeof(path),"%s/MOUSE_SPEED.cfg",g_guest_app_dir);FILE*f=fopen(path,"w");if(!f){logmsg("[BENDER_MOUSE_SPEED][SAVE][ERROR] file=%s errno=%d\n",path,errno);return;}fprintf(f,"# BENDER MOUSE SPEED V2.7.67\n# FN+R1 diminui 25%%; FN+R2 aumenta 25%%. Faixa: 50-400.\nSPEED_PERCENT=%d\n",g_bender_mouse_speed_percent_2767);fclose(f);logmsg("[BENDER_MOUSE_SPEED][SAVE] file=%s percent=%d\n",path,g_bender_mouse_speed_percent_2767);}
static void bender_mouse_speed_load_2767(void){if(g_bender_mouse_speed_loaded_2767)return;g_bender_mouse_speed_loaded_2767=1;char path[1200];snprintf(path,sizeof(path),"%s/MOUSE_SPEED.cfg",g_guest_app_dir);FILE*f=fopen(path,"r");if(!f){logmsg("[BENDER_MOUSE_SPEED][DEFAULT] file=%s percent=%d\n",path,g_bender_mouse_speed_percent_2767);return;}char line[256];while(fgets(line,sizeof(line),f)){bender_cfg_trim_2744(line);if(!line[0]||line[0]=='#')continue;char*eq=strchr(line,'=');if(!eq)continue;*eq++=0;bender_cfg_trim_2744(line);bender_cfg_trim_2744(eq);if(!strcmp(line,"SPEED_PERCENT")){g_bender_mouse_speed_percent_2767=bender_mouse_speed_clamp_2767((int)strtol(eq,NULL,10));}}fclose(f);logmsg("[BENDER_MOUSE_SPEED][LOAD] file=%s percent=%d\n",path,g_bender_mouse_speed_percent_2767);}
static int g_bender_fn_native_2173;
static int g_bender_fn_alias_2173;
static void bender_mouse_speed_hotkeys_2767(int fn){bender_mouse_speed_load_2767();int r1=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);int l2=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_TRIGGERLEFT)>16000;int r2=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>16000;int slow=g_bender_fn_native_2173?r1:(g_bender_fn_alias_2173?l2:0);int fast=fn?r2:0;if(fn&&g_bender_input_mouse_active){int changed=0;if(slow&&!g_bender_mouse_speed_prev_r1_2767){g_bender_mouse_speed_percent_2767=bender_mouse_speed_clamp_2767(g_bender_mouse_speed_percent_2767-25);changed=1;logmsg("[BENDER_MOUSE_SPEED][HOTKEY] key=%s action=slower percent=%d\n",g_bender_fn_alias_2173&&!g_bender_fn_native_2173?"L1+R1+L2":"FN+R1",g_bender_mouse_speed_percent_2767);}if(fast&&!g_bender_mouse_speed_prev_r2_2767){g_bender_mouse_speed_percent_2767=bender_mouse_speed_clamp_2767(g_bender_mouse_speed_percent_2767+25);changed=1;logmsg("[BENDER_MOUSE_SPEED][HOTKEY] key=%s action=faster percent=%d\n",g_bender_fn_alias_2173&&!g_bender_fn_native_2173?"L1+R1+R2":"FN+R2",g_bender_mouse_speed_percent_2767);}if(changed){bender_mouse_speed_save_2767();bender_mouse_speed_notice_2767();}}g_bender_mouse_speed_prev_r1_2767=(fn&&g_bender_input_mouse_active)?slow:0;g_bender_mouse_speed_prev_r2_2767=(fn&&g_bender_input_mouse_active)?fast:0;}
static void bender_input_key_edge(int button,int keycode,const char*name){int cur=bender_input_button(button);int prev=g_bender_input_prev_buttons[button]?1:0;if(cur!=prev)bender_input_inject_key(cur?0:1,keycode,name);g_bender_input_prev_buttons[button]=(unsigned char)cur;}

static char g_bender_soft_text_fix99[768];
static size_t g_bender_soft_text_len_fix99;
static int g_bender_keyboard_shift_fix99;
static int g_bender_soft_keyboard_prev_active_fix99=-1;
static int g_bender_keyboard_prev_x_fix99,g_bender_keyboard_prev_y_fix99,g_bender_keyboard_prev_start_fix99;

static int bender_keyboard_row_count(int row){static const int counts[5]={10,10,9,7,5};return (row>=0&&row<5)?counts[row]:1;}
static const char*bender_keyboard_label(int row,int col){
    static const char*r0[10]={"1","2","3","4","5","6","7","8","9","0"};
    static const char*r1[10]={"Q","W","E","R","T","Y","U","I","O","P"};
    static const char*r2[9]={"A","S","D","F","G","H","J","K","L"};
    static const char*r3[7]={"Z","X","C","V","B","N","M"};
    static const char*r4[5]={"SHIFT","SPACE","BACK","ENTER","CLOSE"};
    if(row==0&&col>=0&&col<10)return r0[col];
    if(row==1&&col>=0&&col<10)return r1[col];
    if(row==2&&col>=0&&col<9)return r2[col];
    if(row==3&&col>=0&&col<7)return r3[col];
    if(row==4&&col>=0&&col<5)return r4[col];
    return "?";
}
static int bender_keyboard_keycode(int row,int col){const char*l=bender_keyboard_label(row,col);if(!l||!*l)return 0;if(row==0){static const int d[10]={8,9,10,11,12,13,14,15,16,7};return d[col];}if(row>=1&&row<=3&&l[1]==0&&l[0]>='A'&&l[0]<='Z')return 29+(l[0]-'A');if(row==4){if(col==1)return 62;if(col==2)return 67;if(col==3)return 66;}return 0;}
static char bender_keyboard_char_fix99(int row,int col){
    const char*l=bender_keyboard_label(row,col);if(!l||!*l)return 0;
    if(row==0&&l[1]==0)return l[0];
    if(row>=1&&row<=3&&l[1]==0){char c=l[0];return g_bender_keyboard_shift_fix99?c:(char)(c+('a'-'A'));}
    if(row==4&&col==1)return ' ';
    return 0;
}
static BObj*bender_soft_jstring_prepare_fix99(void){
    if(!g_bender_soft_jstring_fix100){
        /* Allocate as a private non-internable object, then convert the reserved
           slot to kind=3.  BObj.b remains a private tag, preventing normal
           jni_NewStringUTF interning from ever aliasing this mutable string. */
        BObj*o=new_obj(4,"bender/soft/KeyboardStringFix100","fix100-private");
        if(!o){logmsg("[BENDER_SOFT_KEYBOARD] source=fix100 action=jstring-create error=pool-full\n");return NULL;}
        o->kind=3;
        bender_strlcpy(o->b,"fix100-private",sizeof(o->b));
        bender_jni_pin_object_2768(o);
        g_bender_soft_jstring_fix100=o;
        logmsg("[BENDER_SOFT_KEYBOARD] source=fix100 action=jstring-create object=%p pooled=1 pinned=1 private=1\n",(void*)o);
    }
    BObj*o=g_bender_soft_jstring_fix100;
    if(o->u16){free(o->u16);o->u16=NULL;o->u16_len=0;}
    size_t n=g_bender_soft_text_len_fix99;if(n>=sizeof(o->a))n=sizeof(o->a)-1u;
    if(n)memcpy(o->a,g_bender_soft_text_fix99,n);
    o->a[n]=0;
    o->kind=3;
    return o;
}
static int bender_soft_keyboard_available_fix99(void){return g_bender_set_input_string_fix99&&g_bender_soft_input_owner_fix99;}
static void bender_soft_keyboard_sync_fix99(const char*reason){
    if(!bender_soft_keyboard_available_fix99())return;
    BObj*soft_jstring_fix100=bender_soft_jstring_prepare_fix99();
    if(!soft_jstring_fix100)return;
    g_bender_set_input_string_fix99(&g_env,g_bender_soft_input_owner_fix99,soft_jstring_fix100);
    if(g_bender_set_input_selection_fix99){bjint cursor=(bjint)bender_utf8_utf16_len(g_bender_soft_text_fix99);g_bender_set_input_selection_fix99(&g_env,g_bender_soft_input_owner_fix99,cursor,0);}
    logmsg("[BENDER_SOFT_KEYBOARD] source=fix99 action=set-input-string reason=%s bytes=%zu chars=%d text_buffered=1\n",reason?reason:"type",g_bender_soft_text_len_fix99,(int)bender_utf8_utf16_len(g_bender_soft_text_fix99));
}
static void bender_soft_keyboard_set_visible_fix99(int visible,const char*reason){
    if(g_bender_set_keyboard_visible_fix99&&g_bender_soft_input_owner_fix99)g_bender_set_keyboard_visible_fix99(&g_env,g_bender_soft_input_owner_fix99,(bjboolean)(visible?1:0));
    logmsg("[BENDER_SOFT_KEYBOARD] source=fix99 action=visibility visible=%d reason=%s bridge=%d\n",visible,reason?reason:"state",bender_soft_keyboard_available_fix99()?1:0);
}
static void bender_soft_keyboard_finish_fix99(const char*reason){
    bender_soft_keyboard_set_visible_fix99(0,reason);
    if(g_bender_soft_input_closed_fix99&&g_bender_soft_input_owner_fix99)g_bender_soft_input_closed_fix99(&g_env,g_bender_soft_input_owner_fix99);
    logmsg("[BENDER_SOFT_KEYBOARD] source=fix99 action=soft-input-closed reason=%s bytes=%zu\n",reason?reason:"close",g_bender_soft_text_len_fix99);
}
static void bender_soft_keyboard_lifecycle_fix99(void){
    int active=g_bender_input_keyboard_active?1:0;
    if(g_bender_soft_keyboard_prev_active_fix99<0){g_bender_soft_keyboard_prev_active_fix99=active;if(active){g_bender_soft_text_len_fix99=0;g_bender_soft_text_fix99[0]=0;bender_soft_keyboard_set_visible_fix99(1,"initial-open");}return;}
    if(active==g_bender_soft_keyboard_prev_active_fix99)return;
    g_bender_soft_keyboard_prev_active_fix99=active;
    if(active){g_bender_soft_text_len_fix99=0;g_bender_soft_text_fix99[0]=0;g_bender_keyboard_shift_fix99=0;bender_soft_keyboard_set_visible_fix99(1,"open");}
    else bender_soft_keyboard_finish_fix99("close");
}
static void bender_keyboard_fallback_key_fix99(int keycode){if(keycode){bender_input_inject_key(0,keycode,"virtual-keyboard");bender_input_inject_key(1,keycode,"virtual-keyboard");}}
static void bender_keyboard_backspace_fix99(void){
    if(bender_soft_keyboard_available_fix99()){
        if(g_bender_soft_text_len_fix99){--g_bender_soft_text_len_fix99;g_bender_soft_text_fix99[g_bender_soft_text_len_fix99]=0;bender_soft_keyboard_sync_fix99("backspace");}
    }else bender_keyboard_fallback_key_fix99(67);
}
static void bender_keyboard_enter_fix99(void){
    if(bender_soft_keyboard_available_fix99()){bender_soft_keyboard_sync_fix99("enter");g_bender_input_keyboard_active=0;bender_soft_keyboard_finish_fix99("enter");g_bender_soft_keyboard_prev_active_fix99=0;}
    else bender_keyboard_fallback_key_fix99(66);
}
static void bender_keyboard_send_selected(void){
    int row=g_bender_keyboard_row,col=g_bender_keyboard_col,kc=bender_keyboard_keycode(row,col);const char*l=bender_keyboard_label(row,col);char ch=bender_keyboard_char_fix99(row,col);
    logmsg("[BENDER_KEYBOARD][TYPE] source=fix99 row=%d col=%d label=%s keycode=%d soft_bridge=%d\n",row,col,l,kc,bender_soft_keyboard_available_fix99()?1:0);
    if(row==4&&col==0){g_bender_keyboard_shift_fix99=!g_bender_keyboard_shift_fix99;logmsg("[BENDER_SOFT_KEYBOARD] source=fix99 action=shift enabled=%d\n",g_bender_keyboard_shift_fix99);return;}
    if(row==4&&col==2){bender_keyboard_backspace_fix99();return;}
    if(row==4&&col==3){bender_keyboard_enter_fix99();return;}
    if(row==4&&col==4){g_bender_input_keyboard_active=0;bender_input_set_notice("keyboard",0);return;}
    if(ch){
        if(bender_soft_keyboard_available_fix99()){
            if(g_bender_soft_text_len_fix99+1u<sizeof(g_bender_soft_text_fix99)){g_bender_soft_text_fix99[g_bender_soft_text_len_fix99++]=ch;g_bender_soft_text_fix99[g_bender_soft_text_len_fix99]=0;bender_soft_keyboard_sync_fix99("type");}
        }else bender_keyboard_fallback_key_fix99(kc);
    }else if(kc)bender_keyboard_fallback_key_fix99(kc);
}
static void bender_keyboard_poll(int up,int down,int left,int right){
    int a=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_A),b=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_B),x=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_X),y=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_Y),start=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_START);
    if(up&&!g_bender_keyboard_prev_up){g_bender_keyboard_row=(g_bender_keyboard_row+4)%5;int n=bender_keyboard_row_count(g_bender_keyboard_row);if(g_bender_keyboard_col>=n)g_bender_keyboard_col=n-1;}
    if(down&&!g_bender_keyboard_prev_down){g_bender_keyboard_row=(g_bender_keyboard_row+1)%5;int n=bender_keyboard_row_count(g_bender_keyboard_row);if(g_bender_keyboard_col>=n)g_bender_keyboard_col=n-1;}
    if(left&&!g_bender_keyboard_prev_left){int n=bender_keyboard_row_count(g_bender_keyboard_row);g_bender_keyboard_col=(g_bender_keyboard_col+n-1)%n;}
    if(right&&!g_bender_keyboard_prev_right){int n=bender_keyboard_row_count(g_bender_keyboard_row);g_bender_keyboard_col=(g_bender_keyboard_col+1)%n;}
    if(a&&!g_bender_keyboard_prev_a)bender_keyboard_send_selected();
    if(x&&!g_bender_keyboard_prev_x_fix99)bender_keyboard_backspace_fix99();
    if(y&&!g_bender_keyboard_prev_y_fix99){g_bender_keyboard_shift_fix99=!g_bender_keyboard_shift_fix99;logmsg("[BENDER_SOFT_KEYBOARD] source=fix99 action=shift-hotkey enabled=%d\n",g_bender_keyboard_shift_fix99);}
    if(start&&!g_bender_keyboard_prev_start_fix99)bender_keyboard_enter_fix99();
    if(b&&!g_bender_keyboard_prev_b){g_bender_input_keyboard_active=0;bender_input_set_notice("keyboard",0);logmsg("[BENDER_KEYBOARD][CLOSE] source=fix99 button=B\n");}
    if((up&&!g_bender_keyboard_prev_up)||(down&&!g_bender_keyboard_prev_down)||(left&&!g_bender_keyboard_prev_left)||(right&&!g_bender_keyboard_prev_right))logmsg("[BENDER_KEYBOARD][SELECT] source=fix99 row=%d col=%d label=%s\n",g_bender_keyboard_row,g_bender_keyboard_col,bender_keyboard_label(g_bender_keyboard_row,g_bender_keyboard_col));
    g_bender_keyboard_prev_up=up;g_bender_keyboard_prev_down=down;g_bender_keyboard_prev_left=left;g_bender_keyboard_prev_right=right;g_bender_keyboard_prev_a=a;g_bender_keyboard_prev_b=b;g_bender_keyboard_prev_x_fix99=x;g_bender_keyboard_prev_y_fix99=y;g_bender_keyboard_prev_start_fix99=start;
}
static int g_bender_native_prev_2744[16];static int g_bender_native_axes_2744[6];
static int g_bender_stick_touch_enabled_2744;static int g_bender_stick_touch_visible_2744=1;static int g_bender_stick_deadzone_2744=7000;static int g_bender_stick_cfg_loaded_2744;static int g_bender_stick_fn_l3_prev_2744,g_bender_stick_fn_r3_prev_2744,g_bender_stick_fn_left_prev_2869;
static float g_bender_stick_lcx_2744=140.0f,g_bender_stick_lcy_2744=360.0f,g_bender_stick_lr_2744=90.0f,g_bender_stick_rcx_2744=500.0f,g_bender_stick_rcy_2744=240.0f,g_bender_stick_rr_2744=110.0f;
static int g_bender_stick_prev_l_2744,g_bender_stick_prev_r_2744,g_bender_stick_primary_2744=-1;
/* V2.7.50: guarda a ultima coordenada realmente enviada por cada dedo.
   Ao soltar o analogico, ACTION_UP/ACTION_POINTER_UP acontece nesse ponto,
   nunca no centro da area. Isso elimina o arraste de retorno da camera. */
static float g_bender_stick_last_lx_2746=140.0f,g_bender_stick_last_ly_2746=360.0f;
static float g_bender_stick_last_rx_2746=500.0f,g_bender_stick_last_ry_2746=240.0f;
static float bender_norm_axis_2744(int v){if(v>-g_bender_stick_deadzone_2744&&v<g_bender_stick_deadzone_2744)return 0.0f;float f=(float)v/32767.0f;return f<-1.0f?-1.0f:(f>1.0f?1.0f:f);}
static int bender_bool_2744(const char*v,int d){if(!v||!*v)return d;return !(v[0]=='0'||v[0]=='n'||v[0]=='N'||v[0]=='f'||v[0]=='F');}
static void bender_stick_cfg_load_2744(void){char path[1200];snprintf(path,sizeof(path),"%s/STICK_TOUCH.cfg",g_guest_app_dir);FILE*f=fopen(path,"r");if(!f){g_bender_stick_cfg_loaded_2744=1;logmsg("[BENDER_STICK_TOUCH][DEFAULT] %s\n",path);return;}char line[256];while(fgets(line,sizeof(line),f)){bender_cfg_trim_2744(line);if(!line[0]||line[0]=='#')continue;char*eq=strchr(line,'=');if(!eq)continue;*eq++=0;bender_cfg_trim_2744(line);bender_cfg_trim_2744(eq);if(!strcmp(line,"ENABLED"))g_bender_stick_touch_enabled_2744=bender_bool_2744(eq,g_bender_stick_touch_enabled_2744);else if(!strcmp(line,"VISIBLE"))g_bender_stick_touch_visible_2744=bender_bool_2744(eq,g_bender_stick_touch_visible_2744);else if(!strcmp(line,"DEADZONE"))g_bender_stick_deadzone_2744=(int)strtol(eq,NULL,10);else if(!strcmp(line,"LEFT_CENTER"))sscanf(eq,"%f,%f",&g_bender_stick_lcx_2744,&g_bender_stick_lcy_2744);else if(!strcmp(line,"LEFT_RADIUS"))g_bender_stick_lr_2744=strtof(eq,NULL);else if(!strcmp(line,"RIGHT_CENTER"))sscanf(eq,"%f,%f",&g_bender_stick_rcx_2744,&g_bender_stick_rcy_2744);else if(!strcmp(line,"RIGHT_RADIUS"))g_bender_stick_rr_2744=strtof(eq,NULL);}fclose(f);g_bender_stick_cfg_loaded_2744=1;logmsg("[BENDER_STICK_TOUCH][LOAD] enabled=%d visible=%d deadzone=%d left=%.0f,%.0f,r%.0f right=%.0f,%.0f,r%.0f\n",g_bender_stick_touch_enabled_2744,g_bender_stick_touch_visible_2744,g_bender_stick_deadzone_2744,(double)g_bender_stick_lcx_2744,(double)g_bender_stick_lcy_2744,(double)g_bender_stick_lr_2744,(double)g_bender_stick_rcx_2744,(double)g_bender_stick_rcy_2744,(double)g_bender_stick_rr_2744);}
static int bender_input_inject_native_key_2744(int action,int keycode,const char*source){if(!g_unity_native_inject_event_fn||!g_unity_player_class)return 0;BObj*e=bender_input_event_obj("android/view/KeyEvent",&g_bender_input_key_event);if(!e)return 0;int64_t now=bender_input_now_ms();e->ref0=(uintptr_t)action;e->ref1=(uintptr_t)keycode;e->i64=now;e->i64b=now;e->flags=0x00000401u;bjboolean ok=bender_input_call_inject_fix68(e);if(action==0||action==1)logmsg("[BENDER_INPUT][NATIVE_PAD_KEY] action=%s keycode=%d source=%s result=%u\n",action?"UP":"DOWN",keycode,source?source:"?",(unsigned)ok);return ok?1:0;}
static void bender_native_edge_2744(int b,int kc,const char*n){if(b<0||b>=16)return;int c=bender_input_button(b);if(c!=g_bender_native_prev_2744[b])bender_input_inject_native_key_2744(c?0:1,kc,n);g_bender_native_prev_2744[b]=c;}
static int bender_input_inject_joystick_2744(int lx,int ly,int rx,int ry,int lt,int rt){if(!g_unity_native_inject_event_fn||!g_unity_player_class)return 0;BObj*e=bender_input_event_obj("android/view/MotionEvent",&g_bender_input_motion_event);if(!e)return 0;int64_t now=bender_input_now_ms();e->ref0=1;e->ref1=2;e->i64=now;e->i64b=now;e->p0id=0;e->flags=0x01000010u;e->f0=bender_norm_axis_2744(lx);e->f1=bender_norm_axis_2744(ly);e->f2=bender_norm_axis_2744(rx);e->f3=bender_norm_axis_2744(ry);e->f4=lt>0?(float)lt/32767.0f:0.0f;e->f5=rt>0?(float)rt/32767.0f:0.0f;e->f6=(float)(bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_DPAD_RIGHT)-bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_DPAD_LEFT));e->f7=(float)(bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_DPAD_DOWN)-bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_DPAD_UP));return bender_input_call_inject_fix68(e)?1:0;}
static int bender_input_inject_mt_2744(int action,int count,float x0,float y0,int id0,float x1,float y1,int id1,const char*src){if(!g_unity_native_inject_event_fn||!g_unity_player_class)return 0;BObj*e=bender_input_event_obj("android/view/MotionEvent",&g_bender_input_motion_event);if(!e)return 0;int64_t now=bender_input_now_ms();if((action&255)==0||!g_bender_input_down_time_ms)g_bender_input_down_time_ms=now;e->ref0=(uintptr_t)count;e->ref1=(uintptr_t)action;e->i64=now;e->i64b=g_bender_input_down_time_ms;e->f0=x0;e->f1=y0;e->f2=x1;e->f3=y1;e->p0id=id0;e->p1id=id1;e->flags=0x00001002u;bjboolean ok=bender_input_call_inject_fix68(e);unsigned long n=++g_bender_input_event_count;if((action&255)!=2||n%30==0)logmsg("[BENDER_INPUT][STICK_TOUCH] action=0x%x count=%d p0=%d %.0f,%.0f p1=%d %.0f,%.0f src=%s result=%u\n",action,count,id0,(double)x0,(double)y0,id1,(double)x1,(double)y1,src?src:"?",(unsigned)ok);return ok?1:0;}
static void bender_stick_touch_poll_2744(int lx,int ly,int rx,int ry){float nx=bender_norm_axis_2744(lx),ny=bender_norm_axis_2744(ly),cx=bender_norm_axis_2744(rx),cy=bender_norm_axis_2744(ry);int la=(nx!=0.0f||ny!=0.0f),ra=(cx!=0.0f||cy!=0.0f);float xL=g_bender_stick_lcx_2744+nx*g_bender_stick_lr_2744,yL=g_bender_stick_lcy_2744+ny*g_bender_stick_lr_2744,xR=g_bender_stick_rcx_2744+cx*g_bender_stick_rr_2744,yR=g_bender_stick_rcy_2744+cy*g_bender_stick_rr_2744;
 /* IMPORTANT: when an axis returns to deadzone, xL/xR become the configured
    center. Do NOT use those center coordinates for UP, otherwise Android/Unity
    interprets the release as one last MOVE back to the drag origin. */
 float upXL=g_bender_stick_last_lx_2746,upYL=g_bender_stick_last_ly_2746;
 float upXR=g_bender_stick_last_rx_2746,upYR=g_bender_stick_last_ry_2746;
 if(la){g_bender_stick_last_lx_2746=xL;g_bender_stick_last_ly_2746=yL;}
 if(ra){g_bender_stick_last_rx_2746=xR;g_bender_stick_last_ry_2746=yR;}
 if(!g_bender_stick_prev_l_2744&&!g_bender_stick_prev_r_2744){if(la){g_bender_stick_primary_2744=0;bender_input_inject_mt_2744(0,1,xL,yL,0,0,0,1,"left-down");}else if(ra){g_bender_stick_primary_2744=1;bender_input_inject_mt_2744(0,1,xR,yR,1,0,0,0,"right-down");}}
 else if(g_bender_stick_prev_l_2744&&!g_bender_stick_prev_r_2744){if(la&&ra)bender_input_inject_mt_2744(5|(1<<8),2,xL,yL,0,xR,yR,1,"right-pointer-down");else if(la)bender_input_inject_mt_2744(2,1,xL,yL,0,0,0,1,"left-move");else{bender_input_inject_mt_2744(1,1,upXL,upYL,0,0,0,1,"left-up-no-return");logmsg("[BENDER_STICK_TOUCH][RELEASE] side=LEFT at=%.0f,%.0f center=%.0f,%.0f no_return=1\n",(double)upXL,(double)upYL,(double)g_bender_stick_lcx_2744,(double)g_bender_stick_lcy_2744);g_bender_input_down_time_ms=0;if(ra){g_bender_stick_primary_2744=1;bender_input_inject_mt_2744(0,1,xR,yR,1,0,0,0,"right-switch-down");}}}
 else if(!g_bender_stick_prev_l_2744&&g_bender_stick_prev_r_2744){if(la&&ra)bender_input_inject_mt_2744(5|(1<<8),2,xR,yR,1,xL,yL,0,"left-pointer-down");else if(ra)bender_input_inject_mt_2744(2,1,xR,yR,1,0,0,0,"right-move");else{bender_input_inject_mt_2744(1,1,upXR,upYR,1,0,0,0,"right-up-no-return");logmsg("[BENDER_STICK_TOUCH][RELEASE] side=RIGHT at=%.0f,%.0f center=%.0f,%.0f no_return=1\n",(double)upXR,(double)upYR,(double)g_bender_stick_rcx_2744,(double)g_bender_stick_rcy_2744);g_bender_input_down_time_ms=0;if(la){g_bender_stick_primary_2744=0;bender_input_inject_mt_2744(0,1,xL,yL,0,0,0,1,"left-switch-down");}}}
 else {if(la&&ra){if(g_bender_stick_primary_2744==1)bender_input_inject_mt_2744(2,2,xR,yR,1,xL,yL,0,"both-move");else bender_input_inject_mt_2744(2,2,xL,yL,0,xR,yR,1,"both-move");}else if(la){if(g_bender_stick_primary_2744==1){bender_input_inject_mt_2744(6,2,upXR,upYR,1,xL,yL,0,"right-pointer-up-no-return");g_bender_stick_primary_2744=0;}else bender_input_inject_mt_2744(6|(1<<8),2,xL,yL,0,upXR,upYR,1,"right-pointer-up-no-return");logmsg("[BENDER_STICK_TOUCH][RELEASE] side=RIGHT at=%.0f,%.0f center=%.0f,%.0f no_return=1 multitouch=1\n",(double)upXR,(double)upYR,(double)g_bender_stick_rcx_2744,(double)g_bender_stick_rcy_2744);}else if(ra){if(g_bender_stick_primary_2744==0){bender_input_inject_mt_2744(6,2,upXL,upYL,0,xR,yR,1,"left-pointer-up-no-return");g_bender_stick_primary_2744=1;}else bender_input_inject_mt_2744(6|(1<<8),2,xR,yR,1,upXL,upYL,0,"left-pointer-up-no-return");logmsg("[BENDER_STICK_TOUCH][RELEASE] side=LEFT at=%.0f,%.0f center=%.0f,%.0f no_return=1 multitouch=1\n",(double)upXL,(double)upYL,(double)g_bender_stick_lcx_2744,(double)g_bender_stick_lcy_2744);}else{if(g_bender_stick_primary_2744==1){bender_input_inject_mt_2744(1,1,upXR,upYR,1,0,0,0,"final-right-up-no-return");logmsg("[BENDER_STICK_TOUCH][RELEASE] side=RIGHT at=%.0f,%.0f center=%.0f,%.0f no_return=1 final=1\n",(double)upXR,(double)upYR,(double)g_bender_stick_rcx_2744,(double)g_bender_stick_rcy_2744);}else{bender_input_inject_mt_2744(1,1,upXL,upYL,0,0,0,1,"final-left-up-no-return");logmsg("[BENDER_STICK_TOUCH][RELEASE] side=LEFT at=%.0f,%.0f center=%.0f,%.0f no_return=1 final=1\n",(double)upXL,(double)upYL,(double)g_bender_stick_lcx_2744,(double)g_bender_stick_lcy_2744);}g_bender_stick_primary_2744=-1;g_bender_input_down_time_ms=0;}}
 g_bender_stick_prev_l_2744=la;g_bender_stick_prev_r_2744=ra;}
static void bender_input_augment_2744(int fn){if(!g_bender_stick_cfg_loaded_2744)bender_stick_cfg_load_2744();int l3=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_LEFTSTICK),r3=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSTICK),left=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_DPAD_LEFT);if(fn){if(left&&!g_bender_stick_fn_left_prev_2869){if(g_bender_stick_touch_enabled_2744)bender_stick_touch_poll_2744(0,0,0,0);g_bender_stick_touch_enabled_2744=!g_bender_stick_touch_enabled_2744;bender_input_set_notice("sticktouch",g_bender_stick_touch_enabled_2744);logmsg("[BENDER_RUNTIME_CONTROLS][FIX69] hotkey=FN+DPAD_LEFT action=stick-touch-toggle enabled=%d release_before_disable=1\n",g_bender_stick_touch_enabled_2744);}if(l3&&!g_bender_stick_fn_l3_prev_2744){bender_input_toggle("touchmap",&g_bender_input_touchmap_active);logmsg("[BENDER_RUNTIME_CONTROLS][FIX69] hotkey=FN+L3 action=direct-touch-toggle enabled=%d\n",g_bender_input_touchmap_active);}if(r3&&!g_bender_stick_fn_r3_prev_2744){g_bender_stick_touch_visible_2744=!g_bender_stick_touch_visible_2744;snprintf(g_bender_input_notice,sizeof(g_bender_input_notice),"GUIAS STICKS %s",g_bender_stick_touch_visible_2744?"ATIVOS":"OCULTOS");g_bender_input_notice_until_ms=bender_input_now_ms()+3000ll;logmsg("[BENDER_STICK_TOUCH][GUIDE] visible=%d\n",g_bender_stick_touch_visible_2744);}}g_bender_stick_fn_left_prev_2869=left;g_bender_stick_fn_l3_prev_2744=l3;g_bender_stick_fn_r3_prev_2744=r3;if(fn||g_bender_input_keyboard_active)return;
 bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_A,96,"A");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_B,97,"B");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_X,99,"X");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_Y,100,"Y");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_LEFTSHOULDER,102,"L1");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,103,"R1");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_LEFTSTICK,106,"L3");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSTICK,107,"R3");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_START,108,"START");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_BACK,109,"SELECT");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_DPAD_UP,19,"UP");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_DPAD_DOWN,20,"DOWN");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_DPAD_LEFT,21,"LEFT");bender_native_edge_2744(BENDER_SDL_CONTROLLER_BUTTON_DPAD_RIGHT,22,"RIGHT");int a[6]={bender_input_axis(0),bender_input_axis(1),bender_input_axis(2),bender_input_axis(3),bender_input_axis(4),bender_input_axis(5)};int dirty=0;for(int i=0;i<6;i++){if(abs(a[i]-g_bender_native_axes_2744[i])>384)dirty=1;g_bender_native_axes_2744[i]=a[i];}if(dirty||g_bender_input_poll_count%30==0)bender_input_inject_joystick_2744(a[0],a[1],a[2],a[3],a[4],a[5]);if(g_bender_stick_touch_enabled_2744)bender_stick_touch_poll_2744(a[0],a[1],a[2],a[3]);}

/* ---------------- BENDER Unity in-game editors V2.7.50 ---------------- */
typedef struct BenderAndroidKeyChoice2745 { const char *name; int keycode; } BenderAndroidKeyChoice2745;
static const BenderAndroidKeyChoice2745 g_bender_key_choices_2745[]={
    {"NONE",0},
    {"A",29},{"B",30},{"C",31},{"D",32},{"E",33},{"F",34},{"G",35},{"H",36},{"I",37},{"J",38},{"K",39},{"L",40},{"M",41},
    {"N",42},{"O",43},{"P",44},{"Q",45},{"R",46},{"S",47},{"T",48},{"U",49},{"V",50},{"W",51},{"X",52},{"Y",53},{"Z",54},
    {"0",7},{"1",8},{"2",9},{"3",10},{"4",11},{"5",12},{"6",13},{"7",14},{"8",15},{"9",16},
    {"ENTER",66},{"ESC",111},{"SPACE",62},{"TAB",61},{"BACKSPACE",67},{"SHIFT",59},{"CTRL",113},{"ALT",57},
    {"UP",19},{"DOWN",20},{"LEFT",21},{"RIGHT",22},
    {"F1",131},{"F2",132},{"F3",133},{"F4",134},{"F5",135},{"F6",136},{"F7",137},{"F8",138},{"F9",139},{"F10",140},{"F11",141},{"F12",142}
};
#define BENDER_KEY_CHOICE_COUNT_2745 ((int)(sizeof(g_bender_key_choices_2745)/sizeof(g_bender_key_choices_2745[0])))
static const int g_bender_unity_keymap_defaults_2745[BENDER_UNITY_KEYMAP_COUNT]={66,111,62,61,45,33,0,0,0,0,0,0,19,20,21,22,29,32,51,47,0,0,0,0};
static const char*g_bender_unity_keymap_display_2745[BENDER_UNITY_KEYMAP_COUNT]={"A","B","X","Y","L1","R1","L2","R2","START","SELECT","L3","R3","DPAD UP","DPAD DOWN","DPAD LEFT","DPAD RIGHT","LS LEFT","LS RIGHT","LS UP","LS DOWN","RS LEFT","RS RIGHT","RS UP","RS DOWN"};
static int g_bender_key_editor_active_2745;
static int g_bender_key_editor_stage_2745;
static int g_bender_key_editor_input_2745;
static int g_bender_key_editor_choice_2745;
static int g_bender_stick_editor_active_2745;
static int g_bender_stick_editor_side_2745;
static int g_bender_editor_hot_y_prev_2745;
static int g_bender_editor_hot_x_prev_2745;
static int g_bender_ed_prev_up_2745,g_bender_ed_prev_down_2745,g_bender_ed_prev_left_2745,g_bender_ed_prev_right_2745;
static int g_bender_ed_prev_a_2745,g_bender_ed_prev_b_2745,g_bender_ed_prev_x_2745,g_bender_ed_prev_y_2745,g_bender_ed_prev_start_2745;
static int g_bender_ed_prev_l1_2745,g_bender_ed_prev_r1_2745,g_bender_ed_prev_l3_2745,g_bender_ed_prev_r3_2745;
static float g_bender_stick_backup_lcx_2745,g_bender_stick_backup_lcy_2745,g_bender_stick_backup_lr_2745;
static float g_bender_stick_backup_rcx_2745,g_bender_stick_backup_rcy_2745,g_bender_stick_backup_rr_2745;
static int g_bender_stick_backup_visible_2745,g_bender_stick_backup_enabled_2745;

static void bender_editor_notice_2745(const char*text){snprintf(g_bender_input_notice,sizeof(g_bender_input_notice),"%s",text?text:"");g_bender_input_notice_until_ms=bender_input_now_ms()+3000ll;logmsg("[BENDER_EDITOR][NOTICE] %s\n",g_bender_input_notice);}
static const char*bender_key_name_2745(int code){for(int i=0;i<BENDER_KEY_CHOICE_COUNT_2745;i++)if(g_bender_key_choices_2745[i].keycode==code)return g_bender_key_choices_2745[i].name;return "CUSTOM";}
static int bender_key_choice_index_2745(int code){for(int i=0;i<BENDER_KEY_CHOICE_COUNT_2745;i++)if(g_bender_key_choices_2745[i].keycode==code)return i;return 0;}
static void bender_unity_keymap_save_2745(void){char path[1200];snprintf(path,sizeof(path),"%s/UNITY_KEY_MAP.cfg",g_guest_app_dir);FILE*f=fopen(path,"w");if(!f){logmsg("[BENDER_KEYMAP_EDITOR][SAVE][ERROR] file=%s errno=%d\n",path,errno);bender_editor_notice_2745("ERRO SALVAR MAPA");return;}fprintf(f,"# BENDER UNITY KEY MAP V2.7.50 - salvo pelo editor dentro do jogo\n# Android keycodes; 0 = desativado\n");for(int i=0;i<BENDER_UNITY_KEYMAP_COUNT;i++)fprintf(f,"%s=%d\n",g_bender_unity_keymap_names[i],g_bender_unity_keymap[i]);fclose(f);g_bender_unity_keymap_loaded=1;logmsg("[BENDER_KEYMAP_EDITOR][SAVE] file=%s\n",path);}
static void bender_unity_keymap_defaults_2745(void){for(int i=0;i<BENDER_UNITY_KEYMAP_COUNT;i++)g_bender_unity_keymap[i]=g_bender_unity_keymap_defaults_2745[i];bender_unity_keymap_save_2745();}
static void bender_stick_cfg_save_2745(void){char path[1200];snprintf(path,sizeof(path),"%s/STICK_TOUCH.cfg",g_guest_app_dir);FILE*f=fopen(path,"w");if(!f){logmsg("[BENDER_STICK_EDITOR][SAVE][ERROR] file=%s errno=%d\n",path,errno);bender_editor_notice_2745("ERRO SALVAR STICKS");return;}fprintf(f,"# BENDER STICKS -> ARRASTE V2.7.50 - salvo pelo editor dentro do jogo\nENABLED=%d\nVISIBLE=%d\nDEADZONE=%d\nLEFT_CENTER=%.0f,%.0f\nLEFT_RADIUS=%.0f\nRIGHT_CENTER=%.0f,%.0f\nRIGHT_RADIUS=%.0f\n",g_bender_stick_touch_enabled_2744,g_bender_stick_touch_visible_2744,g_bender_stick_deadzone_2744,(double)g_bender_stick_lcx_2744,(double)g_bender_stick_lcy_2744,(double)g_bender_stick_lr_2744,(double)g_bender_stick_rcx_2744,(double)g_bender_stick_rcy_2744,(double)g_bender_stick_rr_2744);fclose(f);g_bender_stick_cfg_loaded_2744=1;logmsg("[BENDER_STICK_EDITOR][SAVE] file=%s left=%.0f,%.0f,r%.0f right=%.0f,%.0f,r%.0f visible=%d enabled=%d\n",path,(double)g_bender_stick_lcx_2744,(double)g_bender_stick_lcy_2744,(double)g_bender_stick_lr_2744,(double)g_bender_stick_rcx_2744,(double)g_bender_stick_rcy_2744,(double)g_bender_stick_rr_2744,g_bender_stick_touch_visible_2744,g_bender_stick_touch_enabled_2744);}
static void bender_editor_sync_edges_2745(int up,int down,int left,int right){g_bender_ed_prev_up_2745=up;g_bender_ed_prev_down_2745=down;g_bender_ed_prev_left_2745=left;g_bender_ed_prev_right_2745=right;g_bender_ed_prev_a_2745=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_A);g_bender_ed_prev_b_2745=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_B);g_bender_ed_prev_x_2745=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_X);g_bender_ed_prev_y_2745=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_Y);g_bender_ed_prev_start_2745=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_START);g_bender_ed_prev_l1_2745=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_LEFTSHOULDER);g_bender_ed_prev_r1_2745=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);g_bender_ed_prev_l3_2745=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_LEFTSTICK);g_bender_ed_prev_r3_2745=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSTICK);}
static void bender_key_editor_enter_2745(int up,int down,int left,int right){if(!g_bender_unity_keymap_loaded)bender_unity_keymap_load_2744();g_bender_key_editor_active_2745=1;g_bender_key_editor_stage_2745=0;g_bender_key_editor_input_2745=0;g_bender_key_editor_choice_2745=bender_key_choice_index_2745(g_bender_unity_keymap[0]);g_bender_input_keyboard_active=0;bender_editor_sync_edges_2745(up,down,left,right);logmsg("[BENDER_KEYMAP_EDITOR][OPEN] unity=1\n");}
static void bender_key_editor_leave_2745(int save){if(save)bender_unity_keymap_save_2745();g_bender_key_editor_active_2745=0;g_bender_key_editor_stage_2745=0;logmsg("[BENDER_KEYMAP_EDITOR][CLOSE] save=%d\n",save);bender_editor_notice_2745(save?"MAPA TECLAS SALVO":"EDITOR TECLAS FECHADO");}
static void bender_stick_editor_enter_2745(int up,int down,int left,int right){if(!g_bender_stick_cfg_loaded_2744)bender_stick_cfg_load_2744();g_bender_stick_backup_lcx_2745=g_bender_stick_lcx_2744;g_bender_stick_backup_lcy_2745=g_bender_stick_lcy_2744;g_bender_stick_backup_lr_2745=g_bender_stick_lr_2744;g_bender_stick_backup_rcx_2745=g_bender_stick_rcx_2744;g_bender_stick_backup_rcy_2745=g_bender_stick_rcy_2744;g_bender_stick_backup_rr_2745=g_bender_stick_rr_2744;g_bender_stick_backup_visible_2745=g_bender_stick_touch_visible_2744;g_bender_stick_backup_enabled_2745=g_bender_stick_touch_enabled_2744;g_bender_stick_editor_active_2745=1;g_bender_stick_editor_side_2745=0;bender_editor_sync_edges_2745(up,down,left,right);logmsg("[BENDER_STICK_EDITOR][OPEN] side=LEFT\n");}
static void bender_stick_editor_leave_2745(int save){if(save)bender_stick_cfg_save_2745();else{g_bender_stick_lcx_2744=g_bender_stick_backup_lcx_2745;g_bender_stick_lcy_2744=g_bender_stick_backup_lcy_2745;g_bender_stick_lr_2744=g_bender_stick_backup_lr_2745;g_bender_stick_rcx_2744=g_bender_stick_backup_rcx_2745;g_bender_stick_rcy_2744=g_bender_stick_backup_rcy_2745;g_bender_stick_rr_2744=g_bender_stick_backup_rr_2745;g_bender_stick_touch_visible_2744=g_bender_stick_backup_visible_2745;g_bender_stick_touch_enabled_2744=g_bender_stick_backup_enabled_2745;}g_bender_stick_editor_active_2745=0;logmsg("[BENDER_STICK_EDITOR][CLOSE] save=%d\n",save);bender_editor_notice_2745(save?"AREAS STICKS SALVAS":"AJUSTE STICKS CANCELADO");}
static void bender_stick_editor_reset_side_2745(void){if(g_bender_stick_editor_side_2745==0){g_bender_stick_lcx_2744=140.0f;g_bender_stick_lcy_2744=360.0f;g_bender_stick_lr_2744=90.0f;}else{g_bender_stick_rcx_2744=500.0f;g_bender_stick_rcy_2744=240.0f;g_bender_stick_rr_2744=110.0f;}}
static void bender_stick_editor_clamp_2745(void){float*r=g_bender_stick_editor_side_2745?&g_bender_stick_rr_2744:&g_bender_stick_lr_2744;float*x=g_bender_stick_editor_side_2745?&g_bender_stick_rcx_2744:&g_bender_stick_lcx_2744;float*y=g_bender_stick_editor_side_2745?&g_bender_stick_rcy_2744:&g_bender_stick_lcy_2744;if(*r<20)*r=20;if(*r>220)*r=220;if(*x<0)*x=0;if(*y<0)*y=0;if(*x>BENDER_DISPLAY_WIDTH-1)*x=BENDER_DISPLAY_WIDTH-1;if(*y>BENDER_DISPLAY_HEIGHT-1)*y=BENDER_DISPLAY_HEIGHT-1;}
static int bender_input_editor_poll_2745(int fn,int up,int down,int left,int right){int a=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_A),b=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_B),x=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_X),y=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_Y),start=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_START),l1=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_LEFTSHOULDER),r1=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSHOULDER),l3=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_LEFTSTICK),r3=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSTICK);int hoty=fn&&y,hotx=fn&&x;
    if(!g_bender_key_editor_active_2745&&!g_bender_stick_editor_active_2745){if(hoty&&!g_bender_editor_hot_y_prev_2745){bender_key_editor_enter_2745(up,down,left,right);g_bender_editor_hot_y_prev_2745=hoty;g_bender_editor_hot_x_prev_2745=hotx;return 1;}if(hotx&&!g_bender_editor_hot_x_prev_2745){bender_stick_editor_enter_2745(up,down,left,right);g_bender_editor_hot_y_prev_2745=hoty;g_bender_editor_hot_x_prev_2745=hotx;return 1;}g_bender_editor_hot_y_prev_2745=hoty;g_bender_editor_hot_x_prev_2745=hotx;return 0;}
    if(g_bender_key_editor_active_2745&&hoty&&!g_bender_editor_hot_y_prev_2745){bender_key_editor_leave_2745(1);g_bender_editor_hot_y_prev_2745=hoty;g_bender_editor_hot_x_prev_2745=hotx;return 1;}
    if(g_bender_stick_editor_active_2745&&hotx&&!g_bender_editor_hot_x_prev_2745){bender_stick_editor_leave_2745(1);g_bender_editor_hot_y_prev_2745=hoty;g_bender_editor_hot_x_prev_2745=hotx;return 1;}
    g_bender_editor_hot_y_prev_2745=hoty;g_bender_editor_hot_x_prev_2745=hotx;
    if(fn){bender_editor_sync_edges_2745(up,down,left,right);return 1;}
    if(g_bender_key_editor_active_2745){if(g_bender_key_editor_stage_2745==0){if(up&&!g_bender_ed_prev_up_2745)g_bender_key_editor_input_2745=(g_bender_key_editor_input_2745+BENDER_UNITY_KEYMAP_COUNT-1)%BENDER_UNITY_KEYMAP_COUNT;if(down&&!g_bender_ed_prev_down_2745)g_bender_key_editor_input_2745=(g_bender_key_editor_input_2745+1)%BENDER_UNITY_KEYMAP_COUNT;if(left&&!g_bender_ed_prev_left_2745)g_bender_key_editor_input_2745=(g_bender_key_editor_input_2745+BENDER_UNITY_KEYMAP_COUNT-1)%BENDER_UNITY_KEYMAP_COUNT;if(right&&!g_bender_ed_prev_right_2745)g_bender_key_editor_input_2745=(g_bender_key_editor_input_2745+1)%BENDER_UNITY_KEYMAP_COUNT;if(a&&!g_bender_ed_prev_a_2745){g_bender_key_editor_choice_2745=bender_key_choice_index_2745(g_bender_unity_keymap[g_bender_key_editor_input_2745]);g_bender_key_editor_stage_2745=1;}if(x&&!g_bender_ed_prev_x_2745){g_bender_unity_keymap[g_bender_key_editor_input_2745]=0;bender_unity_keymap_save_2745();bender_editor_notice_2745("MAPEAMENTO LIMPO");}if(y&&!g_bender_ed_prev_y_2745){bender_unity_keymap_defaults_2745();bender_editor_notice_2745("PADROES RESTAURADOS");}if(start&&!g_bender_ed_prev_start_2745){bender_key_editor_leave_2745(1);bender_editor_sync_edges_2745(up,down,left,right);return 1;}if(b&&!g_bender_ed_prev_b_2745){bender_key_editor_leave_2745(1);bender_editor_sync_edges_2745(up,down,left,right);return 1;}}
        else{if(left&&!g_bender_ed_prev_left_2745)g_bender_key_editor_choice_2745=(g_bender_key_editor_choice_2745+BENDER_KEY_CHOICE_COUNT_2745-1)%BENDER_KEY_CHOICE_COUNT_2745;if(right&&!g_bender_ed_prev_right_2745)g_bender_key_editor_choice_2745=(g_bender_key_editor_choice_2745+1)%BENDER_KEY_CHOICE_COUNT_2745;if(up&&!g_bender_ed_prev_up_2745)g_bender_key_editor_choice_2745=(g_bender_key_editor_choice_2745+BENDER_KEY_CHOICE_COUNT_2745-10)%BENDER_KEY_CHOICE_COUNT_2745;if(down&&!g_bender_ed_prev_down_2745)g_bender_key_editor_choice_2745=(g_bender_key_editor_choice_2745+10)%BENDER_KEY_CHOICE_COUNT_2745;if(a&&!g_bender_ed_prev_a_2745){g_bender_unity_keymap[g_bender_key_editor_input_2745]=g_bender_key_choices_2745[g_bender_key_editor_choice_2745].keycode;bender_unity_keymap_save_2745();g_bender_key_editor_stage_2745=0;bender_editor_notice_2745("MAPEAMENTO SALVO");}if(b&&!g_bender_ed_prev_b_2745)g_bender_key_editor_stage_2745=0;if(x&&!g_bender_ed_prev_x_2745){g_bender_unity_keymap[g_bender_key_editor_input_2745]=0;bender_unity_keymap_save_2745();g_bender_key_editor_stage_2745=0;bender_editor_notice_2745("MAPEAMENTO LIMPO");}if(start&&!g_bender_ed_prev_start_2745){bender_key_editor_leave_2745(1);bender_editor_sync_edges_2745(up,down,left,right);return 1;}}
        bender_editor_sync_edges_2745(up,down,left,right);return 1;}
    if(g_bender_stick_editor_active_2745){if(l3&&!g_bender_ed_prev_l3_2745)g_bender_stick_editor_side_2745=0;if(r3&&!g_bender_ed_prev_r3_2745)g_bender_stick_editor_side_2745=1;if(a&&!g_bender_ed_prev_a_2745)g_bender_stick_editor_side_2745=!g_bender_stick_editor_side_2745;float*cx=g_bender_stick_editor_side_2745?&g_bender_stick_rcx_2744:&g_bender_stick_lcx_2744;float*cy=g_bender_stick_editor_side_2745?&g_bender_stick_rcy_2744:&g_bender_stick_lcy_2744;float*rr=g_bender_stick_editor_side_2745?&g_bender_stick_rr_2744:&g_bender_stick_lr_2744;if(left)*cx-=2.0f;if(right)*cx+=2.0f;if(up)*cy-=2.0f;if(down)*cy+=2.0f;if(l1)*rr-=1.5f;if(r1)*rr+=1.5f;if(x&&!g_bender_ed_prev_x_2745)g_bender_stick_touch_visible_2744=!g_bender_stick_touch_visible_2744;if(y&&!g_bender_ed_prev_y_2745)bender_stick_editor_reset_side_2745();bender_stick_editor_clamp_2745();if(start&&!g_bender_ed_prev_start_2745){bender_stick_editor_leave_2745(1);bender_editor_sync_edges_2745(up,down,left,right);return 1;}if(b&&!g_bender_ed_prev_b_2745){bender_stick_editor_leave_2745(0);bender_editor_sync_edges_2745(up,down,left,right);return 1;}bender_editor_sync_edges_2745(up,down,left,right);return 1;}
    return 0;
}
/* ---------------- end BENDER Unity in-game editors V2.7.50 ---------------- */
/* BENDER V2.7.173 FN FALLBACK: L1+R1+DPAD, L1+R1+Y, L1+R1+X, L1+R1+L3, L1+R1+R3; mouse speed L1+R1+L2 / L1+R1+R2. */
static __attribute__((unused)) void bender_input_poll(unsigned frame){
    (void)frame;if(!g_bender_input_init_attempted)bender_input_init();if(!g_bender_input_ready)return;if(g_sdl_GameControllerUpdate)g_sdl_GameControllerUpdate();if(g_sdl_JoystickUpdate)g_sdl_JoystickUpdate();++g_bender_input_poll_count;
    if(bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_START)&&bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_BACK))g_bender_exit_combo_recent=12;
    else if(g_bender_exit_combo_recent>0)--g_bender_exit_combo_recent;
    int fn_native=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_GUIDE)||bender_input_raw_button(g_bender_input_fn_raw_button);int fn_alias=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_LEFTSHOULDER)&&bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);g_bender_fn_native_2173=fn_native;g_bender_fn_alias_2173=fn_alias;int fn=fn_native||fn_alias;
    int up=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_DPAD_UP),down=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_DPAD_DOWN),left=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_DPAD_LEFT),right=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_DPAD_RIGHT);if(bender_input_editor_poll_2745(fn,up,down,left,right)){g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_UP]=(unsigned char)up;g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_DOWN]=(unsigned char)down;g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_LEFT]=(unsigned char)left;g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_RIGHT]=(unsigned char)right;g_bender_input_prev_fn=fn;return;}bender_input_augment_2744(fn);
    if(!g_bender_unity_keymap_loaded){bender_unity_keymap_load_2744();}
    if(fn){int y=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_Y);if(up&&!g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_UP])bender_input_toggle("keyboard",&g_bender_input_keyboard_active);if(down&&!g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_DOWN])bender_input_toggle("mouse",&g_bender_input_mouse_active);if(right&&!g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_RIGHT])bender_input_toggle("keymap",&g_bender_input_keymap_active);if(y&&!g_bender_unity_keymap_reload_prev){bender_unity_keymap_load_2744();bender_stick_cfg_load_2744();snprintf(g_bender_input_notice,sizeof(g_bender_input_notice),"MAPAS RECARREGADOS");g_bender_input_notice_until_ms=bender_input_now_ms()+3000ll;}g_bender_unity_keymap_reload_prev=y;}else g_bender_unity_keymap_reload_prev=0;bender_mouse_speed_hotkeys_2767(fn);
    /* ===== ULTRAKILL/TSP rev -L: START+R1 tambem liga/DESLIGA o mouse =====
     * Pedido explicito do usuario no TrimUI Smart Pro (nao tem touch screen).
     * O esquema antigo (FN=GUIDE/raw16 ou L1+R1, depois DOWN) continua
     * valendo como alias - isto e apenas um segundo atalho, mais curto e sem
     * depender do botao GUIDE (que no TSP nao e exposto pelo driver em muitos
     * firmwares, e L1+R1 collide com mira/arma do proprio jogo).
     * START e R1 individualmente NAO sao engolidos: em modo mouse o B passa a
     * ser clique (ver bender_input_poll adiante), entao o toggle caindo aqui
     * nao rouba pause nem tiro.
     * Env: BENDER_MOUSE_TOGGLE_START_R1=0 desliga este atalho. */
    {static int uk_sr1_prev=0,uk_sr1_en=-1;if(uk_sr1_en<0){const char*e=getenv("BENDER_MOUSE_TOGGLE_START_R1");uk_sr1_en=(!e||!*e||strcmp(e,"0"))?1:0;}
     if(uk_sr1_en){int sr1=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_START)&&bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
       if(sr1&&!uk_sr1_prev){bender_input_toggle("mouse",&g_bender_input_mouse_active);logmsg("[BENDER_INPUT][MOUSE_TOGGLE] hotkey=START+R1 -> %s\n",g_bender_input_mouse_active?"ON":"OFF");}
       uk_sr1_prev=sr1;}}
    bender_soft_keyboard_lifecycle_fix99();
    if(g_bender_input_keyboard_active&&!fn)bender_keyboard_poll(up,down,left,right);
    if(g_bender_input_mouse_active&&!fn&&!g_bender_input_keyboard_active&&!g_bender_stick_touch_enabled_2744){int rx=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_RIGHTX),ry=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_RIGHTY);float dx=0,dy=0;if(!g_bender_input_keymap_active){if(left)dx-=5;if(right)dx+=5;if(up)dy-=5;if(down)dy+=5;}if(rx>7000||rx<-7000)dx+=(float)rx/7000.0f;if(ry>7000||ry<-7000)dy+=(float)ry/7000.0f;if(dx||dy){float mouse_scale=(float)g_bender_mouse_speed_percent_2767/100.0f;dx*=mouse_scale;dy*=mouse_scale;g_bender_input_cursor_x+=dx;g_bender_input_cursor_y+=dy;if(g_bender_input_cursor_x<0)g_bender_input_cursor_x=0;if(g_bender_input_cursor_y<0)g_bender_input_cursor_y=0;if(g_bender_input_cursor_x>BENDER_DISPLAY_WIDTH-1)g_bender_input_cursor_x=BENDER_DISPLAY_WIDTH-1;if(g_bender_input_cursor_y>BENDER_DISPLAY_HEIGHT-1)g_bender_input_cursor_y=BENDER_DISPLAY_HEIGHT-1;bender_input_inject_motion(2,g_bender_input_cursor_x,g_bender_input_cursor_y,"mouse-move");}
        /* Clique do mouse virtual: no TrimUI Smart Pro o usuario pediu o
         * botao B (direita) como clique. Mantem A e R3 (esquema antigo) para
         * nao regredir quem ja usava; BENDER_MOUSE_CLICK_B=0 tira o B. */
        int click=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_B)||bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_A)||bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_RIGHTSTICK);int prevclick=(g_bender_input_prev_buttons[15]!=0);if(click!=prevclick)bender_input_inject_motion(click?0:1,g_bender_input_cursor_x,g_bender_input_cursor_y,"mouse-click");g_bender_input_prev_buttons[15]=(unsigned char)click;
    }
    if(g_bender_input_touchmap_active&&!fn&&!g_bender_input_mouse_active&&!g_bender_input_keyboard_active&&!g_bender_stick_touch_enabled_2744){int rx=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_RIGHTX),ry=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_RIGHTY);float dx=0,dy=0;if(left)dx-=5;if(right)dx+=5;if(up)dy-=5;if(down)dy+=5;if(rx>7000||rx<-7000)dx+=(float)rx/7000.0f;if(ry>7000||ry<-7000)dy+=(float)ry/7000.0f;if(dx||dy){g_bender_input_cursor_x+=dx;g_bender_input_cursor_y+=dy;if(g_bender_input_cursor_x<0)g_bender_input_cursor_x=0;if(g_bender_input_cursor_y<0)g_bender_input_cursor_y=0;if(g_bender_input_cursor_x>BENDER_DISPLAY_WIDTH-1)g_bender_input_cursor_x=BENDER_DISPLAY_WIDTH-1;if(g_bender_input_cursor_y>BENDER_DISPLAY_HEIGHT-1)g_bender_input_cursor_y=BENDER_DISPLAY_HEIGHT-1;bender_input_inject_motion(2,g_bender_input_cursor_x,g_bender_input_cursor_y,"touchmap-move");}int ta=bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_A);if(ta!=g_bender_touch_prev_a)bender_input_inject_motion(ta?0:1,g_bender_input_cursor_x,g_bender_input_cursor_y,"touchmap-A");g_bender_touch_prev_a=ta;}
    if(g_bender_input_keymap_active&&!fn&&!g_bender_input_keyboard_active&&!g_bender_input_touchmap_active){int btns[16]={BENDER_SDL_CONTROLLER_BUTTON_A,BENDER_SDL_CONTROLLER_BUTTON_B,BENDER_SDL_CONTROLLER_BUTTON_X,BENDER_SDL_CONTROLLER_BUTTON_Y,BENDER_SDL_CONTROLLER_BUTTON_LEFTSHOULDER,BENDER_SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,-1,-1,BENDER_SDL_CONTROLLER_BUTTON_START,BENDER_SDL_CONTROLLER_BUTTON_BACK,BENDER_SDL_CONTROLLER_BUTTON_LEFTSTICK,BENDER_SDL_CONTROLLER_BUTTON_RIGHTSTICK,BENDER_SDL_CONTROLLER_BUTTON_DPAD_UP,BENDER_SDL_CONTROLLER_BUTTON_DPAD_DOWN,BENDER_SDL_CONTROLLER_BUTTON_DPAD_LEFT,BENDER_SDL_CONTROLLER_BUTTON_DPAD_RIGHT};for(int i=0;i<16;i++)if(btns[i]>=0&&g_bender_unity_keymap[i]>0)bender_input_key_edge(btns[i],g_bender_unity_keymap[i],g_bender_unity_keymap_names[i]);
        int lx=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_LEFTX),ly=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_LEFTY),rx=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_RIGHTX),ry=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_RIGHTY),lt=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_TRIGGERLEFT),rt=bender_input_axis(BENDER_SDL_CONTROLLER_AXIS_TRIGGERRIGHT);static unsigned char ptrig[2];int trig[2]={lt>16000,rt>16000};for(int i=0;i<2;i++){int kc=g_bender_unity_keymap[6+i];if(kc>0&&trig[i]!=(int)ptrig[i])bender_input_inject_key(trig[i]?0:1,kc,g_bender_unity_keymap_names[6+i]);ptrig[i]=(unsigned char)trig[i];}int st[8]={lx<-16000,lx>16000,ly<-16000,ly>16000,rx<-16000,rx>16000,ry<-16000,ry>16000};static unsigned char pst[8];for(int i=0;i<8;i++){int kc=g_bender_unity_keymap[16+i];if(kc>0&&st[i]!=(int)pst[i])bender_input_inject_key(st[i]?0:1,kc,g_bender_unity_keymap_names[16+i]);pst[i]=(unsigned char)st[i];}
    }
    g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_UP]=(unsigned char)up;g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_DOWN]=(unsigned char)down;g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_LEFT]=(unsigned char)left;g_bender_input_prev_buttons[BENDER_SDL_CONTROLLER_BUTTON_DPAD_RIGHT]=(unsigned char)right;g_bender_input_prev_fn=fn;
    if(g_bender_input_poll_count<=3||g_bender_input_poll_count%600==0)logmsg("[BENDER_INPUT][POLL] n=%lu fn=%d guide=%d raw%d=%d modes=%d/%d/%d/%d cursor=%.0f,%.0f\n",g_bender_input_poll_count,fn,bender_input_button(BENDER_SDL_CONTROLLER_BUTTON_GUIDE),g_bender_input_fn_raw_button,bender_input_raw_button(g_bender_input_fn_raw_button),g_bender_input_keyboard_active,g_bender_input_mouse_active,g_bender_input_touchmap_active,g_bender_input_keymap_active,(double)g_bender_input_cursor_x,(double)g_bender_input_cursor_y);
}
/* Minimal visible crosshair for mouse mode; drawn after render-inspector sampling so diagnostics remain game-only. */
typedef void (*BenderInputGLGetFloatvFn)(unsigned int,float*);typedef unsigned char (*BenderInputGLIsEnabledFn)(unsigned int);typedef void (*BenderInputGLEnableFn)(unsigned int);typedef void (*BenderInputGLDisableFn)(unsigned int);typedef void (*BenderInputGLScissorFn)(int,int,int,int);

static unsigned short bender_font3x5(char c){switch(c){
case '0':return 0x7b6f;case '1':return 0x2c97;case '2':return 0x73e7;case '3':return 0x73cf;case '4':return 0x5bc9;case '5':return 0x79cf;case '6':return 0x79ef;case '7':return 0x7249;case '8':return 0x7bef;case '9':return 0x7bcf;
case 'A':return 0x2bed;case 'B':return 0x6bae;case 'C':return 0x7927;case 'D':return 0x6b6e;case 'E':return 0x79a7;case 'F':return 0x79a4;case 'G':return 0x79eb;case 'H':return 0x5bed;case 'I':return 0x7497;case 'J':return 0x124e;case 'K':return 0x5bad;case 'L':return 0x4927;case 'M':return 0x5fed;case 'N':return 0x5fed;case 'O':return 0x7b6f;case 'P':return 0x7be4;case 'Q':return 0x7b7b;case 'R':return 0x7bec;case 'S':return 0x79cf;case 'T':return 0x7492;case 'U':return 0x5b6f;case 'V':return 0x5b6a;case 'W':return 0x5ffd;case 'X':return 0x5aad;case 'Y':return 0x5a92;case 'Z':return 0x72a7;default:return 0;}}
static void bender_gl_rect(BenderInputGLScissorFn sc,BenderGLClearFn clr,int x,int y,int w,int h){if(w<=0||h<=0)return;sc(x,y,w,h);clr(0x00004000u);}
static void bender_draw_text3x5(BenderInputGLScissorFn sc,BenderGLClearFn clr,const char*s,int x,int y,int scale){if(!s)return;for(int ci=0;s[ci];ci++){unsigned short m=bender_font3x5(s[ci]);for(int r=0;r<5;r++)for(int c=0;c<3;c++)if(m&(1u<<(14-(r*3+c))))bender_gl_rect(sc,clr,x+ci*(4*scale)+c*scale,y+(4-r)*scale,scale,scale);}}
static void bender_draw_arrow(BenderInputGLScissorFn sc,BenderGLClearFn clr,int x,int y){static const unsigned short rows[12]={0x100,0x180,0x1c0,0x1e0,0x1f0,0x1f8,0x1fc,0x1e0,0x1b0,0x118,0x00c,0x006};for(int r=0;r<12;r++){unsigned short bits=rows[r];for(int c=0;c<9;c++)if(bits&(1u<<(8-c)))bender_gl_rect(sc,clr,x+c,y-r,2,2);}}
static void bender_draw_key_v3(BenderInputGLScissorFn sc,BenderGLClearFn clr,BenderGLClearColorFn cc,int x,int y,int w,int h,const char*label,int selected){
    if(selected)cc(1.00f,0.72f,0.08f,1.0f);else cc(0.14f,0.18f,0.26f,1.0f);
    bender_gl_rect(sc,clr,x,y,w,h);
    if(selected)cc(0.02f,0.02f,0.02f,1.0f);else cc(1.0f,1.0f,1.0f,1.0f);
    int scale=2;int chars=(int)strlen(label);int tw=chars*(4*scale)-scale;int tx=x+(w-tw)/2;int ty=y+(h-5*scale)/2;
    bender_draw_text3x5(sc,clr,label,tx,ty,scale);
}
static void bender_draw_keyboard_v3(BenderInputGLScissorFn sc,BenderGLClearFn clr,BenderGLClearColorFn cc){
    const int panel_x=10,panel_y=6,panel_w=BENDER_DISPLAY_WIDTH-20,panel_h=198;
    cc(0.82f,0.84f,0.88f,1.0f);bender_gl_rect(sc,clr,panel_x,panel_y,panel_w,panel_h);
    /* OpenGL scissor coordinates originate at the bottom-left.  Keep row 0
       (numbers) physically above QWERTY/A/Z and controls at the bottom. */
    const int row_y[4]={157,125,93,61};
    for(int row=0;row<4;row++){
        int n=bender_keyboard_row_count(row);int kw=52,gap=4,total=n*kw+(n-1)*gap,start=(BENDER_DISPLAY_WIDTH-total)/2;int y=row_y[row];
        for(int col=0;col<n;col++)bender_draw_key_v3(sc,clr,cc,start+col*(kw+gap),y,kw,26,bender_keyboard_label(row,col),(row==g_bender_keyboard_row&&col==g_bender_keyboard_col));
    }
    {int y=24;int widths[5]={88,164,92,104,104};int gap=5,total=0;for(int i=0;i<5;i++)total+=widths[i];total+=gap*4;int x=(BENDER_DISPLAY_WIDTH-total)/2;for(int col=0;col<5;col++){bender_draw_key_v3(sc,clr,cc,x,y,widths[col],27,bender_keyboard_label(4,col),(g_bender_keyboard_row==4&&g_bender_keyboard_col==col));x+=widths[col]+gap;}}
    cc(0.05f,0.05f,0.05f,1.0f);bender_draw_text3x5(sc,clr,g_bender_keyboard_shift_fix99?"A TYPE X BACK Y SHIFT ON START ENTER B CLOSE":"A TYPE X BACK Y SHIFT START ENTER B CLOSE",44,188,1);
}
static void bender_draw_notice_v3(BenderInputGLScissorFn sc,BenderGLClearFn clr,BenderGLClearColorFn cc){
    if(!bender_input_notice_visible())return;
    int scale=2;int chars=(int)strlen(g_bender_input_notice);int tw=chars*(4*scale)-scale;int w=tw+24;if(w>620)w=620;int x=(BENDER_DISPLAY_WIDTH-w)/2;int y=BENDER_DISPLAY_HEIGHT-42;
    cc(0.04f,0.04f,0.07f,1.0f);bender_gl_rect(sc,clr,x,y,w,30);
    cc(1.0f,1.0f,1.0f,1.0f);bender_draw_text3x5(sc,clr,g_bender_input_notice,x+12,y+10,scale);
}


static void bender_draw_box_outline_2745(BenderInputGLScissorFn sc,BenderGLClearFn clr,int cx,int cy,int r){bender_gl_rect(sc,clr,cx-r,cy-r,2,r*2);bender_gl_rect(sc,clr,cx+r-2,cy-r,2,r*2);bender_gl_rect(sc,clr,cx-r,cy-r,r*2,2);bender_gl_rect(sc,clr,cx-r,cy+r-2,r*2,2);bender_gl_rect(sc,clr,cx-6,cy-1,12,2);bender_gl_rect(sc,clr,cx-1,cy-6,2,12);}
static void bender_draw_keymap_editor_2745(BenderInputGLScissorFn sc,BenderGLClearFn clr,BenderGLClearColorFn cc){cc(0.025f,0.03f,0.045f,1.0f);bender_gl_rect(sc,clr,0,0,BENDER_DISPLAY_WIDTH,BENDER_DISPLAY_HEIGHT);cc(1,1,1,1);bender_draw_text3x5(sc,clr,"BENDER KEY MAP EDITOR",18,BENDER_DISPLAY_HEIGHT-34,2);cc(0.65f,0.85f,1.0f,1);bender_draw_text3x5(sc,clr,"FN/L1+R1 Y CLOSE",18,BENDER_DISPLAY_HEIGHT-53,1);int idx=g_bender_key_editor_input_2745;cc(0.7f,0.8f,0.95f,1);bender_draw_text3x5(sc,clr,"CONTROL",18,BENDER_DISPLAY_HEIGHT-92,2);cc(1.0f,0.82f,0.2f,1);bender_draw_text3x5(sc,clr,g_bender_unity_keymap_display_2745[idx],150,BENDER_DISPLAY_HEIGHT-92,2);cc(0.7f,0.8f,0.95f,1);bender_draw_text3x5(sc,clr,"CURRENT",18,BENDER_DISPLAY_HEIGHT-122,2);cc(0.35f,1.0f,0.55f,1);bender_draw_text3x5(sc,clr,bender_key_name_2745(g_bender_unity_keymap[idx]),150,BENDER_DISPLAY_HEIGHT-122,2);cc(0.08f,0.10f,0.15f,1);bender_gl_rect(sc,clr,12,120,BENDER_DISPLAY_WIDTH-24,105);cc(1,1,1,1);if(g_bender_key_editor_stage_2745==0){bender_draw_text3x5(sc,clr,"DPAD SELECT CONTROL",24,202,1);bender_draw_text3x5(sc,clr,"A EDIT   X CLEAR   Y DEFAULTS",24,180,1);bender_draw_text3x5(sc,clr,"START SAVE   B CLOSE",24,158,1);}else{cc(0.75f,0.9f,1,1);bender_draw_text3x5(sc,clr,"NEW KEY",24,202,1);cc(1.0f,0.75f,0.25f,1);bender_draw_text3x5(sc,clr,g_bender_key_choices_2745[g_bender_key_editor_choice_2745].name,130,194,2);cc(1,1,1,1);bender_draw_text3x5(sc,clr,"LEFT RIGHT STEP   UP DOWN PAGE",24,168,1);bender_draw_text3x5(sc,clr,"A SET   B BACK   X CLEAR",24,146,1);bender_draw_text3x5(sc,clr,"START SAVE",24,126,1);}cc(0.6f,0.75f,0.95f,1);bender_draw_text3x5(sc,clr,"FN/L1+R1 DPAD RIGHT TOGGLE",18,54,1);cc(g_bender_input_keymap_active?0.3f:1.0f,g_bender_input_keymap_active?1.0f:0.4f,0.45f,1);bender_draw_text3x5(sc,clr,g_bender_input_keymap_active?"KEY MAP MODE ON":"KEY MAP MODE OFF",18,32,1);}
static void bender_draw_stick_editor_2745(BenderInputGLScissorFn sc,BenderGLClearFn clr,BenderGLClearColorFn cc){int lcx=(int)g_bender_stick_lcx_2744,lcy=BENDER_DISPLAY_HEIGHT-1-(int)g_bender_stick_lcy_2744,lr=(int)g_bender_stick_lr_2744,rcx=(int)g_bender_stick_rcx_2744,rcy=BENDER_DISPLAY_HEIGHT-1-(int)g_bender_stick_rcy_2744,rr=(int)g_bender_stick_rr_2744;cc(g_bender_stick_editor_side_2745==0?1.0f:0.55f,g_bender_stick_editor_side_2745==0?0.75f:0.75f,g_bender_stick_editor_side_2745==0?0.15f:1.0f,1);bender_draw_box_outline_2745(sc,clr,lcx,lcy,lr);cc(g_bender_stick_editor_side_2745==1?1.0f:0.55f,g_bender_stick_editor_side_2745==1?0.75f:0.75f,g_bender_stick_editor_side_2745==1?0.15f:1.0f,1);bender_draw_box_outline_2745(sc,clr,rcx,rcy,rr);cc(0.03f,0.04f,0.065f,1);bender_gl_rect(sc,clr,0,BENDER_DISPLAY_HEIGHT-82,BENDER_DISPLAY_WIDTH,82);bender_gl_rect(sc,clr,0,0,BENDER_DISPLAY_WIDTH,76);cc(1,1,1,1);bender_draw_text3x5(sc,clr,"BENDER STICK AREA EDITOR",14,BENDER_DISPLAY_HEIGHT-25,2);cc(1.0f,0.82f,0.2f,1);bender_draw_text3x5(sc,clr,g_bender_stick_editor_side_2745?"SELECTED RIGHT":"SELECTED LEFT",14,BENDER_DISPLAY_HEIGHT-47,1);char info[1024];if(g_bender_stick_editor_side_2745)snprintf(info,sizeof(info),"X %.0f Y %.0f R %.0f",(double)g_bender_stick_rcx_2744,(double)g_bender_stick_rcy_2744,(double)g_bender_stick_rr_2744);else snprintf(info,sizeof(info),"X %.0f Y %.0f R %.0f",(double)g_bender_stick_lcx_2744,(double)g_bender_stick_lcy_2744,(double)g_bender_stick_lr_2744);cc(0.75f,0.9f,1,1);bender_draw_text3x5(sc,clr,info,14,BENDER_DISPLAY_HEIGHT-66,1);cc(1,1,1,1);bender_draw_text3x5(sc,clr,"L3 LEFT  R3 RIGHT  A SWITCH",12,58,1);bender_draw_text3x5(sc,clr,"DPAD MOVE  L1 SMALLER  R1 BIGGER",12,43,1);bender_draw_text3x5(sc,clr,"X GUIDE  Y RESET",12,28,1);bender_draw_text3x5(sc,clr,"START SAVE  B CANCEL",12,13,1);}

/* BENDER FIX98 - global lightweight health HUD. */
static int g_bender_hud_enabled_fix98=-1;
static int64_t g_bender_hud_next_sample_ms_fix98;
static long g_bender_hud_rss_kb_fix98=-1;
static long g_bender_hud_available_kb_fix98=-1;
static int g_bender_hud_temp_c_fix98=-1;
static unsigned long g_bender_hud_draws_fix98;
static int bender_hud_enabled_fix98(void){
    if(g_bender_hud_enabled_fix98>=0)return g_bender_hud_enabled_fix98;
    const char*e=getenv("BENDER_HUD");
    g_bender_hud_enabled_fix98=(!e||!*e||strcmp(e,"0"))?1:0;
    if(e&&(!strcmp(e,"off")||!strcmp(e,"false")||!strcmp(e,"no")))g_bender_hud_enabled_fix98=0;
    logmsg("[BENDER_HUD] source=fix98 backend=unity action=init enabled=%d sample_ms=1000 metrics=rss,available,temp\n",g_bender_hud_enabled_fix98);
    return g_bender_hud_enabled_fix98;
}
static long bender_hud_read_kb_key_fix98(const char*path,const char*key){
    FILE*f=fopen(path,"r");if(!f)return -1;char line[256];long out=-1;size_t n=strlen(key);
    while(fgets(line,sizeof(line),f)){if(!strncmp(line,key,n)){char*p=line+n;while(*p==' '||*p=='\t'||*p==':')p++;char*end=NULL;long v=strtol(p,&end,10);if(end!=p)out=v;break;}}
    fclose(f);return out;
}
static int bender_hud_read_temp_fix98(void){
    int best=-1;char path[96];
    for(int i=0;i<16;i++){snprintf(path,sizeof(path),"/sys/class/thermal/thermal_zone%d/temp",i);FILE*f=fopen(path,"r");if(!f)continue;long v=0;int ok=fscanf(f,"%ld",&v);fclose(f);if(ok!=1)continue;if(v>1000)v=(v+500)/1000;if(v>=0&&v<=150&&(int)v>best)best=(int)v;}
    return best;
}
static void bender_hud_sample_fix98(void){
    if(!bender_hud_enabled_fix98()){return;}
    int64_t now=bender_input_now_ms();
    if(now<g_bender_hud_next_sample_ms_fix98){return;}
    g_bender_hud_next_sample_ms_fix98=now+1000ll;
    g_bender_hud_rss_kb_fix98=bender_hud_read_kb_key_fix98("/proc/self/status","VmRSS");
    g_bender_hud_available_kb_fix98=bender_hud_read_kb_key_fix98("/proc/meminfo","MemAvailable");
    g_bender_hud_temp_c_fix98=bender_hud_read_temp_fix98();
}
static void bender_hud_draw_fix98(BenderInputGLScissorFn sc,BenderGLClearFn clr,BenderGLClearColorFn cc){
    if(!bender_hud_enabled_fix98()){return;}
    bender_hud_sample_fix98();
    char l1[64],l2[32];
    long rssm=g_bender_hud_rss_kb_fix98>=0?(g_bender_hud_rss_kb_fix98+512)/1024:-1;long freem=g_bender_hud_available_kb_fix98>=0?(g_bender_hud_available_kb_fix98+512)/1024:-1;
    if(rssm>=0&&freem>=0)snprintf(l1,sizeof(l1),"RAM %ldM FREE %ldM",rssm,freem);else if(rssm>=0)snprintf(l1,sizeof(l1),"RAM %ldM",rssm);else snprintf(l1,sizeof(l1),"RAM NA");
    if(g_bender_hud_temp_c_fix98>=0)snprintf(l2,sizeof(l2),"TEMP %dC",g_bender_hud_temp_c_fix98);else snprintf(l2,sizeof(l2),"TEMP NA");
    int scale=2;int c1=(int)strlen(l1),c2=(int)strlen(l2);int chars=c1>c2?c1:c2;int w=chars*(4*scale)-scale+14;if(w<88)w=88;if(w>BENDER_DISPLAY_WIDTH-8)w=BENDER_DISPLAY_WIDTH-8;
    int x=4,y=BENDER_DISPLAY_HEIGHT-38;cc(0.02f,0.02f,0.02f,1.0f);bender_gl_rect(sc,clr,x,y,w,34);cc(1.0f,1.0f,1.0f,1.0f);bender_draw_text3x5(sc,clr,l1,x+7,y+20,scale);bender_draw_text3x5(sc,clr,l2,x+7,y+7,scale);++g_bender_hud_draws_fix98;
}
static void bender_input_draw_cursor(void){if((!g_bender_input_mouse_active&&!g_bender_input_touchmap_active&&!g_bender_input_keyboard_active&&!bender_input_notice_visible()&&!(g_bender_stick_touch_enabled_2744&&g_bender_stick_touch_visible_2744)&&!g_bender_key_editor_active_2745&&!g_bender_stick_editor_active_2745&&!bender_hud_enabled_fix98())||!g_bender_sdl_ready)return;BenderGLClearFn clr=(BenderGLClearFn)bender_gl_real_proc("glClear");BenderGLClearColorFn cc=(BenderGLClearColorFn)bender_gl_real_proc("glClearColor");BenderGLGetIntegervFn gi=(BenderGLGetIntegervFn)bender_gl_real_proc("glGetIntegerv");BenderInputGLGetFloatvFn gf=(BenderInputGLGetFloatvFn)bender_gl_real_proc("glGetFloatv");BenderInputGLIsEnabledFn ie=(BenderInputGLIsEnabledFn)bender_gl_real_proc("glIsEnabled");BenderInputGLEnableFn en=(BenderInputGLEnableFn)bender_gl_real_proc("glEnable");BenderInputGLDisableFn dis=(BenderInputGLDisableFn)bender_gl_real_proc("glDisable");BenderInputGLScissorFn sc=(BenderInputGLScissorFn)bender_gl_real_proc("glScissor");if(!clr||!cc||!gi||!sc||!en||!dis)return;int oldbox[4]={0,0,0,0};float oldcc[4]={0,0,0,0};gi(0x0C10u,oldbox);if(gf)gf(0x0C22u,oldcc);int was=ie?ie(0x0C11u):0;en(0x0C11u);int x=(int)g_bender_input_cursor_x,y=BENDER_DISPLAY_HEIGHT-1-(int)g_bender_input_cursor_y;if(g_bender_key_editor_active_2745){bender_draw_keymap_editor_2745(sc,clr,cc);}else if(g_bender_stick_editor_active_2745){bender_draw_stick_editor_2745(sc,clr,cc);}else{if(g_bender_stick_touch_enabled_2744&&g_bender_stick_touch_visible_2744){int lx=(int)g_bender_stick_lcx_2744,ly=BENDER_DISPLAY_HEIGHT-1-(int)g_bender_stick_lcy_2744,lr=(int)g_bender_stick_lr_2744;int rx=(int)g_bender_stick_rcx_2744,ry=BENDER_DISPLAY_HEIGHT-1-(int)g_bender_stick_rcy_2744,rr=(int)g_bender_stick_rr_2744;cc(0.9f,0.9f,0.9f,1.0f);bender_draw_box_outline_2745(sc,clr,lx,ly,lr);bender_draw_box_outline_2745(sc,clr,rx,ry,rr);}if(g_bender_input_keyboard_active){bender_draw_keyboard_v3(sc,clr,cc);}else if(g_bender_input_mouse_active||g_bender_input_touchmap_active){cc(1.0f,1.0f,1.0f,1.0f);bender_draw_arrow(sc,clr,x,y);}}bender_draw_notice_v3(sc,clr,cc);bender_hud_draw_fix98(sc,clr,cc);if(gf)cc(oldcc[0],oldcc[1],oldcc[2],oldcc[3]);sc(oldbox[0],oldbox[1],oldbox[2],oldbox[3]);if(!was)dis(0x0C11u);}

/* [BENDER_RUNTIME_CONTROLS][FIX69] FN+LEFT=stick-touch FN+L3=direct-touch */
/* [BENDER_SOFT_KEYBOARD] source=fix99 text=nativeSetInputString layout=top-down */
/* [BENDER_SOFT_KEYBOARD] source=fix100 text=pooled-known-jstring-string-getBytes-reusable-byte-array */
/* ---------------- end BENDER Unity input test layer ---------------- */
/* ULTRAKILL fix -R: forward decls (as definicoes ficam junto do present por
 * framebuffer, mais abaixo). O Unity NUNCA chama eglSwapBuffers - a prova e que o
 * shim esta CARREGADO no processo e nunca imprimiu 'libEGL: swap', e o probe conta
 * guest_swaps=0 para sempre. Ele renderiza e para. As unicas oportunidades que
 * temos na THREAD do Unity, com o contexto do Unity CORRENTE, sao os calls GL dele:
 * glFlush/glFinish e o bind do framebuffer 0 (o resolve final para a janela). */
static void uk_flush_present_throttled(void);
static void bender_uk_glFlush(void);
static void bender_uk_glFinish(void);
static void bender_ri_announce(void){if(g_ri_announced)return;g_ri_announced=1;logmsg("[BENDER_RENDER_INSPECTOR][ENABLED] wrappers=clear,viewport,fbo,program,draw,blit framebuffer_sample=4x4 host_present_policy=single-present\\n");}
static void bender_ri_glClear(unsigned int mask){static BenderGLClearFn f;if(!f)f=(BenderGLClearFn)bender_gl_real_proc("glClear");__atomic_add_fetch(&g_ri_clear_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(mask);}
static void bender_ri_glClearColor(float r,float g,float b,float a){static BenderGLClearColorFn f;if(!f)f=(BenderGLClearColorFn)bender_gl_real_proc("glClearColor");g_ri_clear_color[0]=r;g_ri_clear_color[1]=g;g_ri_clear_color[2]=b;g_ri_clear_color[3]=a;__atomic_add_fetch(&g_ri_clear_color_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(r,g,b,a);}
static void bender_ri_glViewport(int x,int y,int w,int h){static BenderGLViewportFn f;if(!f)f=(BenderGLViewportFn)bender_gl_real_proc("glViewport");g_ri_viewport[0]=x;g_ri_viewport[1]=y;g_ri_viewport[2]=w;g_ri_viewport[3]=h;g_bender_rs_logical_viewport_2768[0]=x;g_bender_rs_logical_viewport_2768[1]=y;g_bender_rs_logical_viewport_2768[2]=w;g_bender_rs_logical_viewport_2768[3]=h;if(bender_render_inspector_enabled()){__atomic_add_fetch(&g_ri_viewport_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();}if(f){if(g_bender_rs_active_2768&&g_bender_rs_logical_draw_fbo_2768==0u){if(w<=g_bender_rs_render_w_2768&&h<=g_bender_rs_render_h_2768)f(x,y,w,h);else f(bender_rs_scale_coord_2768(x,g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768),bender_rs_scale_coord_2768(y,g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768),bender_rs_scale_size_2768(w,g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768),bender_rs_scale_size_2768(h,g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768));}else f(x,y,w,h);}}
static void bender_ri_glBindFramebuffer(unsigned int target,unsigned int fbo){static BenderGLBindFramebufferFn f;if(!f)f=(BenderGLBindFramebufferFn)bender_gl_real_proc("glBindFramebuffer");g_ri_last_fbo=fbo;if(bender_render_inspector_enabled()){__atomic_add_fetch(&g_ri_bind_fbo_calls,1ul,__ATOMIC_RELAXED);if(fbo==0u)__atomic_add_fetch(&g_ri_bind_fbo_zero_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();}
    /* ULTRAKILL fix -R: quando o Unity amarra o framebuffer 0 (janela) para o passe
     * final, apresentamos o conteudo ANTERIOR (1 frame de atraso, invisivel na pratica)
     * na thread dele, onde o contexto esta de facto corrente. */
    if(fbo==0u&&(target==BENDER_RS_GL_FRAMEBUFFER||target==BENDER_RS_GL_DRAW_FRAMEBUFFER))uk_flush_present_throttled();
    if(g_bender_rs_active_2768){if(target==BENDER_RS_GL_FRAMEBUFFER){g_bender_rs_logical_draw_fbo_2768=fbo;g_bender_rs_logical_read_fbo_2768=fbo;}else if(target==BENDER_RS_GL_DRAW_FRAMEBUFFER)g_bender_rs_logical_draw_fbo_2768=fbo;else if(target==BENDER_RS_GL_READ_FRAMEBUFFER)g_bender_rs_logical_read_fbo_2768=fbo;if(f)f(target,bender_rs_actual_fbo_2768(fbo));}else if(f)f(target,fbo);}
static void bender_ri_glUseProgram(unsigned int program){static BenderGLUseProgramFn f;if(!f)f=(BenderGLUseProgramFn)bender_gl_real_proc("glUseProgram");__atomic_add_fetch(&g_ri_use_program_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(program);}
static void bender_ri_glDrawArrays(unsigned int mode,int first,int count){static BenderGLDrawArraysFn f;if(!f)f=(BenderGLDrawArraysFn)bender_gl_real_proc("glDrawArrays");__atomic_add_fetch(&g_ri_draw_arrays_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(mode,first,count);}
static void bender_ri_glDrawElements(unsigned int mode,int count,unsigned int type,const void*indices){static BenderGLDrawElementsFn f;if(!f)f=(BenderGLDrawElementsFn)bender_gl_real_proc("glDrawElements");__atomic_add_fetch(&g_ri_draw_elements_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(mode,count,type,indices);}
static void bender_ri_glDrawArraysInstanced(unsigned int mode,int first,int count,int primcount){static BenderGLDrawArraysInstancedFn f;if(!f)f=(BenderGLDrawArraysInstancedFn)bender_gl_real_proc("glDrawArraysInstanced");__atomic_add_fetch(&g_ri_draw_instanced_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(mode,first,count,primcount);}
static void bender_ri_glDrawElementsInstanced(unsigned int mode,int count,unsigned int type,const void*indices,int primcount){static BenderGLDrawElementsInstancedFn f;if(!f)f=(BenderGLDrawElementsInstancedFn)bender_gl_real_proc("glDrawElementsInstanced");__atomic_add_fetch(&g_ri_draw_instanced_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(mode,count,type,indices,primcount);}
static void bender_ri_glDrawElementsBaseVertex(unsigned int mode,int count,unsigned int type,const void*indices,int basevertex){static BenderGLDrawElementsBaseVertexFn f;if(!f)f=(BenderGLDrawElementsBaseVertexFn)bender_gl_real_proc("glDrawElementsBaseVertex");__atomic_add_fetch(&g_ri_draw_elements_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(mode,count,type,indices,basevertex);}
static void bender_ri_glDrawElementsInstancedBaseVertex(unsigned int mode,int count,unsigned int type,const void*indices,int primcount,int basevertex){static BenderGLDrawElementsInstancedBaseVertexFn f;if(!f)f=(BenderGLDrawElementsInstancedBaseVertexFn)bender_gl_real_proc("glDrawElementsInstancedBaseVertex");__atomic_add_fetch(&g_ri_draw_instanced_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(mode,count,type,indices,primcount,basevertex);}
static void bender_ri_glDrawArraysIndirect(unsigned int mode,const void*indirect){static BenderGLDrawArraysIndirectFn f;if(!f)f=(BenderGLDrawArraysIndirectFn)bender_gl_real_proc("glDrawArraysIndirect");__atomic_add_fetch(&g_ri_draw_indirect_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(mode,indirect);}
static void bender_ri_glDrawElementsIndirect(unsigned int mode,unsigned int type,const void*indirect){static BenderGLDrawElementsIndirectFn f;if(!f)f=(BenderGLDrawElementsIndirectFn)bender_gl_real_proc("glDrawElementsIndirect");__atomic_add_fetch(&g_ri_draw_indirect_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(mode,type,indirect);}
static void bender_ri_glBlitFramebuffer(int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1,unsigned int mask,unsigned int filter){static BenderGLBlitFramebufferFn f;if(!f)f=(BenderGLBlitFramebufferFn)bender_gl_real_proc("glBlitFramebuffer");if(bender_render_inspector_enabled()){__atomic_add_fetch(&g_ri_blit_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();}if(f){if(g_bender_rs_active_2768&&g_bender_rs_logical_read_fbo_2768==0u&&(sx0>g_bender_rs_render_w_2768||sx1>g_bender_rs_render_w_2768||sy0>g_bender_rs_render_h_2768||sy1>g_bender_rs_render_h_2768)){sx0=bender_rs_scale_coord_2768(sx0,g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768);sy0=bender_rs_scale_coord_2768(sy0,g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768);sx1=bender_rs_scale_coord_2768(sx1,g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768);sy1=bender_rs_scale_coord_2768(sy1,g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768);}if(g_bender_rs_active_2768&&g_bender_rs_logical_draw_fbo_2768==0u&&(dx0>g_bender_rs_render_w_2768||dx1>g_bender_rs_render_w_2768||dy0>g_bender_rs_render_h_2768||dy1>g_bender_rs_render_h_2768)){dx0=bender_rs_scale_coord_2768(dx0,g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768);dy0=bender_rs_scale_coord_2768(dy0,g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768);dx1=bender_rs_scale_coord_2768(dx1,g_bender_rs_render_w_2768,g_bender_rs_physical_w_2768);dy1=bender_rs_scale_coord_2768(dy1,g_bender_rs_render_h_2768,g_bender_rs_physical_h_2768);}f(sx0,sy0,sx1,sy1,dx0,dy0,dx1,dy1,mask,filter);}}
static void bender_ri_glFramebufferTexture2D(unsigned int target,unsigned int attachment,unsigned int textarget,unsigned int texture,int level){static BenderGLFramebufferTexture2DFn f;if(!f)f=(BenderGLFramebufferTexture2DFn)bender_gl_real_proc("glFramebufferTexture2D");__atomic_add_fetch(&g_ri_fbo_attach_calls,1ul,__ATOMIC_RELAXED);bender_ri_announce();if(f)f(target,attachment,textarget,texture,level);}
static void bender_render_inspector_before_swap(unsigned long swap_index){
    if(!bender_render_inspector_enabled())return;
    static unsigned long pc,pda,pde,pdi,pindir,pbf,pbf0,pblit,puse,pattach;
    unsigned long cc=__atomic_load_n(&g_ri_clear_calls,__ATOMIC_RELAXED),da=__atomic_load_n(&g_ri_draw_arrays_calls,__ATOMIC_RELAXED),de=__atomic_load_n(&g_ri_draw_elements_calls,__ATOMIC_RELAXED),di=__atomic_load_n(&g_ri_draw_instanced_calls,__ATOMIC_RELAXED),indir=__atomic_load_n(&g_ri_draw_indirect_calls,__ATOMIC_RELAXED),bf=__atomic_load_n(&g_ri_bind_fbo_calls,__ATOMIC_RELAXED),bf0=__atomic_load_n(&g_ri_bind_fbo_zero_calls,__ATOMIC_RELAXED),blit=__atomic_load_n(&g_ri_blit_calls,__ATOMIC_RELAXED),use=__atomic_load_n(&g_ri_use_program_calls,__ATOMIC_RELAXED),attach=__atomic_load_n(&g_ri_fbo_attach_calls,__ATOMIC_RELAXED);
    if(swap_index<=12ul||swap_index%300ul==0ul){
        int fbo=(int)g_ri_last_fbo,vp[4]={g_ri_viewport[0],g_ri_viewport[1],g_ri_viewport[2],g_ri_viewport[3]};BenderGLGetIntegervFn gi=(BenderGLGetIntegervFn)bender_gl_real_proc("glGetIntegerv");BenderGLReadPixelsFn rp=(BenderGLReadPixelsFn)bender_gl_real_proc("glReadPixels");if(gi){gi(0x8CA6u,&fbo);gi(0x0BA2u,vp);}unsigned char pix[64];memset(pix,0,sizeof(pix));unsigned avg[4]={0,0,0,0},nonblack=0;int sampled=0;
        if(rp&&vp[2]>0&&vp[3]>0){int sx=vp[0]+vp[2]/2-2,sy=vp[1]+vp[3]/2-2;if(sx<vp[0])sx=vp[0];if(sy<vp[1])sy=vp[1];rp(sx,sy,4,4,0x1908u,0x1401u,pix);sampled=1;for(unsigned i=0;i<16u;i++){for(unsigned k=0;k<4u;k++)avg[k]+=pix[i*4u+k];if(pix[i*4u]||pix[i*4u+1u]||pix[i*4u+2u])nonblack++;}for(unsigned k=0;k<4u;k++)avg[k]/=16u;}
        logmsg("[BENDER_RENDER_INSPECTOR][FRAME] swap=%lu drawA=%lu drawE=%lu drawInst=%lu drawIndirect=%lu clear=%lu useProgram=%lu bindFbo=%lu bindFbo0=%lu blit=%lu attach=%lu currentFbo=%d viewport=%d,%d,%d,%d clearColor=%.3f,%.3f,%.3f,%.3f sample=%s avgRGBA=%u,%u,%u,%u nonblack=%u/16\\n",swap_index,da-pda,de-pde,di-pdi,indir-pindir,cc-pc,use-puse,bf-pbf,bf0-pbf0,blit-pblit,attach-pattach,fbo,vp[0],vp[1],vp[2],vp[3],g_ri_clear_color[0],g_ri_clear_color[1],g_ri_clear_color[2],g_ri_clear_color[3],sampled?"4x4":"none",avg[0],avg[1],avg[2],avg[3],nonblack);
    }
    pc=cc;pda=da;pde=de;pdi=di;pindir=indir;pbf=bf;pbf0=bf0;pblit=blit;puse=use;pattach=attach;
}

/* Unity/Android GLES compatibility for symbols that Mali exposes through GLES alternatives. */
typedef void (*BenderGLTexStorage2DMultisampleFn)(unsigned int,int,unsigned int,int,int,unsigned char);
typedef void (*BenderGLFramebufferTextureLayerFn)(unsigned int,unsigned int,unsigned int,int,int);
static void bender_glTexImage2DMultisample(unsigned int target,int samples,unsigned int internalformat,int width,int height,unsigned char fixedsamplelocations){
    BenderGLTexStorage2DMultisampleFn f=NULL;
    if(g_sdl_GL_GetProcAddress)f=(BenderGLTexStorage2DMultisampleFn)g_sdl_GL_GetProcAddress("glTexStorage2DMultisample");
    if(!f)f=(BenderGLTexStorage2DMultisampleFn)dlsym(RTLD_DEFAULT,"glTexStorage2DMultisample");
    if(f){f(target,samples,internalformat,width,height,fixedsamplelocations);return;}
    logmsg("[BENDER_GLES][BRIDGE_MISSING] glTexImage2DMultisample -> glTexStorage2DMultisample unavailable\n");
}
static void bender_glFramebufferTexture3D(unsigned int target,unsigned int attachment,unsigned int textarget,unsigned int texture,int level,int zoffset){
    (void)textarget; BenderGLFramebufferTextureLayerFn f=NULL;
    if(g_sdl_GL_GetProcAddress)f=(BenderGLFramebufferTextureLayerFn)g_sdl_GL_GetProcAddress("glFramebufferTextureLayer");
    if(!f)f=(BenderGLFramebufferTextureLayerFn)dlsym(RTLD_DEFAULT,"glFramebufferTextureLayer");
    if(f){f(target,attachment,texture,level,zoffset);return;}
    logmsg("[BENDER_GLES][BRIDGE_MISSING] glFramebufferTexture3D -> glFramebufferTextureLayer unavailable\n");
}
static void *bender_gles_proc_bridge(const char *name){
    if(!name)return NULL;
    /* ULTRAKILL fix -R: glFlush/glFinish do guest passam a ser NOSSOS wrappers, para
     * apresentar na thread do Unity (ele nao swapa nunca). BENDER_UK_FLUSH_PRESENT=0
     * devolve os originais sem recompilar. */
    if((!strcmp(name,"glFlush")||!strcmp(name,"glFinish"))){
        const char *e=getenv("BENDER_UK_FLUSH_PRESENT");
        if(!(e&&!strcmp(e,"0"))) return (!strcmp(name,"glFlush"))?(void*)bender_uk_glFlush:(void*)bender_uk_glFinish;
    }
    if(bender_render_scale_enabled_2768()){
        if(strcmp(name,"glViewport")==0)return (void*)bender_ri_glViewport;
        if(strcmp(name,"glBindFramebuffer")==0)return (void*)bender_ri_glBindFramebuffer;
        if(strcmp(name,"glBlitFramebuffer")==0)return (void*)bender_ri_glBlitFramebuffer;
        if(strcmp(name,"glScissor")==0)return (void*)bender_rs_glScissor_2768;
        if(strcmp(name,"glGetIntegerv")==0)return (void*)bender_rs_glGetIntegerv_2768;
    }
    if(bender_render_inspector_enabled()){
        if(strcmp(name,"glClear")==0)return (void*)bender_ri_glClear;
        if(strcmp(name,"glClearColor")==0)return (void*)bender_ri_glClearColor;
        if(strcmp(name,"glViewport")==0)return (void*)bender_ri_glViewport;
        if(strcmp(name,"glBindFramebuffer")==0)return (void*)bender_ri_glBindFramebuffer;
        if(strcmp(name,"glUseProgram")==0)return (void*)bender_ri_glUseProgram;
        if(strcmp(name,"glDrawArrays")==0)return (void*)bender_ri_glDrawArrays;
        if(strcmp(name,"glDrawElements")==0)return (void*)bender_ri_glDrawElements;
        if(strcmp(name,"glDrawArraysInstanced")==0)return (void*)bender_ri_glDrawArraysInstanced;
        if(strcmp(name,"glDrawElementsInstanced")==0)return (void*)bender_ri_glDrawElementsInstanced;
        if(strcmp(name,"glDrawElementsBaseVertex")==0)return (void*)bender_ri_glDrawElementsBaseVertex;
        if(strcmp(name,"glDrawElementsInstancedBaseVertex")==0)return (void*)bender_ri_glDrawElementsInstancedBaseVertex;
        if(strcmp(name,"glDrawArraysIndirect")==0)return (void*)bender_ri_glDrawArraysIndirect;
        if(strcmp(name,"glDrawElementsIndirect")==0)return (void*)bender_ri_glDrawElementsIndirect;
        if(strcmp(name,"glBlitFramebuffer")==0)return (void*)bender_ri_glBlitFramebuffer;
        if(strcmp(name,"glFramebufferTexture2D")==0)return (void*)bender_ri_glFramebufferTexture2D;
    }
    if(strcmp(name,"glTexImage2DMultisample")==0)return (void*)bender_glTexImage2DMultisample;
    if(strcmp(name,"glFramebufferTexture3D")==0)return (void*)bender_glFramebufferTexture3D;
    return NULL;
}

static void *bender_eglGetProcAddress(const char *name){
    if(name){void *bp=bender_gles_proc_bridge(name);if(bp){logmsg("[BENDER_EGL][GET_PROC][BENDER_GLES] name=%s -> %p\n",name,bp);return bp;}}
    if(g_bender_sdl_ready&&g_sdl_GL_GetProcAddress&&name){void *p=g_sdl_GL_GetProcAddress(name);logmsg("[BENDER_EGL][GET_PROC][SDL] name=%s -> %p\n",name,p);if(p)return p;}
    typedef void *(*Fn)(const char*);Fn f=(Fn)bender_host_egl_sym("eglGetProcAddress");void *p=f?f(name):NULL;logmsg("[BENDER_EGL][GET_PROC][RAW] name=%s -> %p\n",name?name:"(null)",p);if(!p&&name){p=bender_gles_proc_bridge(name);if(p)logmsg("[BENDER_EGL][GET_PROC][BENDER_GLES_RAW_FALLBACK] name=%s -> %p\n",name,p);}return p;
}
static const char *bender_eglQueryString(BenderEGLDisplay d,BenderEGLint name){
    if(d==BENDER_EGL_DISPLAY_COOKIE&&g_bender_sdl_ready){const char *r=NULL;switch(name){case BENDER_EGL_VENDOR:r="BENDER SDL2/KMSDRM EGL shim";break;case BENDER_EGL_VERSION:r="1.4 BENDER-SDL";break;case BENDER_EGL_EXTENSIONS:r="EGL_KHR_create_context";break;case BENDER_EGL_CLIENT_APIS:r="OpenGL_ES";break;default:g_bender_egl_error=BENDER_EGL_BAD_PARAMETER;break;}logmsg("[BENDER_EGL][QUERY_STRING][SDL] name=0x%x -> %s\n",name,r?r:"(null)");return r;}
    typedef const char *(*Fn)(BenderEGLDisplay,BenderEGLint);Fn f=(Fn)bender_host_egl_sym("eglQueryString");return f?f(d,name):NULL;
}
static BenderEGLBoolean bender_eglQuerySurface(BenderEGLDisplay d,BenderEGLSurface s,BenderEGLint attr,BenderEGLint *value){
    if(d==BENDER_EGL_DISPLAY_COOKIE&&s==BENDER_EGL_SURFACE_COOKIE&&value&&g_bender_sdl_ready){g_sdl_GL_GetDrawableSize(g_bender_sdl_window,&g_bender_sdl_draw_w,&g_bender_sdl_draw_h);if(attr==BENDER_EGL_WIDTH)*value=bender_render_scale_guest_width_2768();else if(attr==BENDER_EGL_HEIGHT)*value=bender_render_scale_guest_height_2768();else return bender_egl_fail(BENDER_EGL_BAD_ATTRIBUTE);logmsg("[BENDER_EGL][QUERY_SURFACE][SDL] attr=0x%x value=%d\n",attr,*value);return BENDER_EGL_TRUE;}
    typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay,BenderEGLSurface,BenderEGLint,BenderEGLint*);Fn f=(Fn)bender_host_egl_sym("eglQuerySurface");return f?f(d,s,attr,value):0;
}
static BenderEGLBoolean bender_eglSurfaceAttrib(BenderEGLDisplay d,BenderEGLSurface s,BenderEGLint attr,BenderEGLint value){if(d==BENDER_EGL_DISPLAY_COOKIE&&s==BENDER_EGL_SURFACE_COOKIE){logmsg("[BENDER_EGL][SURFACE_ATTRIB][SDL] attr=0x%x value=%d ignored=1\n",attr,value);return BENDER_EGL_TRUE;}typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay,BenderEGLSurface,BenderEGLint,BenderEGLint);Fn f=(Fn)bender_host_egl_sym("eglSurfaceAttrib");return f?f(d,s,attr,value):0;}

/* ================= ULTRAKILL: present por /dev/fb0 (nunca toca no CRTC) ====
 * Lição da rodada anterior: assumir o CRTC (drmModeSetCrtc) e não devolver
 * congela a UI do EmulationStation na tela de transição do ArkOS. O fbdev é o
 * mesmo painel, é escrevível pelo usuário "ark" neste firmware, e escrever nele
 * não interfere nos FBs/modo do ES: quando o port sai, a ES repõe sozinha.
 * Sem fb0 => roda headless (só log). KMS não é mais opção.
 * BENDER_UK_PRESENT=off desliga o blit. */
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef void (*UkReadPixelsFn)(int, int, int, int, unsigned, unsigned, void *);

static int      g_uk_fbfd = -1;
static void    *g_uk_fbmap;
static size_t   g_uk_fblen;
static unsigned  g_uk_w, g_uk_h, g_uk_pitch, g_uk_bpp;
static unsigned  g_uk_ro, g_uk_go, g_uk_bo, g_uk_rl, g_uk_gl, g_uk_bl;
static unsigned char *g_uk_buf;
static UkReadPixelsFn g_uk_read;
static unsigned long  g_uk_present_calls;
static int            g_uk_off;

static void uk_log(const char *fmt, ...) {
    char line[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n > 0) logmsg("%s", line);
}

static void uk_test_pattern(unsigned char *base, unsigned w, unsigned h, unsigned pitch) {
    unsigned yy, xx;
    for (yy = 0; yy < h; yy++) {
        unsigned char *row = base + (size_t)yy * pitch;
        for (xx = 0; xx < w; xx++) {
            unsigned char *px = row + (size_t)xx * 4u;
            if (yy < 24u) { px[0] = px[1] = px[2] = 0xff; }
            else {
                px[0] = (unsigned char)(xx * 255u / (w ? w : 1));
                px[1] = (unsigned char)(yy * 255u / (h ? h : 1));
                px[2] = (unsigned char)(255u - (xx * 255u / (w ? w : 1)));
            }
            px[3] = 0xff;
        }
    }
}

/* ULTRAKILL fix -S: paginação e dimensionamento do readback. O log de 05/11 08:37
 * mostrou o quadro real (bytes_nao_zero ~27%, 600 presents na thread do Unity) mas
 * com dois defeitos: (1) lemos 640x360 de um backbuffer de 1280x720 -> so um canto
 * ampliado = 'monte de pedacos do jogo'; (2) escrevemos SEMPRE na pagina 0 do fb,
 * e se o CRTC esta a varrer a pagina 1 (yres_virtual = 2*yres) a tela fica PRETA
 * enquanto a captura mostra os fragmentos. Estas variaveis resolvem os dois. */
static struct fb_var_screeninfo g_uk_var;
static size_t g_uk_page_bytes;
static unsigned g_uk_page, g_uk_double, g_uk_cap_w, g_uk_cap_h;

static int uk_fb_init(int src_w, int src_h) {
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo finfo;
    const char *dev = getenv("ULTRAKILL_FB_DEVICE");
    size_t need;
    if (!dev || !*dev) dev = "/dev/fb0";
    g_uk_fbfd = open(dev, O_RDWR | O_CLOEXEC);
    if (g_uk_fbfd < 0) {
        uk_log("[BENDER_UK_PRESENT] fb0: open %s falhou: %s\n", dev, strerror(errno));
        return -1;
    }
    if (ioctl(g_uk_fbfd, FBIOGET_VSCREENINFO, &var) || ioctl(g_uk_fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        uk_log("[BENDER_UK_PRESENT] fb0: ioctl VS/FIS falhou: %s\n", strerror(errno));
        return -1;
    }
    if (var.bits_per_pixel != 32 && var.bits_per_pixel != 16) {
        var.bits_per_pixel = 32; var.grayscale = 0;
        var.red.length = var.green.length = var.blue.length = 8;
        var.red.offset = 16; var.green.offset = 8; var.blue.offset = 0;
        var.transp.length = 0; var.transp.offset = 24;
        if (ioctl(g_uk_fbfd, FBIOPUT_VSCREENINFO, &var) ||
            ioctl(g_uk_fbfd, FBIOGET_VSCREENINFO, &var)) {
            uk_log("[BENDER_UK_PRESENT] fb0: nao consegui 32bpp: %s\n", strerror(errno));
            return -1;
        }
    }
    g_uk_w = var.xres; g_uk_h = var.yres; g_uk_pitch = finfo.line_length; g_uk_bpp = var.bits_per_pixel;
    g_uk_var = var;
    g_uk_page_bytes = (size_t)finfo.line_length * (size_t)var.yres;
    g_uk_double = (var.yres_virtual >= 2u * var.yres) ? 1u : 0u;
    /* capacity: o maior entre o painel e o pedido, com teto obvio em 1280x720 para
     * nao estourar no readback (o buffer e alocado por isto). */
    g_uk_cap_w = var.xres > 1280u ? var.xres : 1280u;
    g_uk_cap_h = var.yres > 720u ? var.yres : 720u;
    g_uk_ro = var.red.offset; g_uk_go = var.green.offset; g_uk_bo = var.blue.offset;
    g_uk_rl = var.red.length ? var.red.length : 8;
    g_uk_gl = var.green.length ? var.green.length : 8;
    g_uk_bl = var.blue.length ? var.blue.length : 8;
    need = (size_t)finfo.line_length * (var.yres_virtual ? var.yres_virtual : var.yres);
    g_uk_fblen = (finfo.smem_len && finfo.smem_len < need) ? finfo.smem_len : need;
    g_uk_fbmap = mmap(0, g_uk_fblen, PROT_READ | PROT_WRITE, MAP_SHARED, g_uk_fbfd, 0);
    if (g_uk_fbmap == MAP_FAILED) {
        g_uk_fbmap = NULL;
        uk_log("[BENDER_UK_PRESENT] fb0: mmap de %zu falhou: %s\n", g_uk_fblen, strerror(errno));
        return -1;
    }
    g_uk_buf = (unsigned char *)malloc((size_t)g_uk_cap_w * (size_t)g_uk_cap_h * 4u);
    if (!g_uk_buf) g_uk_buf = (unsigned char *)malloc(1280u * 720u * 4u);
    if (!g_uk_buf) { uk_log("[BENDER_UK_PRESENT] sem buffer de readback\n"); return -1; }
    /* forca a pagina visivel para 0 no arranque, senao comecamos a escrever numa
     * pagina que pode nao ser a que esta a ser varrida. */
    if (g_uk_double) {
        g_uk_var.yoffset = 0;
        if (ioctl(g_uk_fbfd, FBIOPAN_DISPLAY, &g_uk_var) == 0) {
            uk_log("[BENDER_UK_PRESENT] fb duplo: yres_virtual=%u pagina=%zu -> pan yoffset=0 OK\n",
                   g_uk_var.yres_virtual, g_uk_page_bytes);
        } else {
            uk_log("[BENDER_UK_PRESENT] fb duplo: FBIOPAN falhou (%s); escrevemos na pagina 0\n", strerror(errno));
        }
    }
    uk_log("[BENDER_UK_PRESENT] painel=%ux%u virtual=%u pitch=%u bpp=%u duplo=%u buffer=%ux%u\n",
           g_uk_w, g_uk_h, g_uk_var.yres_virtual, g_uk_pitch, g_uk_bpp, g_uk_double, g_uk_cap_w, g_uk_cap_h);
    uk_log("[BENDER_UK_PRESENT] backend=fb0 %ux%u bpp=%u pitch=%u (source %dx%d) - CRTC intacto\n",
           g_uk_w, g_uk_h, g_uk_bpp, g_uk_pitch, src_w, src_h);
    uk_test_pattern((unsigned char *)g_uk_fbmap, g_uk_w, g_uk_h, g_uk_pitch);
    uk_log("[BENDER_UK_PRESENT] padrao de teste pintado (tarja branca + degrade RGB). Se aparecer, o video esta vivo.\n");
    return 0;
}

static void *uk_gl_addr(const char *name) {
    static void *gles;
    void *p = dlsym(RTLD_DEFAULT, name);
    if (p) return p;
    if (!gles) {
        const char *gv = getenv("ULTRAKILL_REAL_LIBGLESV2");
        gles = dlopen((gv && *gv) ? gv : "libGLESv2.so.2", RTLD_NOW | RTLD_NOLOAD);
        if (!gles) gles = dlopen((gv && *gv) ? gv : "libGLESv2.so.2", RTLD_NOW);
    }
    return gles ? dlsym(gles, name) : NULL;
}

static void uk_fb_blit(const unsigned char *rgba, int sw, int sh) {
    unsigned char *base = (unsigned char *)g_uk_fbmap;
    unsigned y, x;
    /* fix -S: escreve na pagina de tras (g_uk_page ja aponta para ela) - senao, com
     * fb de duas paginas, pintamos uma pagina que o CRTC nao esta a mostrar. */
    if (g_uk_double && g_uk_page_bytes && g_uk_page)
        base += g_uk_page_bytes;
    for (y = 0; y < g_uk_h; y++) {
        unsigned sy = (unsigned)((unsigned long long)y * (unsigned long long)sh / (g_uk_h ? g_uk_h : 1));
        unsigned char *drow;
        const unsigned char *srow;
        if (sy >= (unsigned)sh) sy = sh - 1;
        if ((size_t)(base - (unsigned char *)g_uk_fbmap) + (size_t)y * g_uk_pitch + (size_t)g_uk_w * (g_uk_bpp / 8u) > g_uk_fblen) break;
        srow = rgba + (size_t)sy * (size_t)sw * 4u;
        drow = base + (size_t)y * g_uk_pitch;
        for (x = 0; x < g_uk_w; x++) {
            unsigned sx = (unsigned)((unsigned long long)x * (unsigned long long)sw / (g_uk_w ? g_uk_w : 1));
            const unsigned char *p;
            if (sx >= (unsigned)sw) sx = sw - 1;
            p = srow + (size_t)sx * 4u;
            if (g_uk_bpp == 32) {
                uint32_t v = ((uint32_t)(p[0] >> (8 - g_uk_rl)) << g_uk_ro)
                           | ((uint32_t)(p[1] >> (8 - g_uk_gl)) << g_uk_go)
                           | ((uint32_t)(p[2] >> (8 - g_uk_bl)) << g_uk_bo);
                ((uint32_t *)drow)[x] = v;
            } else {
                uint16_t v = (uint16_t)(((p[0] >> (8 - g_uk_rl)) << g_uk_ro)
                      | ((p[1] >> (8 - g_uk_gl)) << g_uk_go)
                      | ((p[2] >> (8 - g_uk_bl)) << g_uk_bo));
                ((uint16_t *)drow)[x] = v;
            }
        }
    }
}

/* ULTRAKILL fix -O: o log de 05/11 07:33 foi cirurgico:
 *   [BENDER_UK_PRESENT] backend=fb0 1280x720 bpp=32 pitch=5120  <- fb0 ABRIU e PINTOU
 *   [BENDER_UK_PRESENT] frame=1..5 readback=640x360 bytes_iguais_zero=921600/921600
 *   [BENDER_EGL][SWAP_INTERVAL] error=The specified window has not been made current
 * ou seja: lemos o framebuffer 0 e ele esta VAZIO, porque (a) o Unity desenha no
 * FBO dele (a copia FBO->backbuffer so acontece no swap, e guest_swaps=0 para
 * sempre) e (b) nesta thread pode nem haver contexto corrente.
 * Estes helpers resolvem os dois: garantem contexto SOb se (e somente se) nao ha
 * nenhum, para nao roubar o contexto do GfxDevice thread do Unity, e descobrem
 * qual FBO tem conteudo de verdade, usando-o como fonte dos frames seguintes. */
typedef void  (*UkGetIntegerVFn)(unsigned, int *);
typedef void  (*UkBindFramebufferFn)(unsigned, unsigned);
typedef const unsigned char *(*UkGetStringFn)(unsigned);
static UkGetIntegerVFn    g_uk_get_integerv;
static UkBindFramebufferFn g_uk_bind_fbo;
static UkGetStringFn      g_uk_get_string;
static unsigned g_uk_src_fbo;      /* 0 = framebuffer padrao */
static int      g_uk_probe_left = 6;   /* tentativas de descoberta de FBO */
static int      g_uk_ctx_announced;
#define UK_GL_READ_FRAMEBUFFER   0x8CA8u
#define UK_GL_DRAW_FRAMEBUFFER   0x8CA9u
#define UK_GL_FRAMEBUFFER        0x8D40u
#define UK_GL_FRAMEBUFFER_BINDING 0x8CA6u
#define UK_GL_READ_FRAMEBUFFER_BINDING 0x8CAAu
#define UK_GL_VERSION            0x1F02u
#define UK_GL_COLOR_BUFFER_BIT   0x00004000u
#define UK_GL_NEAREST            0x2600u
typedef void (*UkBlitFramebufferFn)(int,int,int,int,int,int,int,int,unsigned,unsigned,int);
static UkBlitFramebufferFn g_uk_blit_fb;
static unsigned long g_uk_blit_calls;

/* ULTRAKILL fix -P: segundo caminho de present, e o que tem de funcionar neste
 * hardware: copia o FBO do Unity para o framebuffer 0 do CONTEXTO DO SDL e faz
 * SDL_GL_SwapWindow - e exatamente o caminho que faz o Cuphead aparecer no
 * TrimUI Smart Pro (SDL driver=mali). Nao precisa shader: glBlitFramebuffer
 * (GLES3) resolve. BENDER_UK_BLIT_SDL=0 desliga. */
static int uk_sdl_blit_present(int w, int h){
    const char *off = getenv("BENDER_UK_BLIT_SDL");
    int saved_draw = 0, saved_read = 0;
    int dw, dh;
    if(off && (!strcmp(off,"0") || !strcmp(off,"off"))) return 0;
    if(!g_bender_sdl_ready || !g_sdl_GL_MakeCurrent || !g_sdl_GL_SwapWindow) return 0;
    if(!g_uk_blit_fb) g_uk_blit_fb = (UkBlitFramebufferFn)uk_gl_addr("glBlitFramebuffer");
    if(!g_uk_blit_fb){
        static int announced;
        if(!announced){ announced = 1; uk_log("[BENDER_UK_BLIT] glBlitFramebuffer ausente no driver; ficando so no fb0\n"); }
        return 0;
    }
    if(g_sdl_GL_MakeCurrent(g_bender_sdl_window, g_bender_sdl_gl) != 0) return 0;
    if(g_uk_get_integerv){
        g_uk_get_integerv(UK_GL_FRAMEBUFFER_BINDING, &saved_draw);
        g_uk_get_integerv(UK_GL_READ_FRAMEBUFFER_BINDING, &saved_read);
    }
    dw = g_bender_sdl_draw_w > 0 ? g_bender_sdl_draw_w : w;
    dh = g_bender_sdl_draw_h > 0 ? g_bender_sdl_draw_h : h;
    if(g_uk_bind_fbo){
        g_uk_bind_fbo(UK_GL_READ_FRAMEBUFFER, g_uk_src_fbo);
        g_uk_bind_fbo(UK_GL_DRAW_FRAMEBUFFER, 0u);
    }
    g_uk_blit_fb(0, 0, w, h, 0, 0, dw, dh, UK_GL_COLOR_BUFFER_BIT, UK_GL_NEAREST, 0);
    if(g_uk_bind_fbo){
        g_uk_bind_fbo(UK_GL_DRAW_FRAMEBUFFER, (unsigned)saved_draw);
        g_uk_bind_fbo(UK_GL_READ_FRAMEBUFFER, (unsigned)saved_read);
    }
    g_sdl_GL_SwapWindow(g_bender_sdl_window);
    ++g_uk_blit_calls;
    if(g_uk_blit_calls <= 3ul || g_uk_blit_calls % 300ul == 0ul)
        uk_log("[BENDER_UK_BLIT] FBO=%u -> FB0 SDL %dx%d swap #%lu\n", g_uk_src_fbo, dw, dh, g_uk_blit_calls);
    return 1;
}

static void uk_gl_syms(void){
    if(!g_uk_get_integerv)  g_uk_get_integerv  = (UkGetIntegerVFn)uk_gl_addr("glGetIntegerv");
    if(!g_uk_bind_fbo)      g_uk_bind_fbo      = (UkBindFramebufferFn)uk_gl_addr("glBindFramebuffer");
    if(!g_uk_get_string)    g_uk_get_string    = (UkGetStringFn)uk_gl_addr("glGetString");
}
/* Varredura esbocada: 1 byte a cada 997. Barato e suficiente para 'tem imagem'. */
static int uk_buf_has_content(const unsigned char *b, size_t n){
    size_t k;
    for(k = 0; k < n; k += 997u) if(b[k]) return 1;
    return 0;
}
static void uk_ensure_current(void){
    int none;
    uk_gl_syms();
    none = g_uk_get_string && g_uk_get_string(UK_GL_VERSION) == NULL;
    if(!none) return;
    /* Tenta em toda chamada (barato): se so logarmos/tentarmos uma vez e o
     * primeiro MakeCurrent falhar, ficamos cegos para o resto da sessao. */
    if(g_sdl_GL_MakeCurrent && g_bender_sdl_window && g_bender_sdl_gl){
        int rc = g_sdl_GL_MakeCurrent(g_bender_sdl_window, g_bender_sdl_gl);
        if(!g_uk_ctx_announced){
            g_uk_ctx_announced = 1;
            uk_log("[BENDER_UK_PRESENT] sem contexto corrente nesta thread; SDL_GL_MakeCurrent rc=%d error=%s\n",
                   rc, g_sdl_GetError ? g_sdl_GetError() : "?");
        }
    } else if(!g_uk_ctx_announced){
        g_uk_ctx_announced = 1;
        uk_log("[BENDER_UK_PRESENT] sem contexto corrente e sem SDL_GL_MakeCurrent disponivel\n");
    }
}

static void uk_present_init(int w, int h) {
    const char *off = getenv("BENDER_UK_PRESENT");
    if (off && !strcmp(off, "off")) {
        g_uk_off = 1;
        uk_log("[BENDER_UK_PRESENT] desligado por BENDER_UK_PRESENT=off\n");
        return;
    }
    if (uk_fb_init(w, h) != 0) {
        g_uk_off = 1;
        uk_log("[BENDER_UK_PRESENT] fb0 indisponivel; rodando HEADLESS (sem KMS, para nao sequestrar a tela)\n");
    }
}

static void uk_present(int w, int h) {
    size_t total, k, nz = 0;
    int saved = 0;
    if (g_uk_off || !g_uk_buf || !g_uk_fbmap || w <= 0 || h <= 0) return;
    if (!g_uk_read) {
        g_uk_read = (UkReadPixelsFn)uk_gl_addr("glReadPixels");
        if (!g_uk_read) {
            g_uk_off = 1;
            uk_log("[BENDER_UK_PRESENT] glReadPixels ausente; blit desligado\n");
            return;
        }
    }
    uk_ensure_current();
    uk_gl_syms();
    if (g_uk_bind_fbo && g_uk_get_integerv)
        g_uk_get_integerv(UK_GL_FRAMEBUFFER_BINDING, &saved);
    total = (size_t)w * (size_t)h * 4u;

    /* 1) fonte conhecida (FBO descoberto ou o padrao) */
    if (g_uk_bind_fbo) g_uk_bind_fbo(UK_GL_READ_FRAMEBUFFER, g_uk_src_fbo);
    g_uk_read(0, 0, w, h, 0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/, g_uk_buf);
    if (uk_buf_has_content(g_uk_buf, total)) goto present;

    /* 2) vazio: procure um FBO que tenha imagem (o backbuffer do Unity). So nos
     * primeiros frames e poucas tentativas, para nao pesar em 30 FPS. */
    if (g_uk_probe_left > 0 && g_uk_bind_fbo) {
        unsigned id;
        g_uk_probe_left--;
        for (id = 1u; id <= 64u; id++) {
            g_uk_bind_fbo(UK_GL_READ_FRAMEBUFFER, id);
            g_uk_read(0, 0, w, h, 0x1908, 0x1401, g_uk_buf);
            if (uk_buf_has_content(g_uk_buf, total)) {
                g_uk_src_fbo = id;
                uk_log("[BENDER_UK_PRESENT][FBO] fonte encontrada: GL_READ_FRAMEBUFFER=%u (%dx%d) - usando como quadro\n", id, w, h);
                goto present;
            }
        }
        g_uk_bind_fbo(UK_GL_READ_FRAMEBUFFER, g_uk_src_fbo);
        g_uk_read(0, 0, w, h, 0x1908, 0x1401, g_uk_buf);
        if (uk_buf_has_content(g_uk_buf, total)) goto present;
        uk_log("[BENDER_UK_PRESENT][FBO] nenhum FBO 1..64 com conteudo; tentativas restantes=%d\n", g_uk_probe_left);
        if (g_uk_bind_fbo) g_uk_bind_fbo(UK_GL_READ_FRAMEBUFFER, (unsigned)saved);
        return;   /* nao bata lixo preto no painel */
    }
    if (g_uk_bind_fbo) g_uk_bind_fbo(UK_GL_READ_FRAMEBUFFER, (unsigned)saved);
    return;

present:
    if (g_uk_bind_fbo) g_uk_bind_fbo(UK_GL_READ_FRAMEBUFFER, (unsigned)saved);
    /* fix -S: escreve na pagina do fundo e depois vira o CRTC para ela. */
    if (g_uk_double && g_uk_page_bytes) g_uk_page ^= 1u;
    uk_fb_blit(g_uk_buf, w, h);
    if (g_uk_double && g_uk_page_bytes) {
        g_uk_var.yoffset = (unsigned)(g_uk_page * (g_uk_page_bytes / (g_uk_pitch ? g_uk_pitch : 1u)));
        if (ioctl(g_uk_fbfd, FBIOPAN_DISPLAY, &g_uk_var) != 0) {
            static int pan_warned;
            if (!pan_warned) { pan_warned = 1; uk_log("[BENDER_UK_PRESENT] FBIOPAN falhou (%s); sem flip\n", strerror(errno)); }
            g_uk_double = 0;
        }
    }
    g_uk_present_calls++;
    if (g_uk_present_calls <= 5ul || g_uk_present_calls % 300ul == 0ul) {
        for (k = 0; k < total; k++) if (g_uk_buf[k]) nz++;
        uk_log("[BENDER_UK_PRESENT] frame=%lu readback=%dx%d fbo=%u bytes_zero=%zu/%zu\n",
               g_uk_present_calls, w, h, g_uk_src_fbo, total - nz, total);
    }
}

/* ULTRAKILL fix -R: apresentacao na thread do proprio Unity. Chamado por
 * glFlush/glFinish e pelo bind do FB0 - os tres unicos momentos em que o contexto
 * do Unity esta corrente e a gente pode ler o quadro dele. Taxa limitada a ~35/s
 * para nao afogar o 30 FPS alvo com readback+blit a cada call. */
static unsigned long g_uk_flush_present_calls;
static long long g_uk_flush_last_ms;
static long long uk_monotonic_ms(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return 0;
    return (long long)ts.tv_sec*1000LL+(long long)(ts.tv_nsec/1000000L);
}
static void uk_flush_present_throttled(void){
    const char *e=getenv("BENDER_UK_FLUSH_PRESENT");
    if(e&&!strcmp(e,"0"))return;
    long long now=uk_monotonic_ms();
    if(g_uk_flush_last_ms&&(now-g_uk_flush_last_ms)<28LL)return;
    g_uk_flush_last_ms=now;
    uk_fb_present_frame(0,0);
    unsigned long n=__atomic_add_fetch(&g_uk_flush_present_calls,1ul,__ATOMIC_RELAXED);
    if(n<=3ul||n%300ul==0ul)logmsg("[BENDER_UK_FLUSH_PRESENT] n=%lu apresentando na thread do Unity (contexto corrente)\n",n);
}
static void bender_uk_glFlush(void){typedef void(*Fn)(void);Fn f=(Fn)bender_gl_real_proc("glFlush");if(f)f();uk_flush_present_throttled();}
static void bender_uk_glFinish(void){typedef void(*Fn)(void);Fn f=(Fn)bender_gl_real_proc("glFinish");if(f)f();uk_flush_present_throttled();}

/* ULTRAKILL fix -N: present por framebuffer para o caminho COM SDL.
 * O blit para /dev/fb0 ja existia, mas so era chamado dentro de
 * bender_eglSwapBuffers no ramo !g_bender_sdl_ready - e no TrimUI Smart Pro o
 * SDL (driver=mali) fica pronto, entao esse ramo nunca era tomado e o jogo
 * rodava mudo de video (audio OK, guest_swaps=0, tela parada no ultimo frame
 * da ES). Aqui lemos o framebuffer GL ATUAL (glReadPixels, mesmo contexto que o
 * Unity deixou corrente apos o nativeRender) e copiamos para o painel.
 * BENDER_UK_FB=0 desliga; BENDER_UK_FB_W/H forcem o tamanho do readback.
 * O tamanho e congelado no primeiro frame (o buffer e alocado la) e nunca
 * excedido, senao o glReadPixels escreveria fora do heap. */
static void uk_fb_present_frame(int w,int h){
    const char *off=getenv("BENDER_UK_FB");
    if(off&&(!strcmp(off,"0")||!strcmp(off,"off")))return;
    if(g_uk_off)return;
    int W=w>0?w:0,H=h>0?h:0;
    const char *ew=getenv("BENDER_UK_FB_W"),*eh=getenv("BENDER_UK_FB_H");
    if(ew&&*ew)W=atoi(ew);
    if(eh&&*eh)H=atoi(eh);
    if(W<=0||H<=0){
        /* fix -S: o tamanho tem de ser o que o GL esta mesmo a desenhar (viewport),
         * nao o do nosso ANativeWindow fake (640x360). Ler 640x360 dum backbuffer de
         * 1280x720 da um canto ampliado - e o 'monte de pedacos do jogo' reportado. */
        int vp[4] = {0,0,0,0};
        uk_gl_syms();
        if (g_uk_get_integerv) g_uk_get_integerv(0x0BA2u /*GL_VIEWPORT*/, vp);
        if (vp[2] > 0 && vp[3] > 0) { W = vp[2]; H = vp[3]; }
    }
    if(W<=0||H<=0){
        W=g_native_window.width>0?g_native_window.width:g_bender_sdl_draw_w;
        H=g_native_window.height>0?g_native_window.height:g_bender_sdl_draw_h;
    }
    if(W<=0||H<=0)return;
    if(g_uk_cap_w && W>(int)g_uk_cap_w)W=(int)g_uk_cap_w;
    if(g_uk_cap_h && H>(int)g_uk_cap_h)H=(int)g_uk_cap_h;
    /* fix -S: o buffer e alocado na CAPACIDADE do painel (uk_fb_init), entao o
     * tamanho do readback pode mudar entre frames sem estourar memoria - por isso
     * NAO congelamos mais o tamanho no primeiro init (antes, o primeiro vinha do
     * frame loop sem contexto = 640x360 e prendia o resto da sessao). */
    static int inited=0;
    if(!inited){ uk_present_init(W,H); inited=1; }
    uk_present(W,H);
    /* fix -P: alem do /dev/fb0, tenta o caminho do SDL (blit FBO->FB0+swap).
     * Se o painel for varrido pelo SDL/mali (como no Cuphead), e aqui que aparece. */
    uk_sdl_blit_present(W,H);
}

static BenderEGLBoolean bender_eglSwapBuffers(BenderEGLDisplay d,BenderEGLSurface s){
    if(d==BENDER_EGL_DISPLAY_COOKIE&&s==BENDER_EGL_SURFACE_COOKIE&&g_bender_sdl_ready){unsigned long next_swap=g_bender_egl_swaps+1ul;bender_render_inspector_before_swap(next_swap);bender_render_scale_present_2768();bender_input_draw_cursor();bender_gpu_watchdog_done("eglSwapBuffers-sdl");g_sdl_GL_SwapWindow(g_bender_sdl_window);++g_bender_egl_swaps;
        /* ULTRAKILL fix -Q (log de 05/11 07:56): este e o unico sitio onde o contexto
         * GL esta de facto corrente (a thread do frame loop nao tem: SDL_GL_MakeCurrent
         * rc=-1 'Unable to make EGL context current'), portanto so aqui o readback do
         * FBO do Unity pode apanhar imagem. BENDER_UK_SWAP_THREAD=0 desliga. */
        {const char *st=getenv("BENDER_UK_SWAP_THREAD");if(!(st&&!strcmp(st,"0")))uk_fb_present_frame(0,0);}
        bender_render_scale_resume_2768();if(g_bender_egl_swaps<=10||g_bender_egl_swaps%60u==0u)logmsg("[BENDER_EGL][SWAP][SDL] frame=%lu drawable=%dx%d render_scale=%s\n",g_bender_egl_swaps,g_bender_sdl_draw_w,g_bender_sdl_draw_h,g_bender_rs_active_2768?"75%":"OFF");return BENDER_EGL_TRUE;}
    typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay,BenderEGLSurface);Fn f=(Fn)bender_host_egl_sym("eglSwapBuffers");
    {
        /* ULTRAKILL: sem SDL nao ha quem leve o quadro a tela; blitamos
         * nos no dumb buffer. BENDER_UK_KMS=0 desliga sem recompilar. */
        BenderEGLBoolean rrc=f?f(d,s):0;
        if(rrc&&!g_bender_sdl_ready){
            const char *off=getenv("BENDER_UK_KMS");
            if(!(off&&!strcmp(off,"0"))){
                int W=g_native_window.width>0?g_native_window.width:640;
                int H=g_native_window.height>0?g_native_window.height:480;
                uk_fb_present_frame(W,H);
            }
        }
        return rrc;
    }
}
static BenderEGLBoolean bender_eglSwapInterval(BenderEGLDisplay d,BenderEGLint interval){
    if(d==BENDER_EGL_DISPLAY_COOKIE&&g_bender_sdl_ready){int rc=g_sdl_GL_SetSwapInterval?g_sdl_GL_SetSwapInterval(interval):0;logmsg("[BENDER_EGL][SWAP_INTERVAL][SDL] interval=%d rc=%d error=%s\n",interval,rc,bender_sdl_error());return rc==0?BENDER_EGL_TRUE:BENDER_EGL_FALSE;}
    typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay,BenderEGLint);Fn f=(Fn)bender_host_egl_sym("eglSwapInterval");return f?f(d,interval):0;
}
static BenderEGLBoolean bender_eglTerminate(BenderEGLDisplay d){
    if(d==BENDER_EGL_DISPLAY_COOKIE){logmsg("[BENDER_EGL][TERMINATE][SDL] keep-context-until-process-exit=1\n");g_bender_egl_current=0;return BENDER_EGL_TRUE;}
    typedef BenderEGLBoolean(*Fn)(BenderEGLDisplay);Fn f=(Fn)bender_host_egl_sym("eglTerminate");return f?f(d):0;
}
static BenderEGLBoolean bender_eglReleaseThread(void){if(g_bender_sdl_ready){g_bender_egl_current=0;return BENDER_EGL_TRUE;}typedef BenderEGLBoolean(*Fn)(void);Fn f=(Fn)bender_host_egl_sym("eglReleaseThread");return f?f():BENDER_EGL_TRUE;}
static BenderEGLBoolean bender_eglWaitClient(void){if(g_bender_sdl_ready)return BENDER_EGL_TRUE;typedef BenderEGLBoolean(*Fn)(void);Fn f=(Fn)bender_host_egl_sym("eglWaitClient");return f?f():BENDER_EGL_TRUE;}
/* ---------------- end BENDER EGL bridge ---------------- */

/* Manual ELF mappings are invisible to glibc dl_iterate_phdr. Android libc++abi/libunwind inside libil2cpp uses this to discover PT_GNU_EH_FRAME. */
typedef int (*BenderPhdrCallback)(struct dl_phdr_info*,size_t,void*);
typedef int (*BenderHostDlIteratePhdr)(BenderPhdrCallback,void*);
static BenderHostDlIteratePhdr g_bender_host_dl_iterate_phdr;static unsigned long g_bender_phdr_iter_calls;
static int bender_dl_iterate_phdr(BenderPhdrCallback cb,void*data){if(!cb)return 0;unsigned long call=__atomic_add_fetch(&g_bender_phdr_iter_calls,1ul,__ATOMIC_RELAXED);if(call<=8ul)logmsg("[BENDER_UNWIND][PHDR_BEGIN] call=%lu guest_modules=%d\n",call,g_module_count);for(int i=0;i<g_module_count;i++){Module*m=&g_modules[i];struct dl_phdr_info info;memset(&info,0,sizeof(info));info.dlpi_addr=(ElfW(Addr))m->bias;info.dlpi_name=m->path;info.dlpi_phdr=(const ElfW(Phdr)*)m->ph;info.dlpi_phnum=(ElfW(Half))m->eh->e_phnum;if(call<=8ul)logmsg("[BENDER_UNWIND][PHDR_GUEST] call=%lu module=%s bias=%p phnum=%u\n",call,m->name,(void*)m->bias,(unsigned)info.dlpi_phnum);int rc=cb(&info,sizeof(info),data);if(rc)return rc;}if(!g_bender_host_dl_iterate_phdr){void*h=dlopen("libc.so.6",RTLD_NOW|RTLD_LOCAL);if(h)g_bender_host_dl_iterate_phdr=(BenderHostDlIteratePhdr)dlsym(h,"dl_iterate_phdr");}if(g_bender_host_dl_iterate_phdr){int rc=g_bender_host_dl_iterate_phdr(cb,data);if(call<=8ul)logmsg("[BENDER_UNWIND][PHDR_HOST_DONE] call=%lu rc=%d\n",call,rc);return rc;}if(call<=8ul)logmsg("[BENDER_UNWIND][PHDR_HOST_MISSING] call=%lu\n",call);return 0;}

static long bender_sysconf(int name){
    long r;
    switch(name){
        case 0x27: case 0x28: r=sysconf(_SC_PAGESIZE); break; /* Android _SC_PAGESIZE/_SC_PAGE_SIZE */
        case 0x60: case 0x61: r=4; break;                    /* RK3326: configured/online CPUs */
        case 0x62: r=sysconf(_SC_PHYS_PAGES); break;
        case 0x63: r=sysconf(_SC_AVPHYS_PAGES); break;
        default: r=sysconf(name); break;
    }
    static unsigned long calls;unsigned long n=__atomic_add_fetch(&calls,1ul,__ATOMIC_RELAXED);
    if(n<=32ul||name==0x60||name==0x61)logmsg("[BENDER_BIONIC][SYSCONF] call=%lu android_name=0x%x -> %ld\n",n,name,r);
    return r;
}


/* V2.7.36: Android Media NDK compatibility layer.
 * Unity 6000.3 links libmediandk symbols even when a game is not actively
 * decoding video. These stubs preserve ABI and provide a safe no-media
 * backend: format objects work, extractor/codec objects are valid, and decode
 * queues report TRY_AGAIN_LATER / end-of-stream instead of crashing. */
typedef int32_t BenderMediaStatus;
typedef struct BenderMediaFormat {
    int32_t width,height,channel_count,sample_rate,color_format,frame_rate,stride;
    int64_t duration_us;
    float frame_rate_f;
    char mime[128];
    char language[64];
} BenderMediaFormat;
typedef struct BenderMediaExtractor { int selected_track; int eos; int has_source; } BenderMediaExtractor;
typedef struct BenderMediaCodec { int configured; int started; int input_given; int eos_queued; int eos_output_sent; unsigned output_polls; int format_change_pending; int eos_from_guest; int synthetic_frame_sent; int synthetic_frame_released; unsigned synthetic_frame_wait_polls; void *output_window; unsigned long media_generation_fix79; uint8_t input[16]; char mime[128]; BenderMediaFormat output; } BenderMediaCodec;
static void bender_fix78_image_reader_frame(void*,const char*);
/* FIX79: one media-source generation remains closed after synthetic EOS, preventing Unity decoder reopen loops. */
static unsigned long g_bender_fix79_media_generation;
static unsigned long g_bender_fix79_completed_generation;
static unsigned long g_bender_fix79_restart_rejects;
typedef struct BenderMediaCodecBufferInfo { int32_t offset; int32_t size; int64_t presentationTimeUs; uint32_t flags; } BenderMediaCodecBufferInfo;
#define BENDER_AMEDIA_OK 0
#define BENDER_AMEDIA_ERROR_INVALID_PARAMETER (-10000)
#define BENDER_AMEDIACODEC_INFO_TRY_AGAIN_LATER (-1)
static unsigned long g_bender_media_calls;
static const char *bender_AMEDIAFORMAT_KEY_DURATION="durationUs";
static const char *bender_AMEDIAFORMAT_KEY_WIDTH="width";
static const char *bender_AMEDIAFORMAT_KEY_HEIGHT="height";
static const char *bender_AMEDIAFORMAT_KEY_MIME="mime";
static const char *bender_AMEDIAFORMAT_KEY_CHANNEL_COUNT="channel-count";
static const char *bender_AMEDIAFORMAT_KEY_SAMPLE_RATE="sample-rate";
static const char *bender_AMEDIAFORMAT_KEY_COLOR_FORMAT="color-format";
static const char *bender_AMEDIAFORMAT_KEY_FRAME_RATE="frame-rate";
static const char *bender_AMEDIAFORMAT_KEY_LANGUAGE="language";
static const char *bender_AMEDIAFORMAT_KEY_STRIDE="stride";
static int bender_media_trace(void){unsigned long n=__atomic_add_fetch(&g_bender_media_calls,1ul,__ATOMIC_RELAXED);return n<=40ul||(n%300ul)==0ul;}
static BenderMediaFormat *bender_AMediaFormat_new(void){BenderMediaFormat*f=(BenderMediaFormat*)calloc(1,sizeof(*f));if(f)bender_strlcpy(f->mime,"application/octet-stream",sizeof(f->mime));if(bender_media_trace())logmsg("[BENDER_MEDIANDK][FORMAT_NEW] fmt=%p\\n",(void*)f);return f;}
static BenderMediaStatus bender_AMediaFormat_delete(BenderMediaFormat*f){if(bender_media_trace())logmsg("[BENDER_MEDIANDK][FORMAT_DELETE] fmt=%p\\n",(void*)f);free(f);return BENDER_AMEDIA_OK;}
static bool bender_AMediaFormat_getInt32(BenderMediaFormat*f,const char*k,int32_t*out){if(!f||!k||!out)return false;if(!strcmp(k,"width"))*out=f->width;else if(!strcmp(k,"height"))*out=f->height;else if(!strcmp(k,"channel-count"))*out=f->channel_count;else if(!strcmp(k,"sample-rate"))*out=f->sample_rate;else if(!strcmp(k,"color-format"))*out=f->color_format;else if(!strcmp(k,"frame-rate"))*out=f->frame_rate;else if(!strcmp(k,"stride"))*out=f->stride;else return false;return true;}
static bool bender_AMediaFormat_getInt64(BenderMediaFormat*f,const char*k,int64_t*out){if(!f||!k||!out)return false;if(!strcmp(k,"durationUs")){*out=f->duration_us;return true;}return false;}
static bool bender_AMediaFormat_getFloat(BenderMediaFormat*f,const char*k,float*out){if(!f||!k||!out)return false;if(!strcmp(k,"frame-rate")){*out=f->frame_rate_f?f->frame_rate_f:(float)f->frame_rate;return true;}return false;}
static bool bender_AMediaFormat_getString(BenderMediaFormat*f,const char*k,const char**out){if(!f||!k||!out)return false;if(!strcmp(k,"mime")){*out=f->mime;return true;}if(!strcmp(k,"language")){*out=f->language;return true;}return false;}
static void bender_AMediaFormat_setInt32(BenderMediaFormat*f,const char*k,int32_t v){if(!f||!k)return;if(!strcmp(k,"width"))f->width=v;else if(!strcmp(k,"height"))f->height=v;else if(!strcmp(k,"channel-count"))f->channel_count=v;else if(!strcmp(k,"sample-rate"))f->sample_rate=v;else if(!strcmp(k,"color-format"))f->color_format=v;else if(!strcmp(k,"frame-rate")){f->frame_rate=v;f->frame_rate_f=(float)v;}else if(!strcmp(k,"stride"))f->stride=v;}
static BenderMediaExtractor *bender_AMediaExtractor_new(void){BenderMediaExtractor*e=(BenderMediaExtractor*)calloc(1,sizeof(*e));if(e)e->selected_track=-1;if(bender_media_trace())logmsg("[BENDER_MEDIANDK][EXTRACTOR_NEW] ex=%p policy=synthetic-video-eos\n",(void*)e);return e;}
static BenderMediaStatus bender_AMediaExtractor_delete(BenderMediaExtractor*e){if(bender_media_trace())logmsg("[BENDER_MEDIANDK][EXTRACTOR_DELETE] ex=%p\n",(void*)e);free(e);return BENDER_AMEDIA_OK;}
static BenderMediaStatus bender_AMediaExtractor_setDataSource(BenderMediaExtractor*e,const char*src){if(!e)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;e->has_source=1;e->eos=0;unsigned long gen=__atomic_add_fetch(&g_bender_fix79_media_generation,1ul,__ATOMIC_RELAXED);__atomic_store_n(&g_bender_fix79_completed_generation,0ul,__ATOMIC_RELAXED);logmsg("[BENDER_MEDIANDK][SOURCE_GENERATION] source=fix79 generation=%lu variant=path ex=%p src=%s action=open-new-media\n",gen,(void*)e,src?src:"(null)");if(bender_media_trace())logmsg("[BENDER_MEDIANDK][SET_SOURCE] ex=%p src=%s policy=synthetic-video-eos\n",(void*)e,src?src:"(null)");return BENDER_AMEDIA_OK;}
static BenderMediaStatus bender_AMediaExtractor_setDataSourceFd(BenderMediaExtractor*e,int fd,int64_t off,int64_t len){if(!e)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;e->has_source=1;e->eos=0;unsigned long gen=__atomic_add_fetch(&g_bender_fix79_media_generation,1ul,__ATOMIC_RELAXED);__atomic_store_n(&g_bender_fix79_completed_generation,0ul,__ATOMIC_RELAXED);logmsg("[BENDER_MEDIANDK][SOURCE_GENERATION] source=fix79 generation=%lu variant=fd ex=%p fd=%d off=%lld len=%lld action=open-new-media\n",gen,(void*)e,fd,(long long)off,(long long)len);if(bender_media_trace())logmsg("[BENDER_MEDIANDK][SET_SOURCE_FD] ex=%p fd=%d off=%lld len=%lld policy=synthetic-video-eos\n",(void*)e,fd,(long long)off,(long long)len);return BENDER_AMEDIA_OK;}
static size_t bender_AMediaExtractor_getTrackCount(BenderMediaExtractor*e){size_t n=(e&&e->has_source)?1u:0u;if(bender_media_trace())logmsg("[BENDER_MEDIANDK][TRACK_COUNT] ex=%p count=%zu\n",(void*)e,n);return n;}
static BenderMediaFormat *bender_AMediaExtractor_getTrackFormat(BenderMediaExtractor*e,size_t track){if(!e||!e->has_source||track!=0u)return NULL;BenderMediaFormat*f=bender_AMediaFormat_new();if(f){bender_strlcpy(f->mime,"video/avc",sizeof(f->mime));f->width=640;f->height=480;f->stride=640;f->duration_us=1;f->frame_rate=30;f->frame_rate_f=30.0f;}if(bender_media_trace())logmsg("[BENDER_MEDIANDK][TRACK_FORMAT] track=%zu mime=video/avc size=640x480 durationUs=1\n",track);return f;}
static BenderMediaStatus bender_AMediaExtractor_selectTrack(BenderMediaExtractor*e,size_t track){if(!e||!e->has_source||track!=0u)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;e->selected_track=0;e->eos=1;if(bender_media_trace())logmsg("[BENDER_MEDIANDK][SELECT_TRACK] track=0 action=source-eos-await-surface-frame source=fix76\n");return BENDER_AMEDIA_OK;}
static int bender_AMediaExtractor_getSampleTrackIndex(BenderMediaExtractor*e){(void)e;return -1;}
static bool bender_AMediaExtractor_advance(BenderMediaExtractor*e){if(e)e->eos=1;return false;}
static ssize_t bender_AMediaExtractor_readSampleData(BenderMediaExtractor*e,uint8_t*buf,size_t cap){(void)e;(void)buf;(void)cap;return -1;}
static int64_t bender_AMediaExtractor_getSampleTime(BenderMediaExtractor*e){(void)e;return -1;}
static BenderMediaStatus bender_AMediaExtractor_seekTo(BenderMediaExtractor*e,int64_t pos,int mode){(void)pos;(void)mode;if(e)e->eos=1;return e?BENDER_AMEDIA_OK:BENDER_AMEDIA_ERROR_INVALID_PARAMETER;}
static BenderMediaCodec *bender_AMediaCodec_createDecoderByType(const char*mime){const char*mt=mime?mime:"video/avc";unsigned long gen=__atomic_load_n(&g_bender_fix79_media_generation,__ATOMIC_RELAXED);unsigned long done=__atomic_load_n(&g_bender_fix79_completed_generation,__ATOMIC_RELAXED);if(gen!=0ul&&done==gen&&!strncmp(mt,"video/",6)){unsigned long n=__atomic_add_fetch(&g_bender_fix79_restart_rejects,1ul,__ATOMIC_RELAXED);logmsg("[BENDER_MEDIANDK][RESTART_GUARD] source=fix79 action=reject-reopen generation=%lu completed=%lu reject=%lu mime=%s result=NULL policy=same-source-after-synthetic-eos\n",gen,done,n,mt);return NULL;}BenderMediaCodec*c=(BenderMediaCodec*)calloc(1,sizeof(*c));if(c){c->media_generation_fix79=gen;bender_strlcpy(c->mime,mt,sizeof(c->mime));bender_strlcpy(c->output.mime,c->mime,sizeof(c->output.mime));}if(bender_media_trace())logmsg("[BENDER_MEDIANDK][CODEC_CREATE] codec=%p mime=%s generation=%lu policy=synthetic-video-eos-restart-guard-fix79\n",(void*)c,mt,gen);return c;}
static BenderMediaStatus bender_AMediaCodec_configure(BenderMediaCodec*c,const BenderMediaFormat*f,void*window,void*crypto,uint32_t flags){
    (void)crypto;(void)flags;if(!c)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;c->configured=1;c->output_window=window;if(f)c->output=*f;
    logmsg("[BENDER_MEDIANDK][CODEC_CONFIGURE] source=fix78 codec=%p window=%p mime=%s size=%dx%d flags=0x%x rc=0\n",(void*)c,window,c->output.mime[0]?c->output.mime:c->mime,(int)c->output.width,(int)c->output.height,(unsigned)flags);
    return BENDER_AMEDIA_OK;
}
static BenderMediaStatus bender_AMediaCodec_start(BenderMediaCodec*c){if(!c)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;c->started=1;c->input_given=0;c->eos_queued=0;c->eos_output_sent=0;c->output_polls=0u;c->format_change_pending=1;c->eos_from_guest=0;c->synthetic_frame_sent=0;c->synthetic_frame_released=0;c->synthetic_frame_wait_polls=0u;if(bender_media_trace())logmsg("[BENDER_MEDIANDK][CODEC_START] codec=%p action=await-input-eos-and-surface-frame source=fix76\n",(void*)c);return BENDER_AMEDIA_OK;}
static BenderMediaStatus bender_AMediaCodec_stop(BenderMediaCodec*c){if(!c)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;c->started=0;return BENDER_AMEDIA_OK;}
static BenderMediaStatus bender_AMediaCodec_flush(BenderMediaCodec*c){if(!c)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;c->input_given=0;c->eos_queued=0;c->eos_output_sent=0;c->output_polls=0u;c->format_change_pending=1;c->eos_from_guest=0;c->synthetic_frame_sent=0;c->synthetic_frame_released=0;c->synthetic_frame_wait_polls=0u;return BENDER_AMEDIA_OK;}
static BenderMediaStatus bender_AMediaCodec_delete(BenderMediaCodec*c){if(bender_media_trace())logmsg("[BENDER_MEDIANDK][CODEC_DELETE] codec=%p\n",(void*)c);free(c);return BENDER_AMEDIA_OK;}
static ssize_t bender_AMediaCodec_dequeueInputBuffer(BenderMediaCodec*c,int64_t timeoutUs){(void)timeoutUs;if(!c||!c->started)return BENDER_AMEDIACODEC_INFO_TRY_AGAIN_LATER;if(!c->input_given){c->input_given=1;return 0;}return BENDER_AMEDIACODEC_INFO_TRY_AGAIN_LATER;}
static uint8_t *bender_AMediaCodec_getInputBuffer(BenderMediaCodec*c,size_t idx,size_t*outSize){if(!c||idx!=0u){if(outSize)*outSize=0;return NULL;}if(outSize)*outSize=sizeof(c->input);return c->input;}
static BenderMediaStatus bender_AMediaCodec_queueInputBuffer(BenderMediaCodec*c,size_t idx,int64_t offset,size_t size,uint64_t time,uint32_t flags){(void)offset;(void)time;if(!c||idx!=0u)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;c->input_given=0;if(size==0u||(flags&4u)){c->eos_queued=1;c->eos_from_guest=1;if(bender_media_trace())logmsg("[BENDER_MEDIANDK][EOS_HANDSHAKE] source=fix75 codec=%p stage=input-queued size=%zu flags=0x%x\n",(void*)c,size,(unsigned)flags);}return BENDER_AMEDIA_OK;}
static ssize_t bender_AMediaCodec_dequeueOutputBuffer(BenderMediaCodec*c,BenderMediaCodecBufferInfo*info,int64_t timeoutUs){
    (void)timeoutUs;if(info)memset(info,0,sizeof(*info));
    if(!c||!c->started)return BENDER_AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    if(c->format_change_pending){c->format_change_pending=0;if(bender_media_trace())logmsg("[BENDER_MEDIANDK][FORMAT_CHANGED] source=fix76 codec=%p action=report-before-synthetic-frame\n",(void*)c);return -2;}
    c->output_polls++;
    if(!c->synthetic_frame_sent){
        c->synthetic_frame_sent=1;c->synthetic_frame_wait_polls=0u;
        if(info){info->offset=0;info->size=1;info->presentationTimeUs=0;info->flags=0u;}
        logmsg("[BENDER_MEDIANDK][SYNTHETIC_FRAME] source=fix76 codec=%p index=0 size=1 flags=0 purpose=surface-completion-before-eos\n",(void*)c);
        return 0;
    }
    if(!c->synthetic_frame_released){
        c->synthetic_frame_wait_polls++;
        if(c->synthetic_frame_wait_polls>=8u){c->synthetic_frame_released=1;logmsg("[BENDER_MEDIANDK][FRAME_RELEASE_FALLBACK] source=fix78 codec=%p polls=%u window=%p action=surface-and-imagereader-frame-dispatch\n",(void*)c,c->synthetic_frame_wait_polls,c->output_window);bender_surface_frame_dispatch_fix76("release-timeout-fallback");bender_fix78_image_reader_frame(c->output_window,"release-timeout-fallback");}
        else return BENDER_AMEDIACODEC_INFO_TRY_AGAIN_LATER;
    }
    if(!c->eos_queued&&c->output_polls>=24u){c->eos_queued=1;c->eos_from_guest=0;if(bender_media_trace())logmsg("[BENDER_MEDIANDK][EOS_FALLBACK] source=fix76 codec=%p polls=%u reason=guest-did-not-queue-eos\n",(void*)c,c->output_polls);}
    if(c->eos_queued&&!c->eos_output_sent){c->eos_output_sent=1;if(info)info->flags=4u;if(c->media_generation_fix79!=0ul){__atomic_store_n(&g_bender_fix79_completed_generation,c->media_generation_fix79,__ATOMIC_RELAXED);logmsg("[BENDER_MEDIANDK][RESTART_GUARD] source=fix79 action=arm generation=%lu codec=%p origin=%s policy=block-same-source-video-reopen-until-new-source\n",c->media_generation_fix79,(void*)c,c->eos_from_guest?"guest-input":"bounded-fallback");}if(bender_media_trace())logmsg("[BENDER_MEDIANDK][CODEC_OUTPUT] source=fix76 codec=%p index=0 flags=EOS origin=%s polls=%u surface_frame=completed\n",(void*)c,c->eos_from_guest?"guest-input":"bounded-fallback",c->output_polls);return 0;}
    return BENDER_AMEDIACODEC_INFO_TRY_AGAIN_LATER;
}
static BenderMediaFormat *bender_AMediaCodec_getOutputFormat(BenderMediaCodec*c){if(!c)return NULL;BenderMediaFormat*f=bender_AMediaFormat_new();if(f)*f=c->output;return f;}
static uint8_t *bender_AMediaCodec_getOutputBuffer(BenderMediaCodec*c,size_t idx,size_t*outSize){(void)c;(void)idx;if(outSize)*outSize=0;return NULL;}
static BenderMediaStatus bender_AMediaCodec_releaseOutputBuffer(BenderMediaCodec*c,size_t idx,bool render){
    if(!c)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;
    if(idx==0u&&c->synthetic_frame_sent&&!c->synthetic_frame_released){
        c->synthetic_frame_released=1;
        logmsg("[BENDER_MEDIANDK][FRAME_RELEASE] source=fix78 codec=%p index=%zu render=%d window=%p action=surface-and-imagereader-frame-dispatch\n",(void*)c,idx,render?1:0,c->output_window);
        bender_surface_frame_dispatch_fix76(render?"release-render-true":"release-render-false-synthetic-fallback");
        bender_fix78_image_reader_frame(c->output_window,render?"codec-release-render":"codec-release-synthetic");
    }
    return BENDER_AMEDIA_OK;
}
/* FIX78: Unity alternate video surface backend (AImageReader + AHardwareBuffer control plane). */
typedef struct BenderHardwareBufferDescFix78 {
    uint32_t width,height,layers,format;
    uint64_t usage;
    uint32_t stride,rfu0;
    uint64_t rfu1;
} BenderHardwareBufferDescFix78;
typedef struct BenderHardwareBufferFix78 { int refs; BenderHardwareBufferDescFix78 desc; } BenderHardwareBufferFix78;
typedef struct BenderImageFix78 { int32_t width,height; int64_t timestamp_ns; BenderHardwareBufferFix78 *hardware; } BenderImageFix78;
typedef struct BenderImageListenerFix78 { void *context; void (*onImageAvailable)(void*,void*); } BenderImageListenerFix78;
typedef struct BenderBufferRemovedListenerFix78 { void *context; void (*onBufferRemoved)(void*,void*,void*); } BenderBufferRemovedListenerFix78;
typedef struct BenderImageReaderFix78 {
    int32_t width,height,format,max_images;
    uint64_t usage;
    BenderNativeWindow window;
    BenderImageListenerFix78 image_listener;
    BenderBufferRemovedListenerFix78 removed_listener;
    int image_available;
    uint64_t frame_counter;
} BenderImageReaderFix78;
static BenderImageReaderFix78 *g_bender_image_readers_fix78[8];
static unsigned long g_bender_image_reader_frame_count_fix78;
static BenderImageReaderFix78 *bender_fix78_reader_for_window(void *window){
    if(!window)return NULL;
    for(unsigned i=0;i<8u;i++)if(g_bender_image_readers_fix78[i]&&window==(void*)&g_bender_image_readers_fix78[i]->window)return g_bender_image_readers_fix78[i];
    return NULL;
}
static void bender_fix78_register_reader(BenderImageReaderFix78*r){for(unsigned i=0;i<8u;i++)if(!g_bender_image_readers_fix78[i]){g_bender_image_readers_fix78[i]=r;return;}}
static void bender_fix78_unregister_reader(BenderImageReaderFix78*r){for(unsigned i=0;i<8u;i++)if(g_bender_image_readers_fix78[i]==r)g_bender_image_readers_fix78[i]=NULL;}
static BenderMediaStatus bender_AImageReader_newWithUsage(int32_t width,int32_t height,int32_t format,uint64_t usage,int32_t maxImages,BenderImageReaderFix78 **out){
    if(!out||width<=0||height<=0||maxImages<=0)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;
    BenderImageReaderFix78*r=(BenderImageReaderFix78*)calloc(1,sizeof(*r));if(!r){*out=NULL;return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;}
    r->width=width;r->height=height;r->format=format;r->usage=usage;r->max_images=maxImages;r->window.refs=1;r->window.width=width;r->window.height=height;r->window.format=format;
    bender_fix78_register_reader(r);*out=r;
    logmsg("[BENDER_MEDIANDK][AIMAGE_READER] source=fix78 action=new reader=%p size=%dx%d format=%d usage=0x%llx max=%d window=%p rc=0\n",(void*)r,(int)width,(int)height,(int)format,(unsigned long long)usage,(int)maxImages,(void*)&r->window);
    return BENDER_AMEDIA_OK;
}
static BenderMediaStatus bender_AImageReader_getWindow(BenderImageReaderFix78*r,BenderNativeWindow **out){
    if(!r||!out){return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;}
    *out=&r->window;
    logmsg("[BENDER_MEDIANDK][AIMAGE_READER] source=fix78 action=get-window reader=%p window=%p size=%dx%d rc=0\n",(void*)r,(void*)&r->window,r->window.width,r->window.height);return BENDER_AMEDIA_OK;
}
static BenderMediaStatus bender_AImageReader_setImageListener(BenderImageReaderFix78*r,const BenderImageListenerFix78*l){
    if(!r){return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;}
    if(l){r->image_listener=*l;}else{memset(&r->image_listener,0,sizeof(r->image_listener));}
    logmsg("[BENDER_MEDIANDK][AIMAGE_READER] source=fix78 action=set-listener reader=%p context=%p callback=%p rc=0\n",(void*)r,r->image_listener.context,(void*)r->image_listener.onImageAvailable);return BENDER_AMEDIA_OK;
}
static BenderMediaStatus bender_AImageReader_setBufferRemovedListener(BenderImageReaderFix78*r,const BenderBufferRemovedListenerFix78*l){
    if(!r){return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;}
    if(l){r->removed_listener=*l;}else{memset(&r->removed_listener,0,sizeof(r->removed_listener));}
    logmsg("[BENDER_MEDIANDK][AIMAGE_READER] source=fix78 action=set-buffer-removed-listener reader=%p context=%p callback=%p rc=0\n",(void*)r,r->removed_listener.context,(void*)r->removed_listener.onBufferRemoved);return BENDER_AMEDIA_OK;
}
static void bender_AImageReader_delete(BenderImageReaderFix78*r){if(!r)return;bender_fix78_unregister_reader(r);if(g_bender_fix78_last_surface_window==&r->window){g_bender_fix78_last_surface_window=NULL;g_bender_fix78_last_surface=NULL;}logmsg("[BENDER_MEDIANDK][AIMAGE_READER] source=fix78 action=delete reader=%p\n",(void*)r);free(r);}
static BenderMediaStatus bender_AImageReader_acquireLatestImage(BenderImageReaderFix78*r,BenderImageFix78 **out){
    if(!r||!out){return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;}
    if(!r->image_available){*out=NULL;return BENDER_AMEDIACODEC_INFO_TRY_AGAIN_LATER;}
    BenderImageFix78*im=(BenderImageFix78*)calloc(1,sizeof(*im));BenderHardwareBufferFix78*hb=(BenderHardwareBufferFix78*)calloc(1,sizeof(*hb));if(!im||!hb){free(im);free(hb);*out=NULL;return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;}
    hb->refs=1;hb->desc.width=(uint32_t)r->width;hb->desc.height=(uint32_t)r->height;hb->desc.layers=1u;hb->desc.format=1u;hb->desc.usage=r->usage?r->usage:0x100ull;hb->desc.stride=(uint32_t)r->width;
    im->width=r->width;im->height=r->height;im->timestamp_ns=(int64_t)(++r->frame_counter*33333333ull);im->hardware=hb;r->image_available=0;*out=im;
    logmsg("[BENDER_MEDIANDK][AIMAGE_ACQUIRE] source=fix78 reader=%p image=%p hardware=%p size=%dx%d timestamp=%lld rc=0\n",(void*)r,(void*)im,(void*)hb,(int)im->width,(int)im->height,(long long)im->timestamp_ns);return BENDER_AMEDIA_OK;
}
static void bender_AImage_delete(BenderImageFix78*im){if(!im)return;if(im->hardware){free(im->hardware);im->hardware=NULL;}logmsg("[BENDER_MEDIANDK][AIMAGE_DELETE] source=fix78 image=%p\n",(void*)im);free(im);}
static BenderMediaStatus bender_AImage_deleteAsync(BenderImageFix78*im,int fenceFd){logmsg("[BENDER_MEDIANDK][AIMAGE_DELETE_ASYNC] source=fix78 image=%p fence=%d\n",(void*)im,fenceFd);bender_AImage_delete(im);return BENDER_AMEDIA_OK;}
static BenderMediaStatus bender_AImage_getWidth(const void*image,int32_t*width){if(!image||!width)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;*width=((const BenderImageFix78*)image)->width;return BENDER_AMEDIA_OK;}
static BenderMediaStatus bender_AImage_getTimestamp(const void*image,int64_t*timestamp){if(!image||!timestamp)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;*timestamp=((const BenderImageFix78*)image)->timestamp_ns;return BENDER_AMEDIA_OK;}
static BenderMediaStatus bender_AImage_getHardwareBuffer(const void*image,void **out){if(!image||!out)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;*out=(void*)((const BenderImageFix78*)image)->hardware;return *out?BENDER_AMEDIA_OK:BENDER_AMEDIA_ERROR_INVALID_PARAMETER;}
static void bender_AHardwareBuffer_describe(const BenderHardwareBufferFix78*hb,BenderHardwareBufferDescFix78*out){if(!out)return;memset(out,0,sizeof(*out));if(hb)*out=hb->desc;}
static void bender_AHardwareBuffer_release(BenderHardwareBufferFix78*hb){if(!hb)return;if(hb->refs>0)hb->refs--;/* image owns the synthetic buffer; final free occurs in AImage_delete. */}
static void *bender_ANativeWindow_toSurface(void *env,BenderNativeWindow *window){
    (void)env;if(!window)return NULL;BObj*s=new_obj(4,"android/view/Surface","bender-aimage-reader-surface");if(!s)return NULL;s->ref0=(uintptr_t)window;g_bender_fix78_last_surface=s;g_bender_fix78_last_surface_window=window;
    logmsg("[BENDER_MEDIANDK][ANW_TO_SURFACE] source=fix78 window=%p surface=%p size=%dx%d format=%d\n",(void*)window,(void*)s,window->width,window->height,window->format);return s;
}
static BenderMediaStatus bender_AMediaCodec_setOutputSurface(BenderMediaCodec*c,void*window){if(!c||!window)return BENDER_AMEDIA_ERROR_INVALID_PARAMETER;c->output_window=window;logmsg("[BENDER_MEDIANDK][SET_OUTPUT_SURFACE] source=fix78 codec=%p window=%p reader=%p rc=0\n",(void*)c,window,(void*)bender_fix78_reader_for_window(window));return BENDER_AMEDIA_OK;}
static void bender_fix78_image_reader_frame(void*window,const char*reason){
    BenderImageReaderFix78*r=bender_fix78_reader_for_window(window);if(!r)return;r->image_available=1;unsigned long n=__atomic_add_fetch(&g_bender_image_reader_frame_count_fix78,1ul,__ATOMIC_RELAXED);
    logmsg("[BENDER_MEDIANDK][IMAGEREADER_FRAME] source=fix78 n=%lu reader=%p window=%p callback=%p reason=%s\n",n,(void*)r,window,(void*)r->image_listener.onImageAvailable,reason?reason:"?");
    if(r->image_listener.onImageAvailable)r->image_listener.onImageAvailable(r->image_listener.context,(void*)r);
}


static volatile unsigned long g_bender_memmove_calls;
static volatile unsigned long g_bender_memmove_large_overlap_calls;

static void *bender_memmove_manual_2766(void *dst,const void *src,size_t n){
    if(!dst||!src||n==0u||dst==src)return dst;
    volatile unsigned char *d8=(volatile unsigned char*)dst;
    const volatile unsigned char *s8=(const volatile unsigned char*)src;
    uintptr_t d=(uintptr_t)dst,s=(uintptr_t)src;
    if(d<s || d-s>=n){
        size_t i=0u;
        if(((d|s)&7u)==0u){
            volatile uint64_t *dw=(volatile uint64_t*)dst;
            const volatile uint64_t *sw=(const volatile uint64_t*)src;
            size_t words=n/8u,w=0u;
            for(;w+4u<=words;w+=4u){
                uint64_t a=sw[w],b=sw[w+1u],c=sw[w+2u],e=sw[w+3u];
                dw[w]=a;dw[w+1u]=b;dw[w+2u]=c;dw[w+3u]=e;
            }
            for(;w<words;w++)dw[w]=sw[w];
            i=words*8u;
        }
        for(;i<n;i++)d8[i]=s8[i];
    }else{
        size_t i=n;
        if(((d|s)&7u)==0u){
            while(i&7u){--i;d8[i]=s8[i];}
            while(i>=32u){
                i-=32u;
                const volatile uint64_t *sw=(const volatile uint64_t*)((const unsigned char*)src+i);
                volatile uint64_t *dw=(volatile uint64_t*)((unsigned char*)dst+i);
                uint64_t a=sw[0],b=sw[1],c=sw[2],e=sw[3];
                dw[0]=a;dw[1]=b;dw[2]=c;dw[3]=e;
            }
            while(i>=8u){i-=8u;*(volatile uint64_t*)((unsigned char*)dst+i)=*(const volatile uint64_t*)((const unsigned char*)src+i);}
        }
        while(i){--i;d8[i]=s8[i];}
    }
    return dst;
}

static void bender_memmove_stack_trace_2766(uintptr_t guest_sp,uintptr_t direct_lr,unsigned long hit){
    if(hit!=1u||guest_sp==0u)return;
    pthread_attr_t attr;
    void *stack_base=NULL;
    size_t stack_size=0u;
    uintptr_t stack_lo=0u,stack_hi=0u;
    int attr_ok=(pthread_getattr_np(pthread_self(),&attr)==0);
    if(attr_ok){
        if(pthread_attr_getstack(&attr,&stack_base,&stack_size)==0){
            stack_lo=(uintptr_t)stack_base;
            stack_hi=stack_lo+stack_size;
        }
        pthread_attr_destroy(&attr);
    }
    Module *dm=bender_module_for_runtime_addr(direct_lr);
    uintptr_t direct_off=dm?direct_lr-dm->bias:0u;
    logmsg("[BENDER_MEMMOVE][CALLER_TRACE_BEGIN] hit=%lu guest_sp=%p stack_lo=%p stack_hi=%p direct_lr=%p direct_module=%s direct_off=0x%" PRIxPTR "\n",
           hit,(void*)guest_sp,(void*)stack_lo,(void*)stack_hi,(void*)direct_lr,dm?dm->name:"host-or-unknown",direct_off);
    if(!stack_lo||guest_sp<stack_lo||guest_sp+8u>stack_hi){
        logmsg("[BENDER_MEMMOVE][CALLER_TRACE_ABORT] reason=guest-sp-outside-thread-stack\n");
        return;
    }
    uintptr_t fastcopy_parent_lr=*(const uintptr_t*)guest_sp;
    Module *pm=bender_module_for_runtime_addr(fastcopy_parent_lr);
    uintptr_t parent_off=pm?fastcopy_parent_lr-pm->bias:0u;
    logmsg("[BENDER_MEMMOVE][FASTCOPY_PARENT] saved_lr=%p module=%s offset=0x%" PRIxPTR "\n",
           (void*)fastcopy_parent_lr,pm?pm->name:"host-or-unknown",parent_off);

    if(dm&&!strcmp(dm->name,"libil2cpp.so")&&direct_off==(uintptr_t)0x1b670b4u&&guest_sp+0x50u<=stack_hi){
        uintptr_t source_array=*(const uintptr_t*)(guest_sp+0x40u);
        uintptr_t dest_array=*(const uintptr_t*)(guest_sp+0x38u);
        int32_t source_idx=(int32_t)*(const uintptr_t*)(guest_sp+0x30u);
        int32_t dest_idx=(int32_t)*(const uintptr_t*)(guest_sp+0x28u);
        int32_t length=(int32_t)*(const uintptr_t*)(guest_sp+0x48u);
        logmsg("[BENDER_MEMMOVE][FASTCOPY_ARGS] source=%p dest=%p same=%d source_idx=%d dest_idx=%d length=%d\n",
               (void*)source_array,(void*)dest_array,source_array==dest_array?1:0,source_idx,dest_idx,length);
    }

    /* Swamp Attack 2 / Unity 6000.3 build-specific layout observed in the APK:
       Array::FastCopy frame = 0x50 bytes. System.Array.Copy at 0x3477f7c saves
       its caller LR at parent_sp+0x18.  When FastCopy returns to 0x3477ff0,
       guest_sp+0x68 therefore contains the caller above System.Array.Copy. */
    if(dm&&pm&&!strcmp(dm->name,"libil2cpp.so")&&!strcmp(pm->name,"libil2cpp.so")&&
       direct_off==(uintptr_t)0x1b670b4u&&parent_off==(uintptr_t)0x3477ff0u&&guest_sp+0x70u<=stack_hi){
        uintptr_t arraycopy_caller_lr=*(const uintptr_t*)(guest_sp+0x68u);
        Module *am=bender_module_for_runtime_addr(arraycopy_caller_lr);
        uintptr_t arraycopy_caller_off=am?arraycopy_caller_lr-am->bias:0u;
        logmsg("[BENDER_MEMMOVE][ARRAYCOPY_CALLER] saved_lr=%p module=%s offset=0x%" PRIxPTR " stack_off=0x68\n",
               (void*)arraycopy_caller_lr,am?am->name:"host-or-unknown",arraycopy_caller_off);
    }

    size_t max_scan=2048u;
    if(guest_sp+max_scan>stack_hi)max_scan=(size_t)(stack_hi-guest_sp);
    unsigned emitted=0u;
    uintptr_t last=0u;
    for(size_t off=0u;off+sizeof(uintptr_t)<=max_scan&&emitted<96u;off+=sizeof(uintptr_t)){
        uintptr_t v=*(const uintptr_t*)(guest_sp+off);
        Module *m=bender_module_for_runtime_addr(v);
        if(!m||v==last)continue;
        uintptr_t moff=v-m->bias;
        /* Return addresses are instruction aligned; ignore data pointers into mapped DSOs. */
        if((v&3u)!=0u)continue;
        logmsg("[BENDER_MEMMOVE][STACK_CANDIDATE] idx=%u stack_off=0x%zx addr=%p module=%s offset=0x%" PRIxPTR "\n",
               emitted,off,(void*)v,m->name,moff);
        last=v;
        emitted++;
    }
    logmsg("[BENDER_MEMMOVE][CALLER_TRACE_END] candidates=%u scanned=%zu\n",emitted,max_scan);
}

static volatile unsigned long g_bender_swamp_duck_ammo_resource_repair_hits;

static void bender_swamp_duck_ammo_resource_repair_2766(uintptr_t guest_sp,uintptr_t direct_lr){
    /*
     * Swamp Attack 2 1.3.5 / Unity 6000.3.13f1, APK conhecido.
     * Analise do libil2cpp.usym.so + global-metadata.dat do proprio APK corrigiu
     * a atribuicao anterior: os returns +0x1d04974 e +0x1d04a8c pertencem a
     * Game.PrepareDuckAmmoMode, nao a Damage.SetupDamage.
     *
     * O APK traz ResourcePack.Get(name) em +0x1d2f68c como stub:
     *     mov w0,#0x4fffffff ; ret
     * Esse sentinel de recurso "ilimitado" e somado aos dois totais de
     * PrepareDuckAmmoMode. No teste fisico ambos chegaram a 0x50000001,
     * portanto o total real residual e 0x50000001-0x4fffffff = 2.
     *
     * Corrigimos somente o x20 salvo dos dois List.Insert desta rotina, e
     * somente quando toda a cadeia de offsets e o pacote conhecido coincidem.
     * Isso preserva ResourcePack.Get globalmente (inclusive o comportamento do
     * APK modificado) e nao limita List.Insert/Array.Copy/memmove em outros
     * callers ou jogos.
     */
    if(!guest_sp||!direct_lr)return;
    if(strcmp(BENDER_APP_PACKAGE,"com.hyperdotstudios.swampattack2")!=0)return;
    /* Este reparo existe SOMENTE para o APK MOD 1.3.5 analisado.
       O original 1.3.3 (vc=1734) possui ResourcePack.Get normal e nao deve
       receber nenhum ajuste baseado nos offsets/sentinel da build MOD. */
    if(strcmp(BENDER_APP_VERSION_NAME,"1.3.5")!=0 || BENDER_APP_VERSION_CODE!=1758)return;
    Module *m=bender_module_for_runtime_addr(direct_lr);
    if(!m||strcmp(m->name,"libil2cpp.so")!=0)return;
    uintptr_t base=m->bias;
    if(direct_lr-base!=(uintptr_t)0x1b670b4u)return;

    /* guest_sp = FastCopy SP. Layout confirmado neste APK:
       +0x00: LR FastCopy -> Array.Copy +0x3477ff0
       +0x68: LR Array.Copy -> List.Insert +0x2ed9de0
       +0xc0: LR List.Insert -> Game.PrepareDuckAmmoMode loop A +0x1d04974
                                   ou loop B +0x1d04a8c
       +0xd8: x21 salvo do caller (contador)
       +0xe0: x20 salvo do caller (alvo total). */
    uintptr_t lr_fast=*(const uintptr_t*)(guest_sp+0x00u);
    uintptr_t lr_copy=*(const uintptr_t*)(guest_sp+0x68u);
    uintptr_t lr_insert=*(const uintptr_t*)(guest_sp+0xc0u);
    uintptr_t loop_a=base+(uintptr_t)0x1d04974u;
    uintptr_t loop_b=base+(uintptr_t)0x1d04a8cu;
    if(lr_fast!=base+(uintptr_t)0x3477ff0u ||
       lr_copy!=base+(uintptr_t)0x2ed9de0u ||
       (lr_insert!=loop_a && lr_insert!=loop_b))return;

    uint64_t saved_x21=*(const uint64_t*)(guest_sp+0xd8u);
    uint64_t saved_x20=*(const uint64_t*)(guest_sp+0xe0u);
    uint32_t counter=(uint32_t)saved_x21;
    uint32_t target=(uint32_t)saved_x20;
    const uint32_t resource_sentinel=0x4fffffffu;
    const uint32_t sane_target_limit=4096u;
    if(target<=resource_sentinel || counter>=target)return;

    uint32_t repaired_target=target-resource_sentinel;
    if(repaired_target==0u || repaired_target>sane_target_limit)return;

    /* Ja estamos dentro de uma insercao. Nao podemos voltar no tempo se o
       contador tiver passado do valor reconstruido; nesse caso permitimos
       apenas finalizar coerentemente apos a insercao atual. No caso fisico
       observado: A=max(2,1+1)=2 e B=max(2,0+1)=2. */
    uint32_t after_current=counter+1u;
    uint32_t new_target=repaired_target<after_current?after_current:repaired_target;
    *(uint64_t*)(guest_sp+0xe0u)=(uint64_t)new_target;
    unsigned long hit=__atomic_add_fetch(&g_bender_swamp_duck_ammo_resource_repair_hits,1ul,__ATOMIC_RELAXED);
    const char *loop_name=(lr_insert==loop_a)?"A":"B";
    uintptr_t caller_off=lr_insert-base;
    logmsg("[BENDER_SWAMP_PHASE3][DUCK_AMMO_RESOURCE_SENTINEL_REPAIR] hit=%lu loop=%s target_original=%u resource_sentinel=%u repaired_target=%u counter=%u target_new=%u caller=Game.PrepareDuckAmmoMode+0x%" PRIxPTR " action=subtract-resourcepack-unlimited-sentinel\n",
           hit,loop_name,target,resource_sentinel,repaired_target,counter,new_target,caller_off);
}

static __attribute__((used,noinline)) void *bender_memmove_2766_impl(void *dst,const void *src,size_t n,uintptr_t guest_sp,uintptr_t direct_lr){
    bender_swamp_duck_ammo_resource_repair_2766(guest_sp,direct_lr);
    unsigned long call=__atomic_add_fetch(&g_bender_memmove_calls,1ul,__ATOMIC_RELAXED);
    uintptr_t d=(uintptr_t)dst,s=(uintptr_t)src;
    uintptr_t delta=d>s?d-s:s-d;
    int overlap=(n>0u)&&((d>=s&&d-s<n)||(s>d&&s-d<n));
    int large_overlap=overlap&&n>=65536u&&delta<=64u;
    if(large_overlap){
        unsigned long hit=__atomic_add_fetch(&g_bender_memmove_large_overlap_calls,1ul,__ATOMIC_RELAXED);
        void *retaddr=(void*)direct_lr;
        int dst_exact=0,src_exact=0,dst_inside=0,src_inside=0;
        size_t dst_req=0,dst_usable=0,src_req=0,src_usable=0;
        intptr_t dst_off=0,src_off=0;
        if(hit<=8u || (hit%10000u)==0u){
            pthread_mutex_lock(&g_guest_heap_meta_lock);
            GuestAlloc *de=guest_alloc_find(dst);
            GuestAlloc *se=guest_alloc_find(src);
            GuestAlloc *di=de?de:guest_alloc_find_containing(dst);
            GuestAlloc *si=se?se:guest_alloc_find_containing(src);
            if(de)dst_exact=1;
            if(se)src_exact=1;
            if(di){dst_inside=1;dst_req=di->user_size;dst_usable=di->usable_size;dst_off=(intptr_t)(d-(uintptr_t)di->user);}
            if(si){src_inside=1;src_req=si->user_size;src_usable=si->usable_size;src_off=(intptr_t)(s-(uintptr_t)si->user);}
            pthread_mutex_unlock(&g_guest_heap_meta_lock);
            logmsg("[BENDER_MEMMOVE][LARGE_OVERLAP] hit=%lu call=%lu dst=%p src=%p len=%zu delta=%" PRIuPTR " ret=%p module=%s dst_exact=%d dst_inside=%d dst_off=%" PRIdPTR " dst_req=%zu dst_usable=%zu src_exact=%d src_inside=%d src_off=%" PRIdPTR " src_req=%zu src_usable=%zu impl=manual-overlap-safe\n",
                   hit,call,dst,src,n,delta,retaddr,guest_module_name_for_address(retaddr),dst_exact,dst_inside,dst_off,dst_req,dst_usable,src_exact,src_inside,src_off,src_req,src_usable);
        }
        bender_memmove_stack_trace_2766(guest_sp,direct_lr,hit);
    }
    return bender_memmove_manual_2766(dst,src,n);
}

#if defined(__aarch64__)
/* Bootlin GCC 2020.02 usado pelo R36S ignora __attribute__((naked)) em C.
   Defina o pequeno trampoline diretamente em assembly de topo para preservar
   SP/LR do chamador sem prologo C e sem depender do atributo naked. */
extern void *bender_memmove_2766(void *dst,const void *src,size_t n);
__asm__(
    ".text\n"
    ".align 2\n"
    ".type bender_memmove_2766, %function\n"
    "bender_memmove_2766:\n"
    "mov x3, sp\n"
    "mov x4, x30\n"
    "b bender_memmove_2766_impl\n"
    ".size bender_memmove_2766, .-bender_memmove_2766\n"
);
#else
static void *bender_memmove_2766(void *dst,const void *src,size_t n){
    return bender_memmove_2766_impl(dst,src,n,0u,(uintptr_t)__builtin_return_address(0));
}
#endif

/* BENDER generic Android/Bionic compatibility: ctype + atfork.
 * _ctype_ matches Bionic's public ABI: a data symbol containing a pointer
 * to a 257-byte classification table (leading sentinel + 256 bytes).
 * pthread_atfork/__register_atfork are translated to the host POSIX runtime. */
static const unsigned char g_bender_bionic_ctype_table_2769[257]={
    0x00,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x28,0x28,0x28,0x28,0x28,0x20,
    0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
    0x20,0x88,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
    0x10,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x10,0x10,0x10,0x10,0x10,
    0x10,0x10,0x41,0x41,0x41,0x41,0x41,0x41,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x10,0x10,0x10,0x10,
    0x10,0x10,0x42,0x42,0x42,0x42,0x42,0x42,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
    0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x10,0x10,0x10,0x10,
    0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
    0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
    0x20,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
    0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
    0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
    0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
    0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
    0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
    0x10
};
static const char *g_bender_bionic_ctype_ptr_2769=(const char*)g_bender_bionic_ctype_table_2769;
static int bender_pthread_atfork_2769(void(*prepare)(void),void(*parent)(void),void(*child)(void)){
    int rc=pthread_atfork(prepare,parent,child);
    logmsg("[BENDER_BIONIC][PTHREAD_ATFORK] prepare=%p parent=%p child=%p rc=%d\n",(void*)prepare,(void*)parent,(void*)child,rc);
    return rc;
}
static int bender___register_atfork_2769(void(*prepare)(void),void(*parent)(void),void(*child)(void),void*dso){
    (void)dso;
    return bender_pthread_atfork_2769(prepare,parent,child);
}

static uintptr_t bridge_symbol(const char *name) {
#define BS(n,fn) if(strcmp(name,(n))==0)return (uintptr_t)(fn)
    if(!name)return 0;
    BS("_ctype_",&g_bender_bionic_ctype_ptr_2769);BS("pthread_atfork",bender_pthread_atfork_2769);BS("__register_atfork",bender___register_atfork_2769);
    BS("__cxa_atexit",bender___cxa_atexit);BS("__cxa_finalize",bender___cxa_finalize);BS("dl_iterate_phdr",bender_dl_iterate_phdr);
    BS("malloc",bender_malloc);BS("calloc",bender_calloc);BS("realloc",bender_realloc);BS("free",bender_free);BS("malloc_usable_size",bender_malloc_usable_size);BS("memalign",bender_memalign);BS("posix_memalign",bender_posix_memalign);BS("strdup",bender_strdup);BS("vasprintf",bender_vasprintf);
    BS("sigaction",bender_sigaction);BS("__sigaction",bender_sigaction);BS("sigaction64",bender_sigaction64);BS("signal",bender_signal);BS("bsd_signal",bender_signal);BS("sigaltstack",bender_sigaltstack);BS("sigprocmask",bender_sigprocmask);BS("sigprocmask64",bender_sigprocmask64);BS("pthread_sigmask",bender_pthread_sigmask);BS("pthread_sigmask64",bender_pthread_sigmask64);BS("sigemptyset",bender_sigemptyset);BS("sigfillset",bender_sigfillset);BS("sigaddset",bender_sigaddset);BS("sigdelset",bender_sigdelset);BS("sigismember",bender_sigismember);BS("sigpending",bender_sigpending);BS("sigsuspend",bender_sigsuspend);BS("pthread_create",bender_pthread_create);
    BS("pthread_mutex_init",bender_pthread_mutex_init);BS("pthread_mutex_destroy",bender_pthread_mutex_destroy);BS("pthread_mutex_lock",bender_pthread_mutex_lock);BS("pthread_mutex_trylock",bender_pthread_mutex_trylock);BS("pthread_mutex_unlock",bender_pthread_mutex_unlock);BS("pthread_mutexattr_init",bender_pthread_mutexattr_init);BS("pthread_mutexattr_settype",bender_pthread_mutexattr_settype);BS("pthread_mutexattr_destroy",bender_pthread_mutexattr_destroy);
    BS("pthread_cond_init",bender_pthread_cond_init);BS("pthread_cond_destroy",bender_pthread_cond_destroy);BS("pthread_cond_wait",bender_pthread_cond_wait);BS("pthread_cond_timedwait",bender_pthread_cond_timedwait);BS("pthread_cond_signal",bender_pthread_cond_signal);BS("pthread_cond_broadcast",bender_pthread_cond_broadcast);BS("pthread_condattr_init",bender_pthread_condattr_init);BS("pthread_condattr_setclock",bender_pthread_condattr_setclock);BS("pthread_condattr_destroy",bender_pthread_condattr_destroy);
    BS("pthread_rwlock_init",bender_pthread_rwlock_init);BS("pthread_rwlock_destroy",bender_pthread_rwlock_destroy);BS("pthread_rwlock_rdlock",bender_pthread_rwlock_rdlock);BS("pthread_rwlock_wrlock",bender_pthread_rwlock_wrlock);BS("pthread_rwlock_unlock",bender_pthread_rwlock_unlock);BS("pthread_once",bender_pthread_once);
    BS("pthread_attr_init",bender_pthread_attr_init);BS("pthread_attr_destroy",bender_pthread_attr_destroy);BS("pthread_attr_setdetachstate",bender_pthread_attr_setdetachstate);BS("pthread_attr_setstacksize",bender_pthread_attr_setstacksize);BS("pthread_attr_getstack",bender_pthread_attr_getstack);BS("pthread_getattr_np",bender_pthread_getattr_np);
    BS("sem_init",bender_sem_init);BS("sem_destroy",bender_sem_destroy);BS("sem_wait",bender_sem_wait);BS("sem_timedwait",bender_sem_timedwait);BS("sem_post",bender_sem_post);BS("sem_getvalue",bender_sem_getvalue);
    BS("fwrite",bender_fwrite);BS("fread",bender_fread);BS("fprintf",bender_fprintf);BS("vfprintf",bender_vfprintf);BS("fputc",bender_fputc);BS("fflush",bender_fflush);BS("fseek",bender_fseek);BS("fopen",bender_fopen);BS("fclose",bender_fclose);
    BS("access",bender_access_2771);BS("stat",bender_stat_2771);BS("lstat",bender_lstat_2771);BS("fstat",bender_fstat_2771);BS("open",bender_open_2771);BS("opendir",bender_opendir_2771);BS("realpath",bender_realpath_2771);
    BS("__android_log_print",bender_android_log_print);BS("__android_log_write",bender_android_log_write);BS("__android_log_vprint",bender_android_log_vprint);BS("__android_log_buf_write",bender_android_log_buf_write);
    BS("__errno",bender___errno);BS("__gnu_strerror_r",bender___gnu_strerror_r);BS("android_set_abort_message",bender_android_set_abort_message);BS("__assert2",bender___assert2);BS("strlcpy",bender_strlcpy);BS("__strncpy_chk2",bender___strncpy_chk2);BS("__strlen_chk",bender___strlen_chk);BS("__strchr_chk",bender___strchr_chk);BS("__umask_chk",bender___umask_chk);BS("__FD_SET_chk",bender___FD_SET_chk);BS("__FD_ISSET_chk",bender___FD_ISSET_chk);
    BS("__system_property_get",bender___system_property_get);BS("__system_property_find",bender___system_property_find);BS("__system_property_read",bender___system_property_read);
    BS("sysconf",bender_sysconf);
    BS("ANativeWindow_getWidth",bender_ANativeWindow_getWidth);BS("ANativeWindow_getHeight",bender_ANativeWindow_getHeight);BS("ANativeWindow_setBuffersGeometry",bender_ANativeWindow_setBuffersGeometry);BS("ANativeWindow_acquire",bender_ANativeWindow_acquire);BS("ANativeWindow_release",bender_ANativeWindow_release);BS("ANativeWindow_fromSurface",bender_ANativeWindow_fromSurface);BS("ANativeWindow_toSurface",bender_ANativeWindow_toSurface);
    BS("AAssetManager_fromJava",bender_AAssetManager_fromJava);BS("AAssetManager_open",bender_AAssetManager_open);BS("AAsset_read",bender_AAsset_read);BS("AAsset_seek",bender_AAsset_seek);BS("AAsset_seek64",bender_AAsset_seek64);BS("AAsset_getLength",bender_AAsset_getLength);BS("AAsset_getLength64",bender_AAsset_getLength64);BS("AAsset_getRemainingLength",bender_AAsset_getRemainingLength);BS("AAsset_getRemainingLength64",bender_AAsset_getRemainingLength64);BS("AAsset_getBuffer",bender_AAsset_getBuffer);BS("AAsset_isAllocated",bender_AAsset_isAllocated);BS("AAsset_close",bender_AAsset_close);
    BS("AMediaFormat_new",bender_AMediaFormat_new);BS("AMediaFormat_delete",bender_AMediaFormat_delete);BS("AMediaFormat_getInt32",bender_AMediaFormat_getInt32);BS("AMediaFormat_getInt64",bender_AMediaFormat_getInt64);BS("AMediaFormat_getFloat",bender_AMediaFormat_getFloat);BS("AMediaFormat_getString",bender_AMediaFormat_getString);BS("AMediaFormat_setInt32",bender_AMediaFormat_setInt32);
    BS("AMediaExtractor_new",bender_AMediaExtractor_new);BS("AMediaExtractor_delete",bender_AMediaExtractor_delete);BS("AMediaExtractor_setDataSource",bender_AMediaExtractor_setDataSource);BS("AMediaExtractor_setDataSourceFd",bender_AMediaExtractor_setDataSourceFd);BS("AMediaExtractor_getTrackCount",bender_AMediaExtractor_getTrackCount);BS("AMediaExtractor_getTrackFormat",bender_AMediaExtractor_getTrackFormat);BS("AMediaExtractor_selectTrack",bender_AMediaExtractor_selectTrack);BS("AMediaExtractor_getSampleTrackIndex",bender_AMediaExtractor_getSampleTrackIndex);BS("AMediaExtractor_advance",bender_AMediaExtractor_advance);BS("AMediaExtractor_readSampleData",bender_AMediaExtractor_readSampleData);BS("AMediaExtractor_getSampleTime",bender_AMediaExtractor_getSampleTime);BS("AMediaExtractor_seekTo",bender_AMediaExtractor_seekTo);
    BS("AMediaCodec_createDecoderByType",bender_AMediaCodec_createDecoderByType);BS("AMediaCodec_configure",bender_AMediaCodec_configure);BS("AMediaCodec_setOutputSurface",bender_AMediaCodec_setOutputSurface);BS("AMediaCodec_start",bender_AMediaCodec_start);BS("AMediaCodec_stop",bender_AMediaCodec_stop);BS("AMediaCodec_flush",bender_AMediaCodec_flush);BS("AMediaCodec_delete",bender_AMediaCodec_delete);BS("AMediaCodec_dequeueInputBuffer",bender_AMediaCodec_dequeueInputBuffer);BS("AMediaCodec_getInputBuffer",bender_AMediaCodec_getInputBuffer);BS("AMediaCodec_queueInputBuffer",bender_AMediaCodec_queueInputBuffer);BS("AMediaCodec_dequeueOutputBuffer",bender_AMediaCodec_dequeueOutputBuffer);BS("AMediaCodec_getOutputFormat",bender_AMediaCodec_getOutputFormat);BS("AMediaCodec_getOutputBuffer",bender_AMediaCodec_getOutputBuffer);BS("AMediaCodec_releaseOutputBuffer",bender_AMediaCodec_releaseOutputBuffer);BS("AImageReader_newWithUsage",bender_AImageReader_newWithUsage);BS("AImageReader_getWindow",bender_AImageReader_getWindow);BS("AImageReader_setImageListener",bender_AImageReader_setImageListener);BS("AImageReader_setBufferRemovedListener",bender_AImageReader_setBufferRemovedListener);BS("AImageReader_acquireLatestImage",bender_AImageReader_acquireLatestImage);BS("AImageReader_delete",bender_AImageReader_delete);BS("AImage_getWidth",bender_AImage_getWidth);BS("AImage_getTimestamp",bender_AImage_getTimestamp);BS("AImage_getHardwareBuffer",bender_AImage_getHardwareBuffer);BS("AImage_delete",bender_AImage_delete);BS("AImage_deleteAsync",bender_AImage_deleteAsync);BS("AHardwareBuffer_describe",bender_AHardwareBuffer_describe);BS("AHardwareBuffer_release",bender_AHardwareBuffer_release);
    BS("AMEDIAFORMAT_KEY_DURATION",&bender_AMEDIAFORMAT_KEY_DURATION);BS("AMEDIAFORMAT_KEY_WIDTH",&bender_AMEDIAFORMAT_KEY_WIDTH);BS("AMEDIAFORMAT_KEY_HEIGHT",&bender_AMEDIAFORMAT_KEY_HEIGHT);BS("AMEDIAFORMAT_KEY_MIME",&bender_AMEDIAFORMAT_KEY_MIME);BS("AMEDIAFORMAT_KEY_CHANNEL_COUNT",&bender_AMEDIAFORMAT_KEY_CHANNEL_COUNT);BS("AMEDIAFORMAT_KEY_SAMPLE_RATE",&bender_AMEDIAFORMAT_KEY_SAMPLE_RATE);BS("AMEDIAFORMAT_KEY_COLOR_FORMAT",&bender_AMEDIAFORMAT_KEY_COLOR_FORMAT);BS("AMEDIAFORMAT_KEY_FRAME_RATE",&bender_AMEDIAFORMAT_KEY_FRAME_RATE);BS("AMEDIAFORMAT_KEY_LANGUAGE",&bender_AMEDIAFORMAT_KEY_LANGUAGE);BS("AMEDIAFORMAT_KEY_STRIDE",&bender_AMEDIAFORMAT_KEY_STRIDE);
    BS("eglBindAPI",bender_eglBindAPI);BS("eglChooseConfig",bender_eglChooseConfig);BS("eglCreateContext",bender_eglCreateContext);BS("eglCreatePbufferSurface",bender_eglCreatePbufferSurface);BS("eglCreateWindowSurface",bender_eglCreateWindowSurface);BS("eglDestroyContext",bender_eglDestroyContext);BS("eglDestroySurface",bender_eglDestroySurface);
    BS("eglGetConfigAttrib",bender_eglGetConfigAttrib);BS("eglGetCurrentContext",bender_eglGetCurrentContext);BS("eglGetCurrentDisplay",bender_eglGetCurrentDisplay);BS("eglGetCurrentSurface",bender_eglGetCurrentSurface);BS("eglGetDisplay",bender_eglGetDisplay);BS("eglGetError",bender_eglGetError);BS("eglGetProcAddress",bender_eglGetProcAddress);BS("eglInitialize",bender_eglInitialize);
    BS("eglMakeCurrent",bender_eglMakeCurrent);BS("eglQueryString",bender_eglQueryString);BS("eglQuerySurface",bender_eglQuerySurface);BS("eglReleaseThread",bender_eglReleaseThread);BS("eglSurfaceAttrib",bender_eglSurfaceAttrib);BS("eglSwapBuffers",bender_eglSwapBuffers);BS("eglSwapInterval",bender_eglSwapInterval);BS("eglTerminate",bender_eglTerminate);BS("eglWaitClient",bender_eglWaitClient);
    BS("AChoreographer_getInstance",bender_AChoreographer_getInstance);BS("AChoreographer_postFrameCallback",bender_AChoreographer_postFrameCallback);BS("AChoreographer_postFrameCallback64",bender_AChoreographer_postFrameCallback64);
    BS("ALooper_prepare",bender_ALooper_prepare);BS("ALooper_forThread",bender_ALooper_forThread);BS("ALooper_acquire",bender_ALooper_acquire);BS("ALooper_release",bender_ALooper_release);BS("ALooper_wake",bender_ALooper_wake);BS("ALooper_addFd",bender_ALooper_addFd);BS("ALooper_removeFd",bender_ALooper_removeFd);BS("ALooper_pollOnce",bender_ALooper_pollOnce);BS("ALooper_pollAll",bender_ALooper_pollAll);
    BS("ASensorManager_getInstance",bender_ASensorManager_getInstance);BS("ASensorManager_getSensorList",bender_ASensorManager_getSensorList);BS("ASensorManager_getDefaultSensor",bender_ASensorManager_getDefaultSensor);BS("ASensorManager_createEventQueue",bender_ASensorManager_createEventQueue);BS("ASensorManager_destroyEventQueue",bender_ASensorManager_destroyEventQueue);BS("ASensorEventQueue_enableSensor",bender_ASensorEventQueue_enableSensor);BS("ASensorEventQueue_disableSensor",bender_ASensorEventQueue_disableSensor);BS("ASensorEventQueue_setEventRate",bender_ASensorEventQueue_setEventRate);BS("ASensorEventQueue_hasEvents",bender_ASensorEventQueue_hasEvents);BS("ASensorEventQueue_getEvents",bender_ASensorEventQueue_getEvents);BS("ASensor_getType",bender_ASensor_getType);BS("ASensor_getResolution",bender_ASensor_getResolution);BS("ASensor_getMinDelay",bender_ASensor_getMinDelay);BS("ASensor_getName",bender_ASensor_getName);BS("ASensor_getVendor",bender_ASensor_getVendor);
    if(strcmp(name,"__sF")==0)return (uintptr_t)g_bionic_sF;
#undef BS
    return 0;
}
/* ---------------- end bridge ---------------- */


\
/* BENDER FIX8: managed Unity Application path bridge. */
typedef void (*BenderIl2CppAddInternalCall2772)(const char*,void*);
typedef void *(*BenderIl2CppStringNew2772)(const char*);
static uintptr_t module_symbol(Module *m,const char *name);
static BenderIl2CppStringNew2772 g_bender_il2cpp_string_new_2772;
static int g_bender_application_paths_installed_2772;
static unsigned long g_bender_app_streaming_calls_2772;
static unsigned long g_bender_app_persistent_calls_2772;
static unsigned long g_bender_app_cache_calls_2772;
static unsigned long g_bender_app_data_calls_2772;

static void *bender_application_path_string_2772(const char*kind,const char*path,unsigned long*counter){
    unsigned long n=counter?__atomic_add_fetch(counter,1ul,__ATOMIC_RELAXED):0ul;
    if(n<=8ul||(n%256ul)==0ul)logmsg("[BENDER_APPLICATION_PATHS][CALL] kind=%s n=%lu value=%s\\n",kind?kind:"?",n,path?path:"(null)");
    if(!g_bender_il2cpp_string_new_2772||!path)return NULL;
    return g_bender_il2cpp_string_new_2772(path);
}
static void *bender_icall_streaming_assets_path_2772(void){
    static char p[1536];snprintf(p,sizeof(p),"file://%s/unity_payload/assets",g_guest_app_dir);
    return bender_application_path_string_2772("streamingAssetsPath",p,&g_bender_app_streaming_calls_2772);
}
static void *bender_icall_persistent_data_path_2772(void){
    return bender_application_path_string_2772("persistentDataPath",g_guest_files_dir,&g_bender_app_persistent_calls_2772);
}
static void *bender_icall_temporary_cache_path_2772(void){
    return bender_application_path_string_2772("temporaryCachePath",g_guest_cache_dir,&g_bender_app_cache_calls_2772);
}
static void *bender_icall_data_path_2772(void){
    return bender_application_path_string_2772("dataPath",g_guest_base_apk,&g_bender_app_data_calls_2772);
}
static int bender_application_paths_content_present_2772(void){
    struct stat st;char p[1536];
    const char*rels[]={"/unity_payload/assets/Bundles","/unity_payload/assets/StreamingAssets","/unity_payload/assets/bin/Data/StreamingAssets","/unity_payload/assets/bin/data/StreamingAssets"};
    for(size_t i=0;i<sizeof(rels)/sizeof(rels[0]);i++){
        snprintf(p,sizeof(p),"%s%s",g_guest_app_dir,rels[i]);
        if(stat(p,&st)==0&&S_ISDIR(st.st_mode))return 1;
    }
    return 0;
}
static void __attribute__((unused)) bender_application_paths_install_2772(Module*im){
    if(g_bender_application_paths_installed_2772)return;
    if(!im||im->init_state!=2){logmsg("[BENDER_APPLICATION_PATHS][SKIP] reason=libil2cpp-not-ready\\n");return;}
    if(!bender_application_paths_content_present_2772()){
        logmsg("[BENDER_APPLICATION_PATHS][SKIP] reason=no-bundles-or-streamingassets policy=preserve-default\\n");return;
    }
    uintptr_t addp=module_symbol(im,"il2cpp_add_internal_call");
    uintptr_t strp=module_symbol(im,"il2cpp_string_new");
    if(!addp||!strp){logmsg("[BENDER_APPLICATION_PATHS][SKIP] reason=il2cpp-api-missing add=%p string_new=%p\\n",(void*)addp,(void*)strp);return;}
    BenderIl2CppAddInternalCall2772 add=(BenderIl2CppAddInternalCall2772)addp;
    g_bender_il2cpp_string_new_2772=(BenderIl2CppStringNew2772)strp;
    add("UnityEngine.Application::get_streamingAssetsPath()",(void*)bender_icall_streaming_assets_path_2772);
    add("UnityEngine.Application::get_persistentDataPath()",(void*)bender_icall_persistent_data_path_2772);
    add("UnityEngine.Application::get_temporaryCachePath()",(void*)bender_icall_temporary_cache_path_2772);
    add("UnityEngine.Application::get_dataPath()",(void*)bender_icall_data_path_2772);
    add("UnityEngine.Application::get_streamingAssetsPath",(void*)bender_icall_streaming_assets_path_2772);
    add("UnityEngine.Application::get_persistentDataPath",(void*)bender_icall_persistent_data_path_2772);
    add("UnityEngine.Application::get_temporaryCachePath",(void*)bender_icall_temporary_cache_path_2772);
    add("UnityEngine.Application::get_dataPath",(void*)bender_icall_data_path_2772);
    g_bender_application_paths_installed_2772=1;
    logmsg("[BENDER_APPLICATION_PATHS][INSTALL] mode=IL2CPP-internal-call streaming=file://%s/unity_payload/assets persistent=%s cache=%s data=%s\\n",g_guest_app_dir,g_guest_files_dir,g_guest_cache_dir,g_guest_base_apk);
}
static void __attribute__((unused)) bender_application_paths_health_2772(unsigned frame){
    logmsg("[BENDER_APPLICATION_PATHS][HEALTH] frame=%u installed=%d calls=stream:%lu,persist:%lu,cache:%lu,data:%lu asset_path=req:%lu,hit:%lu,miss:%lu\\n",frame,g_bender_application_paths_installed_2772,g_bender_app_streaming_calls_2772,g_bender_app_persistent_calls_2772,g_bender_app_cache_calls_2772,g_bender_app_data_calls_2772,g_bender_asset_path_requests_2771,g_bender_asset_path_resolves_2771,g_bender_asset_path_misses_2771);
}

/* BENDER FIX9: Unity content async/UWR diagnostics. */
/* BENDER FIX10: AssetBundle memory diagnostics. */
typedef void *(*BenderResolveIcall2773)(const char*);
typedef int32_t (*BenderStringLength2773)(void*);
typedef const uint16_t *(*BenderStringChars2773)(void*);
typedef void *(*BenderUwrCreate2773)(void);
typedef int (*BenderUwrSetUrl2773)(void*,void*);
typedef void *(*BenderUwrBegin2773)(void*);
typedef int (*BenderUwrGetResult2773)(void*);
typedef int (*BenderUwrIsExecuting2773)(void*);
typedef float (*BenderUwrProgress2773)(void*);
typedef int (*BenderUwrGetError2773)(void*);
typedef int (*BenderUwrSetHandler2773)(void*,void*);
typedef void (*BenderUwrRelease2773)(void*);
typedef int (*BenderAsyncIsDone2773)(void*);
typedef float (*BenderAsyncProgress2773)(void*);
typedef void *(*BenderAssetBundleGetter2773)(void*);

typedef void *(*BenderAssetBundleLoadMemory2774)(void*,uint32_t);
typedef void *(*BenderAssetBundleLoadSubAssetsAsync2774)(void*,void*,void*);
typedef void *(*BenderAssetBundleRequestAllAssets2774)(void*);
typedef void (*BenderAssetBundleUnload2774)(void*,int);
typedef struct { void *klass; void *monitor; void *bounds; uintptr_t max_length; } BenderIl2CppArrayHeader2774;
typedef struct { void *request; void *bytes; size_t length; uint32_t crc; uint64_t started_ns; unsigned long polls; int ready; int active; } BenderMemoryBundleTrack2774;
#define BENDER_MEMORY_BUNDLE_TRACK_2774 64
static BenderMemoryBundleTrack2774 g_bender_memory_bundle_track_2774[BENDER_MEMORY_BUNDLE_TRACK_2774];
static BenderAssetBundleLoadMemory2774 g_bender_ab_load_memory_async_orig_2774;
static BenderAssetBundleLoadMemory2774 g_bender_ab_load_memory_sync_orig_2774;
static BenderAssetBundleLoadSubAssetsAsync2774 g_bender_ab_load_subassets_async_orig_2774;
static BenderAssetBundleRequestAllAssets2774 g_bender_ab_req_allassets_orig_2774;
static BenderAssetBundleUnload2774 g_bender_ab_unload_orig_2774;
static unsigned long g_bender_ab_mem_async_calls_2774;
static unsigned long g_bender_ab_mem_sync_calls_2774;
static unsigned long g_bender_ab_mem_async_null_2774;
static unsigned long g_bender_ab_mem_sync_null_2774;
static unsigned long g_bender_ab_mem_bundle_get_2774;
static unsigned long g_bender_ab_mem_bundle_ready_2774;
static unsigned long g_bender_ab_load_assets_async_2774;
static unsigned long g_bender_ab_allassets_calls_2774;
static unsigned long g_bender_ab_allassets_ready_2774;
static unsigned long g_bender_ab_unload_calls_2774;
static uint64_t g_bender_ab_total_input_bytes_2774;
static size_t g_bender_ab_max_input_bytes_2774;


static BenderStringLength2773 g_bender_string_length_2773;
static BenderStringChars2773 g_bender_string_chars_2773;
static BenderUwrCreate2773 g_bender_uwr_create_orig_2773;
static BenderUwrSetUrl2773 g_bender_uwr_seturl_orig_2773;
static BenderUwrBegin2773 g_bender_uwr_begin_orig_2773;
static BenderUwrGetResult2773 g_bender_uwr_result_orig_2773;
static BenderUwrIsExecuting2773 g_bender_uwr_exec_orig_2773;
static BenderUwrProgress2773 g_bender_uwr_progress_orig_2773;
static BenderUwrGetError2773 g_bender_uwr_error_orig_2773;
static BenderUwrSetHandler2773 g_bender_uwr_setdh_orig_2773;
static BenderUwrRelease2773 g_bender_uwr_release_orig_2773;
static BenderAsyncIsDone2773 g_bender_async_done_orig_2773;
static BenderAsyncProgress2773 g_bender_async_progress_orig_2773;
static BenderAssetBundleGetter2773 g_bender_dhab_get_bundle_orig_2773;
static BenderAssetBundleGetter2773 g_bender_abcr_get_bundle_orig_2773;
static int g_bender_content_async_installed_2773;

static unsigned long g_bender_uwr_create_calls_2773;
static unsigned long g_bender_uwr_seturl_calls_2773;
static unsigned long g_bender_uwr_begin_calls_2773;
static unsigned long g_bender_uwr_file_begin_2773;
static unsigned long g_bender_uwr_http_begin_2773;
static unsigned long g_bender_uwr_other_begin_2773;
static unsigned long g_bender_uwr_result_polls_2773;
static unsigned long g_bender_uwr_inprogress_2773;
static unsigned long g_bender_uwr_success_2773;
static unsigned long g_bender_uwr_fail_2773;
static unsigned long g_bender_uwr_exec_polls_2773;
static unsigned long g_bender_uwr_progress_polls_2773;
static unsigned long g_bender_async_done_polls_2773;
static unsigned long g_bender_async_done_true_2773;
static unsigned long g_bender_async_progress_polls_2773;
static unsigned long g_bender_bundle_get_calls_2773;
static unsigned long g_bender_bundle_ready_2773;

#define BENDER_UWR_TRACK_2773 128
#define BENDER_ASYNC_TRACK_2773 128
typedef struct {void*req;void*op;void*dh;char url[1024];int last_result;unsigned long polls;int active;} BenderUwrTrack2773;
typedef struct {void*op;int last_done;unsigned long polls;} BenderAsyncTrack2773;
static BenderUwrTrack2773 g_bender_uwr_track_2773[BENDER_UWR_TRACK_2773];
static BenderAsyncTrack2773 g_bender_async_track_2773[BENDER_ASYNC_TRACK_2773];

static void bender_il2cpp_string_ascii_2773(void*s,char*out,size_t cap){
    if(!out||cap==0u){return;}
    out[0]=0;
    if(!s||!g_bender_string_length_2773||!g_bender_string_chars_2773){return;}
    int32_t n=g_bender_string_length_2773(s);const uint16_t*ch=g_bender_string_chars_2773(s);if(n<0||!ch)return;
    size_t j=0;for(int32_t i=0;i<n&&j+1u<cap;i++){uint16_t c=ch[i];if(c<0x80u)out[j++]=(char)c;else out[j++]='?';}out[j]=0;
}
static BenderUwrTrack2773*bender_uwr_track_get_2773(void*req,int create){
    if(!req){return NULL;}
    BenderUwrTrack2773*free_slot=NULL;
    for(size_t i=0;i<BENDER_UWR_TRACK_2773;i++){if(g_bender_uwr_track_2773[i].req==req)return &g_bender_uwr_track_2773[i];if(!free_slot&&!g_bender_uwr_track_2773[i].active)free_slot=&g_bender_uwr_track_2773[i];}
    if(!create||!free_slot){return NULL;}
    memset(free_slot,0,sizeof(*free_slot));
    free_slot->req=req;free_slot->last_result=-999;free_slot->active=1;
    return free_slot;
}
static BenderAsyncTrack2773*bender_async_track_get_2773(void*op,int create){
    if(!op){return NULL;}
    BenderAsyncTrack2773*free_slot=NULL;
    for(size_t i=0;i<BENDER_ASYNC_TRACK_2773;i++){if(g_bender_async_track_2773[i].op==op)return &g_bender_async_track_2773[i];if(!free_slot&&!g_bender_async_track_2773[i].op)free_slot=&g_bender_async_track_2773[i];}
    if(!create||!free_slot){return NULL;}
    free_slot->op=op;free_slot->last_done=-1;free_slot->polls=0;
    return free_slot;
}
static int bender_url_kind_2773(const char*u){if(!u)return 0;if(!strncmp(u,"file://",7))return 1;if(!strncmp(u,"http://",7)||!strncmp(u,"https://",8))return 2;return 3;}
static void*bender_uwr_create_2773(void){
    void*r=g_bender_uwr_create_orig_2773?g_bender_uwr_create_orig_2773():NULL;unsigned long n=++g_bender_uwr_create_calls_2773;bender_uwr_track_get_2773(r,1);
    if(n<=16ul||(n%128ul)==0ul){logmsg("[BENDER_UWR][CREATE] n=%lu req=%p\n",n,r);}
    return r;
}
static int bender_uwr_seturl_2773(void*req,void*s){
    char u[1024];bender_il2cpp_string_ascii_2773(s,u,sizeof(u));int rc=g_bender_uwr_seturl_orig_2773?g_bender_uwr_seturl_orig_2773(req,s):2;unsigned long n=++g_bender_uwr_seturl_calls_2773;
    BenderUwrTrack2773*t=bender_uwr_track_get_2773(req,1);if(t)bender_strlcpy(t->url,u,sizeof(t->url));
    if(n<=32ul||(n%128ul)==0ul){logmsg("[BENDER_UWR][SET_URL] n=%lu req=%p rc=%d kind=%d url=%s\n",n,req,rc,bender_url_kind_2773(u),u[0]?u:"(empty)");}
    return rc;
}
static int bender_uwr_setdh_2773(void*req,void*dh){
    int rc=g_bender_uwr_setdh_orig_2773?g_bender_uwr_setdh_orig_2773(req,dh):2;BenderUwrTrack2773*t=bender_uwr_track_get_2773(req,1);if(t)t->dh=dh;
    logmsg("[BENDER_UWR][SET_DOWNLOAD_HANDLER] req=%p dh=%p rc=%d url=%s\n",req,dh,rc,t&&t->url[0]?t->url:"(unknown)");return rc;
}
static void*bender_uwr_begin_2773(void*req){
    void*op=g_bender_uwr_begin_orig_2773?g_bender_uwr_begin_orig_2773(req):NULL;unsigned long n=++g_bender_uwr_begin_calls_2773;BenderUwrTrack2773*t=bender_uwr_track_get_2773(req,1);int kind=bender_url_kind_2773(t?t->url:NULL);
    if(t){t->op=op;}
    if(kind==1){++g_bender_uwr_file_begin_2773;}
    else if(kind==2){++g_bender_uwr_http_begin_2773;}
    else {++g_bender_uwr_other_begin_2773;}
    bender_async_track_get_2773(op,1);logmsg("[BENDER_UWR][BEGIN] n=%lu req=%p op=%p kind=%d dh=%p url=%s\n",n,req,op,kind,t?t->dh:NULL,t&&t->url[0]?t->url:"(unknown)");return op;
}
static int bender_uwr_result_2773(void*req){
    int r=g_bender_uwr_result_orig_2773?g_bender_uwr_result_orig_2773(req):4;unsigned long n=++g_bender_uwr_result_polls_2773;BenderUwrTrack2773*t=bender_uwr_track_get_2773(req,1);if(t)t->polls++;
    if(r==0)++g_bender_uwr_inprogress_2773;else if(r==1)++g_bender_uwr_success_2773;else ++g_bender_uwr_fail_2773;
    int changed=t&&(t->last_result!=r);if(t)t->last_result=r;if(changed||n<=24ul||(r==0&&t&&(t->polls%300ul)==0ul))logmsg("[BENDER_UWR][RESULT] req=%p result=%d polls=%lu op=%p url=%s\n",req,r,t?t->polls:0ul,t?t->op:NULL,t&&t->url[0]?t->url:"(unknown)");return r;
}
static int bender_uwr_exec_2773(void*req){int r=g_bender_uwr_exec_orig_2773?g_bender_uwr_exec_orig_2773(req):0;++g_bender_uwr_exec_polls_2773;return r;}
static float bender_uwr_progress_2773(void*req){float r=g_bender_uwr_progress_orig_2773?g_bender_uwr_progress_orig_2773(req):-1.0f;unsigned long n=++g_bender_uwr_progress_polls_2773;if(n<=16ul||(n%300ul)==0ul){BenderUwrTrack2773*t=bender_uwr_track_get_2773(req,0);logmsg("[BENDER_UWR][PROGRESS] req=%p value=%.3f url=%s\n",req,(double)r,t&&t->url[0]?t->url:"(unknown)");}return r;}
static int bender_uwr_error_2773(void*req){int r=g_bender_uwr_error_orig_2773?g_bender_uwr_error_orig_2773(req):2;if(r){BenderUwrTrack2773*t=bender_uwr_track_get_2773(req,0);logmsg("[BENDER_UWR][ERROR] req=%p code=%d url=%s\n",req,r,t&&t->url[0]?t->url:"(unknown)");}return r;}
static void bender_uwr_release_2773(void*req){BenderUwrTrack2773*t=bender_uwr_track_get_2773(req,0);if(t)logmsg("[BENDER_UWR][RELEASE] req=%p result=%d polls=%lu url=%s\n",req,t->last_result,t->polls,t->url[0]?t->url:"(unknown)");if(g_bender_uwr_release_orig_2773)g_bender_uwr_release_orig_2773(req);if(t){memset(t,0,sizeof(*t));}}
static int bender_async_done_2773(void*op){int d=g_bender_async_done_orig_2773?g_bender_async_done_orig_2773(op):0;unsigned long n=++g_bender_async_done_polls_2773;if(d)++g_bender_async_done_true_2773;BenderAsyncTrack2773*t=bender_async_track_get_2773(op,1);if(t)t->polls++;if(t&&(t->last_done!=d||(!d&&(t->polls%300ul)==0ul))){t->last_done=d;logmsg("[BENDER_ASYNC][IS_DONE] op=%p done=%d polls=%lu\n",op,d,t->polls);}else if(n<=16ul)logmsg("[BENDER_ASYNC][IS_DONE] op=%p done=%d polls=%lu\n",op,d,t?t->polls:0ul);return d;}
static float bender_async_progress_2773(void*op){float p=g_bender_async_progress_orig_2773?g_bender_async_progress_orig_2773(op):0.0f;unsigned long n=++g_bender_async_progress_polls_2773;if(n<=16ul||(n%300ul)==0ul)logmsg("[BENDER_ASYNC][PROGRESS] op=%p value=%.3f\n",op,(double)p);return p;}

static size_t bender_il2cpp_byte_array_len_2774(void *arr){
    if(!arr){return 0u;}
    uintptr_t n=((BenderIl2CppArrayHeader2774*)arr)->max_length;
    if(n>UINT64_C(2147483647)){return 0u;}
    return (size_t)n;
}
static const unsigned char *bender_il2cpp_byte_array_data_2774(void *arr){
    if(!arr){return NULL;}
    return (const unsigned char*)arr + sizeof(BenderIl2CppArrayHeader2774);
}
static void bender_bundle_magic_2774(void *arr,size_t len,char out[9]){
    memset(out,0,9u);
    if(!arr||len==0u){return;}
    const unsigned char*p=bender_il2cpp_byte_array_data_2774(arr);
    size_t n=len<8u?len:8u;
    for(size_t i=0;i<n;i++){
        unsigned char c=p[i];
        out[i]=(c>=32u&&c<=126u)?(char)c:'.';
    }
}
static BenderMemoryBundleTrack2774*bender_mem_bundle_track_2774(void *req,int create){
    if(!req){return NULL;}
    BenderMemoryBundleTrack2774*free_slot=NULL;
    for(size_t i=0;i<BENDER_MEMORY_BUNDLE_TRACK_2774;i++){
        BenderMemoryBundleTrack2774*t=&g_bender_memory_bundle_track_2774[i];
        if(t->active&&t->request==req){return t;}
        if(!free_slot&&!t->active){free_slot=t;}
    }
    if(!create||!free_slot){return NULL;}
    memset(free_slot,0,sizeof(*free_slot));
    free_slot->request=req; free_slot->active=1; return free_slot;
}
static void*bender_ab_load_memory_async_2774(void *bytes,uint32_t crc){
    size_t len=bender_il2cpp_byte_array_len_2774(bytes); char magic[9]; bender_bundle_magic_2774(bytes,len,magic);
    void*req=g_bender_ab_load_memory_async_orig_2774?g_bender_ab_load_memory_async_orig_2774(bytes,crc):NULL;
    unsigned long n=++g_bender_ab_mem_async_calls_2774; g_bender_ab_total_input_bytes_2774+=(uint64_t)len;
    if(len>g_bender_ab_max_input_bytes_2774){g_bender_ab_max_input_bytes_2774=len;} if(!req){++g_bender_ab_mem_async_null_2774;}
    BenderMemoryBundleTrack2774*t=bender_mem_bundle_track_2774(req,1); if(t){t->bytes=bytes;t->length=len;t->crc=crc;t->started_ns=bender_net_now_ns_2750();}
    logmsg("[BENDER_ASSETBUNDLE_MEMORY][LOAD_ASYNC] n=%lu bytes_obj=%p length=%zu crc=%u magic=%s request=%p\n",n,bytes,len,(unsigned)crc,magic[0]?magic:"(empty)",req); return req;
}
static void*bender_ab_load_memory_sync_2774(void *bytes,uint32_t crc){
    size_t len=bender_il2cpp_byte_array_len_2774(bytes); char magic[9]; bender_bundle_magic_2774(bytes,len,magic);
    void*bundle=g_bender_ab_load_memory_sync_orig_2774?g_bender_ab_load_memory_sync_orig_2774(bytes,crc):NULL;
    unsigned long n=++g_bender_ab_mem_sync_calls_2774; g_bender_ab_total_input_bytes_2774+=(uint64_t)len;
    if(len>g_bender_ab_max_input_bytes_2774){g_bender_ab_max_input_bytes_2774=len;} if(!bundle){++g_bender_ab_mem_sync_null_2774;}
    logmsg("[BENDER_ASSETBUNDLE_MEMORY][LOAD_SYNC] n=%lu bytes_obj=%p length=%zu crc=%u magic=%s bundle=%p ready=%d\n",n,bytes,len,(unsigned)crc,magic[0]?magic:"(empty)",bundle,bundle?1:0); return bundle;
}
static void*bender_ab_load_subassets_async_2774(void *bundle,void *name,void *type){
    void*r=g_bender_ab_load_subassets_async_orig_2774?g_bender_ab_load_subassets_async_orig_2774(bundle,name,type):NULL; unsigned long n=++g_bender_ab_load_assets_async_2774;
    if(n<=32ul||(n%128ul)==0ul){logmsg("[BENDER_ASSETBUNDLE_MEMORY][LOAD_ASSETS_ASYNC] n=%lu bundle=%p name=%p type=%p request=%p\n",n,bundle,name,type,r);}
    return r;
}
static void*bender_ab_req_allassets_2774(void *req){
    void*a=g_bender_ab_req_allassets_orig_2774?g_bender_ab_req_allassets_orig_2774(req):NULL; unsigned long n=++g_bender_ab_allassets_calls_2774; if(a)++g_bender_ab_allassets_ready_2774;
    if(n<=32ul||(n%128ul)==0ul||a){logmsg("[BENDER_ASSETBUNDLE_MEMORY][ALL_ASSETS] n=%lu request=%p assets=%p ready=%d\n",n,req,a,a?1:0);}
    return a;
}
static void bender_ab_unload_2774(void*bundle,int all){++g_bender_ab_unload_calls_2774;logmsg("[BENDER_ASSETBUNDLE_MEMORY][UNLOAD] bundle=%p all=%d\n",bundle,all);if(g_bender_ab_unload_orig_2774)g_bender_ab_unload_orig_2774(bundle,all);}

static void*bender_dhab_get_bundle_2773(void*dh){void*b=g_bender_dhab_get_bundle_orig_2773?g_bender_dhab_get_bundle_orig_2773(dh):NULL;++g_bender_bundle_get_calls_2773;if(b)++g_bender_bundle_ready_2773;logmsg("[BENDER_ASSETBUNDLE][DOWNLOAD_HANDLER_GET] dh=%p bundle=%p ready=%d\n",dh,b,b?1:0);return b;}
static void*bender_abcr_get_bundle_2773(void*req){
    void*b=g_bender_abcr_get_bundle_orig_2773?g_bender_abcr_get_bundle_orig_2773(req):NULL;
    ++g_bender_bundle_get_calls_2773;if(b)++g_bender_bundle_ready_2773;
    unsigned long n=++g_bender_ab_mem_bundle_get_2774;BenderMemoryBundleTrack2774*t=bender_mem_bundle_track_2774(req,0);
    if(t){t->polls++;if(b){t->ready=1;t->active=0;}}if(b)++g_bender_ab_mem_bundle_ready_2774;
    if(t||n<=16ul||b){uint64_t age_ms=0u;if(t&&t->started_ns){uint64_t now=bender_net_now_ns_2750();if(now>=t->started_ns)age_ms=(now-t->started_ns)/UINT64_C(1000000);}logmsg("[BENDER_ASSETBUNDLE_MEMORY][REQUEST_GET] n=%lu request=%p bundle=%p ready=%d polls=%lu age_ms=%" PRIu64 " length=%zu crc=%u\n",n,req,b,b?1:0,t?t->polls:0ul,age_ms,t?t->length:0u,t?(unsigned)t->crc:0u);}
    logmsg("[BENDER_ASSETBUNDLE][CREATE_REQUEST_GET] req=%p bundle=%p ready=%d\n",req,b,b?1:0);return b;
}

static void __attribute__((unused)) bender_content_async_install_2773(Module*im){
    if(g_bender_content_async_installed_2773||!im||im->init_state!=2){return;}
    uintptr_t rp=module_symbol(im,"il2cpp_resolve_icall"),ap=module_symbol(im,"il2cpp_add_internal_call"),lp=module_symbol(im,"il2cpp_string_length"),cp=module_symbol(im,"il2cpp_string_chars");
    if(!rp||!ap){logmsg("[BENDER_CONTENT_ASYNC][SKIP] reason=il2cpp-icall-api-missing resolve=%p add=%p\n",(void*)rp,(void*)ap);return;}BenderResolveIcall2773 resolve=(BenderResolveIcall2773)rp;BenderIl2CppAddInternalCall2772 add=(BenderIl2CppAddInternalCall2772)ap;g_bender_string_length_2773=(BenderStringLength2773)lp;g_bender_string_chars_2773=(BenderStringChars2773)cp;
#define BENDER_WRAP_ICALL_2773(name,var,wrap,type) do{var=(type)resolve(name);if(var){add(name,(void*)wrap);logmsg("[BENDER_CONTENT_ASYNC][WRAP] name=%s original=%p wrapper=%p\n",name,(void*)var,(void*)wrap);}}while(0)
    BENDER_WRAP_ICALL_2773("UnityEngine.Networking.UnityWebRequest::Create",g_bender_uwr_create_orig_2773,bender_uwr_create_2773,BenderUwrCreate2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.Networking.UnityWebRequest::SetUrl",g_bender_uwr_seturl_orig_2773,bender_uwr_seturl_2773,BenderUwrSetUrl2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.Networking.UnityWebRequest::BeginWebRequest",g_bender_uwr_begin_orig_2773,bender_uwr_begin_2773,BenderUwrBegin2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.Networking.UnityWebRequest::get_result",g_bender_uwr_result_orig_2773,bender_uwr_result_2773,BenderUwrGetResult2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.Networking.UnityWebRequest::IsExecuting",g_bender_uwr_exec_orig_2773,bender_uwr_exec_2773,BenderUwrIsExecuting2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.Networking.UnityWebRequest::GetDownloadProgress",g_bender_uwr_progress_orig_2773,bender_uwr_progress_2773,BenderUwrProgress2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.Networking.UnityWebRequest::GetError",g_bender_uwr_error_orig_2773,bender_uwr_error_2773,BenderUwrGetError2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.Networking.UnityWebRequest::SetDownloadHandler",g_bender_uwr_setdh_orig_2773,bender_uwr_setdh_2773,BenderUwrSetHandler2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.Networking.UnityWebRequest::Release",g_bender_uwr_release_orig_2773,bender_uwr_release_2773,BenderUwrRelease2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.AsyncOperation::get_isDone",g_bender_async_done_orig_2773,bender_async_done_2773,BenderAsyncIsDone2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.AsyncOperation::get_progress",g_bender_async_progress_orig_2773,bender_async_progress_2773,BenderAsyncProgress2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.Networking.DownloadHandlerAssetBundle::get_assetBundle",g_bender_dhab_get_bundle_orig_2773,bender_dhab_get_bundle_2773,BenderAssetBundleGetter2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.AssetBundleCreateRequest::get_assetBundle",g_bender_abcr_get_bundle_orig_2773,bender_abcr_get_bundle_2773,BenderAssetBundleGetter2773);
    BENDER_WRAP_ICALL_2773("UnityEngine.AssetBundle::LoadFromMemoryAsync_Internal",g_bender_ab_load_memory_async_orig_2774,bender_ab_load_memory_async_2774,BenderAssetBundleLoadMemory2774);
    BENDER_WRAP_ICALL_2773("UnityEngine.AssetBundle::LoadFromMemory_Internal",g_bender_ab_load_memory_sync_orig_2774,bender_ab_load_memory_sync_2774,BenderAssetBundleLoadMemory2774);
    BENDER_WRAP_ICALL_2773("UnityEngine.AssetBundle::LoadAssetWithSubAssetsAsync_Internal",g_bender_ab_load_subassets_async_orig_2774,bender_ab_load_subassets_async_2774,BenderAssetBundleLoadSubAssetsAsync2774);
    BENDER_WRAP_ICALL_2773("UnityEngine.AssetBundleRequest::get_allAssets",g_bender_ab_req_allassets_orig_2774,bender_ab_req_allassets_2774,BenderAssetBundleRequestAllAssets2774);
    BENDER_WRAP_ICALL_2773("UnityEngine.AssetBundle::Unload",g_bender_ab_unload_orig_2774,bender_ab_unload_2774,BenderAssetBundleUnload2774);
#undef BENDER_WRAP_ICALL_2773
    g_bender_content_async_installed_2773=1;logmsg("[BENDER_CONTENT_ASYNC][INSTALL] installed=1 uwr=%d async=%d bundle=%d string_api=%d\n",g_bender_uwr_begin_orig_2773?1:0,g_bender_async_done_orig_2773?1:0,(g_bender_dhab_get_bundle_orig_2773||g_bender_abcr_get_bundle_orig_2773)?1:0,(lp&&cp)?1:0);
}
static void __attribute__((unused)) bender_content_async_health_2773(unsigned frame){
    logmsg("[BENDER_CONTENT_ASYNC][HEALTH] frame=%u installed=%d uwr=create:%lu,seturl:%lu,begin:%lu,file:%lu,http:%lu,other:%lu result_polls:%lu,inprogress:%lu,success:%lu,fail:%lu exec:%lu,progress:%lu async=polls:%lu,done:%lu,progress:%lu bundle=get:%lu,ready:%lu\n",frame,g_bender_content_async_installed_2773,g_bender_uwr_create_calls_2773,g_bender_uwr_seturl_calls_2773,g_bender_uwr_begin_calls_2773,g_bender_uwr_file_begin_2773,g_bender_uwr_http_begin_2773,g_bender_uwr_other_begin_2773,g_bender_uwr_result_polls_2773,g_bender_uwr_inprogress_2773,g_bender_uwr_success_2773,g_bender_uwr_fail_2773,g_bender_uwr_exec_polls_2773,g_bender_uwr_progress_polls_2773,g_bender_async_done_polls_2773,g_bender_async_done_true_2773,g_bender_async_progress_polls_2773,g_bender_bundle_get_calls_2773,g_bender_bundle_ready_2773);
    for(size_t i=0;i<BENDER_UWR_TRACK_2773;i++){BenderUwrTrack2773*t=&g_bender_uwr_track_2773[i];if(t->active&&t->last_result==0&&t->polls>=300ul)logmsg("[BENDER_CONTENT_ASYNC][STALL] frame=%u req=%p op=%p polls=%lu url=%s\n",frame,t->req,t->op,t->polls,t->url[0]?t->url:"(unknown)");}
    unsigned mem_pending=0u;uint64_t mem_oldest_ms=0u;uint64_t mem_now=bender_net_now_ns_2750();
    for(size_t i=0;i<BENDER_MEMORY_BUNDLE_TRACK_2774;i++){BenderMemoryBundleTrack2774*t=&g_bender_memory_bundle_track_2774[i];if(!t->active)continue;mem_pending++;if(t->started_ns&&mem_now>=t->started_ns){uint64_t age=(mem_now-t->started_ns)/UINT64_C(1000000);if(age>mem_oldest_ms)mem_oldest_ms=age;}}
    logmsg("[BENDER_ASSETBUNDLE_MEMORY][HEALTH] frame=%u installed=%d load=async:%lu,sync:%lu,null_async:%lu,null_sync:%lu input_bytes=%" PRIu64 ",max:%zu request=get:%lu,ready:%lu pending=%u oldest_ms=%" PRIu64 " assets=load_async:%lu,all_get:%lu,all_ready:%lu unload:%lu\n",frame,g_bender_content_async_installed_2773,g_bender_ab_mem_async_calls_2774,g_bender_ab_mem_sync_calls_2774,g_bender_ab_mem_async_null_2774,g_bender_ab_mem_sync_null_2774,g_bender_ab_total_input_bytes_2774,g_bender_ab_max_input_bytes_2774,g_bender_ab_mem_bundle_get_2774,g_bender_ab_mem_bundle_ready_2774,mem_pending,mem_oldest_ms,g_bender_ab_load_assets_async_2774,g_bender_ab_allassets_calls_2774,g_bender_ab_allassets_ready_2774,g_bender_ab_unload_calls_2774);
    if(mem_pending&&mem_oldest_ms>=10000u)logmsg("[BENDER_ASSETBUNDLE_MEMORY][STALL] frame=%u pending=%u oldest_ms=%" PRIu64 " policy=observe-only\n",frame,mem_pending,mem_oldest_ms);
}

static uintptr_t module_symbol(Module *m, const char *name) {
    if (!m || !m->reserve || !m->symtab || !m->strtab || !name) return 0;
    for (size_t i = 0; i < m->symcount; ++i) {
        Elf64_Sym *s = (Elf64_Sym *)((uint8_t *)m->symtab + i * m->syment);
        if (s->st_shndx == SHN_UNDEF || !s->st_name) continue;
        const char *n = m->strtab + s->st_name;
        if (strcmp(n, name) == 0) return m->bias + s->st_value;
    }
    return 0;
}


static Module *guest_module_from_handle(void *handle) {
    for(int i=0;i<g_module_count;i++) if(handle==(void*)&g_modules[i]) return &g_modules[i];
    return NULL;
}
static Module *guest_module_from_path(const char *path) {
    if(!path) return NULL;
    const char *base=strrchr(path,'/');
    base=base?base+1:path;
    for(int i=0;i<g_module_count;i++) if(strcmp(base,g_modules[i].name)==0) return &g_modules[i];
    char canon[160]; canon[0]=0;
    size_t blen=strlen(base);
    int has_so=(blen>=3u && !strcmp(base+blen-3u,".so"));
    if(!strncmp(base,"lib",3)){
        if(!has_so)snprintf(canon,sizeof(canon),"%s.so",base);
    } else {
        if(has_so)snprintf(canon,sizeof(canon),"lib%s",base);
        else snprintf(canon,sizeof(canon),"lib%s.so",base);
    }
    if(canon[0]){
        for(int i=0;i<g_module_count;i++) if(!strcmp(canon,g_modules[i].name)){
            logmsg("[BENDER_DL][ALIAS] request=%s canonical=%s\n",base,canon);
            return &g_modules[i];
        }
    }
    return NULL;
}

/* BENDER FIX12: IL2CPP native plugin resolution + bounded managed bootstrap trace. */
typedef const char *(*BenderIl2CppFindPlugin2812)(const char*);
typedef void (*BenderIl2CppSetFindPlugin2812)(BenderIl2CppFindPlugin2812);
typedef void (*BenderIl2CppProfilerInstall2812)(void*,void*);
typedef void (*BenderIl2CppProfilerSetEvents2812)(uint32_t);
typedef void (*BenderIl2CppProfilerEnterLeave2812)(void*,void*);
typedef const char *(*BenderIl2CppMethodGetName2812)(const void*);
typedef void *(*BenderIl2CppMethodGetClass2812)(const void*);
typedef const char *(*BenderIl2CppClassGetName2812)(const void*);
typedef const char *(*BenderIl2CppClassGetNamespace2812)(const void*);

static BenderIl2CppSetFindPlugin2812 g_bender_il2cpp_plugin_real_setter_2812;
static BenderIl2CppFindPlugin2812 g_bender_il2cpp_plugin_unity_callback_2812;
static unsigned long g_bender_il2cpp_plugin_dlsym_overrides_2812;
static unsigned long g_bender_il2cpp_plugin_set_calls_2812;
static unsigned long g_bender_il2cpp_plugin_resolve_calls_2812;
static unsigned long g_bender_il2cpp_plugin_guest_hits_2812;
static unsigned long g_bender_il2cpp_plugin_unity_hits_2812;
static unsigned long g_bender_il2cpp_plugin_misses_2812;
static char g_bender_il2cpp_plugin_last_request_2812[192];
static char g_bender_il2cpp_plugin_last_result_2812[384];

static BenderIl2CppProfilerSetEvents2812 g_bender_profiler_set_events_2812;
static BenderIl2CppMethodGetName2812 g_bender_method_get_name_2812;
static BenderIl2CppMethodGetClass2812 g_bender_method_get_class_2812;
static BenderIl2CppClassGetName2812 g_bender_class_get_name_2812;
static BenderIl2CppClassGetNamespace2812 g_bender_class_get_namespace_2812;
static int g_bender_managed_bootstrap_installed_2812;
static int g_bender_managed_bootstrap_disabled_2812;
static unsigned long g_bender_managed_enter_total_2812;
static unsigned long g_bender_managed_interesting_total_2812;
static unsigned long g_bender_managed_unique_2812;
static unsigned long g_bender_managed_remote_2812;
static unsigned long g_bender_managed_content_2812;
static unsigned long g_bender_managed_region_2812;
static unsigned long g_bender_managed_bundle_2812;
static unsigned long g_bender_managed_loading_2812;
static unsigned long g_bender_managed_world_2812;
static unsigned long g_bender_managed_promo_2812;
static unsigned long g_bender_managed_firebase_2812;
static int g_bender_managed_rc_create_2812;
static int g_bender_managed_rc_done_2812;
static int g_bender_managed_content_done_2812;
static int g_bender_managed_load_default_2812;
static int g_bender_managed_load_region_2812;
static int g_bender_managed_region_bundle_done_2812;
static int g_bender_managed_region_done_2812;
static int g_bender_managed_loadstage_done_2812;
static char g_bender_managed_last_2812[256];
#define BENDER_MANAGED_UNIQUE_2812 64
static char g_bender_managed_seen_2812[BENDER_MANAGED_UNIQUE_2812][192];
static int g_bender_profiler_token_2812;

static int bender_ascii_contains_ci_2812(const char*hay,const char*needle){
    if(!hay||!needle||!*needle)return 0;
    for(const char*h=hay;*h;h++){
        const char*a=h,*b=needle;
        while(*a&&*b){
            unsigned char ca=(unsigned char)*a,cb=(unsigned char)*b;
            if(ca>='A'&&ca<='Z')ca=(unsigned char)(ca+('a'-'A'));
            if(cb>='A'&&cb<='Z')cb=(unsigned char)(cb+('a'-'A'));
            if(ca!=cb)break;
            a++;b++;
        }
        if(!*b)return 1;
    }
    return 0;
}
static int bender_managed_interesting_2812(const char*ns,const char*klass,const char*method){
    const char*terms[]={"RemoteConfig","ContentUpdater","ContentUpdate","Region","Bundle","Loading","Splash","TrackSystem","WorldInfo","Promo","Bootstrap","Firebase"};
    for(size_t i=0;i<sizeof(terms)/sizeof(terms[0]);i++){
        if(bender_ascii_contains_ci_2812(ns,terms[i])||bender_ascii_contains_ci_2812(klass,terms[i])||bender_ascii_contains_ci_2812(method,terms[i]))return 1;
    }
    const char*exact[]={"AwakeCoroutine","CreateRemoteConfigManager","OnRemoteConfigManagerInitializeFetchComplete","OnContentUpdateComplete","StartDownload","LoadDefaultRegion","LoadRegionBundle","RegionBundleLoadCompleted","OnRegionLoadComplete","OnLoadStageComplete"};
    for(size_t i=0;i<sizeof(exact)/sizeof(exact[0]);i++)if(method&&!strcmp(method,exact[i]))return 1;
    return 0;
}
static void bender_managed_unique_note_2812(const char*key){
    if(!key||!*key)return;
    for(size_t i=0;i<BENDER_MANAGED_UNIQUE_2812;i++){
        if(g_bender_managed_seen_2812[i][0]&&!strcmp(g_bender_managed_seen_2812[i],key))return;
    }
    for(size_t i=0;i<BENDER_MANAGED_UNIQUE_2812;i++){
        if(!g_bender_managed_seen_2812[i][0]){
            bender_strlcpy(g_bender_managed_seen_2812[i],key,sizeof(g_bender_managed_seen_2812[i]));
            __atomic_add_fetch(&g_bender_managed_unique_2812,1ul,__ATOMIC_RELAXED);
            return;
        }
    }
}
static void bender_managed_enter_2812(void*profiler,const void*method_info){
    (void)profiler;
    if(g_bender_managed_bootstrap_disabled_2812)return;
    __atomic_add_fetch(&g_bender_managed_enter_total_2812,1ul,__ATOMIC_RELAXED);
    if(!method_info||!g_bender_method_get_name_2812)return;
    const char*method=g_bender_method_get_name_2812(method_info);if(!method)method="?";
    void*klass=g_bender_method_get_class_2812?g_bender_method_get_class_2812(method_info):NULL;
    const char*cn=(klass&&g_bender_class_get_name_2812)?g_bender_class_get_name_2812(klass):"?";
    const char*ns=(klass&&g_bender_class_get_namespace_2812)?g_bender_class_get_namespace_2812(klass):"?";
    if(!cn){cn="?";}
    if(!ns){ns="?";}
    if(!bender_managed_interesting_2812(ns,cn,method))return;
    unsigned long n=__atomic_add_fetch(&g_bender_managed_interesting_total_2812,1ul,__ATOMIC_RELAXED);
    char key[192];snprintf(key,sizeof(key),"%s.%s::%s",ns,cn,method);bender_managed_unique_note_2812(key);bender_strlcpy(g_bender_managed_last_2812,key,sizeof(g_bender_managed_last_2812));
    if(bender_ascii_contains_ci_2812(key,"remoteconfig"))__atomic_add_fetch(&g_bender_managed_remote_2812,1ul,__ATOMIC_RELAXED);
    if(bender_ascii_contains_ci_2812(key,"contentupdat"))__atomic_add_fetch(&g_bender_managed_content_2812,1ul,__ATOMIC_RELAXED);
    if(bender_ascii_contains_ci_2812(key,"region"))__atomic_add_fetch(&g_bender_managed_region_2812,1ul,__ATOMIC_RELAXED);
    if(bender_ascii_contains_ci_2812(key,"bundle"))__atomic_add_fetch(&g_bender_managed_bundle_2812,1ul,__ATOMIC_RELAXED);
    if(bender_ascii_contains_ci_2812(key,"loading")||bender_ascii_contains_ci_2812(key,"splash"))__atomic_add_fetch(&g_bender_managed_loading_2812,1ul,__ATOMIC_RELAXED);
    if(bender_ascii_contains_ci_2812(key,"worldinfo"))__atomic_add_fetch(&g_bender_managed_world_2812,1ul,__ATOMIC_RELAXED);
    if(bender_ascii_contains_ci_2812(key,"promo"))__atomic_add_fetch(&g_bender_managed_promo_2812,1ul,__ATOMIC_RELAXED);
    if(bender_ascii_contains_ci_2812(key,"firebase"))__atomic_add_fetch(&g_bender_managed_firebase_2812,1ul,__ATOMIC_RELAXED);
    if(!strcmp(method,"CreateRemoteConfigManager"))g_bender_managed_rc_create_2812=1;
    if(!strcmp(method,"OnRemoteConfigManagerInitializeFetchComplete")||!strcmp(method,"OnRemoteConfigFetchComplete")||!strcmp(method,"OnFetchComplete"))g_bender_managed_rc_done_2812=1;
    if(!strcmp(method,"OnContentUpdateComplete")||!strcmp(method,"OnContentUpdateCompletedEvent"))g_bender_managed_content_done_2812=1;
    if(!strcmp(method,"LoadDefaultRegion"))g_bender_managed_load_default_2812=1;
    if(!strcmp(method,"LoadRegionBundle"))g_bender_managed_load_region_2812=1;
    if(!strcmp(method,"RegionBundleLoadCompleted"))g_bender_managed_region_bundle_done_2812=1;
    if(!strcmp(method,"OnRegionLoadComplete"))g_bender_managed_region_done_2812=1;
    if(!strcmp(method,"OnLoadStageComplete"))g_bender_managed_loadstage_done_2812=1;
    if(n<=96ul||(n%256ul)==0ul)logmsg("[BENDER_MANAGED_BOOTSTRAP][ENTER] n=%lu ns=%s class=%s method=%s\n",n,ns,cn,method);
}
static int bender_fix64_fmod_logging_enabled_2864(void);
static int bender_fix65_fmod_parsed_2865(Module*m);
static const char*bender_il2cpp_plugin_resolve_2812(const char*request){
    unsigned long n=__atomic_add_fetch(&g_bender_il2cpp_plugin_resolve_calls_2812,1ul,__ATOMIC_RELAXED);
    if(bender_fix64_fmod_logging_enabled_2864()&&request&&*request){
        const char*want=NULL;
        if(!strcmp(request,"fmod")||!strcmp(request,"libfmod.so"))want="libfmodL.so";
        else if(!strcmp(request,"fmodstudio")||!strcmp(request,"libfmodstudio.so"))want="libfmodstudioL.so";
        if(want){
            Module*lm=guest_module_from_path(want);
            if(bender_fix65_fmod_parsed_2865(lm)){
                logmsg("[BENDER_FMOD_LOGGING][PLUGIN_REDIRECT] source=fix65 n=%lu request=%s result=%s mapped_before=%d lazy=%d policy=parsed-lazy-pair code_patch=0 forced_completion=0\n",n,request,lm->path,lm->reserve?1:0,lm->lazy_module);
                return lm->path;
            }
        }
    }
    const char*unity_result=g_bender_il2cpp_plugin_unity_callback_2812?g_bender_il2cpp_plugin_unity_callback_2812(request):NULL;
    Module*m=NULL;const char*result=NULL;const char*source="miss";
    if(unity_result&&*unity_result){
        m=guest_module_from_path(unity_result);
        if(m){result=m->path;source="unity-result-extracted-module";__atomic_add_fetch(&g_bender_il2cpp_plugin_guest_hits_2812,1ul,__ATOMIC_RELAXED);}
        else {result=unity_result;source="unity-original";__atomic_add_fetch(&g_bender_il2cpp_plugin_unity_hits_2812,1ul,__ATOMIC_RELAXED);}
    }
    if(!result&&request&&*request){
        m=guest_module_from_path(request);
        if(m){result=m->path;source="guest-extracted-module";__atomic_add_fetch(&g_bender_il2cpp_plugin_guest_hits_2812,1ul,__ATOMIC_RELAXED);}
    }
    if(!result)__atomic_add_fetch(&g_bender_il2cpp_plugin_misses_2812,1ul,__ATOMIC_RELAXED);
    bender_strlcpy(g_bender_il2cpp_plugin_last_request_2812,request?request:"(null)",sizeof(g_bender_il2cpp_plugin_last_request_2812));
    bender_strlcpy(g_bender_il2cpp_plugin_last_result_2812,result?result:"(null)",sizeof(g_bender_il2cpp_plugin_last_result_2812));
    if(n<=64ul||(n%128ul)==0ul||!result)logmsg("[BENDER_IL2CPP_PLUGIN][RESOLVE] n=%lu request=%s unity=%s result=%s source=%s\n",n,request?request:"(null)",unity_result?unity_result:"(null)",result?result:"(null)",source);
    return result;
}
static void bender_il2cpp_plugin_setter_2812(BenderIl2CppFindPlugin2812 cb){
    unsigned long n=__atomic_add_fetch(&g_bender_il2cpp_plugin_set_calls_2812,1ul,__ATOMIC_RELAXED);
    g_bender_il2cpp_plugin_unity_callback_2812=(cb==bender_il2cpp_plugin_resolve_2812)?NULL:cb;
    logmsg("[BENDER_IL2CPP_PLUGIN][SET] n=%lu unity_callback=%p chained_callback=%p real_setter=%p\n",n,(void*)cb,(void*)bender_il2cpp_plugin_resolve_2812,(void*)g_bender_il2cpp_plugin_real_setter_2812);
    if(g_bender_il2cpp_plugin_real_setter_2812)g_bender_il2cpp_plugin_real_setter_2812(bender_il2cpp_plugin_resolve_2812);
}
static void*bender_il2cpp_plugin_dlsym_override_2812(Module*m,const char*symbol){
    if(!m||!symbol||strcmp(m->name,"libil2cpp.so")||strcmp(symbol,"il2cpp_set_find_plugin_callback"))return NULL;
    uintptr_t real=module_symbol(m,symbol);if(!real)return NULL;
    g_bender_il2cpp_plugin_real_setter_2812=(BenderIl2CppSetFindPlugin2812)real;
    unsigned long n=__atomic_add_fetch(&g_bender_il2cpp_plugin_dlsym_overrides_2812,1ul,__ATOMIC_RELAXED);
    logmsg("[BENDER_IL2CPP_PLUGIN][DLSYM_OVERRIDE] n=%lu module=%s symbol=%s real=%p wrapper=%p\n",n,m->name,symbol,(void*)real,(void*)bender_il2cpp_plugin_setter_2812);
    return (void*)bender_il2cpp_plugin_setter_2812;
}
static void __attribute__((unused)) bender_fix12_install_2812(Module*im){
    if(g_bender_managed_bootstrap_installed_2812||!g_bender_application_paths_installed_2772)return;
    if(!im||im->init_state!=2){logmsg("[BENDER_MANAGED_BOOTSTRAP][SKIP] reason=libil2cpp-not-ready\n");return;}
    uintptr_t pi=module_symbol(im,"il2cpp_profiler_install");
    uintptr_t ps=module_symbol(im,"il2cpp_profiler_set_events");
    uintptr_t pe=module_symbol(im,"il2cpp_profiler_install_enter_leave");
    uintptr_t mn=module_symbol(im,"il2cpp_method_get_name");
    uintptr_t mc=module_symbol(im,"il2cpp_method_get_class");
    uintptr_t cn=module_symbol(im,"il2cpp_class_get_name");
    uintptr_t cns=module_symbol(im,"il2cpp_class_get_namespace");
    if(!pi||!ps||!pe||!mn||!mc||!cn||!cns){
        logmsg("[BENDER_MANAGED_BOOTSTRAP][SKIP] reason=profiler-api-missing install=%p events=%p enterleave=%p method_name=%p method_class=%p class_name=%p class_ns=%p\n",(void*)pi,(void*)ps,(void*)pe,(void*)mn,(void*)mc,(void*)cn,(void*)cns);return;
    }
    g_bender_profiler_set_events_2812=(BenderIl2CppProfilerSetEvents2812)ps;
    g_bender_method_get_name_2812=(BenderIl2CppMethodGetName2812)mn;
    g_bender_method_get_class_2812=(BenderIl2CppMethodGetClass2812)mc;
    g_bender_class_get_name_2812=(BenderIl2CppClassGetName2812)cn;
    g_bender_class_get_namespace_2812=(BenderIl2CppClassGetNamespace2812)cns;
    ((BenderIl2CppProfilerInstall2812)pi)((void*)&g_bender_profiler_token_2812,NULL);
    ((BenderIl2CppProfilerEnterLeave2812)pe)((void*)bender_managed_enter_2812,NULL);
    g_bender_profiler_set_events_2812((uint32_t)(1u<<12));
    g_bender_managed_bootstrap_installed_2812=1;
    logmsg("[BENDER_MANAGED_BOOTSTRAP][INSTALL] installed=1 mode=bounded-enter-only event_mask=0x1000 content_paths=1 max_frame=1800\n");
}
static void __attribute__((unused)) bender_fix12_health_2812(unsigned frame){
    logmsg("[BENDER_IL2CPP_PLUGIN][HEALTH] frame=%u override=%lu set=%lu resolve=%lu guest_hit=%lu unity_hit=%lu miss=%lu last_request=%s last_result=%s\n",frame,g_bender_il2cpp_plugin_dlsym_overrides_2812,g_bender_il2cpp_plugin_set_calls_2812,g_bender_il2cpp_plugin_resolve_calls_2812,g_bender_il2cpp_plugin_guest_hits_2812,g_bender_il2cpp_plugin_unity_hits_2812,g_bender_il2cpp_plugin_misses_2812,g_bender_il2cpp_plugin_last_request_2812[0]?g_bender_il2cpp_plugin_last_request_2812:"(none)",g_bender_il2cpp_plugin_last_result_2812[0]?g_bender_il2cpp_plugin_last_result_2812:"(none)");
    logmsg("[BENDER_MANAGED_BOOTSTRAP][HEALTH] frame=%u installed=%d disabled=%d enter=%lu interesting=%lu unique=%lu remote=%lu content=%lu region=%lu bundle=%lu loading=%lu world=%lu promo=%lu firebase=%lu milestones=rc_create:%d,rc_done:%d,content_done:%d,load_default:%d,load_region:%d,region_bundle_done:%d,region_done:%d,loadstage_done:%d last=%s\n",frame,g_bender_managed_bootstrap_installed_2812,g_bender_managed_bootstrap_disabled_2812,g_bender_managed_enter_total_2812,g_bender_managed_interesting_total_2812,g_bender_managed_unique_2812,g_bender_managed_remote_2812,g_bender_managed_content_2812,g_bender_managed_region_2812,g_bender_managed_bundle_2812,g_bender_managed_loading_2812,g_bender_managed_world_2812,g_bender_managed_promo_2812,g_bender_managed_firebase_2812,g_bender_managed_rc_create_2812,g_bender_managed_rc_done_2812,g_bender_managed_content_done_2812,g_bender_managed_load_default_2812,g_bender_managed_load_region_2812,g_bender_managed_region_bundle_done_2812,g_bender_managed_region_done_2812,g_bender_managed_loadstage_done_2812,g_bender_managed_last_2812[0]?g_bender_managed_last_2812:"(none)");
    if(frame>=1800u&&g_bender_managed_bootstrap_installed_2812&&!g_bender_managed_bootstrap_disabled_2812&&g_bender_profiler_set_events_2812){
        g_bender_profiler_set_events_2812(0u);g_bender_managed_bootstrap_disabled_2812=1;
        logmsg("[BENDER_MANAGED_BOOTSTRAP][DISABLE] frame=%u reason=bounded-bootstrap-window preserve-long-run-performance=1\n",frame);
    }
}


/* BENDER FIX13: IL2CPP runtime_invoke fallback + persistent bootstrap tail health. */
/* BENDER FIX14: runtime_invoke long-run bootstrap trace remains active after profiler cutoff. */
typedef void *(*BenderIl2CppRuntimeInvoke2813)(const void*,void*,void**,void**);
static BenderIl2CppRuntimeInvoke2813 g_bender_runtime_invoke_real_2813;
static Module *g_bender_il2cpp_module_2813;
static unsigned long g_bender_runtime_invoke_dlsym_2813;
static unsigned long g_bender_runtime_invoke_total_2813;
static unsigned long g_bender_runtime_invoke_interesting_2813;
static unsigned long g_bender_runtime_invoke_exceptions_2813;
static unsigned long g_bender_runtime_remote_2813;
static unsigned long g_bender_runtime_content_2813;
static unsigned long g_bender_runtime_region_2813;
static unsigned long g_bender_runtime_bundle_2813;
static unsigned long g_bender_runtime_loading_2813;
static unsigned long g_bender_fix13_tail_health_2813;
static unsigned g_bender_fix13_frame_hint_2813;
static int g_bender_fix13_installed_2813;
static char g_bender_runtime_last_2813[256];
static char g_bender_fix13_last_stage_2813[96];
/* BENDER FIX14: decouple runtime_invoke observation from bounded profiler window. */
#define BENDER_FIX14_RECENT_2814 8
static char g_bender_fix14_recent_2814[BENDER_FIX14_RECENT_2814][256];
static unsigned g_bender_fix14_recent_frame_2814[BENDER_FIX14_RECENT_2814];
static unsigned long g_bender_fix14_recent_seq_2814;
static unsigned long g_bender_fix14_post_profiler_interesting_2814;
static unsigned long g_bender_fix14_runtime_described_2814;
static unsigned g_bender_fix14_last_interesting_frame_2814;
static char g_bender_fix14_last_transition_2814[256];

/* BENDER FIX15: preserve application startup/lifecycle evidence separately from engine/SDK noise. */
typedef void *(*BenderIl2CppClassGetImage2815)(void*);
typedef const char *(*BenderIl2CppImageGetName2815)(void*);
static BenderIl2CppClassGetImage2815 g_bender_class_get_image_2815;
static BenderIl2CppImageGetName2815 g_bender_image_get_name_2815;
#define BENDER_FIX15_APP_RECENT_2815 16
#define BENDER_FIX15_APP_STARTUP_2815 32
static char g_bender_fix15_app_recent_2815[BENDER_FIX15_APP_RECENT_2815][320];
static unsigned g_bender_fix15_app_recent_frame_2815[BENDER_FIX15_APP_RECENT_2815];
static unsigned long g_bender_fix15_app_recent_seq_2815;
static char g_bender_fix15_app_startup_2815[BENDER_FIX15_APP_STARTUP_2815][320];
static unsigned g_bender_fix15_app_startup_frame_2815[BENDER_FIX15_APP_STARTUP_2815];
static unsigned long g_bender_fix15_app_startup_count_2815;
static unsigned long g_bender_fix15_app_invokes_2815;
static unsigned long g_bender_fix15_raw_interesting_2815;
static unsigned long g_bender_fix15_bootstrap_candidates_2815;
static unsigned g_bender_fix15_last_app_frame_2815;
static char g_bender_fix15_last_app_2815[320];
static char g_bender_fix15_last_app_transition_2815[320];

static void bender_fix13_resolve_method_api_2813(Module*im){
    if(!im)return;
    g_bender_il2cpp_module_2813=im;
    if(!g_bender_method_get_name_2812){uintptr_t p=module_symbol(im,"il2cpp_method_get_name");if(p)g_bender_method_get_name_2812=(BenderIl2CppMethodGetName2812)p;}
    if(!g_bender_method_get_class_2812){uintptr_t p=module_symbol(im,"il2cpp_method_get_class");if(p)g_bender_method_get_class_2812=(BenderIl2CppMethodGetClass2812)p;}
    if(!g_bender_class_get_name_2812){uintptr_t p=module_symbol(im,"il2cpp_class_get_name");if(p)g_bender_class_get_name_2812=(BenderIl2CppClassGetName2812)p;}
    if(!g_bender_class_get_namespace_2812){uintptr_t p=module_symbol(im,"il2cpp_class_get_namespace");if(p)g_bender_class_get_namespace_2812=(BenderIl2CppClassGetNamespace2812)p;}
    if(!g_bender_class_get_image_2815){uintptr_t p=module_symbol(im,"il2cpp_class_get_image");if(p)g_bender_class_get_image_2815=(BenderIl2CppClassGetImage2815)p;}
    if(!g_bender_image_get_name_2815){uintptr_t p=module_symbol(im,"il2cpp_image_get_name");if(p)g_bender_image_get_name_2815=(BenderIl2CppImageGetName2815)p;}
    if(!g_bender_runtime_invoke_real_2813){uintptr_t p=module_symbol(im,"il2cpp_runtime_invoke");if(p)g_bender_runtime_invoke_real_2813=(BenderIl2CppRuntimeInvoke2813)p;}
}
static void bender_fix13_note_milestone_2813(const char*method){
    if(!method)return;
    if(!strcmp(method,"CreateRemoteConfigManager"))g_bender_managed_rc_create_2812=1;
    if(!strcmp(method,"OnRemoteConfigManagerInitializeFetchComplete")||!strcmp(method,"OnRemoteConfigFetchComplete")||!strcmp(method,"OnFetchComplete"))g_bender_managed_rc_done_2812=1;
    if(!strcmp(method,"OnContentUpdateComplete")||!strcmp(method,"OnContentUpdateCompletedEvent"))g_bender_managed_content_done_2812=1;
    if(!strcmp(method,"LoadDefaultRegion"))g_bender_managed_load_default_2812=1;
    if(!strcmp(method,"LoadRegionBundle"))g_bender_managed_load_region_2812=1;
    if(!strcmp(method,"RegionBundleLoadCompleted"))g_bender_managed_region_bundle_done_2812=1;
    if(!strcmp(method,"OnRegionLoadComplete"))g_bender_managed_region_done_2812=1;
    if(!strcmp(method,"OnLoadStageComplete"))g_bender_managed_loadstage_done_2812=1;
}
static int bender_fix13_describe_method_2813(const void*method_info,char*out,size_t cap,const char**method_out){
    if(out&&cap)out[0]=0;
    if(method_out)*method_out=NULL;
    if(!method_info||!g_bender_method_get_name_2812)return 0;
    const char*method=g_bender_method_get_name_2812(method_info);if(!method)method="?";
    void*klass=g_bender_method_get_class_2812?g_bender_method_get_class_2812(method_info):NULL;
    const char*cn=(klass&&g_bender_class_get_name_2812)?g_bender_class_get_name_2812(klass):"?";
    const char*ns=(klass&&g_bender_class_get_namespace_2812)?g_bender_class_get_namespace_2812(klass):"?";
    if(!cn)cn="?";
    if(!ns)ns="?";
    if(out&&cap)snprintf(out,cap,"%s.%s::%s",ns,cn,method);
    if(method_out)*method_out=method;
    return bender_managed_interesting_2812(ns,cn,method);
}
static int bender_fix14_runtime_noise_2814(const char*key,const char*method){
    if(!key||!method)return 0;
    /* FirebaseMonoBehaviour::Update is a per-frame dispatcher, not a bootstrap milestone. */
    if(!strcmp(method,"Update")&&bender_ascii_contains_ci_2812(key,"FirebaseMonoBehaviour"))return 1;
    return 0;
}
static const char*bender_fix15_image_name_2815(const void*method_info){
    if(!method_info||!g_bender_method_get_class_2812||!g_bender_class_get_image_2815||!g_bender_image_get_name_2815)return "(image-unknown)";
    void*klass=g_bender_method_get_class_2812(method_info);if(!klass)return "(image-unknown)";
    void*image=g_bender_class_get_image_2815(klass);if(!image)return "(image-unknown)";
    const char*name=g_bender_image_get_name_2815(image);return (name&&*name)?name:"(image-unknown)";
}
static int bender_fix15_platform_image_2815(const char*image){
    if(!image||!*image||!strcmp(image,"(image-unknown)"))return 1;
    const char*prefixes[]={"UnityEngine","Unity.","System","mscorlib","netstandard","Mono.","Microsoft.","Firebase","Google.","Newtonsoft","TMPro","Facebook","AppsFlyer","Adjust","AppLovin","IronSource","com.unity"};
    for(size_t i=0;i<sizeof(prefixes)/sizeof(prefixes[0]);i++)if(!strncasecmp(image,prefixes[i],strlen(prefixes[i])))return 1;
    return 0;
}
static int bender_fix15_app_candidate_2815(const char*image){
    if(!image||!*image||!strcmp(image,"(image-unknown)"))return 0;
    if(!strcasecmp(image,"Assembly-CSharp.dll")||!strcasecmp(image,"Assembly-CSharp-firstpass.dll"))return 1;
    return !bender_fix15_platform_image_2815(image);
}
static int bender_fix15_lifecycle_method_2815(const char*method){
    if(!method)return 0;
    const char*names[]={"Awake","OnEnable","Start","OnApplicationPause","OnApplicationFocus","OnDisable","OnDestroy"};
    for(size_t i=0;i<sizeof(names)/sizeof(names[0]);i++)if(!strcmp(method,names[i]))return 1;
    return 0;
}
static int bender_fix15_bootstrap_candidate_2815(const char*key,const char*method){
    if(!key||!method)return 0;
    if(bender_fix14_runtime_noise_2814(key,method))return 0;
    /* Do not mistake UI/input classes such as InputRegion for a content-region loader. */
    if(bender_ascii_contains_ci_2812(key,"InputRegion"))return 0;
    if(bender_ascii_contains_ci_2812(key,"remoteconfig"))return 1;
    if(bender_ascii_contains_ci_2812(key,"contentupdat"))return 1;
    if(bender_ascii_contains_ci_2812(key,"worldinfo"))return 1;
    if(bender_ascii_contains_ci_2812(key,"bootstrap"))return 1;
    if(bender_ascii_contains_ci_2812(key,"loading")||bender_ascii_contains_ci_2812(key,"splash"))return 1;
    if(bender_ascii_contains_ci_2812(key,"bundle"))return 1;
    if(bender_ascii_contains_ci_2812(method,"LoadRegion")||bender_ascii_contains_ci_2812(method,"RegionBundle")||bender_ascii_contains_ci_2812(method,"RegionLoad"))return 1;
    return 0;
}
static void bender_fix15_app_note_2815(const char*image,const char*key,unsigned frame){
    if(!image||!key||!*key)return;
    __atomic_add_fetch(&g_bender_fix15_app_invokes_2815,1ul,__ATOMIC_RELAXED);
    bender_strlcpy(g_bender_fix15_last_app_2815,key,sizeof(g_bender_fix15_last_app_2815));
    g_bender_fix15_last_app_frame_2815=frame;
    if(g_bender_fix15_last_app_transition_2815[0]&&!strcmp(g_bender_fix15_last_app_transition_2815,key))return;
    bender_strlcpy(g_bender_fix15_last_app_transition_2815,key,sizeof(g_bender_fix15_last_app_transition_2815));
    unsigned long seq=__atomic_add_fetch(&g_bender_fix15_app_recent_seq_2815,1ul,__ATOMIC_RELAXED);
    size_t slot=(size_t)((seq-1ul)%BENDER_FIX15_APP_RECENT_2815);
    snprintf(g_bender_fix15_app_recent_2815[slot],sizeof(g_bender_fix15_app_recent_2815[slot]),"%s:%s",image,key);
    g_bender_fix15_app_recent_frame_2815[slot]=frame;
    if(frame<=240u&&g_bender_fix15_app_startup_count_2815<(unsigned long)BENDER_FIX15_APP_STARTUP_2815){
        size_t s=(size_t)g_bender_fix15_app_startup_count_2815++;
        snprintf(g_bender_fix15_app_startup_2815[s],sizeof(g_bender_fix15_app_startup_2815[s]),"%s:%s",image,key);
        g_bender_fix15_app_startup_frame_2815[s]=frame;
    }
}
static void bender_fix15_emit_history_2815(unsigned frame){
    char recent[5400];size_t used=0;recent[0]=0;
    unsigned long seq=__atomic_load_n(&g_bender_fix15_app_recent_seq_2815,__ATOMIC_RELAXED);
    unsigned long count=seq<(unsigned long)BENDER_FIX15_APP_RECENT_2815?seq:(unsigned long)BENDER_FIX15_APP_RECENT_2815;
    unsigned long start=seq>count?seq-count:0ul;
    for(unsigned long k=0;k<count;k++){
        unsigned long logical=start+k;size_t slot=(size_t)(logical%BENDER_FIX15_APP_RECENT_2815);
        int wrote=snprintf(recent+used,sizeof(recent)-used,"%s%lu@%u:%s",used?" | ":"",logical+1ul,g_bender_fix15_app_recent_frame_2815[slot],g_bender_fix15_app_recent_2815[slot][0]?g_bender_fix15_app_recent_2815[slot]:"(empty)");
        if(wrote<0)break;
        if((size_t)wrote>=sizeof(recent)-used){used=sizeof(recent)-1;break;}
        used+=(size_t)wrote;
    }
    logmsg("[BENDER_MANAGED_APP][RECENT] source=fix15-tail frame=%u seq=%lu count=%lu history=%s\n",frame,seq,count,recent[0]?recent:"(none)");
    char startup[7600];used=0;startup[0]=0;unsigned long sc=g_bender_fix15_app_startup_count_2815;
    for(unsigned long k=0;k<sc;k++){
        int wrote=snprintf(startup+used,sizeof(startup)-used,"%s%lu@%u:%s",used?" | ":"",k+1ul,g_bender_fix15_app_startup_frame_2815[k],g_bender_fix15_app_startup_2815[k][0]?g_bender_fix15_app_startup_2815[k]:"(empty)");
        if(wrote<0)break;
        if((size_t)wrote>=sizeof(startup)-used){used=sizeof(startup)-1;break;}
        used+=(size_t)wrote;
    }
    logmsg("[BENDER_MANAGED_APP][STARTUP] source=fix15-tail frame=%u frozen_until=240 count=%lu history=%s\n",frame,sc,startup[0]?startup:"(none)");
}
static void bender_fix14_recent_note_2814(const char*key,unsigned frame){
    if(!key||!*key)return;
    if(g_bender_fix14_last_transition_2814[0]&&!strcmp(g_bender_fix14_last_transition_2814,key))return;
    bender_strlcpy(g_bender_fix14_last_transition_2814,key,sizeof(g_bender_fix14_last_transition_2814));
    unsigned long seq=__atomic_add_fetch(&g_bender_fix14_recent_seq_2814,1ul,__ATOMIC_RELAXED);
    size_t slot=(size_t)((seq-1ul)%BENDER_FIX14_RECENT_2814);
    bender_strlcpy(g_bender_fix14_recent_2814[slot],key,sizeof(g_bender_fix14_recent_2814[slot]));
    g_bender_fix14_recent_frame_2814[slot]=frame;
}
static void bender_fix14_recent_health_2814(unsigned frame){
    char buf[2300];size_t used=0;buf[0]=0;
    unsigned long seq=__atomic_load_n(&g_bender_fix14_recent_seq_2814,__ATOMIC_RELAXED);
    unsigned long count=seq<(unsigned long)BENDER_FIX14_RECENT_2814?seq:(unsigned long)BENDER_FIX14_RECENT_2814;
    unsigned long start=seq>count?seq-count:0ul;
    for(unsigned long k=0;k<count;k++){
        unsigned long logical=start+k;size_t slot=(size_t)(logical%BENDER_FIX14_RECENT_2814);
        const char*key=g_bender_fix14_recent_2814[slot][0]?g_bender_fix14_recent_2814[slot]:"(empty)";
        int wrote=snprintf(buf+used,sizeof(buf)-used,"%s%lu@%u:%s",used?" | ":"",logical+1ul,g_bender_fix14_recent_frame_2814[slot],key);
        if(wrote<0){break;}
        if((size_t)wrote>=sizeof(buf)-used){used=sizeof(buf)-1;break;}
        used+=(size_t)wrote;
    }
    logmsg("[BENDER_MANAGED_INVOKE][RECENT] source=fix14-tail frame=%u seq=%lu count=%lu history=%s\n",frame,seq,count,buf[0]?buf:"(none)");
}

/* BENDER FIX16: read-only IL2CPP direct-call graph + semantic bootstrap edge trace. */
typedef void *(*BenderIl2CppDomainGet2816)(void);
typedef const void **(*BenderIl2CppDomainGetAssemblies2816)(const void*,size_t*);
typedef void *(*BenderIl2CppAssemblyGetImage2816)(const void*);
typedef size_t (*BenderIl2CppImageGetClassCount2816)(const void*);
typedef void *(*BenderIl2CppImageGetClass2816)(const void*,size_t);
typedef const void *(*BenderIl2CppClassGetMethods2816)(void*,void**);

typedef struct { uintptr_t addr; const void *method; } BenderFix16MethodMap2816;
typedef struct { const void *method; unsigned frame; char image[96]; char key[224]; } BenderFix16Root2816;

#define BENDER_FIX16_METHOD_CAP_2816 131072u
#define BENDER_FIX16_ROOT_CAP_2816 16u
#define BENDER_FIX16_VISITED_CAP_2816 192u
#define BENDER_FIX16_EDGE_CAP_2816 320u
#define BENDER_FIX16_RECENT_2816 24u
#define BENDER_FIX16_SCAN_BYTES_2816 8192u
#define BENDER_FIX16_MAX_DEPTH_2816 4u

static BenderIl2CppDomainGet2816 g_bender_fix16_domain_get_2816;
static BenderIl2CppDomainGetAssemblies2816 g_bender_fix16_domain_get_assemblies_2816;
static BenderIl2CppAssemblyGetImage2816 g_bender_fix16_assembly_get_image_2816;
static BenderIl2CppImageGetClassCount2816 g_bender_fix16_image_get_class_count_2816;
static BenderIl2CppImageGetClass2816 g_bender_fix16_image_get_class_2816;
static BenderIl2CppClassGetMethods2816 g_bender_fix16_class_get_methods_2816;
static BenderFix16MethodMap2816 *g_bender_fix16_map_2816;
static size_t g_bender_fix16_map_count_2816;
static unsigned long g_bender_fix16_map_methods_seen_2816;
static unsigned long g_bender_fix16_map_slot0_2816;
static unsigned long g_bender_fix16_map_slot1_2816;
static unsigned long g_bender_fix16_map_rejected_2816;
static unsigned long g_bender_fix16_map_dropped_2816;
static int g_bender_fix16_map_attempted_2816;
static int g_bender_fix16_map_ready_2816;
static BenderFix16Root2816 g_bender_fix16_roots_2816[BENDER_FIX16_ROOT_CAP_2816];
static unsigned g_bender_fix16_root_count_2816;
static int g_bender_fix16_analysis_done_2816;
static unsigned long g_bender_fix16_nodes_2816;
static unsigned long g_bender_fix16_edges_2816;
static unsigned long g_bender_fix16_app_edges_2816;
static unsigned long g_bender_fix16_bootstrap_edges_2816;
static unsigned long g_bender_fix16_bl_total_2816;
static unsigned long g_bender_fix16_bl_unmatched_2816;
static uintptr_t g_bender_fix16_visited_2816[BENDER_FIX16_VISITED_CAP_2816];
static unsigned g_bender_fix16_visited_count_2816;
static char g_bender_fix16_recent_2816[BENDER_FIX16_RECENT_2816][384];
static unsigned long g_bender_fix16_recent_seq_2816;

static int bender_fix16_exec_ptr_2816(uintptr_t p){
    Module*m=g_bender_il2cpp_module_2813;if(!m||!m->reserve||!m->ph||!m->eh||!p)return 0;
    for(int i=0;i<m->eh->e_phnum;i++){
        Elf64_Phdr*ph=&m->ph[i];if(ph->p_type!=PT_LOAD||!(ph->p_flags&PF_X))continue;
        uintptr_t lo=m->bias+ph->p_vaddr,hi=lo+ph->p_memsz;if(p>=lo&&p<hi)return 1;
    }
    return 0;
}
static uintptr_t bender_fix16_method_ptr_2816(const void*method,int*slot_out){
    if(slot_out)*slot_out=-1;
    if(!method)return 0;
    uintptr_t slots[2]={0,0};memcpy(slots,method,sizeof(slots));
    for(int i=0;i<2;i++)if(bender_fix16_exec_ptr_2816(slots[i])){if(slot_out)*slot_out=i;return slots[i];}
    return 0;
}
static int bender_fix16_map_cmp_2816(const void*a,const void*b){
    const BenderFix16MethodMap2816*x=(const BenderFix16MethodMap2816*)a,*y=(const BenderFix16MethodMap2816*)b;
    return x->addr<y->addr?-1:(x->addr>y->addr?1:0);
}
static void bender_fix16_resolve_api_2816(void){
    Module*im=g_bender_il2cpp_module_2813;if(!im)return;
#define BENDER_FIX16_RESOLVE_2816(field,type,name) do{if(!(field)){uintptr_t p=module_symbol(im,name);if(p)(field)=(type)p;}}while(0)
    BENDER_FIX16_RESOLVE_2816(g_bender_fix16_domain_get_2816,BenderIl2CppDomainGet2816,"il2cpp_domain_get");
    BENDER_FIX16_RESOLVE_2816(g_bender_fix16_domain_get_assemblies_2816,BenderIl2CppDomainGetAssemblies2816,"il2cpp_domain_get_assemblies");
    BENDER_FIX16_RESOLVE_2816(g_bender_fix16_assembly_get_image_2816,BenderIl2CppAssemblyGetImage2816,"il2cpp_assembly_get_image");
    BENDER_FIX16_RESOLVE_2816(g_bender_fix16_image_get_class_count_2816,BenderIl2CppImageGetClassCount2816,"il2cpp_image_get_class_count");
    BENDER_FIX16_RESOLVE_2816(g_bender_fix16_image_get_class_2816,BenderIl2CppImageGetClass2816,"il2cpp_image_get_class");
    BENDER_FIX16_RESOLVE_2816(g_bender_fix16_class_get_methods_2816,BenderIl2CppClassGetMethods2816,"il2cpp_class_get_methods");
#undef BENDER_FIX16_RESOLVE_2816
}
static int bender_fix16_build_map_2816(unsigned frame){
    if(g_bender_fix16_map_ready_2816)return 1;
    if(g_bender_fix16_map_attempted_2816>=3)return 0;
    g_bender_fix16_map_attempted_2816++;
    bender_fix16_resolve_api_2816();
    if(!g_bender_fix16_domain_get_2816||!g_bender_fix16_domain_get_assemblies_2816||!g_bender_fix16_assembly_get_image_2816||!g_bender_fix16_image_get_class_count_2816||!g_bender_fix16_image_get_class_2816||!g_bender_fix16_class_get_methods_2816){
        logmsg("[BENDER_MANAGED_DIRECT][MAP] source=fix16 frame=%u ready=0 reason=il2cpp-enumeration-api-missing domain=%p assemblies=%p assembly_image=%p class_count=%p image_class=%p class_methods=%p forced_completion=0\n",frame,(void*)g_bender_fix16_domain_get_2816,(void*)g_bender_fix16_domain_get_assemblies_2816,(void*)g_bender_fix16_assembly_get_image_2816,(void*)g_bender_fix16_image_get_class_count_2816,(void*)g_bender_fix16_image_get_class_2816,(void*)g_bender_fix16_class_get_methods_2816);return 0;
    }
    if(!g_bender_fix16_map_2816){g_bender_fix16_map_2816=(BenderFix16MethodMap2816*)calloc(BENDER_FIX16_METHOD_CAP_2816,sizeof(*g_bender_fix16_map_2816));if(!g_bender_fix16_map_2816){logmsg("[BENDER_MANAGED_DIRECT][MAP] source=fix16 frame=%u ready=0 reason=calloc-failed forced_completion=0\n",frame);return 0;}}
    void*domain=g_bender_fix16_domain_get_2816();size_t ac=0;const void**assemblies=domain?g_bender_fix16_domain_get_assemblies_2816(domain,&ac):NULL;
    if(!assemblies||!ac){logmsg("[BENDER_MANAGED_DIRECT][MAP] source=fix16 frame=%u ready=0 reason=no-assemblies domain=%p assemblies=%p count=%zu forced_completion=0\n",frame,domain,(void*)assemblies,ac);return 0;}
    size_t classes=0;
    for(size_t ai=0;ai<ac;ai++){
        void*image=g_bender_fix16_assembly_get_image_2816(assemblies[ai]);if(!image)continue;size_t cc=g_bender_fix16_image_get_class_count_2816(image);classes+=cc;
        for(size_t ci=0;ci<cc;ci++){void*klass=g_bender_fix16_image_get_class_2816(image,ci);if(!klass)continue;void*iter=NULL;const void*mi=NULL;
            while((mi=g_bender_fix16_class_get_methods_2816(klass,&iter))!=NULL){g_bender_fix16_map_methods_seen_2816++;int slot=-1;uintptr_t ptr=bender_fix16_method_ptr_2816(mi,&slot);if(!ptr){g_bender_fix16_map_rejected_2816++;continue;}if(slot==0)g_bender_fix16_map_slot0_2816++;else if(slot==1)g_bender_fix16_map_slot1_2816++;
                if(g_bender_fix16_map_count_2816<BENDER_FIX16_METHOD_CAP_2816){g_bender_fix16_map_2816[g_bender_fix16_map_count_2816].addr=ptr;g_bender_fix16_map_2816[g_bender_fix16_map_count_2816].method=mi;g_bender_fix16_map_count_2816++;}else g_bender_fix16_map_dropped_2816++;
            }
        }
    }
    if(g_bender_fix16_map_count_2816)qsort(g_bender_fix16_map_2816,g_bender_fix16_map_count_2816,sizeof(*g_bender_fix16_map_2816),bender_fix16_map_cmp_2816);
    g_bender_fix16_map_ready_2816=g_bender_fix16_map_count_2816?1:0;
    logmsg("[BENDER_MANAGED_DIRECT][MAP] source=fix16 frame=%u ready=%d assemblies=%zu classes=%zu methods_seen=%lu mapped=%zu slot0=%lu slot1=%lu rejected=%lu dropped=%lu layout_probe=methodinfo-slot0-or-slot1 code_patch=0 forced_completion=0\n",frame,g_bender_fix16_map_ready_2816,ac,classes,g_bender_fix16_map_methods_seen_2816,g_bender_fix16_map_count_2816,g_bender_fix16_map_slot0_2816,g_bender_fix16_map_slot1_2816,g_bender_fix16_map_rejected_2816,g_bender_fix16_map_dropped_2816);
    return g_bender_fix16_map_ready_2816;
}
static const BenderFix16MethodMap2816*bender_fix16_find_addr_2816(uintptr_t addr){
    size_t lo=0,hi=g_bender_fix16_map_count_2816;while(lo<hi){size_t mid=lo+(hi-lo)/2;uintptr_t a=g_bender_fix16_map_2816[mid].addr;if(a<addr)lo=mid+1;else hi=mid;}if(lo<g_bender_fix16_map_count_2816&&g_bender_fix16_map_2816[lo].addr==addr)return &g_bender_fix16_map_2816[lo];return NULL;
}
static uintptr_t bender_fix16_follow_direct_branch_2816(uintptr_t addr){
    uintptr_t cur=addr;for(int hop=0;hop<2;hop++){if(!bender_fix16_exec_ptr_2816(cur))break;uint32_t ins=0;memcpy(&ins,(const void*)cur,sizeof(ins));if((ins&UINT32_C(0xfc000000))!=UINT32_C(0x14000000))break;int64_t imm=(int64_t)(ins&UINT32_C(0x03ffffff));if(imm&(INT64_C(1)<<25))imm-=INT64_C(1)<<26;cur=(uintptr_t)((int64_t)cur+(imm<<2));}return cur;
}
static size_t bender_fix16_method_span_2816(uintptr_t addr){
    size_t lo=0,hi=g_bender_fix16_map_count_2816;while(lo<hi){size_t mid=lo+(hi-lo)/2;if(g_bender_fix16_map_2816[mid].addr<=addr)lo=mid+1;else hi=mid;}uintptr_t next=(lo<g_bender_fix16_map_count_2816)?g_bender_fix16_map_2816[lo].addr:0;size_t span=(next>addr)?(size_t)(next-addr):BENDER_FIX16_SCAN_BYTES_2816;if(span>BENDER_FIX16_SCAN_BYTES_2816)span=BENDER_FIX16_SCAN_BYTES_2816;span&=~(size_t)3;return span;
}
static int bender_fix16_visited_2816(uintptr_t addr){for(unsigned i=0;i<g_bender_fix16_visited_count_2816;i++)if(g_bender_fix16_visited_2816[i]==addr)return 1;if(g_bender_fix16_visited_count_2816<BENDER_FIX16_VISITED_CAP_2816)g_bender_fix16_visited_2816[g_bender_fix16_visited_count_2816++]=addr;return 0;}
/* BENDER FIX17: strict-Werror bounded formatting for FIX16 recent-edge history. */
static void bender_fix16_recent_note_2816(unsigned depth,const char*caller,const char*callee,const char*image,int app,int boot){
    unsigned long seq=++g_bender_fix16_recent_seq_2816;
    size_t slot=(size_t)((seq-1ul)%BENDER_FIX16_RECENT_2816);
    /* Keep the diagnostic ring entry provably below 384 bytes even under GCC
       -O2 -Wall -Wextra -Werror=format-truncation. Full names remain available
       in the independent [BENDER_MANAGED_DIRECT][EDGE] log line. */
    snprintf(
        g_bender_fix16_recent_2816[slot],
        sizeof(g_bender_fix16_recent_2816[slot]),
        "d%u:%.96s=>%.48s:%.192s%s%s",
        depth,
        caller?caller:"?",
        image?image:"?",
        callee?callee:"?",
        app?":app":"",
        boot?":bootstrap":""
    );
}
static void bender_fix16_scan_method_2816(const void*mi,unsigned depth,const char*root_key){
    if(!mi||depth>BENDER_FIX16_MAX_DEPTH_2816||g_bender_fix16_nodes_2816>=BENDER_FIX16_VISITED_CAP_2816||g_bender_fix16_edges_2816>=BENDER_FIX16_EDGE_CAP_2816)return;
    int slot=-1;
    uintptr_t addr=bender_fix16_method_ptr_2816(mi,&slot);
    if(!addr||bender_fix16_visited_2816(addr))return;
    g_bender_fix16_nodes_2816++;
    char caller[224];const char*cm=NULL;bender_fix13_describe_method_2813(mi,caller,sizeof(caller),&cm);size_t span=bender_fix16_method_span_2816(addr);if(span<4)return;
    for(size_t off=0;off+4<=span&&g_bender_fix16_edges_2816<BENDER_FIX16_EDGE_CAP_2816;off+=4){uintptr_t pc=addr+off;uint32_t ins=0;memcpy(&ins,(const void*)pc,sizeof(ins));if((ins&UINT32_C(0xfc000000))!=UINT32_C(0x94000000))continue;g_bender_fix16_bl_total_2816++;int64_t imm=(int64_t)(ins&UINT32_C(0x03ffffff));if(imm&(INT64_C(1)<<25))imm-=INT64_C(1)<<26;uintptr_t target=(uintptr_t)((int64_t)pc+(imm<<2));const BenderFix16MethodMap2816*hit=bender_fix16_find_addr_2816(target);if(!hit){uintptr_t followed=bender_fix16_follow_direct_branch_2816(target);if(followed!=target)hit=bender_fix16_find_addr_2816(followed);}
        if(!hit){g_bender_fix16_bl_unmatched_2816++;continue;}char callee[224];const char*method=NULL;bender_fix13_describe_method_2813(hit->method,callee,sizeof(callee),&method);const char*image=bender_fix15_image_name_2815(hit->method);int app=bender_fix15_app_candidate_2815(image);int boot=bender_fix15_bootstrap_candidate_2815(callee,method?method:"");if(!app&&!boot)continue;g_bender_fix16_edges_2816++;if(app)g_bender_fix16_app_edges_2816++;if(boot)g_bender_fix16_bootstrap_edges_2816++;bender_fix16_recent_note_2816(depth,caller,callee,image,app,boot);
        if(g_bender_fix16_edges_2816<=120ul||boot)logmsg("[BENDER_MANAGED_DIRECT][EDGE] source=fix16 depth=%u root=%s caller=%s callee_image=%s callee=%s pc=%p target=%p app=%d bootstrap=%d observed_execution=0 code_patch=0 forced_completion=0\n",depth,root_key?root_key:"?",caller,image,callee,(void*)pc,(void*)target,app,boot);
        if(depth<BENDER_FIX16_MAX_DEPTH_2816&&app)bender_fix16_scan_method_2816(hit->method,depth+1,root_key);
    }
}
static void bender_fix16_root_note_2816(const void*mi,const char*image,const char*key,const char*method,unsigned frame){
    if(!mi||!image||!key||!method||frame>240u||!bender_fix15_app_candidate_2815(image))return;
    if(!bender_fix15_lifecycle_method_2815(method)&&strcmp(method,"Update"))return;
    for(unsigned i=0;i<g_bender_fix16_root_count_2816;i++){if(g_bender_fix16_roots_2816[i].method==mi)return;}
    if(g_bender_fix16_root_count_2816>=BENDER_FIX16_ROOT_CAP_2816)return;
    BenderFix16Root2816*r=&g_bender_fix16_roots_2816[g_bender_fix16_root_count_2816++];
    r->method=mi;r->frame=frame;bender_strlcpy(r->image,image,sizeof(r->image));bender_strlcpy(r->key,key,sizeof(r->key));logmsg("[BENDER_MANAGED_DIRECT][ROOT] source=fix16 n=%u frame=%u image=%s method=%s policy=startup-lifecycle-or-update observe_only=1\n",g_bender_fix16_root_count_2816,frame,image,key);
}
static void bender_fix16_emit_recent_2816(unsigned frame){
    char out[9200];size_t used=0;out[0]=0;unsigned long seq=g_bender_fix16_recent_seq_2816,count=seq<BENDER_FIX16_RECENT_2816?seq:BENDER_FIX16_RECENT_2816;for(unsigned long j=0;j<count;j++){unsigned long logical=seq-count+j;size_t slot=(size_t)(logical%BENDER_FIX16_RECENT_2816);int n=snprintf(out+used,sizeof(out)-used,"%s%lu:%s",used?" | ":"",logical+1ul,g_bender_fix16_recent_2816[slot]);if(n<0||(size_t)n>=sizeof(out)-used){used=sizeof(out)-1;out[used]=0;break;}used+=(size_t)n;}
    logmsg("[BENDER_MANAGED_DIRECT][RECENT] source=fix16-tail frame=%u map_ready=%d roots=%u nodes=%lu edges=%lu app_edges=%lu bootstrap_edges=%lu bl_total=%lu bl_unmatched=%lu history=%s code_patch=0 forced_completion=0\n",frame,g_bender_fix16_map_ready_2816,g_bender_fix16_root_count_2816,g_bender_fix16_nodes_2816,g_bender_fix16_edges_2816,g_bender_fix16_app_edges_2816,g_bender_fix16_bootstrap_edges_2816,g_bender_fix16_bl_total_2816,g_bender_fix16_bl_unmatched_2816,out[0]?out:"(none)");
}
static void bender_fix16_direct_tick_2816(unsigned frame){
    if(!g_bender_fix16_analysis_done_2816&&frame>=120u&&g_bender_fix16_root_count_2816){
        if(bender_fix16_build_map_2816(frame)){memset(g_bender_fix16_visited_2816,0,sizeof(g_bender_fix16_visited_2816));g_bender_fix16_visited_count_2816=0;for(unsigned i=0;i<g_bender_fix16_root_count_2816;i++){BenderFix16Root2816*r=&g_bender_fix16_roots_2816[i];logmsg("[BENDER_MANAGED_DIRECT][ANALYZE_ROOT] source=fix16 n=%u frame_seen=%u image=%s method=%s max_depth=%u scan_bytes=%u read_only=1\n",i+1,r->frame,r->image,r->key,BENDER_FIX16_MAX_DEPTH_2816,BENDER_FIX16_SCAN_BYTES_2816);bender_fix16_scan_method_2816(r->method,0,r->key);}
            g_bender_fix16_analysis_done_2816=1;logmsg("[BENDER_MANAGED_DIRECT][SUMMARY] source=fix16 frame=%u map_ready=1 roots=%u nodes=%lu edges=%lu app_edges=%lu bootstrap_edges=%lu bl_total=%lu bl_unmatched=%lu interpretation=static-direct-call-candidates-not-proof-of-execution code_patch=0 forced_completion=0\n",frame,g_bender_fix16_root_count_2816,g_bender_fix16_nodes_2816,g_bender_fix16_edges_2816,g_bender_fix16_app_edges_2816,g_bender_fix16_bootstrap_edges_2816,g_bender_fix16_bl_total_2816,g_bender_fix16_bl_unmatched_2816);}
    }
    if(frame>0u&&(frame%120u)==0u)bender_fix16_emit_recent_2816(frame);
}


/* BENDER FIX18: read-only AArch64 tail-branch + indirect callsite diagnostics. */
#define BENDER_FIX18_UNMATCHED_LOG_CAP_2818 96u
#define BENDER_FIX18_INDIRECT_LOG_CAP_2818 96u
#define BENDER_FIX18_TAIL_LOG_CAP_2818 96u

static int g_bender_fix18_analysis_done_2818;
static unsigned long g_bender_fix18_roots_scanned_2818;
static unsigned long g_bender_fix18_bl_2818;
static unsigned long g_bender_fix18_bl_exact_2818;
static unsigned long g_bender_fix18_bl_unmatched_2818;
static unsigned long g_bender_fix18_b_external_2818;
static unsigned long g_bender_fix18_b_internal_2818;
static unsigned long g_bender_fix18_tail_resolved_2818;
static unsigned long g_bender_fix18_tail_unmatched_2818;
static unsigned long g_bender_fix18_blr_2818;
static unsigned long g_bender_fix18_br_2818;
static unsigned long g_bender_fix18_ret_2818;
static unsigned long g_bender_fix18_unmatched_logged_2818;
static unsigned long g_bender_fix18_indirect_logged_2818;
static unsigned long g_bender_fix18_tail_logged_2818;

static uintptr_t bender_fix18_branch_imm26_target_2818(uintptr_t pc,uint32_t ins){
    int64_t imm=(int64_t)(ins&UINT32_C(0x03ffffff));
    if(imm&(INT64_C(1)<<25))imm-=INT64_C(1)<<26;
    return (uintptr_t)((int64_t)pc+(imm<<2));
}
static void bender_fix18_nearest_method_2818(uintptr_t target,const BenderFix16MethodMap2816**prev,const BenderFix16MethodMap2816**next){
    if(prev)*prev=NULL;
    if(next)*next=NULL;
    size_t lo=0,hi=g_bender_fix16_map_count_2816;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(g_bender_fix16_map_2816[mid].addr<target)lo=mid+1;else hi=mid;}
    if(next&&lo<g_bender_fix16_map_count_2816)*next=&g_bender_fix16_map_2816[lo];
    if(prev&&lo>0)*prev=&g_bender_fix16_map_2816[lo-1];
}
static void bender_fix18_method_label_2818(const BenderFix16MethodMap2816*hit,char*out,size_t cap){
    if(!out||!cap)return;
    out[0]=0;
    if(!hit){bender_strlcpy(out,"(none)",cap);return;}
    const char*mn=NULL;bender_fix13_describe_method_2813(hit->method,out,cap,&mn);
}
static void bender_fix18_log_unmatched_bl_2818(const char*root,const char*caller,uintptr_t pc,uintptr_t target){
    if(g_bender_fix18_unmatched_logged_2818>=BENDER_FIX18_UNMATCHED_LOG_CAP_2818)return;
    g_bender_fix18_unmatched_logged_2818++;
    uintptr_t followed=bender_fix16_follow_direct_branch_2816(target);
    Module*tm=bender_module_for_runtime_addr(target);Module*fm=bender_module_for_runtime_addr(followed);
    const BenderFix16MethodMap2816 *prev=NULL,*next=NULL;bender_fix18_nearest_method_2818(followed,&prev,&next);
    char prev_name[224],next_name[224];bender_fix18_method_label_2818(prev,prev_name,sizeof(prev_name));bender_fix18_method_label_2818(next,next_name,sizeof(next_name));
    uintptr_t prev_delta=(prev&&followed>=prev->addr)?followed-prev->addr:0;uintptr_t next_delta=(next&&next->addr>=followed)?next->addr-followed:0;
    logmsg("[BENDER_MANAGED_CALLSITE][UNMATCHED_BL] source=fix18 root=%s caller=%s pc=%p target=%p target_module=%s target_offset=0x%" PRIxPTR " followed=%p followed_module=%s followed_offset=0x%" PRIxPTR " nearest_prev=%s prev_delta=0x%" PRIxPTR " nearest_next=%s next_delta=0x%" PRIxPTR " observed_execution=0 code_patch=0 forced_completion=0\n",
        root?root:"?",caller?caller:"?",(void*)pc,(void*)target,tm?tm->name:"host-or-unknown",tm?target-tm->bias:0,(void*)followed,fm?fm->name:"host-or-unknown",fm?followed-fm->bias:0,prev_name,prev_delta,next_name,next_delta);
}
static void bender_fix18_log_indirect_2818(const char*kind,const char*root,const char*caller,uintptr_t addr,size_t off,uint32_t ins,unsigned reg){
    if(g_bender_fix18_indirect_logged_2818>=BENDER_FIX18_INDIRECT_LOG_CAP_2818)return;
    g_bender_fix18_indirect_logged_2818++;uint32_t p1=0,p2=0,p3=0,p4=0;
    if(off>=4)memcpy(&p1,(const void*)(addr+off-4),4);
    if(off>=8)memcpy(&p2,(const void*)(addr+off-8),4);
    if(off>=12)memcpy(&p3,(const void*)(addr+off-12),4);
    if(off>=16)memcpy(&p4,(const void*)(addr+off-16),4);
    logmsg("[BENDER_MANAGED_CALLSITE][INDIRECT] source=fix18 kind=%s root=%s caller=%s pc=%p reg=x%u ins=0x%08x prev1=0x%08x prev2=0x%08x prev3=0x%08x prev4=0x%08x observed_execution=0 code_patch=0 forced_completion=0\n",kind?kind:"?",root?root:"?",caller?caller:"?",(void*)(addr+off),reg,ins,p1,p2,p3,p4);
}
static void bender_fix18_log_tail_2818(const char*root,const char*caller,uintptr_t pc,uintptr_t target,const BenderFix16MethodMap2816*hit,unsigned depth){
    if(g_bender_fix18_tail_logged_2818>=BENDER_FIX18_TAIL_LOG_CAP_2818)return;
    g_bender_fix18_tail_logged_2818++;Module*m=bender_module_for_runtime_addr(target);char callee[224];const char*mn=NULL;
    if(hit)bender_fix13_describe_method_2813(hit->method,callee,sizeof(callee),&mn);else bender_strlcpy(callee,"(unmapped)",sizeof(callee));
    const char*image=hit?bender_fix15_image_name_2815(hit->method):"(none)";int app=hit?bender_fix15_app_candidate_2815(image):0;int boot=hit?bender_fix15_bootstrap_candidate_2815(callee,mn?mn:""):0;
    logmsg("[BENDER_MANAGED_CALLSITE][TAIL_EDGE] source=fix18 depth=%u root=%s caller=%s pc=%p target=%p target_module=%s target_offset=0x%" PRIxPTR " callee_image=%s callee=%s resolved=%d app=%d bootstrap=%d observed_execution=0 code_patch=0 forced_completion=0\n",depth,root?root:"?",caller?caller:"?",(void*)pc,(void*)target,m?m->name:"host-or-unknown",m?target-m->bias:0,image,callee,hit?1:0,app,boot);
}
static void bender_fix18_scan_root_2818(const BenderFix16Root2816*r,unsigned index){
    if(!r||!r->method)return;
    int slot=-1;
    uintptr_t addr=bender_fix16_method_ptr_2816(r->method,&slot);
    size_t span=bender_fix16_method_span_2816(addr);
    if(!addr||span<4)return;
    unsigned long bl=0,bl_exact=0,bl_unmatched=0,bext=0,bint=0,tail_res=0,tail_un=0,blr=0,br=0,ret=0;char caller[224];const char*cm=NULL;bender_fix13_describe_method_2813(r->method,caller,sizeof(caller),&cm);
    for(size_t off=0;off+4<=span;off+=4){uintptr_t pc=addr+off;uint32_t ins=0;memcpy(&ins,(const void*)pc,4);
        if((ins&UINT32_C(0xfc000000))==UINT32_C(0x94000000)){bl++;uintptr_t target=bender_fix18_branch_imm26_target_2818(pc,ins);const BenderFix16MethodMap2816*hit=bender_fix16_find_addr_2816(target);if(!hit){uintptr_t f=bender_fix16_follow_direct_branch_2816(target);if(f!=target)hit=bender_fix16_find_addr_2816(f);}
            if(hit)bl_exact++;else{bl_unmatched++;bender_fix18_log_unmatched_bl_2818(r->key,caller,pc,target);}continue;}
        if((ins&UINT32_C(0xfc000000))==UINT32_C(0x14000000)){uintptr_t target=bender_fix18_branch_imm26_target_2818(pc,ins);if(target>=addr&&target<addr+span){bint++;continue;}bext++;const BenderFix16MethodMap2816*hit=bender_fix16_find_addr_2816(target);if(!hit){uintptr_t f=bender_fix16_follow_direct_branch_2816(target);if(f!=target)hit=bender_fix16_find_addr_2816(f);}
            if(hit)tail_res++;else tail_un++;bender_fix18_log_tail_2818(r->key,caller,pc,target,hit,0);continue;}
        if((ins&UINT32_C(0xfffffc1f))==UINT32_C(0xd63f0000)){blr++;bender_fix18_log_indirect_2818("BLR",r->key,caller,addr,off,ins,(ins>>5)&31u);continue;}
        if((ins&UINT32_C(0xfffffc1f))==UINT32_C(0xd61f0000)){br++;bender_fix18_log_indirect_2818("BR",r->key,caller,addr,off,ins,(ins>>5)&31u);continue;}
        if((ins&UINT32_C(0xfffffc1f))==UINT32_C(0xd65f0000)){ret++;continue;}
    }
    g_bender_fix18_roots_scanned_2818++;g_bender_fix18_bl_2818+=bl;g_bender_fix18_bl_exact_2818+=bl_exact;g_bender_fix18_bl_unmatched_2818+=bl_unmatched;g_bender_fix18_b_external_2818+=bext;g_bender_fix18_b_internal_2818+=bint;g_bender_fix18_tail_resolved_2818+=tail_res;g_bender_fix18_tail_unmatched_2818+=tail_un;g_bender_fix18_blr_2818+=blr;g_bender_fix18_br_2818+=br;g_bender_fix18_ret_2818+=ret;
    logmsg("[BENDER_MANAGED_CALLSITE][ROOT] source=fix18 n=%u frame_seen=%u image=%s method=%s addr=%p method_slot=%d span=%zu bl=%lu bl_resolved=%lu bl_unmatched=%lu b_external=%lu b_internal=%lu tail_resolved=%lu tail_unmatched=%lu blr=%lu br=%lu ret=%lu read_only=1 code_patch=0 forced_completion=0\n",index+1u,r->frame,r->image,r->key,(void*)addr,slot,span,bl,bl_exact,bl_unmatched,bext,bint,tail_res,tail_un,blr,br,ret);
}
static void bender_fix18_callsite_tick_2818(unsigned frame){
    if(g_bender_fix18_analysis_done_2818||frame<120u||!g_bender_fix16_map_ready_2816||!g_bender_fix16_analysis_done_2816)return;
    for(unsigned i=0;i<g_bender_fix16_root_count_2816;i++)bender_fix18_scan_root_2818(&g_bender_fix16_roots_2816[i],i);
    g_bender_fix18_analysis_done_2818=1;
    logmsg("[BENDER_MANAGED_CALLSITE][SUMMARY] source=fix18 frame=%u roots=%lu bl=%lu bl_resolved=%lu bl_unmatched=%lu b_external=%lu b_internal=%lu tail_resolved=%lu tail_unmatched=%lu blr=%lu br=%lu ret=%lu unmatched_logged=%lu indirect_logged=%lu tail_logged=%lu interpretation=static-callsite-structure-not-proof-of-execution read_only=1 code_patch=0 forced_completion=0\n",frame,g_bender_fix18_roots_scanned_2818,g_bender_fix18_bl_2818,g_bender_fix18_bl_exact_2818,g_bender_fix18_bl_unmatched_2818,g_bender_fix18_b_external_2818,g_bender_fix18_b_internal_2818,g_bender_fix18_tail_resolved_2818,g_bender_fix18_tail_unmatched_2818,g_bender_fix18_blr_2818,g_bender_fix18_br_2818,g_bender_fix18_ret_2818,g_bender_fix18_unmatched_logged_2818,g_bender_fix18_indirect_logged_2818,g_bender_fix18_tail_logged_2818);
}


/* BENDER FIX19: core application root-priority + per-root unmatched/tail-chain diagnostics. */
#define BENDER_FIX19_UNMATCHED_PER_ROOT_2819 16u
#define BENDER_FIX19_EDGE_PER_ROOT_2819 24u
#define BENDER_FIX19_INDIRECT_PER_ROOT_2819 12u
#define BENDER_FIX19_TAIL_PER_ROOT_2819 12u
#define BENDER_FIX19_TAIL_DEPTH_2819 3u
#define BENDER_FIX19_VISITED_2819 24u
#define BENDER_FIX19_FOCUS_TARGETS_2819 8u

typedef struct BenderFix19Budget2819{
    unsigned unmatched_logged,edge_logged,indirect_logged,tail_logged;
    uintptr_t visited[BENDER_FIX19_VISITED_2819];unsigned visited_count;
    uintptr_t focus_unmatched[BENDER_FIX19_FOCUS_TARGETS_2819];unsigned focus_unmatched_count;
}BenderFix19Budget2819;

static int g_bender_fix19_analysis_done_2819;
static unsigned long g_bender_fix19_core_roots_2819;
static unsigned long g_bender_fix19_methods_2819;
static unsigned long g_bender_fix19_unmatched_logged_2819;
static unsigned long g_bender_fix19_tail_followed_2819;

static int bender_fix19_core_app_image_2819(const char*image){
    if(!image)return 0;
    return !strcmp(image,"Assembly-CSharp.dll")||!strcmp(image,"Assembly-CSharp-firstpass.dll");
}
static int bender_fix19_focus_start_2819(const char*key){
    if(!key)return 0;
    size_t n=strlen(key);const char*s="::Start";size_t m=strlen(s);
    return n>=m&&!strcmp(key+n-m,s);
}
static int bender_fix19_seen_2819(BenderFix19Budget2819*b,uintptr_t addr){
    if(!b||!addr)return 1;
    for(unsigned i=0;i<b->visited_count;i++)if(b->visited[i]==addr)return 1;
    if(b->visited_count<BENDER_FIX19_VISITED_2819)b->visited[b->visited_count++]=addr;
    return 0;
}
static void bender_fix19_context_2819(uintptr_t addr,size_t off,uint32_t*out1,uint32_t*out2,uint32_t*out3,uint32_t*out4){
    if(out1)*out1=0;
    if(out2)*out2=0;
    if(out3)*out3=0;
    if(out4)*out4=0;
    if(out1&&off>=4)memcpy(out1,(const void*)(addr+off-4),4);
    if(out2&&off>=8)memcpy(out2,(const void*)(addr+off-8),4);
    if(out3&&off>=12)memcpy(out3,(const void*)(addr+off-12),4);
    if(out4&&off>=16)memcpy(out4,(const void*)(addr+off-16),4);
}
static void bender_fix19_log_unmatched_2819(BenderFix19Budget2819*b,const char*root,const char*caller,uintptr_t addr,size_t off,uint32_t ins,uintptr_t target,unsigned depth,int focus){
    if(!b||b->unmatched_logged>=BENDER_FIX19_UNMATCHED_PER_ROOT_2819)return;
    b->unmatched_logged++;g_bender_fix19_unmatched_logged_2819++;
    if(focus&&depth==0&&b->focus_unmatched_count<BENDER_FIX19_FOCUS_TARGETS_2819)b->focus_unmatched[b->focus_unmatched_count++]=target;
    uintptr_t followed=bender_fix16_follow_direct_branch_2816(target);
    Module*tm=bender_module_for_runtime_addr(target);Module*fm=bender_module_for_runtime_addr(followed);
    const BenderFix16MethodMap2816 *prev=NULL,*next=NULL;
    bender_fix18_nearest_method_2818(followed,&prev,&next);
    char prev_name[224],next_name[224];
    bender_fix18_method_label_2818(prev,prev_name,sizeof(prev_name));
    bender_fix18_method_label_2818(next,next_name,sizeof(next_name));
    uintptr_t prev_delta=(prev&&followed>=prev->addr)?followed-prev->addr:0;
    uintptr_t next_delta=(next&&next->addr>=followed)?next->addr-followed:0;
    uint32_t p1=0,p2=0,p3=0,p4=0;
    bender_fix19_context_2819(addr,off,&p1,&p2,&p3,&p4);
    logmsg("[BENDER_MANAGED_CALLSITE19][UNMATCHED_BL] source=fix19 depth=%u root=%s caller=%s pc=%p ins=0x%08x target=%p target_module=%s target_offset=0x%" PRIxPTR " followed=%p followed_module=%s followed_offset=0x%" PRIxPTR " nearest_prev=%s prev_delta=0x%" PRIxPTR " nearest_next=%s next_delta=0x%" PRIxPTR " prev1=0x%08x prev2=0x%08x prev3=0x%08x prev4=0x%08x per_root_budget=%u observed_execution=0 read_only=1 code_patch=0 forced_completion=0\n",
        depth,root?root:"?",caller?caller:"?",(void*)(addr+off),ins,(void*)target,tm?tm->name:"host-or-unknown",tm?target-tm->bias:0,(void*)followed,fm?fm->name:"host-or-unknown",fm?followed-fm->bias:0,prev_name,prev_delta,next_name,next_delta,p1,p2,p3,p4,BENDER_FIX19_UNMATCHED_PER_ROOT_2819);
}
static void bender_fix19_log_indirect_2819(BenderFix19Budget2819*b,const char*kind,const char*root,const char*caller,uintptr_t addr,size_t off,uint32_t ins,unsigned depth){
    if(!b||b->indirect_logged>=BENDER_FIX19_INDIRECT_PER_ROOT_2819)return;
    b->indirect_logged++;
    uint32_t p1=0,p2=0,p3=0,p4=0;
    bender_fix19_context_2819(addr,off,&p1,&p2,&p3,&p4);
    logmsg("[BENDER_MANAGED_CALLSITE19][INDIRECT] source=fix19 depth=%u kind=%s root=%s caller=%s pc=%p reg=x%u ins=0x%08x prev1=0x%08x prev2=0x%08x prev3=0x%08x prev4=0x%08x per_root_budget=%u observed_execution=0 read_only=1 code_patch=0 forced_completion=0\n",
        depth,kind?kind:"?",root?root:"?",caller?caller:"?",(void*)(addr+off),(ins>>5)&31u,ins,p1,p2,p3,p4,BENDER_FIX19_INDIRECT_PER_ROOT_2819);
}
static void bender_fix19_focus_targets_2819(const BenderFix19Budget2819*b,char*out,size_t cap){
    if(!out||!cap)return;
    out[0]=0;
    if(!b||!b->focus_unmatched_count){bender_strlcpy(out,"(none)",cap);return;}
    size_t used=0;
    for(unsigned i=0;i<b->focus_unmatched_count;i++){
        int n=snprintf(out+used,cap-used,"%s0x%" PRIxPTR,i?",":"",b->focus_unmatched[i]);
        if(n<0||(size_t)n>=cap-used){out[cap-1]=0;break;}
        used+=(size_t)n;
    }
}
static void bender_fix19_scan_method_2819(const void*mi,const char*root,const char*root_image,unsigned depth,BenderFix19Budget2819*b);
static void bender_fix19_tail_2819(BenderFix19Budget2819*b,const char*root,const char*caller,uintptr_t pc,uintptr_t target,const BenderFix16MethodMap2816*hit,unsigned depth){
    if(!b||b->tail_logged>=BENDER_FIX19_TAIL_PER_ROOT_2819)return;
    b->tail_logged++;
    Module*m=bender_module_for_runtime_addr(target);char callee[224];const char*mn=NULL;
    if(hit)bender_fix13_describe_method_2813(hit->method,callee,sizeof(callee),&mn);
    else bender_strlcpy(callee,"(unmapped)",sizeof(callee));
    const char*image=hit?bender_fix15_image_name_2815(hit->method):"(none)";
    int app=hit?bender_fix15_app_candidate_2815(image):0;
    int core=hit?bender_fix19_core_app_image_2819(image):0;
    logmsg("[BENDER_MANAGED_CALLSITE19][TAIL_CHAIN] source=fix19 depth=%u root=%s caller=%s pc=%p target=%p target_module=%s target_offset=0x%" PRIxPTR " callee_image=%s callee=%s resolved=%d app=%d core_app=%d follow=%d observed_execution=0 read_only=1 code_patch=0 forced_completion=0\n",
        depth,root?root:"?",caller?caller:"?",(void*)pc,(void*)target,m?m->name:"host-or-unknown",m?target-m->bias:0,image,callee,hit?1:0,app,core,(hit&&app&&depth<BENDER_FIX19_TAIL_DEPTH_2819)?1:0);
    if(hit&&app&&depth<BENDER_FIX19_TAIL_DEPTH_2819){
        g_bender_fix19_tail_followed_2819++;
        bender_fix19_scan_method_2819(hit->method,root,image,depth+1,b);
    }
}
static void bender_fix19_scan_method_2819(const void*mi,const char*root,const char*root_image,unsigned depth,BenderFix19Budget2819*b){
    if(!mi||!b||depth>BENDER_FIX19_TAIL_DEPTH_2819)return;
    int slot=-1;
    uintptr_t addr=bender_fix16_method_ptr_2816(mi,&slot);
    if(!addr||bender_fix19_seen_2819(b,addr))return;
    size_t span=bender_fix16_method_span_2816(addr);
    if(span<4)return;
    g_bender_fix19_methods_2819++;
    char caller[224];const char*cm=NULL;
    bender_fix13_describe_method_2813(mi,caller,sizeof(caller),&cm);
    unsigned long bl=0,blr=0,br=0,ret=0,bext=0,bint=0,resolved=0,unmatched=0,tail_res=0,tail_un=0;
    int focus=(depth==0&&bender_fix19_focus_start_2819(root));
    for(size_t off=0;off+4<=span;off+=4){
        uintptr_t pc=addr+off;uint32_t ins=0;memcpy(&ins,(const void*)pc,4);
        if((ins&UINT32_C(0xfc000000))==UINT32_C(0x94000000)){
            bl++;
            uintptr_t target=bender_fix18_branch_imm26_target_2818(pc,ins);
            const BenderFix16MethodMap2816*hit=bender_fix16_find_addr_2816(target);
            if(!hit){uintptr_t f=bender_fix16_follow_direct_branch_2816(target);if(f!=target)hit=bender_fix16_find_addr_2816(f);}
            if(hit){
                resolved++;
                if(b->edge_logged<BENDER_FIX19_EDGE_PER_ROOT_2819){
                    char callee[224];const char*mn=NULL;
                    bender_fix13_describe_method_2813(hit->method,callee,sizeof(callee),&mn);
                    const char*image=bender_fix15_image_name_2815(hit->method);
                    if(bender_fix15_app_candidate_2815(image)){
                        b->edge_logged++;
                        logmsg("[BENDER_MANAGED_CALLSITE19][EDGE] source=fix19 depth=%u root=%s caller=%s callee_image=%s callee=%s pc=%p target=%p core_app=%d observed_execution=0 read_only=1 code_patch=0 forced_completion=0\n",
                            depth,root?root:"?",caller,image,callee,(void*)pc,(void*)target,bender_fix19_core_app_image_2819(image));
                    }
                }
            }else{
                unmatched++;
                bender_fix19_log_unmatched_2819(b,root,caller,addr,off,ins,target,depth,focus);
            }
            continue;
        }
        if((ins&UINT32_C(0xfc000000))==UINT32_C(0x14000000)){
            uintptr_t target=bender_fix18_branch_imm26_target_2818(pc,ins);
            if(target>=addr&&target<addr+span){bint++;continue;}
            bext++;
            const BenderFix16MethodMap2816*hit=bender_fix16_find_addr_2816(target);
            if(!hit){uintptr_t f=bender_fix16_follow_direct_branch_2816(target);if(f!=target)hit=bender_fix16_find_addr_2816(f);}
            if(hit)tail_res++;else tail_un++;
            bender_fix19_tail_2819(b,root,caller,pc,target,hit,depth);
            continue;
        }
        if((ins&UINT32_C(0xfffffc1f))==UINT32_C(0xd63f0000)){blr++;bender_fix19_log_indirect_2819(b,"BLR",root,caller,addr,off,ins,depth);continue;}
        if((ins&UINT32_C(0xfffffc1f))==UINT32_C(0xd61f0000)){br++;bender_fix19_log_indirect_2819(b,"BR",root,caller,addr,off,ins,depth);continue;}
        if((ins&UINT32_C(0xfffffc1f))==UINT32_C(0xd65f0000)){ret++;continue;}
    }
    logmsg("[BENDER_MANAGED_CALLSITE19][METHOD] source=fix19 depth=%u root=%s root_image=%s caller=%s addr=%p method_slot=%d span=%zu bl=%lu bl_resolved=%lu bl_unmatched=%lu b_external=%lu b_internal=%lu tail_resolved=%lu tail_unmatched=%lu blr=%lu br=%lu ret=%lu observed_execution=0 read_only=1 code_patch=0 forced_completion=0\n",
        depth,root?root:"?",root_image?root_image:"?",caller,(void*)addr,slot,span,bl,resolved,unmatched,bext,bint,tail_res,tail_un,blr,br,ret);
    if(focus){
        char targets[320];
        bender_fix19_focus_targets_2819(b,targets,sizeof(targets));
        logmsg("[BENDER_MANAGED_CALLSITE19][FOCUS] source=fix19 role=lifecycle-start root=%s image=%s addr=%p span=%zu bl=%lu bl_resolved=%lu bl_unmatched=%lu b_external=%lu tail_resolved=%lu tail_unmatched=%lu blr=%lu br=%lu ret=%lu focus_unmatched_logged=%u unmatched_targets=%s interpretation=root-callsite-structure-not-proof-of-execution observed_execution=0 read_only=1 code_patch=0 forced_completion=0\n",
            root?root:"?",root_image?root_image:"?",(void*)addr,span,bl,resolved,unmatched,bext,tail_res,tail_un,blr,br,ret,b->focus_unmatched_count,targets);
    }
}
static void bender_fix19_tick_2819(unsigned frame){
    if(g_bender_fix19_analysis_done_2819||frame<120u||!g_bender_fix18_analysis_done_2818||!g_bender_fix16_map_ready_2816)return;
    unsigned skipped_noncore=0;
    for(unsigned i=0;i<g_bender_fix16_root_count_2816;i++){
        BenderFix16Root2816*r=&g_bender_fix16_roots_2816[i];
        if(!bender_fix19_core_app_image_2819(r->image)){skipped_noncore++;continue;}
        BenderFix19Budget2819 b;memset(&b,0,sizeof(b));
        g_bender_fix19_core_roots_2819++;
        bender_fix19_scan_method_2819(r->method,r->key,r->image,0,&b);
    }
    g_bender_fix19_analysis_done_2819=1;
    logmsg("[BENDER_MANAGED_CALLSITE19][SUMMARY] source=fix19 frame=%u core_roots=%lu skipped_noncore_roots=%u methods=%lu unmatched_logged=%lu tail_followed=%lu unmatched_budget_per_root=%u edge_budget_per_root=%u indirect_budget_per_root=%u tail_depth=%u priority=Assembly-CSharp read_only=1 code_patch=0 forced_completion=0\n",
        frame,g_bender_fix19_core_roots_2819,skipped_noncore,g_bender_fix19_methods_2819,g_bender_fix19_unmatched_logged_2819,g_bender_fix19_tail_followed_2819,BENDER_FIX19_UNMATCHED_PER_ROOT_2819,BENDER_FIX19_EDGE_PER_ROOT_2819,BENDER_FIX19_INDIRECT_PER_ROOT_2819,BENDER_FIX19_TAIL_DEPTH_2819);
}

/* BENDER FIX62 - managed lifecycle diagnostic storm guard.
 * Diagnostic-only sampling: every real il2cpp_runtime_invoke still executes.
 * Full lifecycle logging is kept for the first 64 calls in a frame; after that,
 * sparse samples and aggregate storm markers replace per-callback fflush traffic. */
#define BENDER_FIX62_LIFECYCLE_FULL_PER_FRAME_2862 64ul
#define BENDER_FIX62_LIFECYCLE_SAMPLE_PERIOD_2862 512ul
static unsigned g_bender_fix62_lifecycle_frame_2862=0xffffffffu;
static unsigned long g_bender_fix62_lifecycle_frame_calls_2862;
static unsigned long g_bender_fix62_lifecycle_frame_logged_2862;
static unsigned long g_bender_fix62_lifecycle_frame_suppressed_2862;
static unsigned long g_bender_fix62_lifecycle_total_calls_2862;
static unsigned long g_bender_fix62_lifecycle_total_logged_2862;
static unsigned long g_bender_fix62_lifecycle_total_suppressed_2862;
static int g_bender_fix62_lifecycle_verbose_2862=-1;
static char g_bender_fix62_lifecycle_last_2862[384];
static int bender_fix62_lifecycle_should_log_2862(unsigned frame,const char*image,const char*key){
    if(g_bender_fix62_lifecycle_verbose_2862<0){
        const char*e=getenv("BENDER_MANAGED_LIFECYCLE_VERBOSE");
        g_bender_fix62_lifecycle_verbose_2862=(e&&*e&&strcmp(e,"0")&&strcasecmp(e,"false")&&strcasecmp(e,"off")&&strcasecmp(e,"no"))?1:0;
    }
    if(g_bender_fix62_lifecycle_frame_2862!=frame){
        if(g_bender_fix62_lifecycle_frame_2862!=0xffffffffu&&g_bender_fix62_lifecycle_frame_suppressed_2862){
            logmsg("[BENDER_MANAGED_APP][LIFECYCLE_STORM_SUMMARY] source=fix62 frame=%u calls=%lu logged=%lu suppressed=%lu total_calls=%lu total_logged=%lu total_suppressed=%lu last=%s policy=diagnostic-sampling-only code_patch=0 forced_completion=0\n",
                g_bender_fix62_lifecycle_frame_2862,g_bender_fix62_lifecycle_frame_calls_2862,g_bender_fix62_lifecycle_frame_logged_2862,g_bender_fix62_lifecycle_frame_suppressed_2862,
                g_bender_fix62_lifecycle_total_calls_2862,g_bender_fix62_lifecycle_total_logged_2862,g_bender_fix62_lifecycle_total_suppressed_2862,
                g_bender_fix62_lifecycle_last_2862[0]?g_bender_fix62_lifecycle_last_2862:"(none)");
        }
        g_bender_fix62_lifecycle_frame_2862=frame;
        g_bender_fix62_lifecycle_frame_calls_2862=0ul;
        g_bender_fix62_lifecycle_frame_logged_2862=0ul;
        g_bender_fix62_lifecycle_frame_suppressed_2862=0ul;
    }
    unsigned long c=++g_bender_fix62_lifecycle_frame_calls_2862;
    ++g_bender_fix62_lifecycle_total_calls_2862;
    snprintf(g_bender_fix62_lifecycle_last_2862,sizeof(g_bender_fix62_lifecycle_last_2862),"%s:%s",image?image:"?",key?key:"?");
    int emit=g_bender_fix62_lifecycle_verbose_2862||c<=BENDER_FIX62_LIFECYCLE_FULL_PER_FRAME_2862||c==128ul||c==256ul||(c%BENDER_FIX62_LIFECYCLE_SAMPLE_PERIOD_2862)==0ul;
    if(emit){
        ++g_bender_fix62_lifecycle_frame_logged_2862;
        ++g_bender_fix62_lifecycle_total_logged_2862;
    }else{
        ++g_bender_fix62_lifecycle_frame_suppressed_2862;
        ++g_bender_fix62_lifecycle_total_suppressed_2862;
    }
    if(!g_bender_fix62_lifecycle_verbose_2862&&(c==256ul||c==1024ul||c==4096ul||(c>=8192ul&&(c%8192ul)==0ul))){
        logmsg("[BENDER_MANAGED_APP][LIFECYCLE_STORM] source=fix62 frame=%u calls=%lu logged=%lu suppressed=%lu total_calls=%lu total_suppressed=%lu last=%s policy=diagnostic-sampling-only code_patch=0 forced_completion=0\n",
            frame,c,g_bender_fix62_lifecycle_frame_logged_2862,g_bender_fix62_lifecycle_frame_suppressed_2862,g_bender_fix62_lifecycle_total_calls_2862,g_bender_fix62_lifecycle_total_suppressed_2862,
            g_bender_fix62_lifecycle_last_2862[0]?g_bender_fix62_lifecycle_last_2862:"(none)");
    }
    return emit;
}


/* BENDER FIX81: Dead Trigger 2 neutral color-correction fallback.
 * The DT2 gameplay/hideout transition enables MFColorCorrectionEffect and the
 * post-process becomes the final render path. On R36S this pass can yield a
 * near-black framebuffer even though the scene/render loop remains alive.
 * For DT2 only, invoke UnityEngine.Graphics.Blit(source,destination) with the
 * exact OnRenderImage arguments. If method resolution or the neutral blit
 * fails, fall back to the game's original OnRenderImage implementation.
 */
typedef void *(*BenderFix81DomainGet)(void);
typedef const void **(*BenderFix81DomainGetAssemblies)(const void*,size_t*);
typedef void *(*BenderFix81AssemblyGetImage)(const void*);
typedef const char *(*BenderFix81ImageGetName)(const void*);
typedef void *(*BenderFix81ClassFromName)(const void*,const char*,const char*);
typedef const void *(*BenderFix81ClassGetMethodFromName)(void*,const char*,int);
static const void *g_bender_fix81_graphics_blit;
static int g_bender_fix81_graphics_resolve_attempted;
static unsigned long g_bender_fix81_postfx_calls;
static unsigned long g_bender_fix81_postfx_bypassed;
static unsigned long g_bender_fix81_postfx_fallback;

static int bender_fix81_is_dt2(void){return strstr(BENDER_APP_PACKAGE,"deadtrigger2")!=NULL;}
static const void*bender_fix81_resolve_graphics_blit(void){
    if(g_bender_fix81_graphics_blit||g_bender_fix81_graphics_resolve_attempted)return g_bender_fix81_graphics_blit;
    g_bender_fix81_graphics_resolve_attempted=1;
    Module*im=g_bender_il2cpp_module_2813;
    if(!im){logmsg("[BENDER_DT2_POSTFX_BYPASS] source=fix81 action=resolve-fail reason=libil2cpp-module-null\n");return NULL;}
    BenderFix81DomainGet domain_get=(BenderFix81DomainGet)module_symbol(im,"il2cpp_domain_get");
    BenderFix81DomainGetAssemblies domain_get_assemblies=(BenderFix81DomainGetAssemblies)module_symbol(im,"il2cpp_domain_get_assemblies");
    BenderFix81AssemblyGetImage assembly_get_image=(BenderFix81AssemblyGetImage)module_symbol(im,"il2cpp_assembly_get_image");
    BenderFix81ImageGetName image_get_name=(BenderFix81ImageGetName)module_symbol(im,"il2cpp_image_get_name");
    BenderFix81ClassFromName class_from_name=(BenderFix81ClassFromName)module_symbol(im,"il2cpp_class_from_name");
    BenderFix81ClassGetMethodFromName class_get_method=(BenderFix81ClassGetMethodFromName)module_symbol(im,"il2cpp_class_get_method_from_name");
    if(!domain_get||!domain_get_assemblies||!assembly_get_image||!image_get_name||!class_from_name||!class_get_method){
        logmsg("[BENDER_DT2_POSTFX_BYPASS] source=fix81 action=resolve-fail reason=il2cpp-api-missing domain=%p assemblies=%p image=%p image_name=%p class=%p method=%p\n",(void*)domain_get,(void*)domain_get_assemblies,(void*)assembly_get_image,(void*)image_get_name,(void*)class_from_name,(void*)class_get_method);return NULL;
    }
    void*domain=domain_get();size_t count=0;const void**assemblies=domain?domain_get_assemblies(domain,&count):NULL;
    for(size_t i=0;assemblies&&i<count;i++){
        void*image=assembly_get_image(assemblies[i]);const char*iname=image?image_get_name(image):NULL;
        if(!image||!iname)continue;
        if(strcasecmp(iname,"UnityEngine.CoreModule.dll")&&strcasecmp(iname,"UnityEngine.dll"))continue;
        void*klass=class_from_name(image,"UnityEngine","Graphics");if(!klass)continue;
        const void*method=class_get_method(klass,"Blit",2);
        if(method){g_bender_fix81_graphics_blit=method;logmsg("[BENDER_DT2_POSTFX_BYPASS] source=fix81 action=resolved image=%s method=UnityEngine.Graphics::Blit argc=2 method_info=%p policy=neutral-source-to-destination\n",iname,method);break;}
    }
    if(!g_bender_fix81_graphics_blit)logmsg("[BENDER_DT2_POSTFX_BYPASS] source=fix81 action=resolve-fail reason=Graphics.Blit-2-not-found assemblies=%zu\n",count);
    return g_bender_fix81_graphics_blit;
}
static int bender_fix81_try_postfx_bypass(const char*key,const char*method,void**params,void**ret_out){
    if(!bender_fix81_is_dt2()||!key||!method||strcmp(method,"OnRenderImage")||!strstr(key,"MFColorCorrectionEffect::OnRenderImage"))return 0;
    unsigned long n=__atomic_add_fetch(&g_bender_fix81_postfx_calls,1ul,__ATOMIC_RELAXED);
    const void*blit=bender_fix81_resolve_graphics_blit();
    if(!blit||!params||!params[0]||!params[1]){
        unsigned long f=__atomic_add_fetch(&g_bender_fix81_postfx_fallback,1ul,__ATOMIC_RELAXED);
        if(f<=3ul)logmsg("[BENDER_DT2_POSTFX_BYPASS] source=fix81 action=fallback-original call=%lu reason=%s src=%p dst=%p\n",n,!blit?"blit-unresolved":"missing-rendertexture-args",params?params[0]:NULL,params?params[1]:NULL);
        return 0;
    }
    void*local_exc=NULL;void*ret=g_bender_runtime_invoke_real_2813(blit,NULL,params,&local_exc);
    if(local_exc){
        unsigned long f=__atomic_add_fetch(&g_bender_fix81_postfx_fallback,1ul,__ATOMIC_RELAXED);
        if(f<=3ul)logmsg("[BENDER_DT2_POSTFX_BYPASS] source=fix81 action=fallback-original call=%lu reason=neutral-blit-exception exception=%p\n",n,local_exc);
        return 0;
    }
    unsigned long ok=__atomic_add_fetch(&g_bender_fix81_postfx_bypassed,1ul,__ATOMIC_RELAXED);
    if(ok<=3ul||(ok%300ul)==0ul)logmsg("[BENDER_DT2_POSTFX_BYPASS] source=fix81 action=neutral-blit call=%lu bypassed=%lu src=%p dst=%p result=%p policy=dt2-only-no-custom-color-correction\n",n,ok,params[0],params[1],ret);
    if(ret_out){*ret_out=ret;}
    return 1;
}


/* BENDER V2.7.155 — FIX106 read-only application Start coroutine frontier. */
typedef void *(*BenderFix106ObjectGetClass)(void*);
typedef void *(*BenderFix106ClassGetFields)(void*,void**);
typedef const char *(*BenderFix106FieldGetName)(void*);
typedef void (*BenderFix106FieldGetValue)(void*,void*,void*);
typedef const void *(*BenderFix106ClassGetMethods)(void*,void**);

static BenderFix106ObjectGetClass g_bender_fix106_object_get_class;
static BenderFix106ClassGetFields g_bender_fix106_class_get_fields;
static BenderFix106FieldGetName g_bender_fix106_field_get_name;
static BenderFix106FieldGetValue g_bender_fix106_field_get_value;
static BenderFix106ClassGetMethods g_bender_fix106_class_get_methods;
static uintptr_t g_bender_fix106_obj;
static const void *g_bender_fix106_start_mi;
static void *g_bender_fix106_klass;
static const void *g_bender_fix106_movenext_mi;
static uintptr_t g_bender_fix106_movenext_code;
static void *g_bender_fix106_state_field;
static void *g_bender_fix106_current_field;
static unsigned g_bender_fix106_capture_frame;
static unsigned g_bender_fix106_snapshots;
static int g_bender_fix106_api_ready;
static int g_bender_fix106_fields_done;
static int g_bender_fix106_graph_done;
static char g_bender_fix106_type[160];
static char g_bender_fix106_start_key[256];
static char g_bender_fix106_image[96];

static void bender_fix106_resolve_api(void){
    if(g_bender_fix106_api_ready||!g_bender_il2cpp_module_2813)return;
    g_bender_fix106_object_get_class=(BenderFix106ObjectGetClass)module_symbol(g_bender_il2cpp_module_2813,"il2cpp_object_get_class");
    g_bender_fix106_class_get_fields=(BenderFix106ClassGetFields)module_symbol(g_bender_il2cpp_module_2813,"il2cpp_class_get_fields");
    g_bender_fix106_field_get_name=(BenderFix106FieldGetName)module_symbol(g_bender_il2cpp_module_2813,"il2cpp_field_get_name");
    g_bender_fix106_field_get_value=(BenderFix106FieldGetValue)module_symbol(g_bender_il2cpp_module_2813,"il2cpp_field_get_value");
    g_bender_fix106_class_get_methods=(BenderFix106ClassGetMethods)module_symbol(g_bender_il2cpp_module_2813,"il2cpp_class_get_methods");
    g_bender_fix106_api_ready=g_bender_fix106_object_get_class&&g_bender_fix106_class_get_fields&&g_bender_fix106_field_get_name&&g_bender_fix106_field_get_value&&g_bender_fix106_class_get_methods;
    logmsg("[BENDER_START_COROUTINE][API] source=fix106 ready=%d object_get_class=%d class_get_fields=%d field_get_name=%d field_get_value=%d class_get_methods=%d read_only=1 code_patch=0 forced_completion=0\n",
           g_bender_fix106_api_ready,g_bender_fix106_object_get_class!=NULL,g_bender_fix106_class_get_fields!=NULL,
           g_bender_fix106_field_get_name!=NULL,g_bender_fix106_field_get_value!=NULL,g_bender_fix106_class_get_methods!=NULL);
}

static void bender_fix106_type_name(void*klass,char*out,size_t cap){
    if(!out||!cap)return;
    out[0]=0;
    if(!klass)return;
    const char*cn=g_bender_class_get_name_2812?g_bender_class_get_name_2812(klass):NULL;
    const char*ns=g_bender_class_get_namespace_2812?g_bender_class_get_namespace_2812(klass):NULL;
    if(ns&&*ns)snprintf(out,cap,"%s.%s",ns,cn?cn:"?");else snprintf(out,cap,"%s",cn?cn:"?");
}


/* BENDER V2.7.159 — FIX110 GC-safe lifetime for FIX106 diagnostics. */
typedef uintptr_t (*BenderFix110GCHandleNew)(void*,int);
typedef void *(*BenderFix110GCHandleGetTarget)(uintptr_t);
static BenderFix110GCHandleNew g_bender_fix110_gchandle_new;
static BenderFix110GCHandleGetTarget g_bender_fix110_gchandle_get_target;
static uintptr_t g_bender_fix110_start_handle;
static int g_bender_fix110_api_attempted;
static unsigned g_bender_fix110_refreshes;

static void bender_fix110_resolve_api(void){
    if(g_bender_fix110_api_attempted)return;
    g_bender_fix110_api_attempted=1;
    if(g_bender_il2cpp_module_2813){
        g_bender_fix110_gchandle_new=(BenderFix110GCHandleNew)module_symbol(g_bender_il2cpp_module_2813,"il2cpp_gchandle_new");
        g_bender_fix110_gchandle_get_target=(BenderFix110GCHandleGetTarget)module_symbol(g_bender_il2cpp_module_2813,"il2cpp_gchandle_get_target");
    }
    logmsg("[BENDER_START_COROUTINE][GC_API] source=fix110 new=%d get_target=%d policy=diagnostic-strong-nonpinned-handle gameplay_state_write=0 forced_completion=0\n",
           g_bender_fix110_gchandle_new!=NULL,g_bender_fix110_gchandle_get_target!=NULL);
}
static void __attribute__((unused)) bender_fix110_hold(void*obj){
    if(!obj||g_bender_fix110_start_handle)return;
    bender_fix110_resolve_api();
    if(!g_bender_fix110_gchandle_new||!g_bender_fix110_gchandle_get_target){
        logmsg("[BENDER_START_COROUTINE][GC_HOLD_SKIP] source=fix110 obj=%p reason=gchandle-api-unavailable raw_pointer_periodic_snapshot=disabled-by-refresh-guard gameplay_state_write=0 forced_completion=0\n",obj);
        return;
    }
    g_bender_fix110_start_handle=g_bender_fix110_gchandle_new(obj,0);
    void*target=g_bender_fix110_start_handle?g_bender_fix110_gchandle_get_target(g_bender_fix110_start_handle):NULL;
    logmsg("[BENDER_START_COROUTINE][GC_HOLD] source=fix110 obj=%p handle=0x%llx handle_bits=%u target=%p pinned=0 ownership=diagnostic-process-lifetime gameplay_state_write=0 code_patch=0 forced_completion=0\n",
           obj,(unsigned long long)g_bender_fix110_start_handle,(unsigned)(sizeof(g_bender_fix110_start_handle)*8u),target);
    if(target)g_bender_fix106_obj=(uintptr_t)target;
}
static int bender_fix110_refresh(void){
    if(!g_bender_fix110_start_handle){
        /* Never dereference an old raw iterator on late periodic snapshots if the
         * GC handle could not be established.  Early frame<=10 snapshots remain
         * unchanged and are enough to report immediate startup stalls. */
        return g_bender_fix13_frame_hint_2813<=10u;
    }
    if(!g_bender_fix110_gchandle_get_target)return 0;
    void*target=g_bender_fix110_gchandle_get_target(g_bender_fix110_start_handle);
    g_bender_fix110_refreshes++;
    if(!target){
        logmsg("[BENDER_START_COROUTINE][GC_REFRESH_SKIP] source=fix110 n=%u handle=0x%llx reason=null-target late_raw_deref=0 gameplay_state_write=0 forced_completion=0\n",
               g_bender_fix110_refreshes,(unsigned long long)g_bender_fix110_start_handle);
        return 0;
    }
    g_bender_fix106_obj=(uintptr_t)target;
    if(g_bender_fix110_refreshes<=12u||(g_bender_fix110_refreshes%120u)==0u){
        logmsg("[BENDER_START_COROUTINE][GC_REFRESH] source=fix110 n=%u handle=0x%llx target=%p raw_pointer_refreshed=1 gameplay_state_write=0 code_patch=0 forced_completion=0\n",
               g_bender_fix110_refreshes,(unsigned long long)g_bender_fix110_start_handle,target);
    }
    return 1;
}

static void bender_fix106_capture_start(const void*method_info,const char*image,const char*key,const char*method,void*ret,void**exc){
    if(g_bender_fix106_obj||!ret||(exc&&*exc)||!method||strcmp(method,"Start"))return;
    if(!image||!bender_fix15_app_candidate_2815(image))return;
    g_bender_fix106_obj=(uintptr_t)ret;g_bender_fix106_start_mi=method_info;g_bender_fix106_capture_frame=g_bender_fix13_frame_hint_2813;
    logmsg("[BENDER_START_COROUTINE][OBSERVER_POLICY] source=fix112 action=skip-strong-gchandle reason=diagnostic-must-not-change-managed-lifetime early_snapshot_max_frame=10 late_raw_deref=0 gameplay_state_write=0 code_patch=0 forced_completion=0\n");
    bender_strlcpy(g_bender_fix106_start_key,key&&*key?key:"Start",sizeof(g_bender_fix106_start_key));
    bender_strlcpy(g_bender_fix106_image,image,sizeof(g_bender_fix106_image));
    bender_fix106_resolve_api();
    if(g_bender_fix106_api_ready)g_bender_fix106_klass=g_bender_fix106_object_get_class(ret);
    bender_fix106_type_name(g_bender_fix106_klass,g_bender_fix106_type,sizeof(g_bender_fix106_type));
    logmsg("[BENDER_START_COROUTINE][CAPTURE] source=fix106 frame=%u image=%s start=%s start_method_info=%p obj=%p klass=%p type=%s exception=0 policy=first-non-null-app-Start-return read_only=1 code_patch=0 forced_completion=0\n",
           g_bender_fix106_capture_frame,g_bender_fix106_image,g_bender_fix106_start_key,method_info,ret,g_bender_fix106_klass,g_bender_fix106_type[0]?g_bender_fix106_type:"(pending)");
}

static void bender_fix106_resolve_iterator(void){
    if(!g_bender_fix106_obj)return;
    if(!bender_fix110_refresh())return;
    bender_fix106_resolve_api();
    if(!g_bender_fix106_api_ready)return;
    if(!g_bender_fix106_klass)g_bender_fix106_klass=g_bender_fix106_object_get_class((void*)g_bender_fix106_obj);
    if(!g_bender_fix106_klass)return;
    if(!g_bender_fix106_type[0])bender_fix106_type_name(g_bender_fix106_klass,g_bender_fix106_type,sizeof(g_bender_fix106_type));
    if(!g_bender_fix106_fields_done){
        void*it=NULL;void*field=NULL;unsigned n=0;
        while((field=g_bender_fix106_class_get_fields(g_bender_fix106_klass,&it))!=NULL&&n<48u){
            n++;const char*fn=g_bender_fix106_field_get_name(field);if(!fn)fn="?";
            unsigned char raw[32]={0};g_bender_fix106_field_get_value((void*)g_bender_fix106_obj,field,raw);
            uintptr_t raw0=0,raw1=0;memcpy(&raw0,raw,sizeof(raw0));memcpy(&raw1,raw+sizeof(raw0),sizeof(raw1));
            if(!g_bender_fix106_state_field&&(strstr(fn,"1__state")||!strcasecmp(fn,"state")))g_bender_fix106_state_field=field;
            if(!g_bender_fix106_current_field&&(strstr(fn,"2__current")||!strcasecmp(fn,"current")))g_bender_fix106_current_field=field;
            logmsg("[BENDER_START_COROUTINE][FIELD] source=fix106 obj=%p type=%s n=%u name=%s raw0=%p raw1=%p read_only=1 code_patch=0 forced_completion=0\n",
                   (void*)g_bender_fix106_obj,g_bender_fix106_type[0]?g_bender_fix106_type:"?",n,fn,(void*)raw0,(void*)raw1);
        }
        g_bender_fix106_fields_done=1;
    }
    if(!g_bender_fix106_movenext_mi){
        void*it=NULL;const void*mi=NULL;unsigned n=0;
        while((mi=g_bender_fix106_class_get_methods(g_bender_fix106_klass,&it))!=NULL&&n<96u){
            n++;const char*mn=g_bender_method_get_name_2812?g_bender_method_get_name_2812(mi):NULL;if(!mn||strcmp(mn,"MoveNext"))continue;
            int slot=-1;uintptr_t code=bender_fix16_method_ptr_2816(mi,&slot);
            g_bender_fix106_movenext_mi=mi;g_bender_fix106_movenext_code=code;
            logmsg("[BENDER_START_COROUTINE][MOVENEXT] source=fix106 frame=%u obj=%p type=%s method_info=%p image=%s method=MoveNext code=%p slot=%d read_only=1 code_patch=0 forced_completion=0\n",
                   g_bender_fix13_frame_hint_2813,(void*)g_bender_fix106_obj,g_bender_fix106_type[0]?g_bender_fix106_type:"?",mi,bender_fix15_image_name_2815(mi),(void*)code,slot);
            break;
        }
        if(!g_bender_fix106_movenext_mi)logmsg("[BENDER_START_COROUTINE][MOVENEXT] source=fix106 frame=%u obj=%p type=%s method_info=(nil) code=(nil) reason=not-found read_only=1 code_patch=0 forced_completion=0\n",
                                                g_bender_fix13_frame_hint_2813,(void*)g_bender_fix106_obj,g_bender_fix106_type[0]?g_bender_fix106_type:"?");
    }
}

static void bender_fix106_snapshot(unsigned frame){
    if(!bender_fix110_refresh()){logmsg("[BENDER_START_COROUTINE][SNAPSHOT_SKIP] source=fix110 frame=%u reason=no-live-gc-target late_raw_deref=0 gameplay_state_write=0 forced_completion=0\n",frame);return;}
    bender_fix106_resolve_iterator();if(!g_bender_fix106_obj||!g_bender_fix106_api_ready)return;
    int state_known=0,state=0;uintptr_t current=0;
    if(g_bender_fix106_state_field){g_bender_fix106_field_get_value((void*)g_bender_fix106_obj,g_bender_fix106_state_field,&state);state_known=1;}
    if(g_bender_fix106_current_field)g_bender_fix106_field_get_value((void*)g_bender_fix106_obj,g_bender_fix106_current_field,&current);
    char current_type[160]={0};
    if(current){
        void*ck=g_bender_fix106_object_get_class((void*)current);
        if(ck)bender_fix106_type_name(ck,current_type,sizeof(current_type));
    }
    g_bender_fix106_snapshots++;
    logmsg("[BENDER_START_COROUTINE][STATE] source=fix106 n=%u frame=%u age=%u obj=%p type=%s state_known=%d state=%d current=%p current_type=%s movenext=%p code=%p read_only=1 code_patch=0 forced_completion=0\n",
           g_bender_fix106_snapshots,frame,frame>=g_bender_fix106_capture_frame?frame-g_bender_fix106_capture_frame:0u,(void*)g_bender_fix106_obj,
           g_bender_fix106_type[0]?g_bender_fix106_type:"?",state_known,state,(void*)current,current_type[0]?current_type:(current?"(unresolved)":"(null)"),
           g_bender_fix106_movenext_mi,(void*)g_bender_fix106_movenext_code);
}

static uintptr_t bender_fix106_branch_target(uintptr_t pc,uint32_t ins){
    int64_t imm=(int64_t)(ins&UINT32_C(0x03ffffff));if(imm&(INT64_C(1)<<25))imm-=INT64_C(1)<<26;return(uintptr_t)((int64_t)pc+(imm<<2));
}

static void bender_fix106_scan_movenext(unsigned frame){
    if(g_bender_fix106_graph_done||frame<120u||!g_bender_fix16_map_ready_2816)return;
    bender_fix106_resolve_iterator();if(!g_bender_fix106_movenext_mi||!g_bender_fix106_movenext_code)return;
    g_bender_fix106_graph_done=1;
    uintptr_t addr=g_bender_fix106_movenext_code;size_t span=bender_fix16_method_span_2816(addr);if(span>8192u)span=8192u;
    logmsg("[BENDER_START_COROUTINE][CALLGRAPH_BEGIN] source=fix106 frame=%u start=%s type=%s method=MoveNext addr=%p span=%zu map_count=%zu read_only=1 code_patch=0 forced_completion=0\n",
           frame,g_bender_fix106_start_key,g_bender_fix106_type[0]?g_bender_fix106_type:"?",(void*)addr,span,g_bender_fix16_map_count_2816);
    unsigned resolved=0,unmatched=0,indirect=0;
    for(size_t off=0;off+4u<=span;off+=4u){
        uintptr_t pc=addr+off;uint32_t ins=0;memcpy(&ins,(const void*)pc,sizeof(ins));
        int is_bl=((ins&UINT32_C(0xfc000000))==UINT32_C(0x94000000));int is_b=((ins&UINT32_C(0xfc000000))==UINT32_C(0x14000000));
        if(is_bl||is_b){
            uintptr_t target=bender_fix106_branch_target(pc,ins);if(is_b&&target>=addr&&target<addr+span)continue;
            uintptr_t followed=bender_fix16_follow_direct_branch_2816(target);const BenderFix16MethodMap2816*hit=bender_fix16_find_addr_2816(target);if(!hit&&followed!=target)hit=bender_fix16_find_addr_2816(followed);
            if(hit&&hit->method){
                resolved++;if(resolved<=160u){char desc[256]={0};const char*mn=NULL;bender_fix13_describe_method_2813(hit->method,desc,sizeof(desc),&mn);
                    logmsg("[BENDER_START_COROUTINE][EDGE] source=fix106 n=%u kind=%s pc=%p target=%p followed=%p callee_image=%s callee=%s read_only=1 code_patch=0 forced_completion=0\n",
                           resolved,is_bl?"BL":"TAIL_B",(void*)pc,(void*)target,(void*)followed,bender_fix15_image_name_2815(hit->method),desc[0]?desc:(mn?mn:"?"));}
            }else{unmatched++;if(unmatched<=48u)logmsg("[BENDER_START_COROUTINE][UNMATCHED] source=fix106 n=%u kind=%s pc=%p target=%p followed=%p read_only=1 code_patch=0 forced_completion=0\n",unmatched,is_bl?"BL":"TAIL_B",(void*)pc,(void*)target,(void*)followed);}
        }else if((ins&UINT32_C(0xfffffc1f))==UINT32_C(0xd63f0000)||(ins&UINT32_C(0xfffffc1f))==UINT32_C(0xd61f0000)){
            indirect++;if(indirect<=48u){unsigned reg=(unsigned)((ins>>5)&31u);logmsg("[BENDER_START_COROUTINE][INDIRECT] source=fix106 n=%u kind=%s pc=%p reg=x%u ins=0x%08x read_only=1 code_patch=0 forced_completion=0\n",indirect,((ins&UINT32_C(0xfffffc1f))==UINT32_C(0xd63f0000))?"BLR":"BR",(void*)pc,reg,ins);}
        }
    }
    bender_fix16_scan_method_2816(g_bender_fix106_movenext_mi,0u,g_bender_fix106_start_key);
    logmsg("[BENDER_START_COROUTINE][CALLGRAPH_DONE] source=fix106 frame=%u type=%s addr=%p span=%zu resolved=%u unmatched=%u indirect=%u read_only=1 code_patch=0 forced_completion=0\n",
           frame,g_bender_fix106_type[0]?g_bender_fix106_type:"?",(void*)addr,span,resolved,unmatched,indirect);
}


/* BENDER V2.7.156 — FIX107 read-only Start coroutine state-2 predicate trace. */
typedef unsigned (*BenderFix107MethodGetParamCount)(const void*);
static BenderFix107MethodGetParamCount g_bender_fix107_method_get_param_count;
static int g_bender_fix107_done;
static int g_bender_fix107_api_done;

static void bender_fix107_resolve_api(void){
    if(g_bender_fix107_api_done)return;
    g_bender_fix107_api_done=1;
    if(g_bender_il2cpp_module_2813)
        g_bender_fix107_method_get_param_count=(BenderFix107MethodGetParamCount)module_symbol(g_bender_il2cpp_module_2813,"il2cpp_method_get_param_count");
    logmsg("[BENDER_START_PREDICATE][API] source=fix107 method_get_param_count=%d read_only=1 code_patch=0 forced_completion=0\n",
           g_bender_fix107_method_get_param_count!=NULL);
}

static int bender_fix107_tbz_w0_bit0(uintptr_t pc,uint32_t ins,uintptr_t*out){
    if((ins&UINT32_C(0x7f00001f))!=UINT32_C(0x36000000))return 0;
    if(((ins>>19)&31u)!=0u || ((ins>>31)&1u)!=0u)return 0;
    int32_t imm=(int32_t)((ins>>5)&0x3fffu);if(imm&0x2000)imm-=0x4000;
    if(out)*out=(uintptr_t)((intptr_t)pc+((intptr_t)imm<<2));
    return 1;
}
static int bender_fix107_mov_w_imm(uint32_t ins,unsigned value,unsigned*rd){
    if((ins&UINT32_C(0x7f800000))!=UINT32_C(0x52800000))return 0;
    unsigned hw=(ins>>21)&3u;if(hw!=0u)return 0;
    unsigned imm=(ins>>5)&0xffffu;if(imm!=value)return 0;
    if(rd)*rd=ins&31u;
    return 1;
}
static int bender_fix107_yield_state2(uintptr_t target,uintptr_t lo,uintptr_t hi){
    if(target<lo||target+24u>hi)return 0;
    int have_state2=0,have_current_null=0;
    for(unsigned i=0;i<6u;i++){
        uint32_t ins=0;memcpy(&ins,(const void*)(target+i*4u),4u);unsigned rd=0;
        if(bender_fix107_mov_w_imm(ins,2u,&rd))have_state2=1;
        if((ins&UINT32_C(0xffc0001f))==UINT32_C(0xf900001f))have_current_null=1;
    }
    return have_state2&&have_current_null;
}
static int bender_fix107_mov_x0_xn(uint32_t ins,unsigned*out_reg){
    if((ins&UINT32_C(0xffe0ffe0))!=UINT32_C(0xaa0003e0))return 0;
    unsigned r=(ins>>16)&31u;if(r==31u)return 0;if(out_reg)*out_reg=r;return 1;
}
static int bender_fix107_ldr_x(uint32_t ins,unsigned*rt,unsigned*rn,unsigned*off){
    if((ins&UINT32_C(0xffc00000))!=UINT32_C(0xf9400000))return 0;
    if(rt)*rt=ins&31u;
    if(rn)*rn=(ins>>5)&31u;
    if(off)*off=((ins>>10)&0xfffu)*8u;
    return 1;
}
static int bender_fix107_adrp(uintptr_t pc,uint32_t ins,unsigned*rd,uintptr_t*out_page){
    if((ins&UINT32_C(0x9f000000))!=UINT32_C(0x90000000))return 0;
    int64_t immhi=(int64_t)((ins>>5)&0x7ffffu);int64_t immlo=(int64_t)((ins>>29)&3u);
    int64_t imm=(immhi<<2)|immlo;if(imm&(INT64_C(1)<<20))imm-=INT64_C(1)<<21;
    if(rd)*rd=ins&31u;
    if(out_page)*out_page=(uintptr_t)(((intptr_t)pc&~(intptr_t)0xfff)+(imm<<12));
    return 1;
}
static int bender_fix107_module_contains(Module*m,uintptr_t p,size_t n){
    if(!m||!p)return 0;
    uintptr_t lo=(uintptr_t)m->reserve,hi=lo+m->reserve_size;
    return p>=lo&&p<=hi&&n<=hi-p;
}

static void bender_fix107_enum_receiver(void*obj,void*klass,unsigned slot_hint){
    if(!obj||!klass||!g_bender_fix106_class_get_methods)return;
    char tn[160]={0};bender_fix106_type_name(klass,tn,sizeof(tn));
    logmsg("[BENDER_START_PREDICATE][RECEIVER] source=fix107 obj=%p klass=%p type=%s interface_slot_hint=%u read_only=1 code_patch=0 forced_completion=0\n",
           obj,klass,tn[0]?tn:"?",slot_hint);
    bender_fix107_resolve_api();void*it=NULL;const void*mi=NULL;unsigned n=0,logged=0;
    while((mi=g_bender_fix106_class_get_methods(klass,&it))!=NULL&&n<192u){
        n++;const char*mn=g_bender_method_get_name_2812?g_bender_method_get_name_2812(mi):NULL;if(!mn)mn="?";
        int slot=-1;uintptr_t code=bender_fix16_method_ptr_2816(mi,&slot);
        unsigned pc=g_bender_fix107_method_get_param_count?g_bender_fix107_method_get_param_count(mi):999u;
        if(pc==0u || (slot_hint<64u&&slot==(int)slot_hint)){
            logged++;if(logged<=80u)logmsg("[BENDER_START_PREDICATE][METHOD] source=fix107 n=%u name=%s params=%u slot=%d code=%p slot_hint_match=%d read_only=1 code_patch=0 forced_completion=0\n",
                logged,mn,pc,slot,(void*)code,(slot==(int)slot_hint));
        }
    }
    logmsg("[BENDER_START_PREDICATE][METHODS_DONE] source=fix107 scanned=%u logged=%u interface_slot_hint=%u read_only=1 code_patch=0 forced_completion=0\n",n,logged,slot_hint);
}

static void bender_fix107_trace(unsigned frame){
    if(g_bender_fix107_done||frame<120u||!g_bender_fix106_obj||!g_bender_fix106_movenext_code||!g_bender_fix106_state_field)return;
    int state=0;g_bender_fix106_field_get_value((void*)g_bender_fix106_obj,g_bender_fix106_state_field,&state);if(state!=2)return;
    uintptr_t addr=g_bender_fix106_movenext_code;size_t span=bender_fix16_method_span_2816(addr);if(span>8192u)span=8192u;
    Module*im=g_bender_il2cpp_module_2813;if(!bender_fix107_module_contains(im,addr,span))return;
    uintptr_t hi=addr+span;g_bender_fix107_done=1;unsigned sites=0;
    for(size_t off=4u;off+8u<=span;off+=4u){
        uintptr_t pc=addr+off;uint32_t ins=0,next=0;memcpy(&ins,(const void*)pc,4u);memcpy(&next,(const void*)(pc+4u),4u);
        if((ins&UINT32_C(0xfffffc1f))!=UINT32_C(0xd63f0000))continue;
        uintptr_t yield=0;if(!bender_fix107_tbz_w0_bit0(pc+4u,next,&yield)||!bender_fix107_yield_state2(yield,addr,hi))continue;
        sites++;
        unsigned recv=31u;for(unsigned j=1u;j<=8u&&pc>=addr+j*4u;j++){uint32_t q=0;memcpy(&q,(const void*)(pc-j*4u),4u);if(bender_fix107_mov_x0_xn(q,&recv))break;}
        unsigned slot_hint=999u;for(unsigned j=1u;j<=24u&&pc>=addr+j*4u;j++){uint32_t q=0;memcpy(&q,(const void*)(pc-j*4u),4u);unsigned rd=0;if(bender_fix107_mov_w_imm(q,0u,&rd)&&rd==2u){slot_hint=0u;break;}for(unsigned v=1u;v<16u;v++)if(bender_fix107_mov_w_imm(q,v,&rd)&&rd==2u){slot_hint=v;j=25u;break;}}
        logmsg("[BENDER_START_PREDICATE][SITE] source=fix107 n=%u frame=%u state=2 movenext=%p predicate_blr=%p receiver_reg=x%u yield_target=%p interface_slot_hint=%u semantics=false-keeps-state2-current-null read_only=1 code_patch=0 forced_completion=0\n",
               sites,frame,(void*)addr,(void*)pc,recv,(void*)yield,slot_hint);

        /* Recover the common IL2CPP static-field receiver chain, entirely by reads. */
        uintptr_t recv_load_pc=0;unsigned obj_off=0;
        if(recv<31u){
            for(unsigned j=1u;j<=56u&&pc>=addr+j*4u;j++){uint32_t q=0;memcpy(&q,(const void*)(pc-j*4u),4u);unsigned rt=0,rn=0,io=0;if(bender_fix107_ldr_x(q,&rt,&rn,&io)&&rt==recv&&rn==8u){recv_load_pc=pc-j*4u;obj_off=io;break;}}
        }
        uintptr_t sf_load_pc=0;unsigned sf_off=0;
        if(recv_load_pc){for(unsigned j=1u;j<=32u&&recv_load_pc>=addr+j*4u;j++){uint32_t q=0;memcpy(&q,(const void*)(recv_load_pc-j*4u),4u);unsigned rt=0,rn=0,io=0;if(bender_fix107_ldr_x(q,&rt,&rn,&io)&&rt==8u&&rn==0u){sf_load_pc=recv_load_pc-j*4u;sf_off=io;break;}}}
        uintptr_t class_deref_pc=0;unsigned cell_reg=31u;
        if(sf_load_pc){for(unsigned j=1u;j<=40u&&sf_load_pc>=addr+j*4u;j++){uint32_t q=0;memcpy(&q,(const void*)(sf_load_pc-j*4u),4u);unsigned rt=0,rn=0,io=0;if(bender_fix107_ldr_x(q,&rt,&rn,&io)&&rt==0u&&io==0u&&rn<31u){class_deref_pc=sf_load_pc-j*4u;cell_reg=rn;break;}}}
        uintptr_t global_addr=0,adrp_pc=0;unsigned global_off=0;
        if(class_deref_pc&&cell_reg<31u){
            uintptr_t ldrpc=0;
            for(unsigned j=1u;j<=16u&&class_deref_pc>=addr+j*4u;j++){uint32_t q=0;memcpy(&q,(const void*)(class_deref_pc-j*4u),4u);unsigned rt=0,rn=0,io=0;if(bender_fix107_ldr_x(q,&rt,&rn,&io)&&rt==cell_reg&&rn==cell_reg){ldrpc=class_deref_pc-j*4u;global_off=io;break;}}
            if(ldrpc){for(unsigned j=1u;j<=8u&&ldrpc>=addr+j*4u;j++){uint32_t q=0;memcpy(&q,(const void*)(ldrpc-j*4u),4u);unsigned rd=0;uintptr_t page=0;if(bender_fix107_adrp(ldrpc-j*4u,q,&rd,&page)&&rd==cell_reg){adrp_pc=ldrpc-j*4u;global_addr=page+global_off;break;}}}
        }
        logmsg("[BENDER_START_PREDICATE][CHAIN] source=fix107 receiver_load=%p static_fields_load=%p class_deref=%p adrp=%p global_addr=%p static_fields_offset=0x%x object_offset=0x%x read_only=1 code_patch=0 forced_completion=0\n",
               (void*)recv_load_pc,(void*)sf_load_pc,(void*)class_deref_pc,(void*)adrp_pc,(void*)global_addr,sf_off,obj_off);
        if(global_addr&&bender_fix107_module_contains(im,global_addr,sizeof(uintptr_t))){
            uintptr_t cell=0;memcpy(&cell,(const void*)global_addr,sizeof(cell));
            void*klass=NULL;if(cell&&bender_fix107_module_contains(im,cell,sizeof(uintptr_t)))memcpy(&klass,(const void*)cell,sizeof(klass));
            char owner[160]={0};if(klass)bender_fix106_type_name(klass,owner,sizeof(owner));
            void*static_fields=NULL;void*obj=NULL;void*objklass=NULL;
            if(klass&&sf_off<1024u){memcpy(&static_fields,(const unsigned char*)klass+sf_off,sizeof(static_fields));if(static_fields&&obj_off<4096u)memcpy(&obj,(const unsigned char*)static_fields+obj_off,sizeof(obj));}
            if(obj&&g_bender_fix106_object_get_class)objklass=g_bender_fix106_object_get_class(obj);
            char object_type[160]={0};if(objklass)bender_fix106_type_name(objklass,object_type,sizeof(object_type));
            logmsg("[BENDER_START_PREDICATE][STATIC_OBJECT] source=fix107 global_addr=%p cell=%p owner_klass=%p owner_type=%s static_fields=%p object_offset=0x%x obj=%p obj_klass=%p obj_type=%s interface_slot_hint=%u read_only=1 code_patch=0 forced_completion=0\n",
                   (void*)global_addr,(void*)cell,klass,owner[0]?owner:"?",static_fields,obj_off,obj,objklass,object_type[0]?object_type:"?",slot_hint);
            if(obj&&objklass)bender_fix107_enum_receiver(obj,objklass,slot_hint);
        }
    }
    logmsg("[BENDER_START_PREDICATE][DONE] source=fix107 frame=%u state=2 sites=%u interpretation=virtual-bool-false-repeats-yield-null read_only=1 code_patch=0 forced_completion=0\n",frame,sites);
}


/* BENDER V2.7.163 — FIX114 read-only Firebase Initialize continuation trace. */
/* BENDER V2.7.164 — FIX115 capture of the concrete NordFirebase dependency frontier. */
static const void *g_bender_fix115_nord_mi;
static uintptr_t g_bender_fix115_nord_code;
static void *g_bender_fix115_nord_receiver_klass;
static int g_bender_fix115_done;
static int g_bender_fix114_done;

static int bender_fix114_ldr_x_uimm(uint32_t ins,unsigned*rt,unsigned*rn,unsigned*off){
    if((ins&UINT32_C(0xffc00000))!=UINT32_C(0xf9400000))return 0;
    if(rt)*rt=ins&31u;
    if(rn)*rn=(ins>>5)&31u;
    if(off)*off=((ins>>10)&0xfffu)*8u;
    return 1;
}
static int bender_fix114_ldrh_w_uimm(uint32_t ins,unsigned*rt,unsigned*rn,unsigned*off){
    if((ins&UINT32_C(0xffc00000))!=UINT32_C(0x79400000))return 0;
    if(rt)*rt=ins&31u;
    if(rn)*rn=(ins>>5)&31u;
    if(off)*off=((ins>>10)&0xfffu)*2u;
    return 1;
}
static int bender_fix114_strb_w_uimm(uint32_t ins,unsigned*rt,unsigned*rn,unsigned*off){
    if((ins&UINT32_C(0xffc00000))!=UINT32_C(0x39000000))return 0;
    if(rt)*rt=ins&31u;
    if(rn)*rn=(ins>>5)&31u;
    if(off)*off=(ins>>10)&0xfffu;
    return 1;
}
static int bender_fix114_add_w_same_imm(uint32_t ins,unsigned imm,unsigned*reg){
    if((ins&UINT32_C(0x7f000000))!=UINT32_C(0x11000000))return 0;
    unsigned rd=ins&31u,rn=(ins>>5)&31u,iv=(ins>>10)&0xfffu;
    if(rd!=rn||iv!=imm)return 0;
    if(reg)*reg=rd;
    return 1;
}
static int bender_fix114_add_x_imm(uint32_t ins,unsigned*rd,unsigned*rn,unsigned*imm){
    if((ins&UINT32_C(0xff000000))!=UINT32_C(0x91000000))return 0;
    if(((ins>>22)&1u)!=0u)return 0;
    if(rd)*rd=ins&31u;
    if(rn)*rn=(ins>>5)&31u;
    if(imm)*imm=(ins>>10)&0xfffu;
    return 1;
}
static int bender_fix114_mov_w_one(uint32_t ins,unsigned*rd){
    if((ins&UINT32_C(0x7f800000))!=UINT32_C(0x52800000))return 0;
    if(((ins>>21)&3u)!=0u||((ins>>5)&0xffffu)!=1u)return 0;
    if(rd)*rd=ins&31u;
    return 1;
}
static int bender_fix114_ret(uint32_t ins){return ins==UINT32_C(0xd65f03c0);}
static int bender_fix114_blr(uint32_t ins,unsigned*rn){
    if((ins&UINT32_C(0xfffffc1f))!=UINT32_C(0xd63f0000))return 0;
    if(rn)*rn=(ins>>5)&31u;
    return 1;
}
static int bender_fix114_nontrivial(uintptr_t p){return p>(uintptr_t)0x10000u;}

static const void*bender_fix114_find_named_method(void*klass,const char*want){
    if(!klass||!want||!g_bender_fix106_class_get_methods||!g_bender_method_get_name_2812)return NULL;
    void*it=NULL;const void*mi=NULL;unsigned n=0;
    while((mi=g_bender_fix106_class_get_methods(klass,&it))!=NULL&&n++<256u){
        const char*mn=g_bender_method_get_name_2812(mi);
        if(mn&&!strcmp(mn,want))return mi;
    }
    return NULL;
}
static const void*bender_fix114_method_at(void*klass,uintptr_t code){
    if(!klass||!code||!g_bender_fix106_class_get_methods)return NULL;
    void*it=NULL;const void*mi=NULL;unsigned n=0;
    while((mi=g_bender_fix106_class_get_methods(klass,&it))!=NULL&&n++<256u){
        int slot=-1;uintptr_t p=bender_fix16_method_ptr_2816(mi,&slot);
        if(p==code)return mi;
    }
    if(g_bender_fix16_map_ready_2816){
        const BenderFix16MethodMap2816*hit=bender_fix16_find_addr_2816(code);
        if(hit&&hit->addr==code)return hit->method;
    }
    return NULL;
}
static void bender_fix114_log_method(const char*tag,const void*mi,uintptr_t code,int slot){
    char desc[256]={0};const char*mn=NULL;
    if(mi)bender_fix13_describe_method_2813(mi,desc,sizeof(desc),&mn);
    logmsg("[BENDER_FIREBASE_INIT][%s] source=fix114 method_info=%p image=%s method=%s code=%p slot=%d read_only=1 invoke=0 state_write=0 forced_completion=0\n",
           tag,mi,mi?bender_fix15_image_name_2815(mi):"?",desc[0]?desc:(mn?mn:"?"),(void*)code,slot);
}

static void bender_fix114_trace_callback(void*iterator_klass,uintptr_t init_code,size_t span){
    uintptr_t hi=init_code+span;
    for(uintptr_t pc=init_code;pc+12u<=hi;pc+=4u){
        uint32_t a=0,b=0,c=0;memcpy(&a,(void*)pc,4u);memcpy(&b,(void*)(pc+4u),4u);memcpy(&c,(void*)(pc+8u),4u);
        unsigned wr=0,sr=0,rn=0,off=0;
        if(!bender_fix114_mov_w_one(a,&wr)||!bender_fix114_strb_w_uimm(b,&sr,&rn,&off)||!bender_fix114_ret(c))continue;
        if(wr!=sr||rn!=0u)continue;
        const void*mi=bender_fix114_method_at(iterator_klass,pc);int slot=-1;
        if(mi)(void)bender_fix16_method_ptr_2816(mi,&slot);
        char desc[256]={0};const char*mn=NULL;if(mi)bender_fix13_describe_method_2813(mi,desc,sizeof(desc),&mn);
        logmsg("[BENDER_FIREBASE_INIT][CALLBACK] source=fix114 code=%p method_info=%p image=%s method=%s writes_byte_offset=0x%x value=1 semantic=completion-flag-setter-candidate read_only=1 invoke=0 state_write=0 forced_completion=0\n",
               (void*)pc,mi,mi?bender_fix15_image_name_2815(mi):"?",desc[0]?desc:(mn?mn:"?"),off);
    }
}

static int bender_fix114_find_interface_global(uintptr_t init_code,size_t blr_off,Module*mod,uintptr_t*out_global,void**out_iface){
    size_t begin=blr_off>128u?blr_off-128u:0u;
    for(size_t off=begin;off+12u<=blr_off;off+=4u){
        uint32_t a=0;memcpy(&a,(void*)(init_code+off),4u);unsigned r=0;uintptr_t page=0;
        if(!bender_fix107_adrp(init_code+off,a,&r,&page))continue;
        for(size_t j=off+4u;j<=off+16u&&j+4u<=blr_off;j+=4u){
            uint32_t b=0;memcpy(&b,(void*)(init_code+j),4u);unsigned rt=0,rn=0,imm=0;
            if(!bender_fix114_ldr_x_uimm(b,&rt,&rn,&imm)||rt!=r||rn!=r)continue;
            for(size_t k=j+4u;k<=j+24u&&k+4u<=blr_off;k+=4u){
                uint32_t c=0;memcpy(&c,(void*)(init_code+k),4u);unsigned crt=0,crn=0,coff=0;
                if(!bender_fix114_ldr_x_uimm(c,&crt,&crn,&coff)||crt!=1u||crn!=r||coff!=0u)continue;
                uintptr_t ga=page+imm;if(!bender_fix107_module_contains(mod,ga,sizeof(void*)))continue;
                void*cell=*(void**)ga;if(!bender_fix114_nontrivial((uintptr_t)cell))continue;
                void*iface=*(void**)cell;if(!bender_fix114_nontrivial((uintptr_t)iface))continue;
                if(out_global)*out_global=ga;
                if(out_iface)*out_iface=iface;
                return 1;
            }
        }
    }
    return 0;
}

static int bender_fix114_find_receiver_chain(uintptr_t init_code,size_t blr_off,Module*mod,unsigned*static_off,unsigned*object_off,uintptr_t*out_global,void**owner_out,void**receiver_out){
    size_t begin=blr_off>192u?blr_off-192u:0u;
    unsigned base_reg=31u,sf_reg=31u,sf_off=0u,obj_off=0u;
    size_t obj_site=(size_t)-1,sf_site=(size_t)-1;
    for(size_t off=begin;off+4u<=blr_off;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(init_code+off),4u);unsigned rt=0,rn=0,imm=0;
        if(bender_fix114_ldr_x_uimm(ins,&rt,&rn,&imm)&&rt==19u){obj_site=off;sf_reg=rn;obj_off=imm;}
    }
    if(obj_site==(size_t)-1)return 0;
    for(size_t off=begin;off<obj_site;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(init_code+off),4u);unsigned rt=0,rn=0,imm=0;
        if(bender_fix114_ldr_x_uimm(ins,&rt,&rn,&imm)&&rt==sf_reg&&rn==0u){sf_site=off;sf_off=imm;}
    }
    if(sf_site==(size_t)-1)return 0;
    for(size_t off=begin;off<sf_site;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(init_code+off),4u);unsigned rt=0,rn=0,imm=0;
        if(bender_fix114_ldr_x_uimm(ins,&rt,&rn,&imm)&&rt==0u&&imm==0u)base_reg=rn;
    }
    if(base_reg==31u)return 0;
    uintptr_t ga=0;
    for(size_t off=0;off+8u<=sf_site;off+=4u){
        uint32_t a=0,b=0;memcpy(&a,(void*)(init_code+off),4u);memcpy(&b,(void*)(init_code+off+4u),4u);
        unsigned ar=0,rt=0,rn=0,imm=0;uintptr_t page=0;
        if(!bender_fix107_adrp(init_code+off,a,&ar,&page)||ar!=base_reg)continue;
        if(!bender_fix114_ldr_x_uimm(b,&rt,&rn,&imm)||rt!=base_reg||rn!=base_reg)continue;
        ga=page+imm;
    }
    if(!ga||!bender_fix107_module_contains(mod,ga,sizeof(void*)))return 0;
    void*cell=*(void**)ga;if(!bender_fix114_nontrivial((uintptr_t)cell))return 0;
    void*owner=*(void**)cell;if(!bender_fix114_nontrivial((uintptr_t)owner))return 0;
    void*static_fields=*(void**)((unsigned char*)owner+sf_off);
    if(!bender_fix114_nontrivial((uintptr_t)static_fields))return 0;
    void*receiver=*(void**)((unsigned char*)static_fields+obj_off);
    if(!bender_fix114_nontrivial((uintptr_t)receiver))return 0;
    if(static_off)*static_off=sf_off;
    if(object_off)*object_off=obj_off;
    if(out_global)*out_global=ga;
    if(owner_out)*owner_out=owner;
    if(receiver_out)*receiver_out=receiver;
    return 1;
}

static void bender_fix114_trace_dispatch(uintptr_t init_code,size_t span){
    Module*mod=g_bender_il2cpp_module_2813;if(!mod)return;
    for(size_t off=0;off+16u<=span;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(init_code+off),4u);unsigned blrreg=0;
        if(!bender_fix114_blr(ins,&blrreg)||blrreg!=8u)continue;
        uint32_t n1=0,n2=0,n3=0;memcpy(&n1,(void*)(init_code+off+4u),4u);memcpy(&n2,(void*)(init_code+off+8u),4u);memcpy(&n3,(void*)(init_code+off+12u),4u);
        if(!bender_fix114_ret(n3))continue;
        unsigned slot=999u;
        for(size_t p=(off>96u?off-96u:0u);p<off;p+=4u){uint32_t q=0;memcpy(&q,(void*)(init_code+p),4u);unsigned r=0;if(bender_fix114_add_w_same_imm(q,1u,&r))slot=1u;}
        uintptr_t iface_global=0;void*iface=NULL;
        int iface_ok=bender_fix114_find_interface_global(init_code,off,mod,&iface_global,&iface);
        unsigned static_off=0,obj_off=0;uintptr_t owner_global=0;void*owner=NULL;void*receiver=NULL;
        int recv_ok=bender_fix114_find_receiver_chain(init_code,off,mod,&static_off,&obj_off,&owner_global,&owner,&receiver);
        char owner_type[160]={0},recv_type[160]={0},iface_type[160]={0};void*rklass=NULL;
        if(owner)bender_fix106_type_name(owner,owner_type,sizeof(owner_type));
        if(recv_ok&&g_bender_fix106_object_get_class){rklass=g_bender_fix106_object_get_class(receiver);if(rklass)bender_fix106_type_name(rklass,recv_type,sizeof(recv_type));}
        if(iface)bender_fix106_type_name(iface,iface_type,sizeof(iface_type));
        logmsg("[BENDER_FIREBASE_INIT][RECEIVER_CHAIN] source=fix114 blr=%p owner_global=%p owner_class=%p owner_type=%s static_fields_offset=0x%x object_offset=0x%x receiver=%p receiver_class=%p receiver_type=%s recovered=%d read_only=1 invoke=0 state_write=0 forced_completion=0\n",
               (void*)(init_code+off),(void*)owner_global,owner,owner_type[0]?owner_type:"?",static_off,obj_off,receiver,rklass,recv_type[0]?recv_type:"?",recv_ok);
        logmsg("[BENDER_FIREBASE_INIT][INTERFACE] source=fix114 blr=%p interface_global=%p interface_class=%p interface_type=%s slot=%u recovered=%d semantics=register-continuation-dispatch read_only=1 invoke=0 state_write=0 forced_completion=0\n",
               (void*)(init_code+off),(void*)iface_global,iface,iface_type[0]?iface_type:"?",slot,iface_ok);
        const void*iface_mi=NULL;
        if(iface&&g_bender_fix106_class_get_methods){
            void*it=NULL;const void*mi=NULL;unsigned index=0;
            while((mi=g_bender_fix106_class_get_methods(iface,&it))!=NULL&&index<64u){
                int mslot=-1;uintptr_t mcode=bender_fix16_method_ptr_2816(mi,&mslot);
                const char*mn=g_bender_method_get_name_2812?g_bender_method_get_name_2812(mi):NULL;
                logmsg("[BENDER_FIREBASE_INIT][INTERFACE_METHOD] source=fix114 index=%u name=%s slot=%d code=%p target_slot=%u target_match=%d read_only=1 invoke=0 state_write=0 forced_completion=0\n",
                       index,mn?mn:"?",mslot,(void*)mcode,slot,(slot<64u&&(mslot==(int)slot||index==slot)));
                if(slot<64u&&(mslot==(int)slot||index==slot)&&!iface_mi)iface_mi=mi;
                index++;
            }
        }
        if(iface_mi){int s=-1;uintptr_t c=bender_fix16_method_ptr_2816(iface_mi,&s);bender_fix114_log_method("SLOT",iface_mi,c,s);}
        if(recv_ok&&rklass&&iface&&slot<64u){
            unsigned count_off=0,offsets_off=0,vtable_base=0;int got_count=0,got_offsets=0,got_vtable=0;
            size_t begin=off>128u?off-128u:0u;
            for(size_t p=begin;p<off;p+=4u){
                uint32_t q=0;memcpy(&q,(void*)(init_code+p),4u);unsigned rt=0,rn=0,imm=0;
                if(bender_fix114_ldrh_w_uimm(q,&rt,&rn,&imm)&&rn==8u){count_off=imm;got_count=1;}
                if(bender_fix114_ldr_x_uimm(q,&rt,&rn,&imm)&&rn==8u&&imm>0u){offsets_off=imm;got_offsets=1;}
                unsigned rd=0;if(bender_fix114_add_x_imm(q,&rd,&rn,&imm)&&rd==0u&&rn==8u&&imm>0u){vtable_base=imm;got_vtable=1;}
            }
            if(got_count&&got_offsets&&got_vtable){
                uint16_t count=*(uint16_t*)((unsigned char*)rklass+count_off);void*pairs=*(void**)((unsigned char*)rklass+offsets_off);int found=0,base_slot=-1;
                if(bender_fix114_nontrivial((uintptr_t)pairs)&&count<1024u){
                    for(unsigned i=0;i<count;i++){
                        unsigned char*e=(unsigned char*)pairs+i*16u;void*ei=*(void**)e;int32_t bo=0;memcpy(&bo,e+8u,sizeof(bo));
                        if(ei==iface){found=1;base_slot=bo;break;}
                    }
                }
                if(found&&base_slot>=0){
                    unsigned final_slot=(unsigned)base_slot+slot;unsigned char*entry=(unsigned char*)rklass+vtable_base+(size_t)final_slot*16u;
                    uintptr_t method_ptr=*(uintptr_t*)entry;const void*method_info=*(const void**)(entry+8u);char desc[256]={0};const char*mn=NULL;
                    if(method_info)bender_fix13_describe_method_2813(method_info,desc,sizeof(desc),&mn);
                    g_bender_fix115_nord_mi=method_info;g_bender_fix115_nord_code=method_ptr;g_bender_fix115_nord_receiver_klass=rklass;
                    logmsg("[BENDER_FIREBASE_INIT][DISPATCH] source=fix114 receiver_type=%s interface_type=%s interface_slot=%u base_slot=%d final_slot=%u method_ptr=%p method_info=%p method=%s read_only=1 invoke=0 state_write=0 forced_completion=0\n",
                           recv_type[0]?recv_type:"?",iface_type[0]?iface_type:"?",slot,base_slot,final_slot,(void*)method_ptr,method_info,desc[0]?desc:(mn?mn:"?"));
                }
            }
        }
        return;
    }
    logmsg("[BENDER_FIREBASE_INIT][INTERFACE] source=fix114 recovered=0 reason=dispatch-shape-not-found read_only=1 invoke=0 state_write=0 forced_completion=0\n");
}

static void bender_fix114_tick(unsigned frame){
    if(g_bender_fix114_done||frame<6u||frame>10u||!g_bender_fix106_start_mi)return;
    bender_fix106_resolve_api();if(!g_bender_fix106_api_ready||!g_bender_method_get_class_2812)return;
    void*start_klass=g_bender_method_get_class_2812(g_bender_fix106_start_mi);if(!start_klass)return;
    const void*init_mi=bender_fix114_find_named_method(start_klass,"InitializeFirebase");if(!init_mi)return;
    int slot=-1;uintptr_t init_code=bender_fix16_method_ptr_2816(init_mi,&slot);if(!init_code)return;
    Module*mod=bender_module_for_runtime_addr(init_code);if(!mod||strcmp(mod->name,"libil2cpp.so"))return;
    size_t span=bender_fix16_method_span_2816(init_code);if(span<64u||span>2048u)span=1024u;
    g_bender_fix114_done=1;
    char start_type[160]={0};bender_fix106_type_name(start_klass,start_type,sizeof(start_type));
    logmsg("[BENDER_FIREBASE_INIT][SITE] source=fix114 frame=%u start_class=%p start_type=%s method_info=%p method=InitializeFirebase code=%p module=%s offset=0x%" PRIxPTR " span=%zu state_wait=3 flag=isFirebaseInitialized read_only=1 invoke=0 state_write=0 forced_completion=0\n",
           frame,start_klass,start_type[0]?start_type:"?",init_mi,(void*)init_code,mod->name,init_code-mod->bias,span);
    bender_fix114_trace_callback(g_bender_fix106_klass,init_code,span);
    bender_fix114_trace_dispatch(init_code,span);
    logmsg("[BENDER_FIREBASE_INIT][DONE] source=fix114 frame=%u method=InitializeFirebase interpretation=state3-waits-managed-continuation read_only=1 invoke=0 state_write=0 forced_completion=0\n",frame);
}


/* BENDER V2.7.164 — FIX115 read-only NordFirebase dependency continuation trace. */
static int bender_fix115_mov_x(uint32_t ins,unsigned*rd,unsigned*rm){
    if((ins&UINT32_C(0xffe0ffe0))!=UINT32_C(0xaa0003e0))return 0;
    if(rd)*rd=ins&31u;
    if(rm)*rm=(ins>>16)&31u;
    return 1;
}
static int bender_fix115_bl(uint32_t ins){
    return (ins&UINT32_C(0xfc000000))==UINT32_C(0x94000000);
}
static int bender_fix115_b(uint32_t ins){
    return (ins&UINT32_C(0xfc000000))==UINT32_C(0x14000000);
}
static uintptr_t bender_fix115_imm26_target(uintptr_t pc,uint32_t ins){
    int64_t imm=(int64_t)(ins&UINT32_C(0x03ffffff));
    if(imm&(INT64_C(1)<<25))imm-=INT64_C(1)<<26;
    return (uintptr_t)((int64_t)pc+(imm<<2));
}
static int bender_fix115_find_global_for_reg(uintptr_t code,size_t before,unsigned reg,Module*mod,uintptr_t*out_global){
    uintptr_t best=0;
    for(size_t off=0;off+8u<=before;off+=4u){
        uint32_t a=0,b=0;memcpy(&a,(void*)(code+off),4u);memcpy(&b,(void*)(code+off+4u),4u);
        unsigned ar=0,rt=0,rn=0,imm=0;uintptr_t page=0;
        if(!bender_fix107_adrp(code+off,a,&ar,&page)||ar!=reg)continue;
        if(!bender_fix114_ldr_x_uimm(b,&rt,&rn,&imm)||rt!=reg||rn!=reg)continue;
        uintptr_t ga=page+imm;
        if(bender_fix107_module_contains(mod,ga,sizeof(void*)))best=ga;
    }
    if(!best)return 0;
    if(out_global)*out_global=best;
    return 1;
}
static void bender_fix115_log_method(const char*tag,const void*mi,uintptr_t code,int slot,size_t off){
    char desc[320]={0};const char*mn=NULL;
    if(mi)bender_fix13_describe_method_2813(mi,desc,sizeof(desc),&mn);
    logmsg("[BENDER_NORD_FIREBASE][%s] source=fix115 offset=0x%zx method_info=%p image=%s method=%s code=%p slot=%d read_only=1 invoke=0 task_complete=0 flag_write=0 forced_completion=0\n",
           tag,off,mi,mi?bender_fix15_image_name_2815(mi):"?",desc[0]?desc:(mn?mn:"?"),(void*)code,slot);
}
static void bender_fix115_trace_direct(uintptr_t code,size_t span){
    unsigned n=0;
    for(size_t off=0;off+4u<=span;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(code+off),4u);
        if(!bender_fix115_bl(ins)&&!bender_fix115_b(ins))continue;
        uintptr_t target=bender_fix115_imm26_target(code+off,ins);
        if(target>=code&&target<code+span)continue;
        uintptr_t followed=bender_fix16_follow_direct_branch_2816(target);
        const BenderFix16MethodMap2816*hit=bender_fix16_find_addr_2816(target);
        if(!hit&&followed!=target)hit=bender_fix16_find_addr_2816(followed);
        if(hit){
            int slot=-1;uintptr_t mp=bender_fix16_method_ptr_2816(hit->method,&slot);
            bender_fix115_log_method(bender_fix115_bl(ins)?"DIRECT_BL":"DIRECT_B",hit->method,mp,slot,off);
        }else{
            Module*m=bender_module_for_runtime_addr(target);
            logmsg("[BENDER_NORD_FIREBASE][DIRECT_UNMATCHED] source=fix115 offset=0x%zx kind=%s target=%p followed=%p module=%s module_offset=0x%" PRIxPTR " read_only=1 invoke=0 task_complete=0 flag_write=0 forced_completion=0\n",
                   off,bender_fix115_bl(ins)?"BL":"B",(void*)target,(void*)followed,m?m->name:"?",m?target-m->bias:(uintptr_t)0);
        }
        n++;
    }
    logmsg("[BENDER_NORD_FIREBASE][DIRECT_DONE] source=fix115 count=%u read_only=1 invoke=0 task_complete=0 flag_write=0 forced_completion=0\n",n);
}
static void bender_fix115_trace_delegate_methodinfos(uintptr_t code,size_t span,Module*mod){
    unsigned found=0;
    for(size_t off=0;off+4u<=span;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(code+off),4u);
        if(!bender_fix115_bl(ins))continue;
        unsigned method_reg=31u;size_t ldr2_site=(size_t)-1;
        size_t begin=off>48u?off-48u:0u;
        for(size_t p=begin;p<off;p+=4u){
            uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rt=0,rn=0,imm=0;
            if(bender_fix114_ldr_x_uimm(q,&rt,&rn,&imm)&&rt==2u&&imm==0u){method_reg=rn;ldr2_site=p;}
        }
        if(ldr2_site==(size_t)-1||method_reg==31u)continue;
        uintptr_t ga=0;
        if(!bender_fix115_find_global_for_reg(code,ldr2_site,method_reg,mod,&ga))continue;
        void*cell=*(void**)ga;
        if(!bender_fix114_nontrivial((uintptr_t)cell))continue;
        const void*mi=*(const void**)cell;
        if(!bender_fix114_nontrivial((uintptr_t)mi))continue;
        int slot=-1;uintptr_t mp=bender_fix16_method_ptr_2816(mi,&slot);
        const char*mn=g_bender_method_get_name_2812?g_bender_method_get_name_2812(mi):NULL;
        if(!mn||!strstr(mn,"CheckAndFixDependenciesAsync"))continue;
        char desc[320]={0};const char*dmn=NULL;bender_fix13_describe_method_2813(mi,desc,sizeof(desc),&dmn);
        logmsg("[BENDER_NORD_FIREBASE][LAMBDA] source=fix115 call_offset=0x%zx method_global=%p method_cell=%p method_info=%p image=%s method=%s code=%p slot=%d compiler_generated=1 read_only=1 invoke=0 task_complete=0 flag_write=0 forced_completion=0\n",
               off,(void*)ga,cell,mi,bender_fix15_image_name_2815(mi),desc[0]?desc:(dmn?dmn:mn),(void*)mp,slot);
        found++;
    }
    logmsg("[BENDER_NORD_FIREBASE][LAMBDAS_DONE] source=fix115 count=%u expected_names=b__0,b__1 read_only=1 invoke=0 task_complete=0 flag_write=0 forced_completion=0\n",found);
}
static int bender_fix115_find_static_receiver(uintptr_t code,size_t blr_off,Module*mod,unsigned receiver_reg,void**receiver_out,void**klass_out,uintptr_t*global_out,unsigned*static_off_out,unsigned*object_off_out){
    size_t begin=blr_off>288u?blr_off-288u:0u;
    unsigned sf_reg=31u,obj_off=0u;size_t obj_site=(size_t)-1;
    for(size_t p=begin;p<blr_off;p+=4u){
        uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rt=0,rn=0,imm=0;
        if(bender_fix114_ldr_x_uimm(q,&rt,&rn,&imm)&&rt==receiver_reg){sf_reg=rn;obj_off=imm;obj_site=p;}
    }
    if(obj_site==(size_t)-1||sf_reg==31u)return 0;
    unsigned class_reg=31u,static_off=0u;size_t sf_site=(size_t)-1;
    for(size_t p=begin;p<obj_site;p+=4u){
        uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rt=0,rn=0,imm=0;
        if(bender_fix114_ldr_x_uimm(q,&rt,&rn,&imm)&&rt==sf_reg&&imm>=0x80u){class_reg=rn;static_off=imm;sf_site=p;}
    }
    if(sf_site==(size_t)-1||class_reg==31u)return 0;
    unsigned global_reg=31u;size_t class_load=(size_t)-1;
    for(size_t p=begin;p<sf_site;p+=4u){
        uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rt=0,rn=0,imm=0;
        if(bender_fix114_ldr_x_uimm(q,&rt,&rn,&imm)&&rt==class_reg&&imm==0u){global_reg=rn;class_load=p;}
    }
    if(class_load==(size_t)-1||global_reg==31u)return 0;
    uintptr_t ga=0;if(!bender_fix115_find_global_for_reg(code,class_load,global_reg,mod,&ga))return 0;
    void*cell=*(void**)ga;if(!bender_fix114_nontrivial((uintptr_t)cell))return 0;
    void*owner=*(void**)cell;if(!bender_fix114_nontrivial((uintptr_t)owner))return 0;
    void*static_fields=*(void**)((unsigned char*)owner+static_off);if(!bender_fix114_nontrivial((uintptr_t)static_fields))return 0;
    void*receiver=*(void**)((unsigned char*)static_fields+obj_off);if(!bender_fix114_nontrivial((uintptr_t)receiver))return 0;
    void*rklass=g_bender_fix106_object_get_class?g_bender_fix106_object_get_class(receiver):NULL;
    if(!rklass)return 0;
    if(receiver_out)*receiver_out=receiver;
    if(klass_out)*klass_out=rklass;
    if(global_out)*global_out=ga;
    if(static_off_out)*static_off_out=static_off;
    if(object_off_out)*object_off_out=obj_off;
    return 1;
}
static void bender_fix115_trace_interface(uintptr_t code,size_t span,Module*mod){
    for(size_t off=0;off+4u<=span;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(code+off),4u);unsigned blr_reg=0;
        if(!bender_fix114_blr(ins,&blr_reg)||blr_reg!=8u)continue;
        unsigned receiver_reg=31u;
        size_t begin=off>32u?off-32u:0u;
        for(size_t p=begin;p<off;p+=4u){
            uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rd=0,rm=0;
            if(bender_fix115_mov_x(q,&rd,&rm)&&rd==0u)receiver_reg=rm;
        }
        if(receiver_reg==31u)continue;
        unsigned iface_slot=999u;
        size_t scan_begin=off>128u?off-128u:0u;
        for(size_t p=scan_begin;p<off;p+=4u){
            uint32_t q=0;memcpy(&q,(void*)(code+p),4u);
            if((q&UINT32_C(0x7f000000))==UINT32_C(0x11000000)){
                unsigned rd=q&31u,rn=(q>>5)&31u,imm=(q>>10)&0xfffu;
                if(rd==rn&&imm<64u&&imm>0u)iface_slot=imm;
            }
        }
        uintptr_t iface_global=0;void*iface=NULL;
        int iface_ok=bender_fix114_find_interface_global(code,off,mod,&iface_global,&iface);
        void*receiver=NULL;void*rklass=NULL;uintptr_t owner_global=0;unsigned static_off=0,obj_off=0;
        int recv_ok=bender_fix115_find_static_receiver(code,off,mod,receiver_reg,&receiver,&rklass,&owner_global,&static_off,&obj_off);
        char iface_type[192]={0},recv_type[192]={0};
        if(iface)bender_fix106_type_name(iface,iface_type,sizeof(iface_type));
        if(rklass)bender_fix106_type_name(rklass,recv_type,sizeof(recv_type));
        logmsg("[BENDER_NORD_FIREBASE][INTERFACE] source=fix115 blr_offset=0x%zx blr=%p receiver_reg=x%u receiver=%p receiver_class=%p receiver_type=%s owner_global=%p static_fields_offset=0x%x object_offset=0x%x interface_global=%p interface_class=%p interface_type=%s interface_slot=%u receiver_recovered=%d interface_recovered=%d read_only=1 invoke=0 task_complete=0 flag_write=0 forced_completion=0\n",
               off,(void*)(code+off),receiver_reg,receiver,rklass,recv_type[0]?recv_type:"?",(void*)owner_global,static_off,obj_off,(void*)iface_global,iface,iface_type[0]?iface_type:"?",iface_slot,recv_ok,iface_ok);
        if(!recv_ok||!iface_ok||iface_slot>=64u)return;
        unsigned count_off=0,offsets_off=0,vtable_base=0;int got_count=0,got_offsets=0,got_vtable=0;
        for(size_t p=scan_begin;p<off;p+=4u){
            uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rt=0,rn=0,imm=0;
            if(bender_fix114_ldrh_w_uimm(q,&rt,&rn,&imm)&&rn==8u){count_off=imm;got_count=1;}
            if(bender_fix114_ldr_x_uimm(q,&rt,&rn,&imm)&&rn==8u&&imm>0u){offsets_off=imm;got_offsets=1;}
            unsigned rd=0;
            if(bender_fix114_add_x_imm(q,&rd,&rn,&imm)&&rn==8u&&imm>0u){vtable_base=imm;got_vtable=1;}
        }
        if(!got_count||!got_offsets||!got_vtable)return;
        uint16_t count=*(uint16_t*)((unsigned char*)rklass+count_off);
        void*pairs=*(void**)((unsigned char*)rklass+offsets_off);int found=0,base_slot=-1;
        if(bender_fix114_nontrivial((uintptr_t)pairs)&&count<1024u){
            for(unsigned i=0;i<count;i++){
                unsigned char*e=(unsigned char*)pairs+i*16u;void*ei=*(void**)e;int32_t bo=0;memcpy(&bo,e+8u,sizeof(bo));
                if(ei==iface){found=1;base_slot=bo;break;}
            }
        }
        if(found&&base_slot>=0){
            unsigned final_slot=(unsigned)base_slot+iface_slot;
            unsigned char*entry=(unsigned char*)rklass+vtable_base+(size_t)final_slot*16u;
            uintptr_t method_ptr=*(uintptr_t*)entry;const void*method_info=*(const void**)(entry+8u);
            char desc[320]={0};const char*mn=NULL;if(method_info)bender_fix13_describe_method_2813(method_info,desc,sizeof(desc),&mn);
            logmsg("[BENDER_NORD_FIREBASE][DISPATCH] source=fix115 receiver_type=%s interface_type=%s interface_slot=%u base_slot=%d final_slot=%u method_ptr=%p method_info=%p method=%s read_only=1 invoke=0 task_complete=0 flag_write=0 forced_completion=0\n",
                   recv_type[0]?recv_type:"?",iface_type[0]?iface_type:"?",iface_slot,base_slot,final_slot,(void*)method_ptr,method_info,desc[0]?desc:(mn?mn:"?"));
        }
        return;
    }
    logmsg("[BENDER_NORD_FIREBASE][INTERFACE] source=fix115 recovered=0 reason=blr-interface-shape-not-found read_only=1 invoke=0 task_complete=0 flag_write=0 forced_completion=0\n");
}
static void bender_fix115_tick(unsigned frame){
    if(g_bender_fix115_done||frame<120u||!g_bender_fix16_map_ready_2816)return;
    if(!g_bender_fix115_nord_code||!g_bender_fix115_nord_mi)return;
    Module*mod=bender_module_for_runtime_addr(g_bender_fix115_nord_code);
    if(!mod||strcmp(mod->name,"libil2cpp.so"))return;
    size_t span=bender_fix16_method_span_2816(g_bender_fix115_nord_code);
    if(span<64u||span>4096u)span=1024u;
    g_bender_fix115_done=1;
    char desc[320]={0};const char*mn=NULL;bender_fix13_describe_method_2813(g_bender_fix115_nord_mi,desc,sizeof(desc),&mn);
    char receiver_type[192]={0};if(g_bender_fix115_nord_receiver_klass)bender_fix106_type_name(g_bender_fix115_nord_receiver_klass,receiver_type,sizeof(receiver_type));
    logmsg("[BENDER_NORD_FIREBASE][SITE] source=fix115 frame=%u method_info=%p image=%s method=%s code=%p module=%s offset=0x%" PRIxPTR " span=%zu receiver_type=%s physical_frontier=state3-isFirebaseInitialized read_only=1 invoke=0 task_complete=0 flag_write=0 forced_completion=0\n",
           frame,g_bender_fix115_nord_mi,bender_fix15_image_name_2815(g_bender_fix115_nord_mi),desc[0]?desc:(mn?mn:"?"),(void*)g_bender_fix115_nord_code,mod->name,g_bender_fix115_nord_code-mod->bias,span,receiver_type[0]?receiver_type:"?");
    bender_fix115_trace_delegate_methodinfos(g_bender_fix115_nord_code,span,mod);
    bender_fix115_trace_interface(g_bender_fix115_nord_code,span,mod);
    bender_fix115_trace_direct(g_bender_fix115_nord_code,span);
    logmsg("[BENDER_NORD_FIREBASE][DONE] source=fix115 frame=%u interpretation=trace-internal-dependency-continuation-frontier next=inspect-concrete-async-result-not-force-success read_only=1 invoke=0 task_complete=0 flag_write=0 forced_completion=0\n",frame);
}


/* BENDER V2.7.165 — FIX116 read-only NordFirebase async-contract resolver. */
static int g_bender_fix116_done;

static int bender_fix116_reg_written(uint32_t ins,unsigned reg){
    /* Conservative AArch64 GPR destination test for the forms used by this method. */
    unsigned rd=ins&31u;
    if(rd!=reg)return 0;
    if((ins&UINT32_C(0x9f000000))==UINT32_C(0x90000000))return 1; /* ADRP */
    if((ins&UINT32_C(0xffc00000))==UINT32_C(0xf9400000))return 1; /* LDR X,uimm */
    if((ins&UINT32_C(0xff000000))==UINT32_C(0x91000000))return 1; /* ADD X,imm */
    if((ins&UINT32_C(0xffe0ffe0))==UINT32_C(0xaa0003e0))return 1; /* MOV X alias */
    return 0;
}

static int bender_fix116_find_adrp_ldr_global(uintptr_t code,size_t limit,unsigned reg,Module*mod,uintptr_t*out_global,size_t*out_adrp,size_t*out_ldr){
    if(!code||!mod||reg>=31u)return 0;
    uintptr_t best=0;size_t ba=0,bl=0;
    for(size_t aoff=0;aoff+4u<=limit;aoff+=4u){
        uint32_t a=0;memcpy(&a,(void*)(code+aoff),4u);
        unsigned ar=0;uintptr_t page=0;
        if(!bender_fix107_adrp(code+aoff,a,&ar,&page)||ar!=reg)continue;
        size_t maxj=aoff+28u;if(maxj>limit)maxj=limit;
        for(size_t j=aoff+4u;j+4u<=maxj;j+=4u){
            uint32_t b=0;memcpy(&b,(void*)(code+j),4u);
            unsigned rt=0,rn=0,imm=0;
            if(bender_fix114_ldr_x_uimm(b,&rt,&rn,&imm)&&rt==reg&&rn==reg){
                uintptr_t ga=page+(uintptr_t)imm;
                if(bender_fix107_module_contains(mod,ga,sizeof(void*))){best=ga;ba=aoff;bl=j;}
                break;
            }
            if(bender_fix116_reg_written(b,reg))break;
        }
    }
    if(!best)return 0;
    if(out_global)*out_global=best;
    if(out_adrp)*out_adrp=ba;
    if(out_ldr)*out_ldr=bl;
    return 1;
}

static const void*bender_fix116_methodinfo_from_global(uintptr_t ga,void**cell_out){
    if(!ga)return NULL;
    void*cell=*(void**)ga;
    if(cell_out)*cell_out=cell;
    if(!bender_fix114_nontrivial((uintptr_t)cell))return NULL;
    const void*mi=*(const void**)cell;
    if(!bender_fix114_nontrivial((uintptr_t)mi))return NULL;
    return mi;
}

static void bender_fix116_describe_method(const void*mi,char*desc,size_t cap,const char**mn,uintptr_t*mp,int*slot){
    if(desc&&cap)desc[0]='\0';
    if(mn)*mn=NULL;
    if(mp)*mp=0;
    if(slot)*slot=-1;
    if(!mi)return;
    const char*name=NULL;
    if(g_bender_method_get_name_2812)name=g_bender_method_get_name_2812(mi);
    if(mn)*mn=name;
    if(desc&&cap)bender_fix13_describe_method_2813(mi,desc,cap,NULL);
    int s=-1;uintptr_t p=bender_fix16_method_ptr_2816(mi,&s);
    if(mp)*mp=p;
    if(slot)*slot=s;
}

static unsigned bender_fix116_trace_delegates(uintptr_t code,size_t span,Module*mod){
    unsigned found=0;
    for(size_t off=0;off+4u<=span;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(code+off),4u);
        if(!bender_fix115_bl(ins))continue;
        /* Delegate .ctor receives MethodInfo in x2; locate ldr x2,[xN] shortly before BL. */
        unsigned method_reg=31u;size_t ldr2=(size_t)-1;
        size_t begin=off>40u?off-40u:0u;
        for(size_t p=begin;p<off;p+=4u){
            uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rt=0,rn=0,imm=0;
            if(bender_fix114_ldr_x_uimm(q,&rt,&rn,&imm)&&rt==2u&&imm==0u){method_reg=rn;ldr2=p;}
        }
        if(ldr2==(size_t)-1||method_reg>=31u)continue;
        uintptr_t ga=0;size_t adrp=0,gldr=0;
        if(!bender_fix116_find_adrp_ldr_global(code,ldr2,method_reg,mod,&ga,&adrp,&gldr))continue;
        void*cell=NULL;const void*mi=bender_fix116_methodinfo_from_global(ga,&cell);
        if(!mi)continue;
        char desc[384]={0};const char*mn=NULL;uintptr_t mp=0;int slot=-1;
        bender_fix116_describe_method(mi,desc,sizeof(desc),&mn,&mp,&slot);
        if(!mn||!strstr(mn,"CheckAndFixDependenciesAsync"))continue;
        logmsg("[BENDER_NORD_ASYNC][DELEGATE] source=fix116 index=%u call_offset=0x%zx ldr_x2_offset=0x%zx method_reg=x%u adrp_offset=0x%zx global_ldr_offset=0x%zx method_global=%p method_cell=%p method_info=%p image=%s method=%s code=%p slot=%d interleaved_adrp_ldr=1 read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               found,off,ldr2,method_reg,adrp,gldr,(void*)ga,cell,mi,bender_fix15_image_name_2815(mi),desc[0]?desc:(mn?mn:"?"),(void*)mp,slot);
        found++;
        if(found>=2u)break;
    }
    logmsg("[BENDER_NORD_ASYNC][DELEGATES_DONE] source=fix116 count=%u expected=2 names=b__0,b__1 decoder=bounded-interleaved-adrp-ldr read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",found);
    return found;
}

static int bender_fix116_find_interface_global(uintptr_t code,size_t blr_off,Module*mod,uintptr_t*out_global,void**out_iface){
    size_t begin=blr_off>256u?blr_off-256u:0u;
    for(size_t aoff=begin;aoff+4u<blr_off;aoff+=4u){
        uint32_t a=0;memcpy(&a,(void*)(code+aoff),4u);unsigned reg=0;uintptr_t page=0;
        if(!bender_fix107_adrp(code+aoff,a,&reg,&page)||reg>=31u)continue;
        size_t maxj=aoff+32u;if(maxj>blr_off)maxj=blr_off;
        for(size_t j=aoff+4u;j+4u<=maxj;j+=4u){
            uint32_t b=0;memcpy(&b,(void*)(code+j),4u);unsigned rt=0,rn=0,imm=0;
            if(bender_fix114_ldr_x_uimm(b,&rt,&rn,&imm)&&rt==reg&&rn==reg){
                uintptr_t ga=page+(uintptr_t)imm;
                if(!bender_fix107_module_contains(mod,ga,sizeof(void*)))break;
                size_t maxk=j+40u;if(maxk>blr_off)maxk=blr_off;
                for(size_t k=j+4u;k+4u<=maxk;k+=4u){
                    uint32_t c=0;memcpy(&c,(void*)(code+k),4u);unsigned crt=0,crn=0,coff=0;
                    if(bender_fix114_ldr_x_uimm(c,&crt,&crn,&coff)&&crt==1u&&crn==reg&&coff==0u){
                        void*cell=*(void**)ga;if(!bender_fix114_nontrivial((uintptr_t)cell))break;
                        void*iface=*(void**)cell;if(!bender_fix114_nontrivial((uintptr_t)iface))break;
                        if(out_global)*out_global=ga;
                        if(out_iface)*out_iface=iface;
                        return 1;
                    }
                    if(bender_fix116_reg_written(c,reg))break;
                }
                break;
            }
            if(bender_fix116_reg_written(b,reg))break;
        }
    }
    return 0;
}

static void bender_fix116_trace_interface(uintptr_t code,size_t span,Module*mod){
    for(size_t off=0;off+4u<=span;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(code+off),4u);unsigned br=0;
        if(!bender_fix114_blr(ins,&br)||br!=8u)continue;
        /* The physical Unity 6 path moves the service receiver to x0 shortly before BLR. */
        unsigned receiver_reg=31u;size_t begin=off>48u?off-48u:0u;
        for(size_t p=begin;p<off;p+=4u){uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rd=0,rm=0;if(bender_fix115_mov_x(q,&rd,&rm)&&rd==0u)receiver_reg=rm;}
        if(receiver_reg>=31u)continue;
        void*receiver=NULL;void*rklass=NULL;uintptr_t owner_global=0;unsigned static_off=0,obj_off=0;
        int recv_ok=bender_fix115_find_static_receiver(code,off,mod,receiver_reg,&receiver,&rklass,&owner_global,&static_off,&obj_off);
        uintptr_t iface_global=0;void*iface=NULL;int iface_ok=bender_fix116_find_interface_global(code,off,mod,&iface_global,&iface);
        unsigned iface_slot=999u;
        size_t sb=off>160u?off-160u:0u;
        for(size_t p=sb;p<off;p+=4u){
            uint32_t q=0;memcpy(&q,(void*)(code+p),4u);
            if((q&UINT32_C(0x7f000000))==UINT32_C(0x11000000)){
                unsigned rd=q&31u,rn=(q>>5)&31u,imm=(q>>10)&0xfffu;
                if(rd==rn&&imm>0u&&imm<64u)iface_slot=imm;
            }
        }
        char rt[192]={0},it[192]={0};if(rklass)bender_fix106_type_name(rklass,rt,sizeof(rt));if(iface)bender_fix106_type_name(iface,it,sizeof(it));
        logmsg("[BENDER_NORD_ASYNC][INTERFACE] source=fix116 blr_offset=0x%zx receiver=%p receiver_class=%p receiver_type=%s interface_global=%p interface_class=%p interface_type=%s interface_slot=%u receiver_recovered=%d interface_recovered=%d search_back=256 read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               off,receiver,rklass,rt[0]?rt:"?",(void*)iface_global,iface,it[0]?it:"?",iface_slot,recv_ok,iface_ok);
        if(iface&&g_bender_fix106_class_get_methods){
            void*iter=NULL;const void*mi=NULL;unsigned idx=0;
            while((mi=g_bender_fix106_class_get_methods(iface,&iter))!=NULL&&idx<64u){
                char d[320]={0};const char*mn=NULL;uintptr_t mp=0;int sl=-1;bender_fix116_describe_method(mi,d,sizeof(d),&mn,&mp,&sl);
                logmsg("[BENDER_NORD_ASYNC][INTERFACE_METHOD] source=fix116 index=%u target_index=%u target_match=%d method_info=%p image=%s method=%s code=%p slot=%d read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
                       idx,iface_slot,(idx==iface_slot),mi,bender_fix15_image_name_2815(mi),d[0]?d:(mn?mn:"?"),(void*)mp,sl);
                idx++;
            }
        }
        if(!recv_ok||!iface_ok||iface_slot>=64u)return;
        /* Recover class interface-offset layout from the same AArch64 lookup sequence. */
        unsigned count_off=0,offsets_off=0,vtable_base=0;int got_count=0,got_offsets=0,got_vtable=0;
        for(size_t p=sb;p<off;p+=4u){
            uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rt0=0,rn=0,imm=0;
            if(bender_fix114_ldrh_w_uimm(q,&rt0,&rn,&imm)&&rn==8u){count_off=imm;got_count=1;}
            if(bender_fix114_ldr_x_uimm(q,&rt0,&rn,&imm)&&rn==8u&&imm>0u){offsets_off=imm;got_offsets=1;}
            unsigned rd=0;if(bender_fix114_add_x_imm(q,&rd,&rn,&imm)&&rn==8u&&imm>0u){vtable_base=imm;got_vtable=1;}
        }
        if(!got_count||!got_offsets||!got_vtable){logmsg("[BENDER_NORD_ASYNC][DISPATCH] source=fix116 recovered=0 reason=class-interface-layout-not-recovered read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");return;}
        uint16_t count=*(uint16_t*)((unsigned char*)rklass+count_off);void*pairs=*(void**)((unsigned char*)rklass+offsets_off);int base_slot=-1;
        if(bender_fix114_nontrivial((uintptr_t)pairs)&&count<1024u){for(unsigned i=0;i<count;i++){unsigned char*e=(unsigned char*)pairs+i*16u;void*ei=*(void**)e;int32_t bo=0;memcpy(&bo,e+8u,sizeof(bo));if(ei==iface){base_slot=bo;break;}}}
        if(base_slot<0){logmsg("[BENDER_NORD_ASYNC][DISPATCH] source=fix116 recovered=0 reason=interface-base-slot-not-found read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");return;}
        unsigned final_slot=(unsigned)base_slot+iface_slot;unsigned char*entry=(unsigned char*)rklass+vtable_base+(size_t)final_slot*16u;
        uintptr_t method_ptr=*(uintptr_t*)entry;const void*method_info=*(const void**)(entry+8u);char d[384]={0};const char*mn=NULL;uintptr_t mp=0;int sl=-1;bender_fix116_describe_method(method_info,d,sizeof(d),&mn,&mp,&sl);
        logmsg("[BENDER_NORD_ASYNC][DISPATCH] source=fix116 receiver_type=%s interface_type=%s interface_slot=%u base_slot=%d final_slot=%u method_ptr=%p method_info=%p image=%s method=%s method_slot=%d read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               rt[0]?rt:"?",it[0]?it:"?",iface_slot,base_slot,final_slot,(void*)method_ptr,method_info,method_info?bender_fix15_image_name_2815(method_info):"?",d[0]?d:(mn?mn:"?"),sl);
        return;
    }
    logmsg("[BENDER_NORD_ASYNC][INTERFACE] source=fix116 recovered=0 reason=blr-shape-not-found read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
}

static void bender_fix116_trace_tail_method(uintptr_t code,size_t span,Module*mod){
    for(size_t off=0;off+4u<=span;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(code+off),4u);
        if(!bender_fix115_b(ins))continue;
        uintptr_t target=bender_fix115_imm26_target(code+off,ins);
        if(target>=code&&target<code+span)continue;
        unsigned method_reg=31u;size_t ldr2=(size_t)-1;size_t begin=off>72u?off-72u:0u;
        for(size_t p=begin;p<off;p+=4u){uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rt=0,rn=0,imm=0;if(bender_fix114_ldr_x_uimm(q,&rt,&rn,&imm)&&rt==2u&&imm==0u){method_reg=rn;ldr2=p;}}
        if(ldr2==(size_t)-1||method_reg>=31u)continue;
        uintptr_t ga=0;size_t adrp=0,gldr=0;if(!bender_fix116_find_adrp_ldr_global(code,ldr2,method_reg,mod,&ga,&adrp,&gldr))continue;
        void*cell=NULL;const void*mi=bender_fix116_methodinfo_from_global(ga,&cell);if(!mi)continue;
        char d[384]={0};const char*mn=NULL;uintptr_t mp=0;int sl=-1;bender_fix116_describe_method(mi,d,sizeof(d),&mn,&mp,&sl);
        logmsg("[BENDER_NORD_ASYNC][TAIL_METHOD] source=fix116 branch_offset=0x%zx branch_target=%p ldr_x2_offset=0x%zx method_reg=x%u adrp_offset=0x%zx global_ldr_offset=0x%zx method_global=%p method_cell=%p method_info=%p image=%s method=%s code=%p slot=%d read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               off,(void*)target,ldr2,method_reg,adrp,gldr,(void*)ga,cell,mi,bender_fix15_image_name_2815(mi),d[0]?d:(mn?mn:"?"),(void*)mp,sl);
        return;
    }
    logmsg("[BENDER_NORD_ASYNC][TAIL_METHOD] source=fix116 recovered=0 reason=tail-methodinfo-pattern-not-found read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
}

static void bender_fix116_tick(unsigned frame){
    if(g_bender_fix116_done||frame<121u||!g_bender_fix16_map_ready_2816)return;
    if(!g_bender_fix115_nord_code||!g_bender_fix115_nord_mi)return;
    Module*mod=bender_module_for_runtime_addr(g_bender_fix115_nord_code);if(!mod||strcmp(mod->name,"libil2cpp.so"))return;
    size_t span=bender_fix16_method_span_2816(g_bender_fix115_nord_code);if(span<64u||span>4096u)span=1024u;
    g_bender_fix116_done=1;
    char d[384]={0};const char*mn=NULL;uintptr_t mp=0;int sl=-1;bender_fix116_describe_method(g_bender_fix115_nord_mi,d,sizeof(d),&mn,&mp,&sl);
    logmsg("[BENDER_NORD_ASYNC][SITE] source=fix116 frame=%u method_info=%p image=%s method=%s code=%p module=%s offset=0x%" PRIxPTR " span=%zu decoder=interleaved-adrp-ldr-window physical_fix115_lambdas=0 physical_fix115_interface=nord-ironsource read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           frame,g_bender_fix115_nord_mi,bender_fix15_image_name_2815(g_bender_fix115_nord_mi),d[0]?d:(mn?mn:"?"),(void*)g_bender_fix115_nord_code,mod->name,g_bender_fix115_nord_code-mod->bias,span);
    unsigned delegates=bender_fix116_trace_delegates(g_bender_fix115_nord_code,span,mod);
    bender_fix116_trace_interface(g_bender_fix115_nord_code,span,mod);
    bender_fix116_trace_tail_method(g_bender_fix115_nord_code,span,mod);
    logmsg("[BENDER_NORD_ASYNC][DONE] source=fix116 frame=%u delegates=%u interpretation=resolve-async-contract-before-any-functional-fix next=physical-log-decides-missing-continuation read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",frame,delegates);
}


/* BENDER V2.7.166 — FIX117 read-only Firebase dependency Task frontier trace. */
static int g_bender_fix117_done;

static void bender_fix117_describe_addr(const char*tag,uintptr_t addr){
    const BenderFix16MethodMap2816*hit=bender_fix16_find_addr_2816(addr);
    uintptr_t followed=addr;
    if(!hit){followed=bender_fix16_follow_direct_branch_2816(addr);if(followed!=addr)hit=bender_fix16_find_addr_2816(followed);}
    Module*m=bender_module_for_runtime_addr(addr);
    if(hit){
        char d[384]={0};const char*mn=NULL;uintptr_t mp=0;int sl=-1;
        bender_fix116_describe_method(hit->method,d,sizeof(d),&mn,&mp,&sl);
        logmsg("[BENDER_FIREBASE_DEP_TASK][ADDR] source=fix117 tag=%s addr=%p followed=%p module=%s module_offset=0x%" PRIxPTR " method_map=1 method_info=%p image=%s method=%s code=%p slot=%d read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               tag?tag:"?",(void*)addr,(void*)followed,m?m->name:"?",m?addr-m->bias:0,hit->method,bender_fix15_image_name_2815(hit->method),d[0]?d:(mn?mn:"?"),(void*)mp,sl);
    }else{
        logmsg("[BENDER_FIREBASE_DEP_TASK][ADDR] source=fix117 tag=%s addr=%p followed=%p module=%s module_offset=0x%" PRIxPTR " method_map=0 method_info=(nil) image=? method=? read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               tag?tag:"?",(void*)addr,(void*)followed,m?m->name:"?",m?addr-m->bias:0);
    }
}

static __attribute__((unused)) const void*bender_fix117_candidate_methodinfo(uintptr_t code,size_t edge_off,unsigned arg_reg,Module*mod,uintptr_t*out_ga,size_t*out_arg_ldr){
    if(!code||!mod||arg_reg>7u)return NULL;
    size_t begin=edge_off>80u?edge_off-80u:0u;
    size_t best=(size_t)-1;unsigned src=31u;
    for(size_t p=begin;p<edge_off;p+=4u){
        uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rt=0,rn=0,imm=0;
        if(bender_fix114_ldr_x_uimm(q,&rt,&rn,&imm)&&rt==arg_reg&&imm==0u){best=p;src=rn;}
    }
    if(best==(size_t)-1||src>=31u)return NULL;
    uintptr_t ga=0;size_t adrp=0,gldr=0;
    if(!bender_fix116_find_adrp_ldr_global(code,best,src,mod,&ga,&adrp,&gldr))return NULL;
    (void)adrp;(void)gldr;
    void*cell=NULL;const void*mi=bender_fix116_methodinfo_from_global(ga,&cell);
    if(!mi)return NULL;
    if(out_ga)*out_ga=ga;
    if(out_arg_ldr)*out_arg_ldr=best;
    return mi;
}

static void bender_fix117_trace_edges(const char*tag,uintptr_t code,size_t span,Module*mod){
    if(!code||!span||!mod)return;
    unsigned logged=0;
    for(size_t off=0;off+4u<=span&&logged<64u;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(code+off),4u);
        int is_bl=bender_fix115_bl(ins),is_b=bender_fix115_b(ins);
        if(!is_bl&&!is_b)continue;
        uintptr_t target=bender_fix115_imm26_target(code+off,ins);
        if(is_b&&target>=code&&target<code+span)continue;
        const BenderFix16MethodMap2816*hit=bender_fix16_find_addr_2816(target);
        uintptr_t followed=target;if(!hit){followed=bender_fix16_follow_direct_branch_2816(target);if(followed!=target)hit=bender_fix16_find_addr_2816(followed);}
        Module*tm=bender_module_for_runtime_addr(target);
        char d[384]={0};const char*mn=NULL;uintptr_t mp=0;int sl=-1;
        if(hit)bender_fix116_describe_method(hit->method,d,sizeof(d),&mn,&mp,&sl);
        logmsg("[BENDER_FIREBASE_DEP_TASK][EDGE] source=fix117 tag=%s edge=%u kind=%s offset=0x%zx pc=%p target=%p followed=%p module=%s module_offset=0x%" PRIxPTR " method_map=%d method_info=%p image=%s method=%s read_only=1 observed_execution=0 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               tag?tag:"?",logged,is_bl?"BL":"B",off,(void*)(code+off),(void*)target,(void*)followed,tm?tm->name:"?",tm?target-tm->bias:0,hit?1:0,hit?hit->method:NULL,hit?bender_fix15_image_name_2815(hit->method):"?",hit?(d[0]?d:(mn?mn:"?")):"?");
        /* FIX118: physical FIX117 crashed after EDGE 4 while a speculative
           nested pointer was being treated as MethodInfo. Keep the proven direct edge
           trace, but do not dereference unvalidated MethodInfo candidates. */
        logmsg("[BENDER_FIREBASE_DEP_TASK][METHODINFO_GUARD] source=fix118 tag=%s edge=%u offset=0x%zx action=skip-unvalidated-methodinfo-candidate-deref physical_regression=fix117-frame122-sigsegv read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               tag?tag:"?",logged,off);
        logged++;
    }
    logmsg("[BENDER_FIREBASE_DEP_TASK][EDGES_DONE] source=fix117 tag=%s count=%u span=%zu read_only=1 observed_execution=0 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",tag?tag:"?",logged,span);
}

static unsigned bender_fix117_collect_delegates(uintptr_t code,size_t span,Module*mod,uintptr_t out_code[2],const void*out_mi[2]){
    unsigned found=0;out_code[0]=out_code[1]=0;out_mi[0]=out_mi[1]=NULL;
    for(size_t off=0;off+4u<=span&&found<2u;off+=4u){
        uint32_t ins=0;memcpy(&ins,(void*)(code+off),4u);if(!bender_fix115_bl(ins))continue;
        unsigned method_reg=31u;size_t ldr2=(size_t)-1;size_t begin=off>40u?off-40u:0u;
        for(size_t p=begin;p<off;p+=4u){uint32_t q=0;memcpy(&q,(void*)(code+p),4u);unsigned rt=0,rn=0,imm=0;if(bender_fix114_ldr_x_uimm(q,&rt,&rn,&imm)&&rt==2u&&imm==0u){method_reg=rn;ldr2=p;}}
        if(ldr2==(size_t)-1||method_reg>=31u)continue;
        uintptr_t ga=0;size_t a=0,l=0;if(!bender_fix116_find_adrp_ldr_global(code,ldr2,method_reg,mod,&ga,&a,&l))continue;
        void*cell=NULL;const void*mi=bender_fix116_methodinfo_from_global(ga,&cell);if(!mi)continue;
        char d[384]={0};const char*mn=NULL;uintptr_t mp=0;int sl=-1;bender_fix116_describe_method(mi,d,sizeof(d),&mn,&mp,&sl);
        if(!mn||!strstr(mn,"CheckAndFixDependenciesAsync"))continue;
        out_code[found]=mp;out_mi[found]=mi;
        logmsg("[BENDER_FIREBASE_DEP_TASK][DELEGATE] source=fix117 index=%u method_info=%p image=%s method=%s code=%p slot=%d read_only=1 observed_execution=0 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",found,mi,bender_fix15_image_name_2815(mi),d[0]?d:mn,(void*)mp,sl);
        found++;
    }
    return found;
}

static void bender_fix117_tick(unsigned frame){
    if(g_bender_fix117_done||frame<122u||!g_bender_fix16_map_ready_2816)return;
    if(!g_bender_fix115_nord_code||!g_bender_fix115_nord_mi)return;
    Module*mod=bender_module_for_runtime_addr(g_bender_fix115_nord_code);if(!mod||strcmp(mod->name,"libil2cpp.so"))return;
    size_t nord_span=bender_fix16_method_span_2816(g_bender_fix115_nord_code);if(nord_span<0x224u||nord_span>4096u)nord_span=1024u;
    g_bender_fix117_done=1;

    logmsg("[BENDER_FIREBASE_DEP_TASK][SITE] source=fix117 frame=%u nord_code=%p nord_offset=0x%" PRIxPTR " contract_call_offset=0x1dc continuation_call_offset=0x220 physical_fix116_delegate0=b__0 physical_fix116_delegate1=b__1 physical_fix116_interface=INordIronSource.add_OnImpressionDataReadyEvent physical_fix116_tail=Firebase.Extensions.TaskExtension.ContinueWithOnMainThread read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           frame,(void*)g_bender_fix115_nord_code,g_bender_fix115_nord_code-mod->bias);

    uintptr_t delegates[2]={0,0};const void*dmi[2]={NULL,NULL};unsigned dc=bender_fix117_collect_delegates(g_bender_fix115_nord_code,nord_span,mod,delegates,dmi);
    for(unsigned i=0;i<dc;i++){
        char tag[32];snprintf(tag,sizeof(tag),"delegate_b__%u",i);
        bender_fix117_describe_addr(tag,delegates[i]);
        size_t ds=bender_fix16_method_span_2816(delegates[i]);if(ds<4u||ds>768u)ds=256u;
        bender_fix117_trace_edges(tag,delegates[i],ds,mod);
    }

    size_t dep_off=0x1dcu;uint32_t dep_ins=0;memcpy(&dep_ins,(void*)(g_bender_fix115_nord_code+dep_off),4u);
    if(bender_fix115_bl(dep_ins)){
        uintptr_t dep_target=bender_fix115_imm26_target(g_bender_fix115_nord_code+dep_off,dep_ins);
        bender_fix117_describe_addr("dependency_async_target",dep_target);
        /* Physical disassembly shows this helper tail-exits before +0x140.  Keep the
           diagnostic bounded to that contract body even when it is absent from the
           managed MethodInfo map (generic/PInvoke/helper bodies can be unmapped). */
        bender_fix117_trace_edges("dependency_async_target",dep_target,0x140u,mod);
    }else{
        logmsg("[BENDER_FIREBASE_DEP_TASK][TARGET] source=fix117 recovered=0 reason=contract-offset-not-BL offset=0x1dc instruction=0x%08x read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",dep_ins);
    }

    logmsg("[BENDER_FIREBASE_DEP_TASK][JNI_STATE] source=fix117 frame=%u nativeOnResult=%p java_task_created=%lu java_task_completed=%lu java_task_failed=%lu interpretation=physical-fix116-path-did-not-use-old-java-task-bridge-unless-counters-change read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           frame,(void*)g_firebase_jni_result_fn_2757,g_firebase_task_created_2757,g_firebase_task_completed_2757,g_firebase_task_failed_2757);
    logmsg("[BENDER_FIREBASE_DEP_TASK][DONE] source=fix117 frame=%u delegates=%u interpretation=resolve-plus-correlate-real-dependency-task-frontier next=physical-log-decides-whether-task-construction-result-or-main-thread-continuation-is-missing read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",frame,dc);
}


/* BENDER V2.7.168 — FIX119 validated MethodInfo reference trace. */
static int g_bender_fix119_done;

static const BenderFix16MethodMap2816*bender_fix119_find_validated_methodinfo(const void*candidate){
    if(!candidate||!g_bender_fix16_map_2816||!g_bender_fix16_map_count_2816)return NULL;
    for(size_t i=0;i<g_bender_fix16_map_count_2816;i++){
        if(g_bender_fix16_map_2816[i].method==candidate)return &g_bender_fix16_map_2816[i];
    }
    return NULL;
}

static int bender_fix119_read_validated_methodinfo_global(uintptr_t ga,Module*owner,const void**out_mi,void**out_cell,const BenderFix16MethodMap2816**out_hit){
    if(out_mi)*out_mi=NULL;
    if(out_cell)*out_cell=NULL;
    if(out_hit)*out_hit=NULL;
    if(!ga||!owner||!bender_fix107_module_contains(owner,ga,sizeof(void*)))return 0;
    uintptr_t cell=0;memcpy(&cell,(const void*)ga,sizeof(cell));
    if(!bender_fix114_nontrivial(cell))return 0;
    Module*cm=bender_module_for_runtime_addr(cell);if(!cm||!cm->reserve)return 0;
    uintptr_t lo=(uintptr_t)cm->reserve,hi=lo+cm->reserve_size;
    if(cell<lo||cell+sizeof(void*)<cell||cell+sizeof(void*)>hi)return 0;
    const void*candidate=NULL;memcpy(&candidate,(const void*)cell,sizeof(candidate));
    const BenderFix16MethodMap2816*hit=bender_fix119_find_validated_methodinfo(candidate);
    if(!hit)return 0;
    if(out_mi)*out_mi=candidate;
    if(out_cell)*out_cell=(void*)cell;
    if(out_hit)*out_hit=hit;
    return 1;
}

static unsigned bender_fix119_trace_validated_globals(const char*tag,uintptr_t code,size_t span,Module*mod){
    if(!tag||!code||!span||!mod)return 0u;
    uintptr_t seen[96];unsigned seen_n=0,found=0;
    memset(seen,0,sizeof(seen));
    for(size_t off=0;off+4u<=span;off+=4u){
        uint32_t a=0;memcpy(&a,(const void*)(code+off),sizeof(a));
        unsigned reg=0;uintptr_t page=0;
        if(!bender_fix107_adrp(code+off,a,&reg,&page)||reg>=31u)continue;
        size_t maxj=off+32u;if(maxj>span)maxj=span;
        for(size_t j=off+4u;j+4u<=maxj;j+=4u){
            uint32_t b=0;memcpy(&b,(const void*)(code+j),sizeof(b));
            unsigned rt=0,rn=0,imm=0;
            if(bender_fix114_ldr_x_uimm(b,&rt,&rn,&imm)&&rt==reg&&rn==reg){
                uintptr_t ga=page+(uintptr_t)imm;
                int duplicate=0;for(unsigned k=0;k<seen_n;k++)if(seen[k]==ga){duplicate=1;break;}
                if(!duplicate&&seen_n<96u)seen[seen_n++]=ga;
                if(!duplicate){
                    const void*candidate=NULL;void*cell=NULL;const BenderFix16MethodMap2816*hit=NULL;
                    if(bender_fix119_read_validated_methodinfo_global(ga,mod,&candidate,&cell,&hit)){
                        char d[384]={0};const char*mn=NULL;uintptr_t mp=0;int sl=-1;
                        bender_fix116_describe_method(candidate,d,sizeof(d),&mn,&mp,&sl);
                        logmsg("[BENDER_FIREBASE_VALIDATED_REF][METHODINFO] source=fix119 tag=%s index=%u adrp_offset=0x%zx ldr_offset=0x%zx reg=x%u method_global=%p method_cell=%p method_info=%p map_code=%p image=%s method=%s code=%p slot=%d validation=exact-fix16-methodinfo-pointer-membership cell_module=%s read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
                               tag,found,off,j,reg,(void*)ga,cell,candidate,(void*)(hit?hit->addr:0),bender_fix15_image_name_2815(candidate),d[0]?d:(mn?mn:"?"),(void*)mp,sl,bender_module_for_runtime_addr((uintptr_t)cell)?bender_module_for_runtime_addr((uintptr_t)cell)->name:"?");
                        found++;
                    }
                }
                break;
            }
            if(bender_fix116_reg_written(b,reg))break;
        }
    }
    logmsg("[BENDER_FIREBASE_VALIDATED_REF][SCAN_DONE] source=fix119 tag=%s refs=%u span=%zu globals_seen=%u validation=fix16-identity-only read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",tag,found,span,seen_n);
    return found;
}

static void bender_fix119_tick(unsigned frame){
    if(g_bender_fix119_done||frame<123u||!g_bender_fix16_map_ready_2816)return;
    if(!g_bender_fix117_done||!g_bender_fix115_nord_code)return;
    Module*mod=bender_module_for_runtime_addr(g_bender_fix115_nord_code);if(!mod||strcmp(mod->name,"libil2cpp.so"))return;
    g_bender_fix119_done=1;

    size_t dep_off=0x1dcu;uint32_t dep_ins=0;memcpy(&dep_ins,(const void*)(g_bender_fix115_nord_code+dep_off),sizeof(dep_ins));
    uintptr_t dep_target=0;unsigned total=0;
    if(bender_fix115_bl(dep_ins)){
        dep_target=bender_fix115_imm26_target(g_bender_fix115_nord_code+dep_off,dep_ins);
        Module*dm=bender_module_for_runtime_addr(dep_target);
        logmsg("[BENDER_FIREBASE_VALIDATED_REF][TARGET] source=fix119 frame=%u tag=dependency_async_target call_offset=0x1dc addr=%p module=%s module_offset=0x%" PRIxPTR " physical_fix118_addr=libil2cpp+0x5ad417c physical_fix118_b1_observed=0 java_task_counters=0/0/0 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               frame,(void*)dep_target,dm?dm->name:"?",dm?dep_target-dm->bias:0);
        total+=bender_fix119_trace_validated_globals("dependency_async_target",dep_target,0x140u,mod);
    }else{
        logmsg("[BENDER_FIREBASE_VALIDATED_REF][TARGET] source=fix119 frame=%u recovered=0 reason=contract-offset-not-BL offset=0x1dc instruction=0x%08x read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",frame,dep_ins);
    }

    uintptr_t delegates[2]={0,0};const void*dmi[2]={NULL,NULL};
    size_t nord_span=bender_fix16_method_span_2816(g_bender_fix115_nord_code);if(nord_span<0x224u||nord_span>4096u)nord_span=1024u;
    unsigned dc=bender_fix117_collect_delegates(g_bender_fix115_nord_code,nord_span,mod,delegates,dmi);
    for(unsigned i=0;i<dc;i++){
        char tag[32];snprintf(tag,sizeof(tag),"delegate_b__%u",i);
        size_t ds=bender_fix16_method_span_2816(delegates[i]);if(ds<4u||ds>256u)ds=256u;
        total+=bender_fix119_trace_validated_globals(tag,delegates[i],ds,mod);
    }

    logmsg("[BENDER_FIREBASE_VALIDATED_REF][DONE] source=fix119 frame=%u dependency_target=%p delegates=%u validated_refs=%u interpretation=validated-methodinfo-references-only-next-physical-log-identifies-real-firebase-operation read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",frame,(void*)dep_target,dc,total);
}


/* BENDER V2.7.169 — FIX120 exact Firebase async MethodInfo resolver. */
typedef uint32_t (*BenderFix120MethodGetParamCount)(const void*);
typedef const void *(*BenderFix120MethodGetParam)(const void*,uint32_t);
typedef const void *(*BenderFix120MethodGetReturnType)(const void*);
typedef char *(*BenderFix120TypeGetName)(const void*);
typedef void (*BenderFix120Il2CppFree)(void*);

static BenderFix120MethodGetParamCount g_bender_fix120_method_get_param_count;
static BenderFix120MethodGetParam g_bender_fix120_method_get_param;
static BenderFix120MethodGetReturnType g_bender_fix120_method_get_return_type;
static BenderFix120TypeGetName g_bender_fix120_type_get_name;
static BenderFix120Il2CppFree g_bender_fix120_free;
static int g_bender_fix120_done;

static void bender_fix120_resolve_signature_api(Module*mod){
    if(!mod)return;
#define BENDER_FIX120_RESOLVE(field,type,name) do{if(!(field)){uintptr_t p=module_symbol(mod,name);if(p)(field)=(type)p;}}while(0)
    BENDER_FIX120_RESOLVE(g_bender_fix120_method_get_param_count,BenderFix120MethodGetParamCount,"il2cpp_method_get_param_count");
    BENDER_FIX120_RESOLVE(g_bender_fix120_method_get_param,BenderFix120MethodGetParam,"il2cpp_method_get_param");
    BENDER_FIX120_RESOLVE(g_bender_fix120_method_get_return_type,BenderFix120MethodGetReturnType,"il2cpp_method_get_return_type");
    BENDER_FIX120_RESOLVE(g_bender_fix120_type_get_name,BenderFix120TypeGetName,"il2cpp_type_get_name");
    BENDER_FIX120_RESOLVE(g_bender_fix120_free,BenderFix120Il2CppFree,"il2cpp_free");
#undef BENDER_FIX120_RESOLVE
}

static void bender_fix120_type_name(const void*type,char*out,size_t cap){
    if(!out||!cap)return;
    out[0]='\0';
    if(!type||!g_bender_fix120_type_get_name){bender_strlcpy(out,"?",cap);return;}
    char*n=g_bender_fix120_type_get_name(type);
    if(n&&*n)bender_strlcpy(out,n,cap);else bender_strlcpy(out,"?",cap);
    if(n&&g_bender_fix120_free)g_bender_fix120_free(n);
}

static void bender_fix120_signature(const void*mi,char*out,size_t cap){
    if(!out||!cap)return;
    out[0]='\0';
    if(!mi){bender_strlcpy(out,"return=? params=?",cap);return;}
    const void*rt=g_bender_fix120_method_get_return_type?g_bender_fix120_method_get_return_type(mi):NULL;
    char rtn[160]={0};bender_fix120_type_name(rt,rtn,sizeof(rtn));
    uint32_t pc=g_bender_fix120_method_get_param_count?g_bender_fix120_method_get_param_count(mi):0u;
    size_t used=0;int w=snprintf(out,cap,"return=%s param_count=%u params=[",rtn[0]?rtn:"?",pc);
    if(w<0)return;
    if((size_t)w>=cap){out[cap-1]='\0';return;}
    used=(size_t)w;
    for(uint32_t i=0;i<pc&&i<16u;i++){
        const void*pt=g_bender_fix120_method_get_param?g_bender_fix120_method_get_param(mi,i):NULL;
        char ptn[160]={0};bender_fix120_type_name(pt,ptn,sizeof(ptn));
        w=snprintf(out+used,cap-used,"%s%s",i?",":"",ptn[0]?ptn:"?");
        if(w<0)break;
        if((size_t)w>=cap-used){used=cap-1u;break;}
        used+=(size_t)w;
    }
    if(used<cap-1u){w=snprintf(out+used,cap-used,"]");(void)w;}
}

typedef struct { const char*tag; uintptr_t addr; uintptr_t module_offset; } BenderFix120Target;

static int bender_fix120_method_identity(const void*mi,const char**ns_out,const char**cn_out,const char**mn_out){
    if(ns_out)*ns_out="?";
    if(cn_out)*cn_out="?";
    if(mn_out)*mn_out="?";
    if(!mi||!g_bender_method_get_name_2812)return 0;
    const char*mn=g_bender_method_get_name_2812(mi);if(!mn)mn="?";
    void*klass=g_bender_method_get_class_2812?g_bender_method_get_class_2812(mi):NULL;
    const char*cn=(klass&&g_bender_class_get_name_2812)?g_bender_class_get_name_2812(klass):"?";
    const char*ns=(klass&&g_bender_class_get_namespace_2812)?g_bender_class_get_namespace_2812(klass):"?";
    if(!cn)cn="?";
    if(!ns)ns="?";
    if(ns_out)*ns_out=ns;
    if(cn_out)*cn_out=cn;
    if(mn_out)*mn_out=mn;
    return 1;
}

static int bender_fix120_is_firebaseapp_dependency(const char*ns,const char*cn,const char*mn){
    if(!ns||!cn||!mn)return 0;
    if(strcmp(ns,"Firebase")||strcmp(cn,"FirebaseApp"))return 0;
    return !strcmp(mn,"CheckAndFixDependenciesAsync")||!strcmp(mn,"FixDependenciesAsync");
}

static void bender_fix120_log_method(const char*marker,const char*tag,const void*mi,uintptr_t ptr,int slot,size_t ai,size_t ci,unsigned long ordinal,const char*match_kind){
    const char*ns="?",*cn="?",*mn="?";bender_fix120_method_identity(mi,&ns,&cn,&mn);
    char sig[720]={0};bender_fix120_signature(mi,sig,sizeof(sig));
    logmsg("%s source=fix120 tag=%s assembly_index=%zu class_index=%zu method_ordinal=%lu image=%s namespace=%s class=%s method=%s method_info=%p code=%p slot=%d %s match_kind=%s read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           marker,tag?tag:"?",ai,ci,ordinal,bender_fix15_image_name_2815(mi),ns,cn,mn,mi,(void*)ptr,slot,sig[0]?sig:"return=? param_count=? params=?",match_kind?match_kind:"diagnostic");
}

static void bender_fix120_enumerate(Module*mod,BenderFix120Target*targets,size_t target_count,unsigned long*methods_seen,unsigned long*ptr_valid,unsigned*matches,unsigned*firebase_focus){
    if(methods_seen)*methods_seen=0;
    if(ptr_valid)*ptr_valid=0;
    if(matches)*matches=0;
    if(firebase_focus)*firebase_focus=0;
    bender_fix16_resolve_api_2816();bender_fix13_resolve_method_api_2813(mod);bender_fix120_resolve_signature_api(mod);
    if(!g_bender_fix16_domain_get_2816||!g_bender_fix16_domain_get_assemblies_2816||!g_bender_fix16_assembly_get_image_2816||!g_bender_fix16_image_get_class_count_2816||!g_bender_fix16_image_get_class_2816||!g_bender_fix16_class_get_methods_2816){
        logmsg("[BENDER_FIREBASE_EXACT_METHOD][ENUM] source=fix120 ready=0 reason=il2cpp-enumeration-api-missing read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");return;
    }
    void*domain=g_bender_fix16_domain_get_2816();size_t ac=0;const void**assemblies=domain?g_bender_fix16_domain_get_assemblies_2816(domain,&ac):NULL;
    if(!assemblies||!ac){logmsg("[BENDER_FIREBASE_EXACT_METHOD][ENUM] source=fix120 ready=0 reason=no-assemblies domain=%p assemblies=%p count=%zu read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",domain,(void*)assemblies,ac);return;}
    unsigned long ordinal=0;
    for(size_t ai=0;ai<ac;ai++){
        void*image=g_bender_fix16_assembly_get_image_2816(assemblies[ai]);if(!image)continue;
        size_t cc=g_bender_fix16_image_get_class_count_2816(image);
        for(size_t ci=0;ci<cc;ci++){
            void*klass=g_bender_fix16_image_get_class_2816(image,ci);if(!klass)continue;
            void*iter=NULL;const void*mi=NULL;
            while((mi=g_bender_fix16_class_get_methods_2816(klass,&iter))!=NULL){
                ordinal++;if(methods_seen)(*methods_seen)++;
                int slot=-1;uintptr_t ptr=bender_fix16_method_ptr_2816(mi,&slot);if(!ptr)continue;if(ptr_valid)(*ptr_valid)++;
                const char*ns="?",*cn="?",*mn="?";bender_fix120_method_identity(mi,&ns,&cn,&mn);
                if(bender_fix120_is_firebaseapp_dependency(ns,cn,mn)){
                    if(firebase_focus)(*firebase_focus)++;
                    bender_fix120_log_method("[BENDER_FIREBASEAPP_METHOD]","Firebase.FirebaseApp",mi,ptr,slot,ai,ci,ordinal,"focused-metadata-enumeration");
                }
                for(size_t ti=0;ti<target_count;ti++){
                    uintptr_t target=targets[ti].addr;
                    if(ptr==target){
                        if(matches)(*matches)++;
                        bender_fix120_log_method("[BENDER_FIREBASE_EXACT_METHOD][MATCH]",targets[ti].tag,mi,ptr,slot,ai,ci,ordinal,"exact-code-pointer");
                    }
                }
            }
        }
    }
    logmsg("[BENDER_FIREBASE_EXACT_METHOD][ENUM] source=fix120 ready=1 assemblies=%zu methods_seen=%lu executable_method_ptrs=%lu matches=%u firebaseapp_dependency_methods=%u fix16_storage_cap_independent=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           ac,methods_seen?*methods_seen:0ul,ptr_valid?*ptr_valid:0ul,matches?*matches:0u,firebase_focus?*firebase_focus:0u);
}

static void bender_fix120_tick(unsigned frame){
    if(g_bender_fix120_done||frame<124u||!g_bender_fix119_done||!g_bender_fix115_nord_code)return;
    Module*mod=bender_module_for_runtime_addr(g_bender_fix115_nord_code);if(!mod||strcmp(mod->name,"libil2cpp.so"))return;
    g_bender_fix120_done=1;

    size_t dep_off=0x1dcu;uint32_t dep_ins=0;memcpy(&dep_ins,(const void*)(g_bender_fix115_nord_code+dep_off),sizeof(dep_ins));
    uintptr_t dep_target=0;if(bender_fix115_bl(dep_ins))dep_target=bender_fix115_imm26_target(g_bender_fix115_nord_code+dep_off,dep_ins);
    BenderFix120Target targets[]={
        {"dependency_runtime_plus1dc",dep_target,dep_target?dep_target-mod->bias:0},
        {"physical_dependency_0x5ad417c",mod->bias+(uintptr_t)0x5ad417cu,(uintptr_t)0x5ad417cu},
        {"adjacent_0x5ad3fe0",mod->bias+(uintptr_t)0x5ad3fe0u,(uintptr_t)0x5ad3fe0u},
        {"adjacent_0x5b2de44",mod->bias+(uintptr_t)0x5b2de44u,(uintptr_t)0x5b2de44u},
        {"adjacent_0x6167408",mod->bias+(uintptr_t)0x6167408u,(uintptr_t)0x6167408u},
        {"adjacent_0x5ad08ec",mod->bias+(uintptr_t)0x5ad08ecu,(uintptr_t)0x5ad08ecu},
        {"adjacent_0x800f91c",mod->bias+(uintptr_t)0x800f91cu,(uintptr_t)0x800f91cu},
    };
    const size_t tc=sizeof(targets)/sizeof(targets[0]);
    for(size_t i=0;i<tc;i++){
        logmsg("[BENDER_FIREBASE_EXACT_METHOD][TARGET] source=fix120 frame=%u tag=%s addr=%p module=%s module_offset=0x%" PRIxPTR " exact_pointer_match_only=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               frame,targets[i].tag,(void*)targets[i].addr,mod->name,targets[i].module_offset);
    }
    unsigned long methods_seen=0,ptr_valid=0;unsigned matches=0,firebase_focus=0;
    bender_fix120_enumerate(mod,targets,tc,&methods_seen,&ptr_valid,&matches,&firebase_focus);
    logmsg("[BENDER_FIX120][DONE] source=fix120 frame=%u dependency_target=%p dependency_module_offset=0x%" PRIxPTR " methods_seen=%lu executable_method_ptrs=%lu exact_matches=%u firebaseapp_dependency_methods=%u next=physical-log-identifies-exact-async-operation-or-proves-target-is-native-helper read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           frame,(void*)dep_target,dep_target?dep_target-mod->bias:0,methods_seen,ptr_valid,matches,firebase_focus);
}


/* BENDER V2.7.170 — FIX121 read-only Firebase async call-graph resolver. */
#define BENDER_FIX121_MAX_ROOTS 8u
#define BENDER_FIX121_MAX_EDGES 192u
#define BENDER_FIX121_MAX_REFS 192u

typedef struct {
    const char *tag;
    uintptr_t code;
    size_t span;
    const void *method_info;
} BenderFix121Root;

typedef struct {
    const char *root_tag;
    size_t root_offset;
    const char *kind;
    uintptr_t target;
    unsigned matched;
} BenderFix121Edge;

typedef struct {
    const char *root_tag;
    size_t adrp_offset;
    size_t ldr_offset;
    unsigned reg;
    uintptr_t global_addr;
    void *cell;
    const void *candidate;
    unsigned matched;
} BenderFix121Ref;

static int g_bender_fix121_done;
static BenderFix121Root g_bender_fix121_roots[BENDER_FIX121_MAX_ROOTS];
static unsigned g_bender_fix121_root_count;
static BenderFix121Edge g_bender_fix121_edges[BENDER_FIX121_MAX_EDGES];
static unsigned g_bender_fix121_edge_count;
static BenderFix121Ref g_bender_fix121_refs[BENDER_FIX121_MAX_REFS];
static unsigned g_bender_fix121_ref_count;
static char g_bender_fix121_delegate_tags[2][32];

static size_t bender_fix121_safe_span(uintptr_t code,size_t fallback,size_t cap){
    size_t span=code?bender_fix16_method_span_2816(code):0u;
    if(span<4u||span>cap)span=fallback;
    if(span>cap)span=cap;
    return span;
}

static int bender_fix121_add_root(const char*tag,uintptr_t code,const void*mi,size_t fallback,size_t cap){
    if(!tag||!code||g_bender_fix121_root_count>=BENDER_FIX121_MAX_ROOTS)return 0;
    for(unsigned i=0;i<g_bender_fix121_root_count;i++){
        if(g_bender_fix121_roots[i].code==code)return 1;
    }
    BenderFix121Root*r=&g_bender_fix121_roots[g_bender_fix121_root_count++];
    r->tag=tag;r->code=code;r->method_info=mi;r->span=bender_fix121_safe_span(code,fallback,cap);
    Module*m=bender_module_for_runtime_addr(code);
    logmsg("[BENDER_FIREBASE_CALLGRAPH][ROOT] source=fix121 tag=%s code=%p module=%s module_offset=0x%" PRIxPTR " span=%zu method_info=%p read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           tag,(void*)code,m?m->name:"?",m?code-m->bias:0,r->span,mi);
    return 1;
}

static void bender_fix121_discover_firebase_roots(void){
    bender_fix16_resolve_api_2816();
    if(!g_bender_fix16_domain_get_2816||!g_bender_fix16_domain_get_assemblies_2816||!g_bender_fix16_assembly_get_image_2816||!g_bender_fix16_image_get_class_count_2816||!g_bender_fix16_image_get_class_2816||!g_bender_fix16_class_get_methods_2816)return;
    void*domain=g_bender_fix16_domain_get_2816();size_t ac=0;
    const void**assemblies=domain?g_bender_fix16_domain_get_assemblies_2816(domain,&ac):NULL;
    if(!assemblies)return;
    for(size_t ai=0;ai<ac;ai++){
        void*image=g_bender_fix16_assembly_get_image_2816(assemblies[ai]);if(!image)continue;
        size_t cc=g_bender_fix16_image_get_class_count_2816(image);
        for(size_t ci=0;ci<cc;ci++){
            void*klass=g_bender_fix16_image_get_class_2816(image,ci);if(!klass)continue;
            const char*cn=g_bender_class_get_name_2812?g_bender_class_get_name_2812(klass):NULL;
            const char*ns=g_bender_class_get_namespace_2812?g_bender_class_get_namespace_2812(klass):NULL;
            if(!cn||!ns||strcmp(ns,"Firebase")||strcmp(cn,"FirebaseApp"))continue;
            void*iter=NULL;const void*mi=NULL;
            while((mi=g_bender_fix16_class_get_methods_2816(klass,&iter))!=NULL){
                const char*mn=g_bender_method_get_name_2812?g_bender_method_get_name_2812(mi):NULL;
                if(!mn)continue;
                int slot=-1;uintptr_t ptr=bender_fix16_method_ptr_2816(mi,&slot);(void)slot;
                if(!ptr)continue;
                if(!strcmp(mn,"CheckDependenciesAsync"))bender_fix121_add_root("FirebaseApp.CheckDependenciesAsync",ptr,mi,0x19cu,0x400u);
                else if(!strcmp(mn,"CheckAndFixDependenciesAsync"))bender_fix121_add_root("FirebaseApp.CheckAndFixDependenciesAsync",ptr,mi,0x144u,0x400u);
                else if(!strcmp(mn,"FixDependenciesAsync"))bender_fix121_add_root("FirebaseApp.FixDependenciesAsync",ptr,mi,0x200u,0x400u);
            }
        }
    }
}

static void bender_fix121_add_nord_roots(Module*mod){
    if(g_bender_fix115_nord_code)bender_fix121_add_root("NordFirebaseImpl.CheckAndFixDependenciesAsync",g_bender_fix115_nord_code,NULL,0x240u,0x500u);
    if(!mod||!g_bender_fix115_nord_code)return;
    uintptr_t delegates[2]={0,0};const void*dmi[2]={NULL,NULL};
    size_t span=bender_fix121_safe_span(g_bender_fix115_nord_code,0x240u,0x500u);
    unsigned dc=bender_fix117_collect_delegates(g_bender_fix115_nord_code,span,mod,delegates,dmi);
    if(dc>2u)dc=2u;
    for(unsigned i=0;i<dc;i++){
        snprintf(g_bender_fix121_delegate_tags[i],sizeof(g_bender_fix121_delegate_tags[i]),"delegate_b__%u",i);
        bender_fix121_add_root(g_bender_fix121_delegate_tags[i],delegates[i],dmi[i],0x100u,0x100u);
    }
}

static void bender_fix121_collect_edges(const BenderFix121Root*r){
    if(!r||!r->code||!r->span)return;
    uintptr_t lo=r->code,hi=r->code+r->span;
    for(size_t off=0;off+4u<=r->span;off+=4u){
        uint32_t ins=0;memcpy(&ins,(const void*)(r->code+off),sizeof(ins));
        const char*kind=NULL;
        if(bender_fix115_bl(ins))kind="BL";
        else if(bender_fix115_b(ins))kind="B";
        else continue;
        uintptr_t target=bender_fix115_imm26_target(r->code+off,ins);
        if(!target)continue;
        if(kind[0]=='B'&&kind[1]=='\0'&&target>=lo&&target<hi)continue;
        if(g_bender_fix121_edge_count>=BENDER_FIX121_MAX_EDGES)break;
        BenderFix121Edge*e=&g_bender_fix121_edges[g_bender_fix121_edge_count++];
        e->root_tag=r->tag;e->root_offset=off;e->kind=kind;e->target=target;e->matched=0u;
        Module*m=bender_module_for_runtime_addr(target);
        logmsg("[BENDER_FIREBASE_CALLGRAPH][EDGE] source=fix121 root=%s offset=0x%zx kind=%s target=%p module=%s module_offset=0x%" PRIxPTR " exact_method_match=pending read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               r->tag,off,kind,(void*)target,m?m->name:"?",m?target-m->bias:0);
    }
}

static int bender_fix121_candidate_from_global(uintptr_t ga,Module*owner,void**cell_out,const void**candidate_out){
    if(cell_out)*cell_out=NULL;
    if(candidate_out)*candidate_out=NULL;
    if(!ga||!owner||!bender_fix107_module_contains(owner,ga,sizeof(void*)))return 0;
    uintptr_t cell=0;memcpy(&cell,(const void*)ga,sizeof(cell));
    if(!bender_fix114_nontrivial(cell))return 0;
    Module*cm=bender_module_for_runtime_addr(cell);if(!cm||!cm->reserve)return 0;
    uintptr_t lo=(uintptr_t)cm->reserve,hi=lo+cm->reserve_size;
    if(cell<lo||cell+sizeof(void*)<cell||cell+sizeof(void*)>hi)return 0;
    const void*candidate=NULL;memcpy(&candidate,(const void*)cell,sizeof(candidate));
    if(!bender_fix114_nontrivial((uintptr_t)candidate))return 0;
    if(cell_out)*cell_out=(void*)cell;
    if(candidate_out)*candidate_out=candidate;
    return 1;
}

static void bender_fix121_collect_refs(const BenderFix121Root*r,Module*mod){
    if(!r||!r->code||!r->span||!mod)return;
    for(size_t off=0;off+4u<=r->span;off+=4u){
        uint32_t a=0;memcpy(&a,(const void*)(r->code+off),sizeof(a));
        unsigned reg=0;uintptr_t page=0;
        if(!bender_fix107_adrp(r->code+off,a,&reg,&page)||reg>=31u)continue;
        size_t maxj=off+32u;if(maxj>r->span)maxj=r->span;
        for(size_t j=off+4u;j+4u<=maxj;j+=4u){
            uint32_t b=0;memcpy(&b,(const void*)(r->code+j),sizeof(b));
            unsigned rt=0,rn=0,imm=0;
            if(bender_fix114_ldr_x_uimm(b,&rt,&rn,&imm)&&rt==reg&&rn==reg){
                uintptr_t ga=page+(uintptr_t)imm;void*cell=NULL;const void*candidate=NULL;
                if(bender_fix121_candidate_from_global(ga,mod,&cell,&candidate)){
                    int duplicate=0;
                    for(unsigned k=0;k<g_bender_fix121_ref_count;k++){
                        BenderFix121Ref*q=&g_bender_fix121_refs[k];
                        if(q->root_tag==r->tag&&q->global_addr==ga){duplicate=1;break;}
                    }
                    if(!duplicate&&g_bender_fix121_ref_count<BENDER_FIX121_MAX_REFS){
                        BenderFix121Ref*q=&g_bender_fix121_refs[g_bender_fix121_ref_count++];
                        q->root_tag=r->tag;q->adrp_offset=off;q->ldr_offset=j;q->reg=reg;q->global_addr=ga;q->cell=cell;q->candidate=candidate;q->matched=0u;
                    }
                }
                break;
            }
            if(bender_fix116_reg_written(b,reg))break;
        }
    }
}

static void bender_fix121_log_identity(const char*marker,const char*root,size_t offset,const void*mi,uintptr_t ptr,const char*extra){
    const char*ns="?",*cn="?",*mn="?";bender_fix120_method_identity(mi,&ns,&cn,&mn);
    char sig[720]={0};bender_fix120_signature(mi,sig,sizeof(sig));
    logmsg("%s source=fix121 root=%s offset=0x%zx image=%s namespace=%s class=%s method=%s method_info=%p code=%p %s %s read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           marker,root?root:"?",offset,bender_fix15_image_name_2815(mi),ns,cn,mn,mi,(void*)ptr,sig[0]?sig:"return=? param_count=? params=?",extra?extra:"");
}

static void bender_fix121_match_full_enumeration(unsigned long*methods_seen,unsigned*edge_matches,unsigned*ref_matches){
    if(methods_seen)*methods_seen=0ul;
    if(edge_matches)*edge_matches=0u;
    if(ref_matches)*ref_matches=0u;
    bender_fix16_resolve_api_2816();
    if(!g_bender_fix16_domain_get_2816||!g_bender_fix16_domain_get_assemblies_2816||!g_bender_fix16_assembly_get_image_2816||!g_bender_fix16_image_get_class_count_2816||!g_bender_fix16_image_get_class_2816||!g_bender_fix16_class_get_methods_2816)return;
    void*domain=g_bender_fix16_domain_get_2816();size_t ac=0;const void**assemblies=domain?g_bender_fix16_domain_get_assemblies_2816(domain,&ac):NULL;if(!assemblies)return;
    for(size_t ai=0;ai<ac;ai++){
        void*image=g_bender_fix16_assembly_get_image_2816(assemblies[ai]);if(!image)continue;
        size_t cc=g_bender_fix16_image_get_class_count_2816(image);
        for(size_t ci=0;ci<cc;ci++){
            void*klass=g_bender_fix16_image_get_class_2816(image,ci);if(!klass)continue;
            void*iter=NULL;const void*mi=NULL;
            while((mi=g_bender_fix16_class_get_methods_2816(klass,&iter))!=NULL){
                if(methods_seen)(*methods_seen)++;
                int slot=-1;uintptr_t ptr=bender_fix16_method_ptr_2816(mi,&slot);(void)slot;
                if(ptr){
                    for(unsigned ei=0;ei<g_bender_fix121_edge_count;ei++){
                        BenderFix121Edge*e=&g_bender_fix121_edges[ei];
                        if(!e->matched&&ptr==e->target){
                            e->matched=1u;if(edge_matches)(*edge_matches)++;
                            char extra[96];snprintf(extra,sizeof(extra),"kind=%s target_match=exact-code-pointer",e->kind);
                            bender_fix121_log_identity("[BENDER_FIREBASE_CALLGRAPH][EDGE_MATCH]",e->root_tag,e->root_offset,mi,ptr,extra);
                        }
                    }
                }
                for(unsigned ri=0;ri<g_bender_fix121_ref_count;ri++){
                    BenderFix121Ref*q=&g_bender_fix121_refs[ri];
                    if(!q->matched&&mi==q->candidate){
                        q->matched=1u;if(ref_matches)(*ref_matches)++;
                        char extra[192];snprintf(extra,sizeof(extra),"validation=exact-full-runtime-methodinfo-membership global=%p cell=%p reg=x%u ldr_offset=0x%zx",(void*)q->global_addr,q->cell,q->reg,q->ldr_offset);
                        bender_fix121_log_identity("[BENDER_FIREBASE_CALLGRAPH][METHODINFO_REF]",q->root_tag,q->adrp_offset,mi,ptr,extra);
                    }
                }
            }
        }
    }
}

static void bender_fix121_tick(unsigned frame){
    if(g_bender_fix121_done||frame<126u||!g_bender_fix120_done||!g_bender_fix115_nord_code)return;
    Module*mod=bender_module_for_runtime_addr(g_bender_fix115_nord_code);if(!mod||strcmp(mod->name,"libil2cpp.so"))return;
    g_bender_fix121_done=1;
    g_bender_fix121_root_count=0u;g_bender_fix121_edge_count=0u;g_bender_fix121_ref_count=0u;
    memset(g_bender_fix121_roots,0,sizeof(g_bender_fix121_roots));memset(g_bender_fix121_edges,0,sizeof(g_bender_fix121_edges));memset(g_bender_fix121_refs,0,sizeof(g_bender_fix121_refs));

    bender_fix121_discover_firebase_roots();
    bender_fix121_add_nord_roots(mod);
    for(unsigned i=0;i<g_bender_fix121_root_count;i++){
        bender_fix121_collect_edges(&g_bender_fix121_roots[i]);
        bender_fix121_collect_refs(&g_bender_fix121_roots[i],mod);
    }
    unsigned long methods_seen=0;unsigned edge_matches=0,ref_matches=0;
    bender_fix121_match_full_enumeration(&methods_seen,&edge_matches,&ref_matches);
    unsigned unresolved_edges=0,unresolved_refs=0;
    for(unsigned i=0;i<g_bender_fix121_edge_count;i++)if(!g_bender_fix121_edges[i].matched)unresolved_edges++;
    for(unsigned i=0;i<g_bender_fix121_ref_count;i++)if(!g_bender_fix121_refs[i].matched)unresolved_refs++;
    logmsg("[BENDER_FIREBASE_CALLGRAPH][DONE] source=fix121 frame=%u roots=%u edges=%u edge_matches=%u unresolved_edges=%u candidate_refs=%u methodinfo_refs=%u unresolved_refs=%u methods_seen=%lu physical_fix120_dependency=Firebase.FirebaseApp.CheckAndFixDependenciesAsync next=physical-log-identifies-inner-task-chain-and-unresolved-generic-frontier read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           frame,g_bender_fix121_root_count,g_bender_fix121_edge_count,edge_matches,unresolved_edges,g_bender_fix121_ref_count,ref_matches,unresolved_refs,methods_seen);
}


/* BENDER V2.7.171 — FIX122 Sh.Main::Start NullReference frontier trace.
 * physical_fix121_main_start_nre=1
 * Strictly read-only: field snapshots + static AArch64 call graph only.
 */
#define BENDER_FIX122_MAX_EDGES 256u
#define BENDER_FIX122_MAX_FIELDS 160u

typedef void *(*BenderFix122ClassGetFields)(void *, void **);
typedef const char *(*BenderFix122FieldGetName)(void *);
typedef uint32_t (*BenderFix122FieldGetFlags)(void *);
typedef const void *(*BenderFix122FieldGetType)(void *);
typedef char *(*BenderFix122TypeGetName)(const void *);
typedef void (*BenderFix122FieldGetValue)(void *, void *, void *);
typedef void (*BenderFix122FieldStaticGetValue)(void *, void *);
typedef void *(*BenderFix122ClassGetParent)(void *);
typedef void (*BenderFix122Free)(void *);

typedef struct {
    size_t offset;
    const char *kind;
    uintptr_t target;
    uint32_t ins;
    unsigned matched;
} BenderFix122Edge;

static BenderFix122ClassGetFields g_bender_fix122_class_get_fields;
static BenderFix122FieldGetName g_bender_fix122_field_get_name;
static BenderFix122FieldGetFlags g_bender_fix122_field_get_flags;
static BenderFix122FieldGetType g_bender_fix122_field_get_type;
static BenderFix122TypeGetName g_bender_fix122_type_get_name;
static BenderFix122FieldGetValue g_bender_fix122_field_get_value;
static BenderFix122FieldStaticGetValue g_bender_fix122_field_static_get_value;
static BenderFix122ClassGetParent g_bender_fix122_class_get_parent;
static BenderFix122Free g_bender_fix122_free;
static int g_bender_fix122_api_attempted;
static int g_bender_fix122_api_ready;
static int g_bender_fix122_seen_enter;
static int g_bender_fix122_seen_exception;
static const void *g_bender_fix122_main_start_mi;
static void *g_bender_fix122_main_obj;
static uintptr_t g_bender_fix122_main_code;
static size_t g_bender_fix122_main_span;
static BenderFix122Edge g_bender_fix122_edges[BENDER_FIX122_MAX_EDGES];
static unsigned g_bender_fix122_edge_count;

static int bender_fix122_target(const char *key,const char *image){
    if(!key||strcmp(key,"Sh.Main::Start"))return 0;
    return !image||!strcmp(image,"Assembly-CSharp.dll")||bender_ascii_contains_ci_2812(image,"Assembly-CSharp");
}

static int bender_fix122_resolve_field_api(void){
    if(g_bender_fix122_api_attempted)return g_bender_fix122_api_ready;
    g_bender_fix122_api_attempted=1;
    Module*m=g_bender_il2cpp_module_2813;
    if(!m){
        logmsg("[BENDER_MAIN_START_NRE][API] source=fix122 ready=0 reason=libil2cpp-module-missing read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
        return 0;
    }
    g_bender_fix122_class_get_fields=(BenderFix122ClassGetFields)module_symbol(m,"il2cpp_class_get_fields");
    g_bender_fix122_field_get_name=(BenderFix122FieldGetName)module_symbol(m,"il2cpp_field_get_name");
    g_bender_fix122_field_get_flags=(BenderFix122FieldGetFlags)module_symbol(m,"il2cpp_field_get_flags");
    g_bender_fix122_field_get_type=(BenderFix122FieldGetType)module_symbol(m,"il2cpp_field_get_type");
    g_bender_fix122_type_get_name=(BenderFix122TypeGetName)module_symbol(m,"il2cpp_type_get_name");
    g_bender_fix122_field_get_value=(BenderFix122FieldGetValue)module_symbol(m,"il2cpp_field_get_value");
    g_bender_fix122_field_static_get_value=(BenderFix122FieldStaticGetValue)module_symbol(m,"il2cpp_field_static_get_value");
    g_bender_fix122_class_get_parent=(BenderFix122ClassGetParent)module_symbol(m,"il2cpp_class_get_parent");
    g_bender_fix122_free=(BenderFix122Free)module_symbol(m,"il2cpp_free");
    g_bender_fix122_api_ready=g_bender_fix122_class_get_fields&&g_bender_fix122_field_get_name&&
        g_bender_fix122_field_get_flags&&g_bender_fix122_field_get_value&&
        g_bender_fix122_field_static_get_value;
    logmsg("[BENDER_MAIN_START_NRE][API] source=fix122 ready=%d class_get_fields=%d field_name=%d field_flags=%d field_type=%d type_name=%d field_get=%d static_get=%d class_parent=%d read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           g_bender_fix122_api_ready,g_bender_fix122_class_get_fields!=NULL,g_bender_fix122_field_get_name!=NULL,
           g_bender_fix122_field_get_flags!=NULL,g_bender_fix122_field_get_type!=NULL,g_bender_fix122_type_get_name!=NULL,
           g_bender_fix122_field_get_value!=NULL,g_bender_fix122_field_static_get_value!=NULL,g_bender_fix122_class_get_parent!=NULL);
    return g_bender_fix122_api_ready;
}

static void bender_fix122_snapshot_fields(const void *mi,void *obj,const char *stage){
    if(!mi||!stage||!bender_fix122_resolve_field_api())return;
    void*klass=g_bender_method_get_class_2812?g_bender_method_get_class_2812(mi):NULL;
    if(!klass)return;
    unsigned total=0,depth=0;
    for(void*k=klass;k&&depth<8u&&total<BENDER_FIX122_MAX_FIELDS;depth++){
        const char*owner=g_bender_class_get_name_2812?g_bender_class_get_name_2812(k):"?";
        const char*owner_ns=g_bender_class_get_namespace_2812?g_bender_class_get_namespace_2812(k):"?";
        void*it=NULL;void*field=NULL;
        while((field=g_bender_fix122_class_get_fields(k,&it))!=NULL&&total<BENDER_FIX122_MAX_FIELDS){
            total++;
            const char*name=g_bender_fix122_field_get_name(field);if(!name)name="?";
            uint32_t flags=g_bender_fix122_field_get_flags(field);
            int is_static=(flags&0x0010u)?1:0;
            int is_literal=(flags&0x0040u)?1:0;
            uintptr_t raw[4]={0,0,0,0};int value_read=0;
            if(!is_literal){
                if(is_static){g_bender_fix122_field_static_get_value(field,raw);value_read=1;}
                else if(obj){g_bender_fix122_field_get_value(obj,field,raw);value_read=1;}
            }
            char*type_name=NULL;
            if(g_bender_fix122_field_get_type&&g_bender_fix122_type_get_name){
                const void*ft=g_bender_fix122_field_get_type(field);
                if(ft)type_name=g_bender_fix122_type_get_name(ft);
            }
            logmsg("[BENDER_MAIN_START_NRE][FIELD] source=fix122 stage=%s n=%u depth=%u owner=%s.%s name=%s type=%s flags=0x%08x static=%d literal=%d value_read=%d raw0=%p raw1=%p zero0=%d obj=%p read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
                   stage,total,depth,owner_ns?owner_ns:"?",owner?owner:"?",name,type_name?type_name:"?",flags,is_static,is_literal,value_read,
                   (void*)raw[0],(void*)raw[1],raw[0]==0u?1:0,obj);
            if(type_name&&g_bender_fix122_free)g_bender_fix122_free(type_name);
        }
        if(!g_bender_fix122_class_get_parent)break;
        k=g_bender_fix122_class_get_parent(k);
    }
    logmsg("[BENDER_MAIN_START_NRE][FIELDS_DONE] source=fix122 stage=%s fields=%u class_depth=%u obj=%p read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           stage,total,depth,obj);
}

static size_t bender_fix122_safe_span(uintptr_t code){
    size_t span=code?bender_fix16_method_span_2816(code):0u;
    if(span<4u||span>0x1800u)span=0x1000u;
    return span;
}

static void bender_fix122_collect_edges(void){
    if(!g_bender_fix122_main_code||!g_bender_fix122_main_span)return;
    uintptr_t lo=g_bender_fix122_main_code,hi=lo+g_bender_fix122_main_span;
    g_bender_fix122_edge_count=0u;memset(g_bender_fix122_edges,0,sizeof(g_bender_fix122_edges));
    for(size_t off=0;off+4u<=g_bender_fix122_main_span;off+=4u){
        uint32_t ins=0;memcpy(&ins,(const void*)(g_bender_fix122_main_code+off),sizeof(ins));
        const char*kind=NULL;
        if(bender_fix115_bl(ins))kind="BL";
        else if(bender_fix115_b(ins))kind="B";
        else continue;
        uintptr_t target=bender_fix115_imm26_target(g_bender_fix122_main_code+off,ins);
        if(!target)continue;
        if(kind[0]=='B'&&kind[1]=='\0'&&target>=lo&&target<hi)continue;
        if(g_bender_fix122_edge_count>=BENDER_FIX122_MAX_EDGES)break;
        BenderFix122Edge*e=&g_bender_fix122_edges[g_bender_fix122_edge_count++];
        e->offset=off;e->kind=kind;e->target=target;e->ins=ins;e->matched=0u;
        Module*m=bender_module_for_runtime_addr(target);
        logmsg("[BENDER_MAIN_START_NRE][EDGE] source=fix122 offset=0x%zx kind=%s ins=0x%08x target=%p module=%s module_offset=0x%" PRIxPTR " exact_method_match=pending read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               off,kind,ins,(void*)target,m?m->name:"?",m?target-m->bias:0);
    }
}

static void bender_fix122_match_edges(void){
    bender_fix16_resolve_api_2816();
    if(!g_bender_fix16_domain_get_2816||!g_bender_fix16_domain_get_assemblies_2816||
       !g_bender_fix16_assembly_get_image_2816||!g_bender_fix16_image_get_class_count_2816||
       !g_bender_fix16_image_get_class_2816||!g_bender_fix16_class_get_methods_2816)return;
    void*domain=g_bender_fix16_domain_get_2816();size_t ac=0;
    const void**assemblies=domain?g_bender_fix16_domain_get_assemblies_2816(domain,&ac):NULL;
    if(!assemblies)return;
    unsigned long methods_seen=0;unsigned matches=0;
    for(size_t ai=0;ai<ac;ai++){
        void*image=g_bender_fix16_assembly_get_image_2816(assemblies[ai]);if(!image)continue;
        size_t cc=g_bender_fix16_image_get_class_count_2816(image);
        for(size_t ci=0;ci<cc;ci++){
            void*klass=g_bender_fix16_image_get_class_2816(image,ci);if(!klass)continue;
            void*iter=NULL;const void*mi=NULL;
            while((mi=g_bender_fix16_class_get_methods_2816(klass,&iter))!=NULL){
                methods_seen++;
                int slot=-1;uintptr_t ptr=bender_fix16_method_ptr_2816(mi,&slot);(void)slot;
                if(!ptr)continue;
                for(unsigned ei=0;ei<g_bender_fix122_edge_count;ei++){
                    BenderFix122Edge*e=&g_bender_fix122_edges[ei];
                    if(e->matched||ptr!=e->target)continue;
                    e->matched=1u;matches++;
                    const char*ns="?",*cn="?",*mn="?";bender_fix120_method_identity(mi,&ns,&cn,&mn);
                    char sig[720]={0};bender_fix120_signature(mi,sig,sizeof(sig));
                    logmsg("[BENDER_MAIN_START_NRE][EDGE_MATCH] source=fix122 offset=0x%zx kind=%s image=%s namespace=%s class=%s method=%s method_info=%p code=%p %s validation=exact-full-runtime-methodpointer read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
                           e->offset,e->kind,bender_fix15_image_name_2815(mi),ns,cn,mn,mi,(void*)ptr,sig[0]?sig:"return=? param_count=? params=?");
                }
            }
        }
    }
    unsigned unresolved=0;for(unsigned i=0;i<g_bender_fix122_edge_count;i++)if(!g_bender_fix122_edges[i].matched)unresolved++;
    logmsg("[BENDER_MAIN_START_NRE][CALLGRAPH_DONE] source=fix122 edges=%u exact_matches=%u unresolved=%u methods_seen=%lu code=%p span=%zu physical_fix121_main_start_nre=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           g_bender_fix122_edge_count,matches,unresolved,methods_seen,(void*)g_bender_fix122_main_code,g_bender_fix122_main_span);
}

static void bender_fix122_before_runtime_invoke(const void*method_info,void*obj,const char*key,const char*image){
    if(!bender_fix122_target(key,image)||g_bender_fix122_seen_enter)return;
    g_bender_fix122_seen_enter=1;g_bender_fix122_main_start_mi=method_info;g_bender_fix122_main_obj=obj;
    int slot=-1;g_bender_fix122_main_code=bender_fix16_method_ptr_2816(method_info,&slot);
    g_bender_fix122_main_span=bender_fix122_safe_span(g_bender_fix122_main_code);
    Module*m=bender_module_for_runtime_addr(g_bender_fix122_main_code);
    logmsg("[BENDER_MAIN_START_NRE][ENTER] source=fix122 frame=%u method=%s image=%s method_info=%p obj=%p code=%p module=%s module_offset=0x%" PRIxPTR " slot=%d span=%zu premain_expected_state=3 physical_fix121_main_start_nre=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           g_bender_fix13_frame_hint_2813,key?key:"?",image?image:"?",method_info,obj,(void*)g_bender_fix122_main_code,m?m->name:"?",m?g_bender_fix122_main_code-m->bias:0,slot,g_bender_fix122_main_span);
    bender_fix122_snapshot_fields(method_info,obj,"pre");
}

static void bender_fix122_after_runtime_invoke(const void*method_info,void*obj,const char*key,const char*image,void**exc){
    if(!bender_fix122_target(key,image)||g_bender_fix122_seen_exception||!exc||!*exc)return;
    g_bender_fix122_seen_exception=1;
    logmsg("[BENDER_MAIN_START_NRE][EXCEPTION] source=fix122 frame=%u method=%s image=%s method_info=%p obj=%p exception=%p same_method=%d same_obj=%d physical_fix121_main_start_nre=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           g_bender_fix13_frame_hint_2813,key?key:"?",image?image:"?",method_info,obj,*exc,method_info==g_bender_fix122_main_start_mi?1:0,obj==g_bender_fix122_main_obj?1:0);
    bender_fix122_snapshot_fields(method_info,obj,"exception");
    bender_fix122_collect_edges();
    bender_fix122_match_edges();
}


/* BENDER V2.7.172 — FIX123 read-only Firebase lambda frontier resolver. */
#define BENDER_FIX123_MAX_ROOTS 24u
#define BENDER_FIX123_MAX_EDGES 384u
#define BENDER_FIX123_MAX_REFS 384u

typedef struct {
    char tag[112];
    uintptr_t code;
    size_t span;
    const void *method_info;
} BenderFix123Root;

typedef struct {
    const char *root_tag;
    size_t root_offset;
    const char *kind;
    uintptr_t target;
    unsigned matched;
} BenderFix123Edge;

typedef struct {
    const char *root_tag;
    size_t adrp_offset;
    size_t ldr_offset;
    unsigned reg;
    uintptr_t global_addr;
    void *cell;
    const void *candidate;
    unsigned matched;
} BenderFix123Ref;

static int g_bender_fix123_done;
static BenderFix123Root g_bender_fix123_roots[BENDER_FIX123_MAX_ROOTS];
static unsigned g_bender_fix123_root_count;
static BenderFix123Edge g_bender_fix123_edges[BENDER_FIX123_MAX_EDGES];
static unsigned g_bender_fix123_edge_count;
static BenderFix123Ref g_bender_fix123_refs[BENDER_FIX123_MAX_REFS];
static unsigned g_bender_fix123_ref_count;

static int bender_fix123_target_name(const char*mn){
    if(!mn)return 0;
    return !strcmp(mn,"<CheckDependenciesAsync>b__56_0") ||
           !strcmp(mn,"<CheckAndFixDependenciesAsync>b__57_0") ||
           !strcmp(mn,"<CheckAndFixDependenciesAsync>b__57_1") ||
           !strcmp(mn,"CheckDependencies") ||
           !strcmp(mn,"CheckDependenciesInternal") ||
           !strcmp(mn,"CheckDependenciesNoThread") ||
           !strcmp(mn,"CheckDependenciesThread") ||
           !strcmp(mn,"CheckDependenciesPendingThread");
}

static int bender_fix123_add_root(const void*mi,uintptr_t code,const char*ns,const char*cn,const char*mn){
    if(!mi||!code||!mn||g_bender_fix123_root_count>=BENDER_FIX123_MAX_ROOTS)return 0;
    for(unsigned i=0;i<g_bender_fix123_root_count;i++){
        if(g_bender_fix123_roots[i].code==code)return 1;
    }
    BenderFix123Root*r=&g_bender_fix123_roots[g_bender_fix123_root_count++];
    memset(r,0,sizeof(*r));
    snprintf(r->tag,sizeof(r->tag),"%s%s%s.%s",ns&&*ns?ns:"",ns&&*ns?".":"",cn?cn:"?",mn);
    r->code=code;r->method_info=mi;r->span=bender_fix121_safe_span(code,0x180u,0x600u);
    Module*m=bender_module_for_runtime_addr(code);
    char sig[720]={0};bender_fix120_signature(mi,sig,sizeof(sig));
    logmsg("[BENDER_FIREBASE_LAMBDA_FRONTIER][ROOT] source=fix123 tag=%s image=%s namespace=%s class=%s method=%s method_info=%p code=%p module=%s module_offset=0x%" PRIxPTR " span=%zu %s physical_fix121_lambda_b56_0=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           r->tag,bender_fix15_image_name_2815(mi),ns?ns:"?",cn?cn:"?",mn,mi,(void*)code,m?m->name:"?",m?code-m->bias:0,r->span,sig[0]?sig:"return=? param_count=? params=?");
    return 1;
}

static void bender_fix123_discover_roots(unsigned long*methods_seen){
    if(methods_seen)*methods_seen=0ul;
    bender_fix16_resolve_api_2816();
    if(!g_bender_fix16_domain_get_2816||!g_bender_fix16_domain_get_assemblies_2816||
       !g_bender_fix16_assembly_get_image_2816||!g_bender_fix16_image_get_class_count_2816||
       !g_bender_fix16_image_get_class_2816||!g_bender_fix16_class_get_methods_2816)return;
    void*domain=g_bender_fix16_domain_get_2816();size_t ac=0;
    const void**assemblies=domain?g_bender_fix16_domain_get_assemblies_2816(domain,&ac):NULL;
    if(!assemblies)return;
    for(size_t ai=0;ai<ac;ai++){
        void*image=g_bender_fix16_assembly_get_image_2816(assemblies[ai]);if(!image)continue;
        size_t cc=g_bender_fix16_image_get_class_count_2816(image);
        for(size_t ci=0;ci<cc;ci++){
            void*klass=g_bender_fix16_image_get_class_2816(image,ci);if(!klass)continue;
            const char*cn=g_bender_class_get_name_2812?g_bender_class_get_name_2812(klass):NULL;
            const char*ns=g_bender_class_get_namespace_2812?g_bender_class_get_namespace_2812(klass):NULL;
            void*iter=NULL;const void*mi=NULL;
            while((mi=g_bender_fix16_class_get_methods_2816(klass,&iter))!=NULL){
                if(methods_seen)(*methods_seen)++;
                const char*mn=g_bender_method_get_name_2812?g_bender_method_get_name_2812(mi):NULL;
                if(!bender_fix123_target_name(mn))continue;
                const char*img=bender_fix15_image_name_2815(mi);
                if(!img||strcmp(img,"Firebase.App.dll"))continue;
                int slot=-1;uintptr_t ptr=bender_fix16_method_ptr_2816(mi,&slot);(void)slot;
                if(ptr)bender_fix123_add_root(mi,ptr,ns,cn,mn);
            }
        }
    }
}

static void bender_fix123_collect_edges(const BenderFix123Root*r){
    if(!r||!r->code||!r->span)return;
    uintptr_t lo=r->code,hi=r->code+r->span;
    for(size_t off=0;off+4u<=r->span;off+=4u){
        uint32_t ins=0;memcpy(&ins,(const void*)(r->code+off),sizeof(ins));
        const char*kind=NULL;
        if(bender_fix115_bl(ins))kind="BL";
        else if(bender_fix115_b(ins))kind="B";
        else continue;
        uintptr_t target=bender_fix115_imm26_target(r->code+off,ins);if(!target)continue;
        if(kind[0]=='B'&&kind[1]=='\0'&&target>=lo&&target<hi)continue;
        if(g_bender_fix123_edge_count>=BENDER_FIX123_MAX_EDGES)break;
        BenderFix123Edge*e=&g_bender_fix123_edges[g_bender_fix123_edge_count++];
        e->root_tag=r->tag;e->root_offset=off;e->kind=kind;e->target=target;e->matched=0u;
        Module*m=bender_module_for_runtime_addr(target);
        logmsg("[BENDER_FIREBASE_LAMBDA_FRONTIER][EDGE] source=fix123 root=%s offset=0x%zx kind=%s target=%p module=%s module_offset=0x%" PRIxPTR " exact_method_match=pending read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
               r->tag,off,kind,(void*)target,m?m->name:"?",m?target-m->bias:0);
    }
}

static void bender_fix123_collect_refs(const BenderFix123Root*r,Module*mod){
    if(!r||!r->code||!r->span||!mod)return;
    for(size_t off=0;off+4u<=r->span;off+=4u){
        uint32_t a=0;memcpy(&a,(const void*)(r->code+off),sizeof(a));
        unsigned reg=0;uintptr_t page=0;
        if(!bender_fix107_adrp(r->code+off,a,&reg,&page)||reg>=31u)continue;
        size_t maxj=off+32u;if(maxj>r->span)maxj=r->span;
        for(size_t j=off+4u;j+4u<=maxj;j+=4u){
            uint32_t b=0;memcpy(&b,(const void*)(r->code+j),sizeof(b));
            unsigned rt=0,rn=0,imm=0;
            if(bender_fix114_ldr_x_uimm(b,&rt,&rn,&imm)&&rt==reg&&rn==reg){
                uintptr_t ga=page+(uintptr_t)imm;void*cell=NULL;const void*candidate=NULL;
                if(bender_fix121_candidate_from_global(ga,mod,&cell,&candidate)){
                    int duplicate=0;
                    for(unsigned k=0;k<g_bender_fix123_ref_count;k++){
                        BenderFix123Ref*q=&g_bender_fix123_refs[k];
                        if(!strcmp(q->root_tag,r->tag)&&q->global_addr==ga){duplicate=1;break;}
                    }
                    if(!duplicate&&g_bender_fix123_ref_count<BENDER_FIX123_MAX_REFS){
                        BenderFix123Ref*q=&g_bender_fix123_refs[g_bender_fix123_ref_count++];
                        q->root_tag=r->tag;q->adrp_offset=off;q->ldr_offset=j;q->reg=reg;
                        q->global_addr=ga;q->cell=cell;q->candidate=candidate;q->matched=0u;
                    }
                }
                break;
            }
            if(bender_fix116_reg_written(b,reg))break;
        }
    }
}

static void bender_fix123_log_identity(const char*marker,const char*root,size_t offset,const void*mi,uintptr_t ptr,const char*extra){
    const char*ns="?",*cn="?",*mn="?";bender_fix120_method_identity(mi,&ns,&cn,&mn);
    char sig[720]={0};bender_fix120_signature(mi,sig,sizeof(sig));
    logmsg("%s source=fix123 root=%s offset=0x%zx image=%s namespace=%s class=%s method=%s method_info=%p code=%p %s %s read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           marker,root?root:"?",offset,bender_fix15_image_name_2815(mi),ns,cn,mn,mi,(void*)ptr,sig[0]?sig:"return=? param_count=? params=?",extra?extra:"");
}

static void bender_fix123_match_full_enumeration(unsigned long*methods_seen,unsigned*edge_matches,unsigned*ref_matches){
    if(methods_seen)*methods_seen=0ul;
    if(edge_matches)*edge_matches=0u;
    if(ref_matches)*ref_matches=0u;
    bender_fix16_resolve_api_2816();
    if(!g_bender_fix16_domain_get_2816||!g_bender_fix16_domain_get_assemblies_2816||
       !g_bender_fix16_assembly_get_image_2816||!g_bender_fix16_image_get_class_count_2816||
       !g_bender_fix16_image_get_class_2816||!g_bender_fix16_class_get_methods_2816)return;
    void*domain=g_bender_fix16_domain_get_2816();size_t ac=0;
    const void**assemblies=domain?g_bender_fix16_domain_get_assemblies_2816(domain,&ac):NULL;if(!assemblies)return;
    for(size_t ai=0;ai<ac;ai++){
        void*image=g_bender_fix16_assembly_get_image_2816(assemblies[ai]);if(!image)continue;
        size_t cc=g_bender_fix16_image_get_class_count_2816(image);
        for(size_t ci=0;ci<cc;ci++){
            void*klass=g_bender_fix16_image_get_class_2816(image,ci);if(!klass)continue;
            void*iter=NULL;const void*mi=NULL;
            while((mi=g_bender_fix16_class_get_methods_2816(klass,&iter))!=NULL){
                if(methods_seen)(*methods_seen)++;
                int slot=-1;uintptr_t ptr=bender_fix16_method_ptr_2816(mi,&slot);(void)slot;
                if(ptr){
                    for(unsigned ei=0;ei<g_bender_fix123_edge_count;ei++){
                        BenderFix123Edge*e=&g_bender_fix123_edges[ei];
                        if(!e->matched&&ptr==e->target){
                            e->matched=1u;if(edge_matches)(*edge_matches)++;
                            char extra[96];snprintf(extra,sizeof(extra),"kind=%s target_match=exact-code-pointer",e->kind);
                            bender_fix123_log_identity("[BENDER_FIREBASE_LAMBDA_FRONTIER][EDGE_MATCH]",e->root_tag,e->root_offset,mi,ptr,extra);
                        }
                    }
                }
                for(unsigned ri=0;ri<g_bender_fix123_ref_count;ri++){
                    BenderFix123Ref*q=&g_bender_fix123_refs[ri];
                    if(!q->matched&&mi==q->candidate){
                        q->matched=1u;if(ref_matches)(*ref_matches)++;
                        char extra[224];snprintf(extra,sizeof(extra),"validation=exact-full-runtime-methodinfo-membership global=%p cell=%p reg=x%u ldr_offset=0x%zx",(void*)q->global_addr,q->cell,q->reg,q->ldr_offset);
                        bender_fix123_log_identity("[BENDER_FIREBASE_LAMBDA_FRONTIER][METHODINFO_REF]",q->root_tag,q->adrp_offset,mi,ptr,extra);
                    }
                }
            }
        }
    }
}

static void bender_fix123_tick(unsigned frame){
    if(g_bender_fix123_done||frame<128u||!g_bender_fix121_done)return;
    g_bender_fix123_done=1;
    memset(g_bender_fix123_roots,0,sizeof(g_bender_fix123_roots));
    memset(g_bender_fix123_edges,0,sizeof(g_bender_fix123_edges));
    memset(g_bender_fix123_refs,0,sizeof(g_bender_fix123_refs));
    g_bender_fix123_root_count=0u;g_bender_fix123_edge_count=0u;g_bender_fix123_ref_count=0u;

    unsigned long discover_methods=0;bender_fix123_discover_roots(&discover_methods);
    for(unsigned i=0;i<g_bender_fix123_root_count;i++){
        Module*m=bender_module_for_runtime_addr(g_bender_fix123_roots[i].code);
        bender_fix123_collect_edges(&g_bender_fix123_roots[i]);
        if(m)bender_fix123_collect_refs(&g_bender_fix123_roots[i],m);
    }
    unsigned long match_methods=0;unsigned edge_matches=0,ref_matches=0;
    bender_fix123_match_full_enumeration(&match_methods,&edge_matches,&ref_matches);
    unsigned unresolved_edges=0,unresolved_refs=0;
    for(unsigned i=0;i<g_bender_fix123_edge_count;i++)if(!g_bender_fix123_edges[i].matched)unresolved_edges++;
    for(unsigned i=0;i<g_bender_fix123_ref_count;i++)if(!g_bender_fix123_refs[i].matched)unresolved_refs++;
    logmsg("[BENDER_FIREBASE_LAMBDA_FRONTIER][DONE] source=fix123 frame=%u roots=%u edges=%u edge_matches=%u unresolved_edges=%u candidate_refs=%u methodinfo_refs=%u unresolved_refs=%u discover_methods=%lu match_methods=%lu physical_fix121_lambda_b56_0=1 physical_fix122_main_nre_reproduced=0 next=physical-log-identifies-sync-check-and-task-generic-frontier read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",
           frame,g_bender_fix123_root_count,g_bender_fix123_edge_count,edge_matches,unresolved_edges,g_bender_fix123_ref_count,ref_matches,unresolved_refs,discover_methods,match_methods);
}


/* BENDER V2.7.173 - FIX124 read-only Firebase Android dependency frontier. */
#define BENDER_FIX124_MAX_ROOTS 32u
#define BENDER_FIX124_MAX_EDGES 512u
typedef struct { char tag[128]; uintptr_t code; size_t span; const void*mi; } BenderFix124Root;
typedef struct { const char*root; size_t off; const char*kind; uintptr_t target; unsigned matched; } BenderFix124Edge;
static int g_bender_fix124_done;
static BenderFix124Root g_bender_fix124_roots[BENDER_FIX124_MAX_ROOTS];
static BenderFix124Edge g_bender_fix124_edges[BENDER_FIX124_MAX_EDGES];
static unsigned g_bender_fix124_root_count,g_bender_fix124_edge_count;

static int bender_fix124_name_match(const char*cn,const char*mn){
    if(!cn||!mn)return 0;
    if(!strcmp(cn,"AppUtil"))return !strcmp(mn,"CheckAndroidDependencies")||!strcmp(mn,"InitializePlayServicesInternal")||!strcmp(mn,"TerminatePlayServicesInternal")||strstr(mn,"PlayServices")!=NULL;
    if(!strcmp(cn,"AppUtilPINVOKE"))return strstr(mn,"AndroidDependencies")!=NULL||strstr(mn,"PlayServices")!=NULL||strstr(mn,"GooglePlayServices")!=NULL;
    if(!strcmp(cn,"FirebaseApp"))return !strcmp(mn,"CheckDependenciesInternal")||!strcmp(mn,"FixDependenciesAsync");
    return 0;
}

static void bender_fix124_add_root(const void*mi,uintptr_t code,const char*ns,const char*cn,const char*mn){
    if(!mi||!code||g_bender_fix124_root_count>=BENDER_FIX124_MAX_ROOTS)return;
    for(unsigned i=0;i<g_bender_fix124_root_count;i++)if(g_bender_fix124_roots[i].code==code)return;
    BenderFix124Root*r=&g_bender_fix124_roots[g_bender_fix124_root_count++];memset(r,0,sizeof(*r));
    snprintf(r->tag,sizeof(r->tag),"%s%s%s.%s",ns&&*ns?ns:"",ns&&*ns?".":"",cn?cn:"?",mn?mn:"?");r->code=code;r->mi=mi;r->span=bender_fix121_safe_span(code,0x180u,0x700u);
    Module*m=bender_module_for_runtime_addr(code);char sig[720]={0};bender_fix120_signature(mi,sig,sizeof(sig));
    logmsg("[BENDER_FIREBASE_ANDROID_DEP][ROOT] source=fix124 tag=%s image=%s namespace=%s class=%s method=%s method_info=%p code=%p module=%s module_offset=0x%" PRIxPTR " span=%zu %s physical_fix123_check_android_dependencies=1 return_contract=Firebase.GooglePlayServicesAvailability read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",r->tag,bender_fix15_image_name_2815(mi),ns?ns:"?",cn?cn:"?",mn?mn:"?",mi,(void*)code,m?m->name:"?",m?code-m->bias:0,r->span,sig[0]?sig:"return=? param_count=? params=?");
}

static void bender_fix124_discover(unsigned long*seen){
    if(seen){*seen=0;}
    bender_fix16_resolve_api_2816();
    if(!g_bender_fix16_domain_get_2816||!g_bender_fix16_domain_get_assemblies_2816||!g_bender_fix16_assembly_get_image_2816||!g_bender_fix16_image_get_class_count_2816||!g_bender_fix16_image_get_class_2816||!g_bender_fix16_class_get_methods_2816)return;
    void*d=g_bender_fix16_domain_get_2816();size_t ac=0;const void**as=d?g_bender_fix16_domain_get_assemblies_2816(d,&ac):NULL;if(!as)return;
    for(size_t ai=0;ai<ac;ai++){void*im=g_bender_fix16_assembly_get_image_2816(as[ai]);if(!im)continue;size_t cc=g_bender_fix16_image_get_class_count_2816(im);for(size_t ci=0;ci<cc;ci++){void*k=g_bender_fix16_image_get_class_2816(im,ci);if(!k)continue;const char*cn=g_bender_class_get_name_2812?g_bender_class_get_name_2812(k):NULL;const char*ns=g_bender_class_get_namespace_2812?g_bender_class_get_namespace_2812(k):NULL;void*it=NULL;const void*mi=NULL;while((mi=g_bender_fix16_class_get_methods_2816(k,&it))!=NULL){if(seen)(*seen)++;const char*mn=g_bender_method_get_name_2812?g_bender_method_get_name_2812(mi):NULL;if(!bender_fix124_name_match(cn,mn))continue;const char*img=bender_fix15_image_name_2815(mi);if(!img||strcmp(img,"Firebase.App.dll"))continue;int slot=-1;uintptr_t p=bender_fix16_method_ptr_2816(mi,&slot);(void)slot;if(p)bender_fix124_add_root(mi,p,ns,cn,mn);}}}
}

static void bender_fix124_collect_edges(const BenderFix124Root*r){
    if(!r||!r->code||!r->span){return;}
    uintptr_t lo=r->code,hi=r->code+r->span;
    for(size_t off=0;off+4u<=r->span;off+=4u){uint32_t ins=0;memcpy(&ins,(const void*)(r->code+off),4);const char*kind=NULL;if(bender_fix115_bl(ins))kind="BL";else if(bender_fix115_b(ins))kind="B";else continue;uintptr_t t=bender_fix115_imm26_target(r->code+off,ins);if(!t)continue;if(kind[0]=='B'&&kind[1]=='\0'&&t>=lo&&t<hi)continue;if(g_bender_fix124_edge_count>=BENDER_FIX124_MAX_EDGES)break;BenderFix124Edge*e=&g_bender_fix124_edges[g_bender_fix124_edge_count++];e->root=r->tag;e->off=off;e->kind=kind;e->target=t;e->matched=0;Module*m=bender_module_for_runtime_addr(t);logmsg("[BENDER_FIREBASE_ANDROID_DEP][EDGE] source=fix124 root=%s offset=0x%zx kind=%s target=%p module=%s module_offset=0x%" PRIxPTR " exact_method_match=pending read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",r->tag,off,kind,(void*)t,m?m->name:"host-or-unknown",m?t-m->bias:0);}
}

static void bender_fix124_match(unsigned long*seen,unsigned*matches){
    if(seen){*seen=0;}
    if(matches){*matches=0;}
    bender_fix16_resolve_api_2816();
    if(!g_bender_fix16_domain_get_2816||!g_bender_fix16_domain_get_assemblies_2816||!g_bender_fix16_assembly_get_image_2816||!g_bender_fix16_image_get_class_count_2816||!g_bender_fix16_image_get_class_2816||!g_bender_fix16_class_get_methods_2816)return;
    void*d=g_bender_fix16_domain_get_2816();size_t ac=0;const void**as=d?g_bender_fix16_domain_get_assemblies_2816(d,&ac):NULL;if(!as)return;
    for(size_t ai=0;ai<ac;ai++){void*im=g_bender_fix16_assembly_get_image_2816(as[ai]);if(!im)continue;size_t cc=g_bender_fix16_image_get_class_count_2816(im);for(size_t ci=0;ci<cc;ci++){void*k=g_bender_fix16_image_get_class_2816(im,ci);if(!k)continue;void*it=NULL;const void*mi=NULL;while((mi=g_bender_fix16_class_get_methods_2816(k,&it))!=NULL){if(seen)(*seen)++;int slot=-1;uintptr_t p=bender_fix16_method_ptr_2816(mi,&slot);(void)slot;if(!p)continue;for(unsigned ei=0;ei<g_bender_fix124_edge_count;ei++){BenderFix124Edge*e=&g_bender_fix124_edges[ei];if(e->matched||p!=e->target)continue;e->matched=1;if(matches)(*matches)++;const char*ns="?",*cn="?",*mn="?";bender_fix120_method_identity(mi,&ns,&cn,&mn);char sig[720]={0};bender_fix120_signature(mi,sig,sizeof(sig));logmsg("[BENDER_FIREBASE_ANDROID_DEP][EDGE_MATCH] source=fix124 root=%s offset=0x%zx image=%s namespace=%s class=%s method=%s method_info=%p code=%p %s kind=%s target_match=exact-code-pointer read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",e->root,e->off,bender_fix15_image_name_2815(mi),ns,cn,mn,mi,(void*)p,sig[0]?sig:"return=? param_count=? params=?",e->kind);}}}}
}

static void bender_fix124_tick(unsigned frame){
    if(g_bender_fix124_done||frame<160u||!g_bender_fix123_done)return;
    g_bender_fix124_done=1;memset(g_bender_fix124_roots,0,sizeof(g_bender_fix124_roots));memset(g_bender_fix124_edges,0,sizeof(g_bender_fix124_edges));g_bender_fix124_root_count=0;g_bender_fix124_edge_count=0;
    unsigned long ds=0,ms=0;unsigned matches=0;bender_fix124_discover(&ds);for(unsigned i=0;i<g_bender_fix124_root_count;i++)bender_fix124_collect_edges(&g_bender_fix124_roots[i]);bender_fix124_match(&ms,&matches);
    unsigned unresolved=0,native_edges=0;for(unsigned i=0;i<g_bender_fix124_edge_count;i++){BenderFix124Edge*e=&g_bender_fix124_edges[i];if(e->matched)continue;unresolved++;Module*m=bender_module_for_runtime_addr(e->target);if(m&&strcmp(m->name,"libil2cpp.so")){native_edges++;logmsg("[BENDER_FIREBASE_ANDROID_DEP][NATIVE_EDGE] source=fix124 root=%s offset=0x%zx kind=%s target=%p module=%s module_offset=0x%" PRIxPTR " managed_exact_match=0 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",e->root,e->off,e->kind,(void*)e->target,m->name,e->target-m->bias);}}
    logmsg("[BENDER_FIREBASE_ANDROID_DEP][DONE] source=fix124 frame=%u roots=%u edges=%u edge_matches=%u unresolved_edges=%u native_edges=%u discover_methods=%lu match_methods=%lu physical_fix123_check_android_dependencies=1 google_play_services_availability_not_fabricated=1 next=physical-log-identifies-android-play-services-frontier read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n",frame,g_bender_fix124_root_count,g_bender_fix124_edge_count,matches,unresolved,native_edges,ds,ms);
}

static void bender_fix106_tick(unsigned frame){
    if(!g_bender_fix106_obj)return;
    if(frame<=10u||(frame%120u)==0u)bender_fix106_snapshot(frame);
    bender_fix106_scan_movenext(frame);
    bender_fix107_trace(frame);
    bender_fix114_tick(frame);
    bender_fix115_tick(frame);
    bender_fix116_tick(frame);
    bender_fix117_tick(frame);
    bender_fix119_tick(frame);
    bender_fix120_tick(frame);
    bender_fix121_tick(frame);
    bender_fix123_tick(frame);
    bender_fix124_tick(frame);
}


/* FIX122 hooks are observation-only and are defined later, after the IL2CPP
 * MethodInfo helpers used by their post-exception call-graph inventory. */
static void bender_fix122_before_runtime_invoke(const void*,void*,const char*,const char*);
static void bender_fix122_after_runtime_invoke(const void*,void*,const char*,const char*,void**);

static void*bender_runtime_invoke_wrapper_2813(const void*method_info,void*obj,void**params,void**exc){
    unsigned long n=__atomic_add_fetch(&g_bender_runtime_invoke_total_2813,1ul,__ATOMIC_RELAXED);
    if(!g_bender_runtime_invoke_real_2813&&g_bender_il2cpp_module_2813)bender_fix13_resolve_method_api_2813(g_bender_il2cpp_module_2813);
    char key[256]={0};const char*method=NULL;int interesting=0;int raw_interesting=0;int app_candidate=0;int app_lifecycle=0;int app_lifecycle_log=0;
    const char*image="(image-unknown)";uint64_t app_begin_ns=0;unsigned long app_outer_total=n;
    /* FIX15: runtime_invoke remains long-run, but startup evidence is split into
       application assemblies versus engine/SDK traffic and false keyword matches. */
    if(!g_bender_managed_loadstage_done_2812){
        raw_interesting=bender_fix13_describe_method_2813(method_info,key,sizeof(key),&method);
        if(key[0]){
            __atomic_add_fetch(&g_bender_fix14_runtime_described_2814,1ul,__ATOMIC_RELAXED);
            bender_fix14_recent_note_2814(key,g_bender_fix13_frame_hint_2813);
            image=bender_fix15_image_name_2815(method_info);
            app_candidate=bender_fix15_app_candidate_2815(image);
            if(app_candidate){bender_fix15_app_note_2815(image,key,g_bender_fix13_frame_hint_2813);bender_fix16_root_note_2816(method_info,image,key,method,g_bender_fix13_frame_hint_2813);}
        }
        if(raw_interesting)__atomic_add_fetch(&g_bender_fix15_raw_interesting_2815,1ul,__ATOMIC_RELAXED);
        interesting=raw_interesting&&bender_fix15_bootstrap_candidate_2815(key,method);
        if(interesting)__atomic_add_fetch(&g_bender_fix15_bootstrap_candidates_2815,1ul,__ATOMIC_RELAXED);
        app_lifecycle=app_candidate&&bender_fix15_lifecycle_method_2815(method);
        if(app_lifecycle){
            app_lifecycle_log=bender_fix62_lifecycle_should_log_2862(g_bender_fix13_frame_hint_2813,image,key);
            if(app_lifecycle_log){
                app_begin_ns=bender_net_now_ns_2750();
                logmsg("[BENDER_MANAGED_APP][LIFECYCLE_ENTER] frame=%u total=%lu image=%s method=%s obj=%p source=fix62 sampled=1\n",g_bender_fix13_frame_hint_2813,n,image,key,obj);
            }
        }
        if(interesting){
            unsigned long in=__atomic_add_fetch(&g_bender_runtime_invoke_interesting_2813,1ul,__ATOMIC_RELAXED);
            bender_strlcpy(g_bender_runtime_last_2813,key,sizeof(g_bender_runtime_last_2813));
            g_bender_fix14_last_interesting_frame_2814=g_bender_fix13_frame_hint_2813;
            if(g_bender_managed_bootstrap_disabled_2812)__atomic_add_fetch(&g_bender_fix14_post_profiler_interesting_2814,1ul,__ATOMIC_RELAXED);
            if(bender_ascii_contains_ci_2812(key,"remoteconfig"))__atomic_add_fetch(&g_bender_runtime_remote_2813,1ul,__ATOMIC_RELAXED);
            if(bender_ascii_contains_ci_2812(key,"contentupdat"))__atomic_add_fetch(&g_bender_runtime_content_2813,1ul,__ATOMIC_RELAXED);
            if(bender_ascii_contains_ci_2812(key,"region"))__atomic_add_fetch(&g_bender_runtime_region_2813,1ul,__ATOMIC_RELAXED);
            if(bender_ascii_contains_ci_2812(key,"bundle"))__atomic_add_fetch(&g_bender_runtime_bundle_2813,1ul,__ATOMIC_RELAXED);
            if(bender_ascii_contains_ci_2812(key,"loading")||bender_ascii_contains_ci_2812(key,"splash"))__atomic_add_fetch(&g_bender_runtime_loading_2813,1ul,__ATOMIC_RELAXED);
            bender_fix13_note_milestone_2813(method);
            if(in<=96ul||(in%256ul)==0ul)logmsg("[BENDER_MANAGED_INVOKE][ENTER] n=%lu total=%lu frame=%u method=%s obj=%p source=il2cpp_runtime_invoke fix14_longrun=1 profiler_disabled=%d\n",in,n,g_bender_fix13_frame_hint_2813,key,obj,g_bender_managed_bootstrap_disabled_2812);
        }
    }
    if(!g_bender_runtime_invoke_real_2813){
        logmsg("[BENDER_MANAGED_INVOKE][ERROR] n=%lu reason=real-il2cpp_runtime_invoke-missing method=%s\n",n,key[0]?key:"(unknown)");
        return NULL;
    }
    void*fix81_ret=NULL;
    if(bender_fix81_try_postfx_bypass(key,method,params,&fix81_ret)){if(exc){*exc=NULL;}return fix81_ret;}
    bender_fix122_before_runtime_invoke(method_info,obj,key,image);
    void*ret=g_bender_runtime_invoke_real_2813(method_info,obj,params,exc);
    bender_fix122_after_runtime_invoke(method_info,obj,key,image,exc);
    bender_fix106_capture_start(method_info,image,key,method,ret,exc);
    if(app_lifecycle&&app_lifecycle_log){
        uint64_t app_end_ns=bender_net_now_ns_2750();
        unsigned long now_total=__atomic_load_n(&g_bender_runtime_invoke_total_2813,__ATOMIC_RELAXED);
        unsigned long nested=(now_total>app_outer_total)?(now_total-app_outer_total):0ul;
        uint64_t dur_us=(app_end_ns>=app_begin_ns)?((app_end_ns-app_begin_ns)/UINT64_C(1000)):0;
        logmsg("[BENDER_MANAGED_APP][LIFECYCLE_RETURN] frame=%u image=%s method=%s duration_us=%" PRIu64 " nested_runtime_invoke=%lu result=%p exception=%d source=fix62 sampled=1\n",g_bender_fix13_frame_hint_2813,image,key,dur_us,nested,ret,(exc&&*exc)?1:0);
    }
    if(exc&&*exc){
        unsigned long en=__atomic_add_fetch(&g_bender_runtime_invoke_exceptions_2813,1ul,__ATOMIC_RELAXED);
        logmsg("[BENDER_MANAGED_INVOKE][EXCEPTION] n=%lu exception=%p method=%s\n",en,*exc,key[0]?key:"(unknown)");
        if(g_bender_il2cpp_module_2813){
            uintptr_t fmt_exc=module_symbol(g_bender_il2cpp_module_2813,"il2cpp_format_exception");
            uintptr_t fmt_st=module_symbol(g_bender_il2cpp_module_2813,"il2cpp_format_stack_trace");
            char emsg[2048];char estack[4096];emsg[0]=0;estack[0]=0;
            if(fmt_exc)((void(*)(void*,char*,int))fmt_exc)(*exc,emsg,(int)sizeof(emsg));
            if(fmt_st)((void(*)(void*,char*,int))fmt_st)(*exc,estack,(int)sizeof(estack));
            logmsg("[BENDER_MANAGED_EXCEPTION_DETAIL] source=fix64 n=%lu method=%s format_exception=%p format_stack=%p message=%.1800s stack=%.3600s code_patch=0 forced_completion=0\n",
                   en,key[0]?key:"(unknown)",(void*)fmt_exc,(void*)fmt_st,emsg[0]?emsg:"(empty)",estack[0]?estack:"(empty)");
        }
    }else if(interesting&&(__atomic_load_n(&g_bender_runtime_invoke_interesting_2813,__ATOMIC_RELAXED)<=32ul)){
        logmsg("[BENDER_MANAGED_INVOKE][RETURN] method=%s result=%p exception=0\n",key,ret);
    }
    return ret;
}
static void*bender_runtime_invoke_dlsym_override_2813(Module*m,const char*symbol){
    if(!m||!symbol||strcmp(m->name,"libil2cpp.so")||strcmp(symbol,"il2cpp_runtime_invoke"))return NULL;
    uintptr_t real=module_symbol(m,symbol);if(!real)return NULL;
    g_bender_il2cpp_module_2813=m;g_bender_runtime_invoke_real_2813=(BenderIl2CppRuntimeInvoke2813)real;
    unsigned long n=__atomic_add_fetch(&g_bender_runtime_invoke_dlsym_2813,1ul,__ATOMIC_RELAXED);
    logmsg("[BENDER_MANAGED_INVOKE][DLSYM_OVERRIDE] n=%lu module=%s symbol=%s real=%p wrapper=%p\n",n,m->name,symbol,(void*)real,(void*)bender_runtime_invoke_wrapper_2813);
    return (void*)bender_runtime_invoke_wrapper_2813;
}
static const char*bender_fix13_stage_2813(void){
    if(g_bender_managed_loadstage_done_2812)return "load-stage-complete";
    if(g_bender_managed_region_done_2812)return "region-complete";
    if(g_bender_managed_region_bundle_done_2812)return "region-bundle-complete";
    if(g_bender_managed_load_region_2812)return "load-region-started";
    if(g_bender_managed_load_default_2812)return "load-default-region";
    if(g_bender_managed_content_done_2812)return "content-update-complete";
    if(g_bender_managed_rc_done_2812)return "remote-config-complete";
    if(g_bender_managed_rc_create_2812)return "remote-config-created";
    if(g_bender_fix15_bootstrap_candidates_2815)return "bootstrap-candidate-observed";
    if(g_bender_fix15_app_invokes_2815)return "app-startup-observed";
    if(g_bender_managed_interesting_total_2812||g_bender_fix15_raw_interesting_2815)return "managed-observed-no-bootstrap";
    return "pre-observation";
}
static const char*bender_fix13_gate_2813(void){
    if(!g_bender_managed_load_default_2812)return "before-load-default-region";
    if(!g_bender_managed_load_region_2812)return "between-load-default-and-load-region";
    if(!g_bender_managed_region_bundle_done_2812)return "waiting-region-bundle-completion";
    if(!g_bender_managed_region_done_2812)return "waiting-region-complete-callback";
    if(!g_bender_managed_loadstage_done_2812)return "waiting-load-stage-complete";
    return "bootstrap-chain-complete";
}
static const char*bender_fix13_visibility_2813(void){
    int p=g_bender_managed_enter_total_2812>0ul;int r=g_bender_runtime_invoke_total_2813>0ul;
    if(p&&r)return "profiler+runtime-invoke";
    if(p)return "profiler";
    if(r)return "runtime-invoke";
    return "none-yet";
}
static void bender_fix13_tail_health_2813(unsigned frame,const char*reason){
    unsigned long seq=__atomic_add_fetch(&g_bender_fix13_tail_health_2813,1ul,__ATOMIC_RELAXED);
    g_bender_fix13_frame_hint_2813=frame;
    const char*stage=bender_fix13_stage_2813();const char*gate=bender_fix13_gate_2813();const char*visibility=bender_fix13_visibility_2813();
    if(strcmp(g_bender_fix13_last_stage_2813,stage)){
        bender_strlcpy(g_bender_fix13_last_stage_2813,stage,sizeof(g_bender_fix13_last_stage_2813));
        logmsg("[BENDER_MANAGED_BOOTSTRAP][STAGE] seq=%lu frame=%u stage=%s gate=%s visibility=%s\n",seq,frame,stage,gate,visibility);
    }
    unsigned age=(g_bender_fix14_last_interesting_frame_2814<=frame)?(frame-g_bender_fix14_last_interesting_frame_2814):0u;
    unsigned app_age=(g_bender_fix15_last_app_frame_2815<=frame)?(frame-g_bender_fix15_last_app_frame_2815):0u;
    logmsg("[BENDER_MANAGED_BOOTSTRAP][HEALTH] source=fix15-tail seq=%lu reason=%s frame=%u installed=%d profiler_disabled=%d profiler_enter=%lu profiler_interesting=%lu runtime_invoke=%lu runtime_described=%lu raw_interesting=%lu bootstrap_candidates=%lu invoke_interesting=%lu post_profiler_interesting=%lu last_interesting_frame=%u interesting_age_frames=%u app_invokes=%lu app_transitions=%lu last_app_frame=%u app_quiet_age_frames=%u invoke_exceptions=%lu unique=%lu categories=remote:%lu,content:%lu,region:%lu,bundle:%lu,loading:%lu invoke_categories=remote:%lu,content:%lu,region:%lu,bundle:%lu,loading:%lu milestones=rc_create:%d,rc_done:%d,content_done:%d,load_default:%d,load_region:%d,region_bundle_done:%d,region_done:%d,loadstage_done:%d stage=%s gate=%s visibility=%s last_profiler=%s last_invoke=%s last_app=%s classifier=runtime-invoke-longrun fix15_classifier=app-startup-semantic\n",seq,reason?reason:"periodic",frame,g_bender_managed_bootstrap_installed_2812,g_bender_managed_bootstrap_disabled_2812,g_bender_managed_enter_total_2812,g_bender_managed_interesting_total_2812,g_bender_runtime_invoke_total_2813,g_bender_fix14_runtime_described_2814,g_bender_fix15_raw_interesting_2815,g_bender_fix15_bootstrap_candidates_2815,g_bender_runtime_invoke_interesting_2813,g_bender_fix14_post_profiler_interesting_2814,g_bender_fix14_last_interesting_frame_2814,age,g_bender_fix15_app_invokes_2815,g_bender_fix15_app_recent_seq_2815,g_bender_fix15_last_app_frame_2815,app_age,g_bender_runtime_invoke_exceptions_2813,g_bender_managed_unique_2812,g_bender_managed_remote_2812,g_bender_managed_content_2812,g_bender_managed_region_2812,g_bender_managed_bundle_2812,g_bender_managed_loading_2812,g_bender_runtime_remote_2813,g_bender_runtime_content_2813,g_bender_runtime_region_2813,g_bender_runtime_bundle_2813,g_bender_runtime_loading_2813,g_bender_managed_rc_create_2812,g_bender_managed_rc_done_2812,g_bender_managed_content_done_2812,g_bender_managed_load_default_2812,g_bender_managed_load_region_2812,g_bender_managed_region_bundle_done_2812,g_bender_managed_region_done_2812,g_bender_managed_loadstage_done_2812,stage,gate,visibility,g_bender_managed_last_2812[0]?g_bender_managed_last_2812:"(none)",g_bender_runtime_last_2813[0]?g_bender_runtime_last_2813:"(none)",g_bender_fix15_last_app_2815[0]?g_bender_fix15_last_app_2815:"(none)");
    logmsg("[BENDER_MANAGED_BOOTSTRAP][GATE] source=fix15-tail frame=%u stage=%s gate=%s visibility=%s evidence=profiler:%lu,runtime_invoke:%lu,interesting:%lu+%lu,raw_runtime:%lu,bootstrap_candidates:%lu,post_profiler:%lu forced_completion=0\n",frame,stage,gate,visibility,g_bender_managed_enter_total_2812,g_bender_runtime_invoke_total_2813,g_bender_managed_interesting_total_2812,g_bender_runtime_invoke_interesting_2813,g_bender_fix15_raw_interesting_2815,g_bender_fix15_bootstrap_candidates_2815,g_bender_fix14_post_profiler_interesting_2814);
    bender_fix14_recent_health_2814(frame);
    bender_fix15_emit_history_2815(frame);
    logmsg("[BENDER_IL2CPP_PLUGIN][HEALTH] source=fix15-tail frame=%u override=%lu set=%lu resolve=%lu guest_hit=%lu unity_hit=%lu miss=%lu runtime_invoke_override=%lu last_request=%s last_result=%s\n",frame,g_bender_il2cpp_plugin_dlsym_overrides_2812,g_bender_il2cpp_plugin_set_calls_2812,g_bender_il2cpp_plugin_resolve_calls_2812,g_bender_il2cpp_plugin_guest_hits_2812,g_bender_il2cpp_plugin_unity_hits_2812,g_bender_il2cpp_plugin_misses_2812,g_bender_runtime_invoke_dlsym_2813,g_bender_il2cpp_plugin_last_request_2812[0]?g_bender_il2cpp_plugin_last_request_2812:"(none)",g_bender_il2cpp_plugin_last_result_2812[0]?g_bender_il2cpp_plugin_last_result_2812:"(none)");
}
static void __attribute__((unused)) bender_fix13_install_2813(Module*im){
    if(g_bender_fix13_installed_2813)return;
    if(!im||im->init_state!=2){logmsg("[BENDER_MANAGED_BOOTSTRAP][SKIP] source=fix13 reason=libil2cpp-not-ready\n");return;}
    bender_fix13_resolve_method_api_2813(im);
    g_bender_fix13_installed_2813=1;
    logmsg("[BENDER_MANAGED_BOOTSTRAP][START] source=fix19 generic=1 forced_completion=0 tail_period_frames=120 direct_call_graph=1 callsite_trace=1 tail_branch_scan=1 indirect_callsite_scan=1 root_priority=Assembly-CSharp per_root_budget=1 tail_chain=1 code_patch=0 profiler_installed=%d runtime_invoke_real=%p runtime_classifier_longrun=1 app_startup_trace=1 semantic_region_filter=1 stop_on_loadstage_complete=1\n",g_bender_managed_bootstrap_installed_2812,(void*)g_bender_runtime_invoke_real_2813);
    logmsg("[BENDER_MANAGED_INVOKE][INSTALL] real=%p method_name=%p method_class=%p class_name=%p class_ns=%p mode=dlsym-fallback-observe-only fix15_app_startup_classifier=1\n",(void*)g_bender_runtime_invoke_real_2813,(void*)g_bender_method_get_name_2812,(void*)g_bender_method_get_class_2812,(void*)g_bender_class_get_name_2812,(void*)g_bender_class_get_namespace_2812);
    bender_fix13_tail_health_2813(0u,"install");
}
static void __attribute__((unused)) bender_fix13_frame_tick_2813(unsigned frame){
    g_bender_fix13_frame_hint_2813=frame;
    bender_fix16_direct_tick_2816(frame);
    bender_fix18_callsite_tick_2818(frame);
    bender_fix19_tick_2819(frame);
    bender_fix106_tick(frame);
    if(frame>0u&&(frame%120u)==0u)bender_fix13_tail_health_2813(frame,"periodic-tail");
}

static int bender_fmod_force_nosound_enabled(void){
    const char *e=getenv("BENDER_FMOD_FORCE_NOSOUND");
    return (e && *e && strcmp(e,"0")!=0) ? 1 : 0;
}
static int bender_fix63_fmod_recovery_enabled_2863(void){
    const char *e=getenv("BENDER_FMOD_STUDIO_RECOVERY");
    if(!e||!*e)return 1;
    return strcmp(e,"0")&&strcasecmp(e,"off")&&strcasecmp(e,"false")&&strcasecmp(e,"disabled");
}
static int g_bender_fix65_fmod_pair_announced_2865;
static int bender_fix65_fmod_parsed_2865(Module*m){
    return (m&&m->raw&&m->raw_size>0u&&m->path[0])?1:0;
}
static int bender_fix64_fmod_logging_enabled_2864(void){
    const char *e=getenv("BENDER_FMOD_LOGGING_LIBS");
    int disabled=(e&&*e&&(!strcmp(e,"0")||!strcasecmp(e,"off")||!strcasecmp(e,"false")||!strcasecmp(e,"disabled")))?1:0;
    Module*fm=guest_module_from_path("libfmodL.so");
    Module*sm=guest_module_from_path("libfmodstudioL.so");
    int core_present=bender_fix65_fmod_parsed_2865(fm);
    int studio_present=bender_fix65_fmod_parsed_2865(sm);
    int enabled=(!disabled&&core_present&&studio_present)?1:0;
    if(!g_bender_fix65_fmod_pair_announced_2865){
        g_bender_fix65_fmod_pair_announced_2865=1;
        logmsg("[BENDER_FMOD_LOGGING][PAIR_STATE] source=fix65 enabled=%d disabled_by_env=%d core_present=%d core_mapped=%d core_lazy=%d core_reloc=%d core_raw=%zu studio_present=%d studio_mapped=%d studio_lazy=%d studio_reloc=%d studio_raw=%zu policy=parsed-pair-not-reserve code_patch=0 forced_completion=0\n",
               enabled,disabled,core_present,(fm&&fm->reserve)?1:0,fm?fm->lazy_module:-1,fm?fm->relocate_state:-1,fm?fm->raw_size:0u,
               studio_present,(sm&&sm->reserve)?1:0,sm?sm->lazy_module:-1,sm?sm->relocate_state:-1,sm?sm->raw_size:0u);
    }
    return enabled;
}
static Module*bender_fix63_fmod_module_2863(void){
    if(bender_fix64_fmod_logging_enabled_2864()){
        Module*lm=guest_module_from_path("libfmodL.so");
        if(lm)return lm; /* FIX65: lazy mapping is valid here; plugin dlopen activates it. */
    }
    Module*m=guest_module_from_path("libfmod.so");
    if(!m||!m->reserve){
        Module*lm=guest_module_from_path("libfmodL.so");
        if(lm&&lm->reserve)m=lm;
    }
    return m;
}
static Module*bender_fix63_fmodstudio_module_2863(void){
    if(bender_fix64_fmod_logging_enabled_2864()){
        Module*lm=guest_module_from_path("libfmodstudioL.so");
        if(lm)return lm; /* FIX65: do not fall back merely because the lazy image is unmapped. */
    }
    Module*m=guest_module_from_path("libfmodstudio.so");
    if(!m||!m->reserve){
        Module*lm=guest_module_from_path("libfmodstudioL.so");
        if(lm&&lm->reserve)m=lm;
    }
    return m;
}
static uintptr_t bender_fix63_symbol_pair_2863(Module*m,const char*a,const char*b){
    uintptr_t p=(m&&a)?module_symbol(m,a):0;
    if(!p&&m&&b)p=module_symbol(m,b);
    return p;
}
typedef int (*BenderFmodSystemSetOutputFn)(void *system,int output_type);
typedef int (*BenderFmodStudioSystemInitializeFn)(void *studio,int maxchannels,unsigned int studio_flags,unsigned int core_flags,void *extra_driver_data);
typedef int (*BenderFmodStudioGetCoreSystemFn)(void *studio,void **core_out);
typedef int (*BenderFmodSystemGetOutputFn)(void *system,int *output_type);
static unsigned long g_bender_fix63_setoutput_calls_2863;
static unsigned long g_bender_fix63_setoutput_fallbacks_2863;
static unsigned long g_bender_fix63_init_calls_2863;
static unsigned long g_bender_fix63_init_retries_2863;
static unsigned long g_bender_fix63_init_recovered_2863;
static unsigned long g_bender_fix64_debug_messages_2864;
static int g_bender_fix64_debug_init_attempted_2864;
typedef int (*BenderFmodDebugCallbackFn2864)(unsigned int,const char*,int,const char*,const char*);
typedef int (*BenderFmodDebugInitializeFn2864)(unsigned int,int,BenderFmodDebugCallbackFn2864,const char*);
static int bender_fix64_fmod_debug_callback_2864(unsigned int flags,const char*file,int line,const char*func,const char*message){
    unsigned long n=++g_bender_fix64_debug_messages_2864;
    logmsg("[BENDER_FMOD_DEBUG][MESSAGE] source=fix65 n=%lu flags=0x%x file=%.180s line=%d func=%.160s message=%.900s\n",
           n,flags,file?file:"(null)",line,func?func:"(null)",message?message:"(null)");
    return 0;
}
static void bender_fix64_fmod_debug_enable_2864(void){
    if(g_bender_fix64_debug_init_attempted_2864)return;
    g_bender_fix64_debug_init_attempted_2864=1;
    if(!bender_fix64_fmod_logging_enabled_2864()){
        logmsg("[BENDER_FMOD_DEBUG][SKIP] source=fix65 reason=logging-libs-disabled-or-pair-missing\n");
        return;
    }
    Module*fm=bender_fix63_fmod_module_2863();
    if(!fm||!fm->reserve){
        logmsg("[BENDER_FMOD_DEBUG][ERROR] source=fix65 module=%s mapped=%d lazy=%d reloc=%d reason=logging-core-not-active-before-studio-init\n",
               fm?fm->name:"(null)",(fm&&fm->reserve)?1:0,fm?fm->lazy_module:-1,fm?fm->relocate_state:-1);
        return;
    }
    uintptr_t p=bender_fix63_symbol_pair_2863(fm,"FMOD_Debug_Initialize","FMOD5_Debug_Initialize");
    if(!p){
        logmsg("[BENDER_FMOD_DEBUG][ERROR] source=fix65 module=%s reason=Debug_Initialize-missing\n",fm?fm->name:"(null)");
        return;
    }
    /* FMOD 2.x: ERROR|WARNING|LOG plus TYPE_TRACE, CALLBACK mode. */
    const unsigned int flags=0x00000807u;
    const int mode_callback=2;
    int rc=((BenderFmodDebugInitializeFn2864)p)(flags,mode_callback,bender_fix64_fmod_debug_callback_2864,NULL);
    logmsg("[BENDER_FMOD_DEBUG][INIT] source=fix65 module=%s flags=0x%x mode=callback rc=%d callback=%p code_patch=0 forced_completion=0\n",
           fm?fm->name:"(null)",flags,rc,(void*)bender_fix64_fmod_debug_callback_2864);
}
static int bender_fix63_fmod_system_set_output_trace_2863(void *system,int requested_output){
    Module *fm=bender_fix63_fmod_module_2863();
    uintptr_t real=bender_fix63_symbol_pair_2863(fm,"FMOD_System_SetOutput","FMOD5_System_SetOutput");
    const int FMOD_OUTPUTTYPE_NOSOUND_BENDER=2;
    unsigned long n=++g_bender_fix63_setoutput_calls_2863;
    if(!real){
        logmsg("[BENDER_FMOD_STUDIO_RECOVERY][SET_OUTPUT][ERROR] source=fix63 n=%lu system=%p requested=%d reason=real-symbol-missing code_patch=0 forced_completion=0\n",n,system,requested_output);
        return 1;
    }
    int effective=bender_fmod_force_nosound_enabled()?FMOD_OUTPUTTYPE_NOSOUND_BENDER:requested_output;
    int rc=((BenderFmodSystemSetOutputFn)real)(system,effective);
    logmsg("[BENDER_FMOD_STUDIO_RECOVERY][SET_OUTPUT] source=fix63 n=%lu system=%p requested=%d effective=%d rc=%d force_nosound=%d policy=real-api-first code_patch=0 forced_completion=0\n",n,system,requested_output,effective,rc,bender_fmod_force_nosound_enabled());
    if(rc!=0&&effective!=FMOD_OUTPUTTYPE_NOSOUND_BENDER&&bender_fix63_fmod_recovery_enabled_2863()){
        int fallback_rc=((BenderFmodSystemSetOutputFn)real)(system,FMOD_OUTPUTTYPE_NOSOUND_BENDER);
        ++g_bender_fix63_setoutput_fallbacks_2863;
        logmsg("[BENDER_FMOD_STUDIO_RECOVERY][SET_OUTPUT_FALLBACK] source=fix63 n=%lu system=%p original_rc=%d fallback=NOSOUND(%d) fallback_rc=%d policy=real-api-error-triggered code_patch=0 forced_completion=0\n",n,system,rc,FMOD_OUTPUTTYPE_NOSOUND_BENDER,fallback_rc);
        if(fallback_rc==0)return 0;
    }
    return rc;
}
static int bender_fmod_studio_system_initialize_trace(void *studio,int maxchannels,unsigned int studio_flags,unsigned int core_flags,void *extra_driver_data){
    Module *sm=bender_fix63_fmodstudio_module_2863();
    Module *fm=bender_fix63_fmod_module_2863();
    bender_fix64_fmod_debug_enable_2864();
    uintptr_t real=bender_fix63_symbol_pair_2863(sm,"FMOD_Studio_System_Initialize",NULL);
    uintptr_t getcore=bender_fix63_symbol_pair_2863(sm,"FMOD_Studio_System_GetLowLevelSystem","FMOD_Studio_System_GetCoreSystem");
    uintptr_t getoutput=bender_fix63_symbol_pair_2863(fm,"FMOD_System_GetOutput","FMOD5_System_GetOutput");
    uintptr_t setoutput=bender_fix63_symbol_pair_2863(fm,"FMOD_System_SetOutput","FMOD5_System_SetOutput");
    const int FMOD_OUTPUTTYPE_NOSOUND_BENDER=2;
    void *core=NULL;int getcore_rc=-1,output=-1,getoutput_rc=-1;
    unsigned long n=++g_bender_fix63_init_calls_2863;
    if(getcore)getcore_rc=((BenderFmodStudioGetCoreSystemFn)getcore)(studio,&core);
    if(core&&getoutput)getoutput_rc=((BenderFmodSystemGetOutputFn)getoutput)(core,&output);
    logmsg("[BENDER_FMOD_INIT_TRACE][PRE] source=fix63 n=%lu studio=%p core=%p getcore_rc=%d output=%d getoutput_rc=%d maxchannels=%d studio_flags=0x%x core_flags=0x%x extra=%p recovery=%d code_patch=0 forced_completion=0\n",n,studio,core,getcore_rc,output,getoutput_rc,maxchannels,studio_flags,core_flags,extra_driver_data,bender_fix63_fmod_recovery_enabled_2863());
    if(!real){logmsg("[BENDER_FMOD_INIT_TRACE][ERROR] source=fix63 real FMOD_Studio_System_Initialize missing\n");return 1;}
    set_checkpoint("FMOD Studio Initialize maxchannels=%d studio_flags=0x%x core_flags=0x%x output=%d",maxchannels,studio_flags,core_flags,output);
    int rc=((BenderFmodStudioSystemInitializeFn)real)(studio,maxchannels,studio_flags,core_flags,extra_driver_data);
    output=-1;getoutput_rc=-1;if(core&&getoutput)getoutput_rc=((BenderFmodSystemGetOutputFn)getoutput)(core,&output);
    logmsg("[BENDER_FMOD_INIT_TRACE][POST] source=fix63 n=%lu studio=%p core=%p rc=%d output=%d getoutput_rc=%d code_patch=0 forced_completion=0\n",n,studio,core,rc,output,getoutput_rc);
    if(rc!=0&&core&&setoutput&&bender_fix63_fmod_recovery_enabled_2863()){
        int set_rc=((BenderFmodSystemSetOutputFn)setoutput)(core,FMOD_OUTPUTTYPE_NOSOUND_BENDER);
        logmsg("[BENDER_FMOD_STUDIO_RECOVERY][INIT_FALLBACK] source=fix63 n=%lu studio=%p core=%p original_rc=%d set_output=NOSOUND(%d) set_rc=%d policy=real-api-error-triggered code_patch=0 forced_completion=0\n",n,studio,core,rc,FMOD_OUTPUTTYPE_NOSOUND_BENDER,set_rc);
        if(set_rc==0){
            ++g_bender_fix63_init_retries_2863;
            int retry_rc=((BenderFmodStudioSystemInitializeFn)real)(studio,maxchannels,studio_flags,core_flags,extra_driver_data);
            if(retry_rc==0)++g_bender_fix63_init_recovered_2863;
            output=-1;getoutput_rc=-1;if(getoutput)getoutput_rc=((BenderFmodSystemGetOutputFn)getoutput)(core,&output);
            logmsg("[BENDER_FMOD_STUDIO_RECOVERY][INIT_RETRY] source=fix63 n=%lu studio=%p core=%p retry_rc=%d output=%d getoutput_rc=%d recovered=%d totals=set_calls:%lu,set_fallbacks:%lu,init_calls:%lu,init_retries:%lu,init_recovered:%lu code_patch=0 forced_completion=0\n",n,studio,core,retry_rc,output,getoutput_rc,retry_rc==0,g_bender_fix63_setoutput_calls_2863,g_bender_fix63_setoutput_fallbacks_2863,g_bender_fix63_init_calls_2863,g_bender_fix63_init_retries_2863,g_bender_fix63_init_recovered_2863);
            return retry_rc;
        }
    }
    return rc;
}

static int run_init_array(Module *m) {
    if(!m) return -1;
    if(m->init_state==2) return 0;
    if(m->init_state==1){logmsg("[UNITY_PROBE][CTOR][REENTRANT] module=%s\n",m->name);return 0;}
    m->init_state=1;
    if(!m->init_array||!m->init_array_count){m->init_state=2;logmsg("[UNITY_PROBE][CTOR][NONE] module=%s\n",m->name);return 0;}
    uintptr_t *arr=(uintptr_t*)(m->bias+m->init_array);
    logmsg("[UNITY_PROBE][CTOR][BEGIN] module=%s count=%zu\n",m->name,m->init_array_count);
    for(size_t i=0;i<m->init_array_count;i++){
        uintptr_t fn=arr[i]; if(!fn||fn==(uintptr_t)-1)continue;
        set_checkpoint("ctor module=%s index=%zu/%zu fn=%p",m->name,i,m->init_array_count,(void*)fn);
        install_crash_handlers();
        logmsg("[UNITY_PROBE][CTOR] module=%s index=%zu fn=%p\n",m->name,i,(void*)fn);
        int il2cpp=!strcmp(m->name,"libil2cpp.so");
        size_t alloc_before=g_guest_alloc_total, free_before=g_guest_free_total, active_before=g_guest_alloc_active;
        if(il2cpp || (i%25u)==0u) guest_heap_validate_all("before-ctor");
        if(il2cpp && i<=5u){g_guest_heap_trace_detail=1;logmsg("[BENDER_HEAP][CTOR_TRACE][BEGIN] module=%s index=%zu alloc=%zu free=%zu active=%zu\n",m->name,i,alloc_before,free_before,active_before);}
        ((void(*)(void))fn)();
        if(il2cpp && i<=5u){g_guest_heap_trace_detail=0;logmsg("[BENDER_HEAP][CTOR_TRACE][END] module=%s index=%zu new_alloc=%zu new_free=%zu active_before=%zu active_after=%zu\n",m->name,i,g_guest_alloc_total-alloc_before,g_guest_free_total-free_before,active_before,g_guest_alloc_active);}
        if(il2cpp || (i%25u)==0u || i+1u==m->init_array_count) guest_heap_validate_all("after-ctor");
    }
    m->init_state=2; set_checkpoint("ctor complete module=%s count=%zu",m->name,m->init_array_count);
    logmsg("[UNITY_PROBE][CTOR][OK] module=%s count=%zu\n",m->name,m->init_array_count); return 0;
}
static void set_guest_dlerror(const char *fmt,...) {va_list ap;va_start(ap,fmt);vsnprintf(g_guest_dlerror,sizeof(g_guest_dlerror),fmt,ap);va_end(ap);logmsg("[BENDER_DL][ERROR] %s\n",g_guest_dlerror);}
static int relocate_module(Module *m, bool verbose);
static void protect_module(Module *m);

static int bender_guest_module_prepare(Module *m){
    if(!m)return -1;
    if(m->relocate_state==2 && m->reserve)return 0;
    if(m->relocate_state==1){
        logmsg("[BENDER_LAZY_MODULE][REENTRANT] module=%s state=relocating\n",m->name);
        return 0;
    }
    m->relocate_state=1;
    set_checkpoint("lazy prepare begin module=%s mapped=%d",m->name,m->reserve?1:0);
    install_crash_handlers();

    /* Android linker inicializa dependencias antes do modulo solicitado.
       Para Firebase isso garante App antes de Analytics/Auth/RemoteConfig. */
    for(int n=0;n<m->needed_count;n++){
        Module *dep=guest_module_from_path(m->needed[n]);
        if(dep && dep!=m && dep->lazy_module){
            int drc=bender_guest_module_prepare(dep);
            if(drc!=0){
                m->relocate_state=0;
                set_guest_dlerror("dependencia lazy %s falhou rc=%d para %s",dep->name,drc,m->name);
                return -10+drc;
            }
        }
    }
    if(!m->reserve){
        set_checkpoint("lazy map module=%s",m->name);
        int mrc=map_module(m);
        if(mrc!=0){
            m->relocate_state=0;
            set_guest_dlerror("map lazy %s falhou rc=%d",m->name,mrc);
            logmsg("[BENDER_LAZY_MODULE][MAP_FAIL] module=%s rc=%d\n",m->name,mrc);
            return -20+mrc;
        }
    }
    m->relative_count=m->self_count=m->cross_count=m->host_count=m->missing_count=m->unsupported_count=0;
    set_checkpoint("lazy relocate module=%s ranges=%d symcount=%zu",m->name,m->range_count,m->symcount);
    relocate_module(m,false);
    if(m->missing_count || m->unsupported_count){
        m->relocate_state=0;
        set_guest_dlerror("relocation lazy %s missing=%zu unsupported=%zu",m->name,m->missing_count,m->unsupported_count);
        logmsg("[BENDER_LAZY_MODULE][RELOC_FAIL] module=%s missing=%zu unsupported=%zu\n",m->name,m->missing_count,m->unsupported_count);
        return -30;
    }
    protect_module(m);
    m->protect_state=2;
    m->relocate_state=2;
    set_checkpoint("lazy prepare complete module=%s",m->name);
    logmsg("[BENDER_LAZY_MODULE][READY] module=%s mapped=%p span=%zu reloc=ok policy=dlopen-on-demand\n",m->name,m->reserve,m->reserve_size);
    return 0;
}

static int bender_guest_module_activate(Module *m){
    if(!m)return -1;
    if(m->lazy_module){
        int prc=bender_guest_module_prepare(m);
        if(prc!=0)return prc;
    }
    if(!strcmp(m->name,"libil2cpp.so")&&g_guest_block_il2cpp){
        set_guest_dlerror("libil2cpp bloqueada pela fase native-load-unity"); return -2;
    }
    if(!strcmp(m->name,"libunity.so")){
        if(g_guest_allow_unity_init)run_init_array(m);
        return 0;
    }
    if(!strcmp(m->name,"libil2cpp.so")){
        if(g_guest_allow_il2cpp_init)run_init_array(m);
        return 0;
    }
    if(!strcmp(m->name,"libmain.so"))return 0;
    if(run_init_array(m)!=0){
        set_guest_dlerror("falha init_array modulo opcional %s",m->name); return -3;
    }
    uintptr_t jni=module_symbol(m,"JNI_OnLoad");
    if(jni&&m->jni_onload_state==0){
        m->jni_onload_state=1;
        set_checkpoint("optional JNI_OnLoad begin module=%s fn=%p",m->name,(void*)jni);
        install_crash_handlers();
        bjint ver=((bjint(*)(BVM*,void*))jni)(&g_vm,NULL);
        if(g_jni_exception||g_jni_fatal_count){
            set_guest_dlerror("JNI_OnLoad modulo opcional %s deixou exception=%d fatal=%d",m->name,g_jni_exception,g_jni_fatal_count);
            return -4;
        }
        m->jni_onload_state=2;
        logmsg("[BENDER_OPTIONAL_MODULE][JNI_ONLOAD][OK] module=%s version=0x%08x fn=%p\n",m->name,(unsigned)ver,(void*)jni);
    }
    logmsg("[BENDER_OPTIONAL_MODULE][ACTIVE] module=%s init_state=%d jni_onload=%d\n",m->name,m->init_state,m->jni_onload_state);
    return 0;
}
static void *bender_guest_dlopen(const char *filename,int flags) {
    void*caller=__builtin_return_address(0);const char*caller_mod=guest_module_name_for_address(caller);
    logmsg("[BENDER_DL][REQUEST] path=%s flags=0x%x caller=%p caller_module=%s\n",filename?filename:"(null)",flags,caller,caller_mod?caller_mod:"host/unknown");
    if(!filename)return dlopen(NULL,flags);
    Module *m=guest_module_from_path(filename);
    if(m){
        if(!strcmp(m->name,"libmcprime.so")){++g_mcprime_dlopen_requests_2762;logmsg("[BENDER_FOOTBALL_NATIVE][DLOPEN] module=%s request=%lu caller=%s pre_init=%d pre_jni=%d\n",m->name,g_mcprime_dlopen_requests_2762,caller_mod?caller_mod:"?",m->init_state,m->jni_onload_state);}
        else if(!strcmp(m->name,"libmcservices.so")){++g_mcservices_dlopen_requests_2762;logmsg("[BENDER_FOOTBALL_NATIVE][DLOPEN] module=%s request=%lu caller=%s pre_init=%d pre_jni=%d\n",m->name,g_mcservices_dlopen_requests_2762,caller_mod?caller_mod:"?",m->init_state,m->jni_onload_state);}
        if(bender_guest_module_activate(m)!=0)return NULL;
        logmsg("[BENDER_DL] guest handle module=%s handle=%p init_state=%d jni_onload=%d\n",m->name,(void*)m,m->init_state,m->jni_onload_state);
        g_guest_dlerror[0]=0;return m;
    }
    const char *base=strrchr(filename,'/');base=base?base+1:filename;const char *host=NULL;
    if(!strcmp(base,"libc.so"))host="libc.so.6";
    else if(!strcmp(base,"libEGL.so"))host="libEGL.so.1";
    else if(!strcmp(base,"libGLESv2.so")||!strcmp(base,"libGLESv3.so"))host="libGLESv2.so.2";
    else if(!strcmp(base,"libz.so"))host="libz.so.1";
    else if(!strcmp(base,"libm.so"))host="libm.so.6";
    else if(!strcmp(base,"libdl.so"))host="libdl.so.2";
    else if(!strcmp(base,"libstdc++.so"))host="libstdc++.so.6";
    else if(!strcmp(base,"libOpenSLES.so"))host="libOpenSLES.so";
    if(!strcmp(base,"libandroid.so")){
        g_guest_dlerror[0]=0;
        logmsg("[BENDER_ANDROID_DL][OPEN] guest=%s handle=%p policy=synthetic-asset-nativewindow\n",base,(void*)&g_bender_libandroid_token);
        return &g_bender_libandroid_token;
    }
    if(!strcmp(base,"libmediandk.so")){
        g_guest_dlerror[0]=0;
        logmsg("[BENDER_MEDIANDK][OPEN] guest=%s handle=%p policy=synthetic-video-eos\n",base,(void*)&g_bender_libmediandk_token);
        return &g_bender_libmediandk_token;
    }
    if(!strcmp(base,"libvulkan.so")||!strcmp(base,"libvulkan.so.1")){
        set_checkpoint("guest dlopen Vulkan Android bloqueado para fallback GLES: %s flags=0x%x",base,flags);
        logmsg("[BENDER_VULKAN][DISABLED] guest=%s action=return-null reason=android-vulkan-handle-incompativel-com-loader-host fallback=GLES/EGL\n",base);
        set_guest_dlerror("Vulkan Android indisponivel neste probe R36S; use fallback GLES/EGL");
        return NULL;
    }
    if(host){
        set_checkpoint("guest dlopen host-map %s -> %s flags=0x%x",base,host,flags);
        void*h=dlopen(host,flags);
        logmsg("[BENDER_DL] host-map %s -> %s handle=%p\n",base,host,h);
        if(!h)set_guest_dlerror("host dlopen %s: %s",host,dlerror());
        else g_guest_dlerror[0]=0;
        return h;
    }
    set_guest_dlerror("Android library ainda sem bridge dinamica: %s",filename);return NULL;
}
static void *bender_guest_dlsym(void *handle,const char *symbol) {
    if(handle==(void*)&g_bender_libandroid_token){
        uintptr_t bp=bridge_symbol(symbol);
        logmsg("[BENDER_ANDROID_DL][DLSYM] symbol=%s -> %p\n",symbol?symbol:"(null)",(void*)bp);
        if(!bp)set_guest_dlerror("libandroid.so synthetic sem bridge para %s",symbol?symbol:"(null)"); else g_guest_dlerror[0]=0;
        return (void*)bp;
    }
    if(handle==(void*)&g_bender_libmediandk_token){
        uintptr_t bp=bridge_symbol(symbol);
        logmsg("[BENDER_MEDIANDK][DLSYM] symbol=%s -> %p\n",symbol?symbol:"(null)",(void*)bp);
        if(!bp)set_guest_dlerror("libmediandk.so synthetic sem bridge para %s",symbol?symbol:"(null)"); else g_guest_dlerror[0]=0;
        return (void*)bp;
    }
    Module*m=guest_module_from_handle(handle);
    if(m){
        void *fix13_override=bender_runtime_invoke_dlsym_override_2813(m,symbol);
        if(fix13_override){g_guest_dlerror[0]=0;return fix13_override;}
        void *fix12_override=bender_il2cpp_plugin_dlsym_override_2812(m,symbol);
        if(fix12_override){g_guest_dlerror[0]=0;return fix12_override;}
        if(symbol && (!strcmp(m->name,"libfmod.so")||!strcmp(m->name,"libfmodL.so")) && (!strcmp(symbol,"FMOD5_System_SetOutput")||!strcmp(symbol,"FMOD_System_SetOutput"))){
            g_guest_dlerror[0]=0;
            logmsg("[BENDER_FMOD_STUDIO_RECOVERY][DLSYM_SET_OUTPUT] source=fix63 module=%s symbol=%s wrapper=%p recovery=%d\n",m->name,symbol,(void*)bender_fix63_fmod_system_set_output_trace_2863,bender_fix63_fmod_recovery_enabled_2863());
            return (void*)bender_fix63_fmod_system_set_output_trace_2863;
        }
        if(symbol && (!strcmp(m->name,"libfmodstudio.so")||!strcmp(m->name,"libfmodstudioL.so")) && !strcmp(symbol,"FMOD_Studio_System_Initialize")){
            g_guest_dlerror[0]=0;
            logmsg("[BENDER_FMOD_INIT_TRACE][DLSYM_OVERRIDE] source=fix63 module=%s symbol=%s wrapper=%p recovery=%d\n",m->name,symbol,(void*)bender_fmod_studio_system_initialize_trace,bender_fix63_fmod_recovery_enabled_2863());
            return (void*)bender_fmod_studio_system_initialize_trace;
        }
        uintptr_t p=module_symbol(m,symbol);
        logmsg("[BENDER_DL] dlsym guest module=%s symbol=%s -> %p\n",m->name,symbol?symbol:"",(void*)p);
        if(!p)set_guest_dlerror("symbol %s ausente em %s",symbol?symbol:"",m->name); else g_guest_dlerror[0]=0;
        return(void*)p;
    }
    set_checkpoint("guest dlsym handle=%p symbol=%s",handle,symbol?symbol:"(null)");
    void*p=NULL; const char*src="HOST_HANDLE";
    if(symbol&&!strncmp(symbol,"egl",3)){uintptr_t bp=bridge_symbol(symbol);if(bp){p=(void*)bp;src="BENDER_EGL_BRIDGE";}}
    if(!p&&symbol&&!strncmp(symbol,"gl",2)){p=bender_gles_proc_bridge(symbol);if(p)src="BENDER_GLES_BRIDGE";}
    if(!p&&symbol) p=dlsym(handle?handle:RTLD_DEFAULT,symbol);
    /* ULTRAKILL fix -Q (a causa de guest_swaps=0 para sempre no log de 05/11 07:56):
     * o Unity resolve as entradas dele por dlsym. Se receber o eglSwapBuffers DO HOST,
     * o present escapa do probe inteiro (e nem o blit fb0 nem o swap SDL sao chamados
     * na thread com contexto). Devolvemos os nossos wrappers da familia de swap, para
     * o cair no sitio certo. BENDER_UK_SWAP_BRIDGE=0 desliga. */
    if(symbol&&(!strncmp(symbol,"eglSwapBuffers",14))){
        const char *sw=getenv("BENDER_UK_SWAP_BRIDGE");
        if(!(sw&&!strcmp(sw,"0"))&&!p) p=bridge_symbol("eglSwapBuffers");
        if(!(sw&&!strcmp(sw,"0"))&&p){
            g_guest_dlerror[0]=0;
            logmsg("[BENDER_UK_SWAP_BRIDGE][DLSYM] symbol=%s -> wrapper proprio=%p (present na thread do Unity)\n",symbol,p);
            return p;
        }
    }
    if(!p&&symbol){uintptr_t bp=bridge_symbol(symbol);if(bp){p=(void*)bp;src="BENDER_BRIDGE_FALLBACK";}}
    if(!p&&symbol){p=dlsym(RTLD_DEFAULT,symbol);if(p)src="HOST_DEFAULT_FALLBACK";}
    logmsg("[BENDER_DL] dlsym host handle=%p symbol=%s source=%s -> %p\n",handle,symbol?symbol:"",src,p);
    if(!p)set_guest_dlerror("dlsym sem bridge: %s",symbol?symbol:"(null)"); else g_guest_dlerror[0]=0;
    return p;
}
static int bender_guest_dlclose(void *handle){if(handle==(void*)&g_bender_libandroid_token){logmsg("[BENDER_ANDROID_DL][CLOSE] synthetic libandroid handle mantido\n");return 0;}if(handle==(void*)&g_bender_libmediandk_token){logmsg("[BENDER_MEDIANDK][CLOSE] synthetic libmediandk handle mantido\n");return 0;}Module*m=guest_module_from_handle(handle);if(m){logmsg("[BENDER_DL] dlclose guest module=%s (mantido mapeado pelo probe)\n",m->name);return 0;}return dlclose(handle);}
static char *bender_guest_dlerror(void){if(g_guest_dlerror[0])return g_guest_dlerror;return dlerror();}

static uintptr_t resolve_external(Module *requester, const char *name, const char **source) {
    if(name&&strcmp(name,"memmove")==0){*source="BENDER_MEMMOVE_SAFE";return(uintptr_t)bender_memmove_2766;}
    if(name&&strcmp(name,"socket")==0){*source="BENDER_NETWORK_HOST";return(uintptr_t)bender_net_socket_2750;}
    if(name&&strcmp(name,"connect")==0){*source="BENDER_NETWORK_HOST";return(uintptr_t)bender_net_connect_2750;}
    if(name&&strcmp(name,"getaddrinfo")==0){*source="BENDER_NETWORK_HOST";return(uintptr_t)bender_net_getaddrinfo_2750;}
    if(name&&strcmp(name,"freeaddrinfo")==0){*source="BENDER_NETWORK_HOST";return(uintptr_t)bender_net_freeaddrinfo_2752;}
    if(name&&strcmp(name,"gai_strerror")==0){*source="BENDER_NETWORK_HOST";return(uintptr_t)bender_net_gai_strerror_2752;}
    if(name&&strcmp(name,"dlopen")==0){*source="BENDER_GUEST_DL";return(uintptr_t)bender_guest_dlopen;}
    if(name&&strcmp(name,"dlsym")==0){*source="BENDER_GUEST_DL";return(uintptr_t)bender_guest_dlsym;}
    if(name&&strcmp(name,"dlclose")==0){*source="BENDER_GUEST_DL";return(uintptr_t)bender_guest_dlclose;}
    if(name&&strcmp(name,"dlerror")==0){*source="BENDER_GUEST_DL";return(uintptr_t)bender_guest_dlerror;}
    /* FIX66: direct DT_NEEDED guest scope wins before the historical global scan.
       This prevents sibling ABI variants (e.g. libfmod.so vs libfmodL.so) from
       interposing on each other solely because of module registration order. */
    if(requester&&name&&*name){
        for(int ni=0;ni<requester->needed_count;ni++){
            Module*dep=guest_module_from_path(requester->needed[ni]);
            if(!dep||dep==requester||!dep->reserve)continue;
            uintptr_t dp=module_symbol(dep,name);
            if(dp){
                *source=dep->name;
                if(!strcmp(requester->name,"libfmodstudioL.so")&&!strcmp(dep->name,"libfmodL.so")){
                    static unsigned long g_bender_fix66_fmod_needed_hits_2866;
                    unsigned long hn=__atomic_add_fetch(&g_bender_fix66_fmod_needed_hits_2866,1ul,__ATOMIC_RELAXED);
                    if(hn<=24ul)logmsg("[BENDER_FMOD_LOGGING][DEPENDENCY_BIND] source=fix66 n=%lu requester=%s needed=%s symbol=%s target=%p policy=dt-needed-first-global-fallback code_patch=0 forced_completion=0\n",hn,requester->name,dep->name,name,(void*)dp);
                }
                return dp;
            }
        }
    }
    for (int i = 0; i < g_module_count; ++i) {
        Module *m = &g_modules[i]; if (m == requester) continue;
        uintptr_t p = module_symbol(m, name);
        if (p) { *source = m->name; return p; }
    }
    uintptr_t bp = bridge_symbol(name);
    if (bp) { *source = "BENDER_BRIDGE"; return bp; }
    void *p = dlsym(RTLD_DEFAULT, name);
    if (p) { *source = "HOST_GLIBC"; return (uintptr_t)p; }
    *source = "MISSING"; return 0;
}

static int relocate_range(Module *m, RelaRange *rr, bool verbose) {
    size_t count = rr->entsz ? rr->size / rr->entsz : 0;
    for (size_t i = 0; i < count; ++i) {
        Elf64_Rela *r = (Elf64_Rela *)(m->bias + rr->vaddr + i * rr->entsz);
        uint32_t type = (uint32_t)ELF64_R_TYPE(r->r_info);
        uint32_t si = (uint32_t)ELF64_R_SYM(r->r_info);
        uintptr_t *where = (uintptr_t *)(m->bias + r->r_offset);
        if (type == R_AARCH64_RELATIVE) {
            *where = m->bias + (uintptr_t)r->r_addend; ++m->relative_count; continue;
        }
        if (type != R_AARCH64_ABS64 && type != R_AARCH64_GLOB_DAT && type != R_AARCH64_JUMP_SLOT) {
            ++m->unsupported_count;
            if (verbose) logmsg("[UNITY_PROBE][UNSUPPORTED] module=%s reloc=%u offset=0x%" PRIx64 "\n", m->name, type, r->r_offset);
            continue;
        }
        if (si >= m->symcount) { ++m->missing_count; continue; }
        Elf64_Sym *s = (Elf64_Sym *)((uint8_t *)m->symtab + (size_t)si * m->syment);
        const char *name = s->st_name ? m->strtab + s->st_name : "";
        uintptr_t target = 0; const char *src = "";
        if (s->st_shndx != SHN_UNDEF) {
            target = m->bias + s->st_value; src = "SELF"; ++m->self_count;
        } else {
            target = resolve_external(m, name, &src);
            if (target) {
                if (strcmp(src, "HOST_GLIBC") == 0) ++m->host_count; else ++m->cross_count;
            } else if (ELF64_ST_BIND(s->st_info) == STB_WEAK) {
                src = "WEAK_NULL";
            } else ++m->missing_count;
        }
        if (target) {
            *where = target + ((type == R_AARCH64_ABS64) ? (uintptr_t)r->r_addend : 0u);
            if (verbose && s->st_shndx == SHN_UNDEF) logmsg("[UNITY_PROBE][RESOLVE] module=%s symbol=%s source=%s\n", m->name, name, src);
        } else {
            *where = 0;
            if (ELF64_ST_BIND(s->st_info) == STB_WEAK) {
                if (verbose) logmsg("[UNITY_PROBE][WEAK_NULL] module=%s symbol=%s reloc=%u\n", m->name, name, type);
            } else logmsg("[UNITY_PROBE][MISSING] module=%s symbol=%s reloc=%u\n", m->name, name, type);
        }
    }
    return 0;
}

static int relocate_module(Module *m, bool verbose) {
    for (int i = 0; i < m->range_count; ++i) relocate_range(m, &m->ranges[i], verbose);
    logmsg("[UNITY_PROBE][SUMMARY] module=%s relative=%zu self=%zu cross=%zu host=%zu missing=%zu unsupported=%zu\n",
           m->name, m->relative_count, m->self_count, m->cross_count, m->host_count, m->missing_count, m->unsupported_count);
    return 0;
}

static void protect_module(Module *m) {
    for (uint16_t i = 0; i < m->eh->e_phnum; ++i) {
        Elf64_Phdr *p = &m->ph[i]; if (p->p_type != PT_LOAD) continue;
        uintptr_t start = align_down(m->bias + p->p_vaddr, (uintptr_t)g_pagesize);
        uintptr_t end = align_up(m->bias + p->p_vaddr + p->p_memsz, (uintptr_t)g_pagesize);
        int prot = ((p->p_flags & PF_R) ? PROT_READ : 0) | ((p->p_flags & PF_W) ? PROT_WRITE : 0) | ((p->p_flags & PF_X) ? PROT_EXEC : 0);
        if (end > start && mprotect((void *)start, end - start, prot) != 0)
            logmsg("[UNITY_PROBE][WARN] mprotect module=%s errno=%d %s\n", m->name, errno, strerror(errno));
    }
}

static int add_module(const char *dir, const char *name) {
    if (g_module_count >= MAX_MODULES) return -1;
    Module *m = &g_modules[g_module_count]; memset(m, 0, sizeof(*m));
    snprintf(m->name, sizeof(m->name), "%s", name);
    snprintf(m->path, sizeof(m->path), "%s/%s", dir, name);
    if (read_file(m->path, &m->raw, &m->raw_size) != 0) {
        logmsg("[UNITY_PROBE][ERROR] file=%s errno=%d %s\n", m->path, errno, strerror(errno)); return -2;
    }
    int rc = parse_module(m);
    if (rc != 0) { logmsg("[UNITY_PROBE][ERROR] parse module=%s rc=%d\n", name, rc); return -3; }
    ++g_module_count; return 0;
}

static void cleanup(void) {
    bender_jni_choreographer_stop_worker();
    for (int i = 0; i < g_module_count; ++i) {
        if (g_modules[i].reserve && g_modules[i].reserve != MAP_FAILED) munmap(g_modules[i].reserve, g_modules[i].reserve_size);
        free(g_modules[i].raw);
    }
}

static void usage(const char *argv0) {
    fprintf(stderr, "uso: %s --dir unity_payload/lib/arm64-v8a [--app port_root] [--phase scan|map|jni-main|native-load-unity|native-load|unity-initjni|unity-lifecycle|unity-surface|unity-render|unity-loop|unity-run] [--log unity_probe.log] [--verbose]\n", argv0);
}

int main(int argc, char **argv) {
    const char *dir = "unity_payload/lib/arm64-v8a";
    const char *phase = "map";
    const char *appdir = ".";
    const char *logpath = "unity_probe.log";
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--app") && i + 1 < argc) appdir = argv[++i];
        else if (!strcmp(argv[i], "--phase") && i + 1 < argc) phase = argv[++i];
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) logpath = argv[++i];
        else if (!strcmp(argv[i], "--verbose")) verbose = true;
        else { usage(argv[0]); return 2; }
    }
    bender_strlcpy(g_guest_lib_dir,dir,sizeof(g_guest_lib_dir));
    bender_strlcpy(g_guest_app_dir,appdir,sizeof(g_guest_app_dir));
    {
        char bender_runtime_unified_82[1200];
        snprintf(bender_runtime_unified_82,sizeof(bender_runtime_unified_82),"%s/unity_payload/BENDER_RUNTIME_UNIFIED.apk",g_guest_app_dir);
        if(access(bender_runtime_unified_82,R_OK)==0){
            bender_strlcpy(g_guest_base_apk,bender_runtime_unified_82,sizeof(g_guest_base_apk));
            logmsg("[BENDER_APKSET_UNIFIED] source=fix82 action=select-runtime-unified path=%s policy=sourceDir+getPackageCodePath-unified-assets\n",g_guest_base_apk);
        }else{
            snprintf(g_guest_base_apk,sizeof(g_guest_base_apk),"%s/base.apk",g_guest_app_dir);
            logmsg("[BENDER_APKSET_UNIFIED] source=fix82 action=fallback-base path=%s reason=runtime-unified-missing\n",g_guest_base_apk);
        }
    }
    snprintf(g_guest_data_dir,sizeof(g_guest_data_dir),"%s/data",g_guest_app_dir);
    snprintf(g_guest_files_dir,sizeof(g_guest_files_dir),"%s/data/files",g_guest_app_dir);
    snprintf(g_guest_external_files_dir,sizeof(g_guest_external_files_dir),"%s/data/external",g_guest_app_dir);
    snprintf(g_guest_cache_dir,sizeof(g_guest_cache_dir),"%s/data/cache",g_guest_app_dir);
    snprintf(g_guest_obb_dir,sizeof(g_guest_obb_dir),"%s/data/obb",g_guest_app_dir);
    (void)mkdir(g_guest_data_dir,0755);(void)mkdir(g_guest_files_dir,0755);(void)mkdir(g_guest_external_files_dir,0755);(void)mkdir(g_guest_cache_dir,0755);(void)mkdir(g_guest_obb_dir,0755);
    g_pagesize = sysconf(_SC_PAGESIZE); if (g_pagesize <= 0) g_pagesize = 4096;
    bender_panel_log_transport_init();
    g_log = fopen(logpath, "w");
    if(g_log)g_log_fd=fileno(g_log);
    install_crash_handlers();
    set_checkpoint("startup phase=%s",phase);
    logmsg("============================================================\n");
    logmsg("BENDER UNITY IL2CPP AARCH64 PROBE 2.7.66 AUDIO EXPERIMENTAL\n");
    struct stat apkst; int apk_ok=(stat(g_guest_base_apk,&apkst)==0&&S_ISREG(apkst.st_mode));
    logmsg("[BENDER_APK] app_dir=%s base_apk=%s exists=%d size=%lld\n",g_guest_app_dir,g_guest_base_apk,apk_ok,apk_ok?(long long)apkst.st_size:-1LL);
    bender_asset_path_diag_2771();
    logmsg("[BENDER_APP_IDENTITY] package=%s versionName=%s versionCode=%d minSdk=%d targetSdk=%d policy=apk-manifest-injected\n",BENDER_APP_PACKAGE,BENDER_APP_VERSION_NAME,(int)BENDER_APP_VERSION_CODE,(int)BENDER_APP_MIN_SDK,(int)BENDER_APP_TARGET_SDK);
    logmsg("[BENDER_STORAGE] state=mounted files=%s external=%s cache=%s obb=%s\n",g_guest_files_dir,g_guest_external_files_dir,g_guest_cache_dir,g_guest_obb_dir);
    logmsg("[BENDER_DISPLAY] size=%dx%d densityDpi=%d refresh=%.2fHz vsyncOffset=%" PRId64 "ns presentationDeadline=%" PRId64 "ns\n",BENDER_DISPLAY_WIDTH,BENDER_DISPLAY_HEIGHT,BENDER_DISPLAY_DENSITY_DPI,(double)BENDER_DISPLAY_REFRESH_HZ,(int64_t)BENDER_DISPLAY_VSYNC_OFFSET_NS,(int64_t)BENDER_DISPLAY_PRESENTATION_DEADLINE_NS);
char _loc_l[16],_loc_c[16],_loc_tag[40],_loc_legacy[40];bender_locale_values_2749(_loc_l,sizeof(_loc_l),_loc_c,sizeof(_loc_c),_loc_tag,sizeof(_loc_tag),_loc_legacy,sizeof(_loc_legacy));logmsg("[BENDER_STDIO] bionic_FILE_size=0x%x __sF=%p stdin=%p stdout=%p stderr=%p locale=%s jniPools=%d/%d/%d\n",(unsigned)BENDER_BIONIC_FILE_SIZE,(void*)g_bionic_sF,(void*)stdin,(void*)stdout,(void*)stderr,_loc_tag,BENDER_JNI_MAX_CLASSES,BENDER_JNI_MAX_METHODS,BENDER_JNI_MAX_OBJECTS);bender_apks_diag_2749();bender_network_diag_2750();bender_network_preflight_2755();bender_firebase_diag_2755();
    logmsg("phase=%s dir=%s host_machine=%s\n", phase, dir,
#if defined(__aarch64__)
           "aarch64"
#elif defined(__x86_64__)
           "x86_64"
#else
           "other"
#endif
    );
    logmsg("============================================================\n");

    const char *core[] = {"libmain.so", "libunity.so", "libil2cpp.so"};
    for (size_t i = 0; i < sizeof(core)/sizeof(core[0]); ++i) {
        if (add_module(dir, core[i]) != 0) { cleanup(); if (g_log) fclose(g_log); return 10 + (int)i; }
    }
    const char *optional[] = {"libfmod.so","libfmodstudio.so","libresonanceaudio.so"};
    for(size_t i=0;i<sizeof(optional)/sizeof(optional[0]);++i){
        char opath[1200]; snprintf(opath,sizeof(opath),"%s/%s",dir,optional[i]);
        if(access(opath,R_OK)==0){
            int orc=add_module(dir,optional[i]);
            logmsg("[BENDER_OPTIONAL_MODULE][REGISTER] module=%s rc=%d path=%s\n",optional[i],orc,opath);
            if(orc!=0){cleanup();if(g_log)fclose(g_log);return 16+(int)i;}
        } else {
            logmsg("[BENDER_OPTIONAL_MODULE][SKIP] module=%s reason=not-present\n",optional[i]);
        }
    }
    /* V2.7.54: register every additional ARM64 module lazily. Core and
       optional FMOD modules were already registered above. Dependencies are
       prepared recursively when the Android guest calls dlopen. */
    DIR *lazy_dir_2751=opendir(dir);
    if(lazy_dir_2751){
        struct dirent *de=NULL;
        while((de=readdir(lazy_dir_2751))!=NULL){
            const char *fn=de->d_name; size_t flen=strlen(fn);
            if(flen<4u || strcmp(fn+flen-3u,".so")!=0)continue;
            int exists=0; for(int mi=0;mi<g_module_count;mi++)if(!strcmp(g_modules[mi].name,fn)){exists=1;break;}
            if(exists)continue;
            char fpath[1200]; snprintf(fpath,sizeof(fpath),"%s/%s",dir,fn);
            unsigned char ehdr4[4]={0};int efd=open(fpath,O_RDONLY);ssize_t er=-1;if(efd>=0){er=read(efd,ehdr4,sizeof(ehdr4));close(efd);}
            if(er!=4||ehdr4[0]!=0x7f||ehdr4[1]!='E'||ehdr4[2]!='L'||ehdr4[3]!='F'){logmsg("[BENDER_LAZY_MODULE][SKIP_NON_ELF] module=%s path=%s\n",fn,fpath);continue;}
            int before_count=g_module_count; int frc=add_module(dir,fn);
            if(frc==0 && g_module_count>before_count){
                g_modules[g_module_count-1].lazy_module=1;
                logmsg("[BENDER_LAZY_MODULE][AUTO_REGISTER_ALL] module=%s policy=parse-now-map-relocate-on-dlopen path=%s\n",fn,fpath);
            } else {
                logmsg("[BENDER_LAZY_MODULE][AUTO_REGISTER_ALL][ERROR] module=%s rc=%d path=%s\n",fn,frc,fpath);
                closedir(lazy_dir_2751); cleanup(); if(g_log)fclose(g_log); return 26;
            }
        }
        closedir(lazy_dir_2751);
    }
    if (!strcmp(phase, "scan")) {
        logmsg("[UNITY_PROBE][OK] scan complete; nenhuma biblioteca foi executada.\n");
        cleanup(); if (g_log) fclose(g_log); return 0;
    }
    bool want_jni_main = !strcmp(phase, "jni-main");
    bool want_native_unity = !strcmp(phase, "native-load-unity");
    bool want_unity_initjni = !strcmp(phase, "unity-initjni");
    bool want_unity_lifecycle = !strcmp(phase, "unity-lifecycle");
    bool want_unity_surface = !strcmp(phase, "unity-surface");
    bool want_unity_run = !strcmp(phase, "unity-run");
    bool want_unity_loop = !strcmp(phase, "unity-loop") || want_unity_run;
    bool want_unity_render = !strcmp(phase, "unity-render") || want_unity_loop;
    g_bender_continuous_run_mode=want_unity_run?1:0;
    if(want_unity_run){g_bender_run_stop_requested=0;bender_install_run_stop_handlers();logmsg("[BENDER_RUNTIME_BUILD] version=2.7.152 fix102=reliance-static-wrapper fix103=methodid-declaring-class-fallback-guard\n");logmsg("[BENDER_FIX104][ARMED] version=2.7.153 final_stage=1 target=RGPushNotification.getSavedPushPayloadData contract=[]\n");logmsg("[BENDER_FIX105][ARMED] version=2.7.154 signature_normalization=reference-class-dot-slash-equivalence target=RGPushNotification.getSavedPushPayloadData contract=[]\n");logmsg("[BENDER_FIX106][ARMED] version=2.7.155 mode=app-start-coroutine-frontier-read-only state_snapshot=1 movenext_callgraph=1 forced_completion=0\n");logmsg("[BENDER_FIX107][ARMED] version=2.7.156 mode=state2-virtual-predicate-read-only static-object-recovery=1 method-enum=1 forced_completion=0\n");logmsg("[BENDER_FIX108][ARMED] version=2.7.157 mode=restored-global-ref-ownership ump_callback_global_ref=1 plugin_owned_dismiss_dispatch=1 force_google_cmp_ready=0 code_patch=0 forced_completion=0\n");logmsg("[BENDER_FIX109][ARMED] version=2.7.158 mode=reliance-selective-permission-plugin-callback payload=DEX-PermissionInfo-array grant_fabricated=0 coroutine_flag_write=0 code_patch=0 forced_completion=0\n");logmsg("[BENDER_FIX110][ARMED] version=2.7.159 mode=fix106-coroutine-gchandle-lifetime strong=1 pinned=0 process_lifetime=1 late_raw_deref=0 gameplay_state_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX111][ARMED] version=2.7.160 mode=il2cpp-gchandle-opaque-width storage=uintptr_t handle_bits=%u truncation=0 gameplay_state_write=0 code_patch=0 forced_completion=0\n",(unsigned)(sizeof(g_bender_fix110_start_handle)*8u));
logmsg("[BENDER_FIX112][ARMED] version=2.7.161 mode=non-owning-start-coroutine-observer strong_gchandle=0 early_snapshot_max_frame=10 late_snapshot_guard=fix110-no-handle gameplay_state_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX113][ARMED] version=2.7.162 mode=werror-unused-symbol-annotations primary_helper=bender_fix110_hold dormant_helpers_preserved=1 ownership_restored=0 gameplay_state_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX114][ARMED] version=2.7.163 mode=firebase-init-continuation-trace early_frame_only=1 interface_dispatch=1 callback_setter=1 read_only=1 invoke=0 state_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX115][ARMED] version=2.7.164 mode=nord-firebase-dependency-continuation-trace frame_gate=120 method_map_required=1 lambdas=1 interface_dispatch=1 direct_edges=1 read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX116][ARMED] version=2.7.165 mode=nord-firebase-async-contract-resolver adrp_ldr_window=28 interface_search_back=256 delegate_methodinfo=1 tail_methodinfo=1 read_only=1 invoke=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX117][ARMED] version=2.7.166 mode=firebase-dependency-task-frontier-trace contract_call=0x1dc delegates=b__0,b__1 direct_edges=1 methodinfo_candidates=1 java_task_counters=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX118][ARMED] version=2.7.167 mode=fix117-methodinfo-safety-guard physical_crash=frame122-sigsegv disable_unvalidated_methodinfo_deref=1 preserve_direct_edges=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX119][ARMED] version=2.7.168 mode=firebase-dependency-validated-methodinfo-ref-trace physical_fix118_frame720_no-sigsegv=1 dependency_target_offset=0x5ad417c fix16_identity_validation=1 intermediate_cell_module_validation=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX120][ARMED] version=2.7.169 mode=exact-firebase-async-method-resolver physical_fix119_validated_refs=0 enumerate_all_runtime_methods=1 fix16_storage_cap_independent=1 exact_code_pointer_match=1 firebaseapp_dependency_focus=1 signature_types=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX121][ARMED] version=2.7.170 mode=firebase-async-callgraph-exact-resolver physical_fix120_dependency=Firebase.FirebaseApp.CheckAndFixDependenciesAsync physical_fix120_return=Task<Firebase.DependencyStatus> direct_bl_b_edges=1 full_runtime_methodinfo_identity=1 candidate_deref_before_validation=0 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX122][ARMED] version=2.7.171 mode=main-start-nullreference-frontier-trace physical_fix121_main_start_nre=1 preinvoke_field_snapshot=1 exception_field_snapshot=1 direct_bl_b_edges=1 full_runtime_methodpointer_identity=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX123][ARMED] version=2.7.172 mode=firebase-lambda-sync-dependency-frontier physical_fix121_lambda_b56_0=1 physical_fix121_lambda_b57_0=1 physical_fix122_main_nre_reproduced=0 full_runtime_methodinfo_identity=1 direct_bl_b_edges=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FIX124][ARMED] version=2.7.173 mode=firebase-android-dependency-frontier physical_fix123_check_android_dependencies=1 target=Firebase.AppUtil.CheckAndroidDependencies return=Firebase.GooglePlayServicesAvailability full_runtime_method_identity=1 direct_bl_b_edges=1 read_only=1 invoke=0 delegate_call=0 task_complete=0 flag_write=0 code_patch=0 forced_completion=0\n");
logmsg("[BENDER_FN_FALLBACK][ARMED] version=2.7.173 modifier=FN|L1+R1 native_fn_preserved=1 alias_dpad=1 alias_editors=1 alias_sticks=1 alias_mouse_speed_l2r2=1\n");logmsg("[BENDER_RUN][MODE] continuous=1 exit=SIGTERM/SIGINT watchdog=idle-only input_hook=%p\n",g_unity_native_inject_event_fn);logmsg("[BENDER_RENDER_INSPECTOR][CONFIG] enabled=%d host_present=bootstrap-only-after-first-guest-swap sample=4x4 optional_audio_loader=FMOD reflection_bridge=1 fmod_java_shim=1 fmod_audio=physical-opensles-first-nosound-optin libandroid=synthetic firebase_loader=lazy-auto mediandk=synthetic-video-dt2-intro-fix80 postfx=dt2-neutral-blit-fix81 jni_class_cache=canonical apk_identity=manifest android_java_proxy=ReflectionHelper-nativeProxyInvoke ui_thread=inline-runnable ump=offline-form-complete pad=embedded-universal-apk+real-split-dirs+download-callback+proxy-values network=real-linux-host+android-connectivity+preflight firebase=android-resources-first+task-future-bridge clevertap=typed-context+profile-ready crashlytics=data-collection-default-on+typed-objects pad-wrapper=v2758-compatible+late-instance-reuse remoteconfig=local-success-defaults auth=persistent-user+synthetic-token-result inputdevice=int-array identity=stable-android-id+uri+prefs input=v6-nativepad+ingame-keymap-editor+ingame-stick-area-editor+mouse-coexist+dual-stick-touch+release-no-return fmod_init_trace=1 relocation_diagnostics=pre-bootstrap systemclock=monotonic-ms applovin=class-sensitive loading_gate=v2765 mediandk_eos=1 football_force_load=rollback networkmonitor=host-online billing=setup-ready exit=gptokeyb-start-select pad_listener=captured audio=fmod-sdl2-directbuffer\n",bender_render_inspector_enabled());}
    bool want_unity_init_stage = want_unity_initjni || want_unity_lifecycle || want_unity_surface || want_unity_render;
    bool want_native_all = !strcmp(phase, "native-load") || want_unity_init_stage;
    bool want_jni_flow = want_jni_main || want_native_unity || want_native_all;
    if (strcmp(phase, "map") != 0 && !want_jni_flow) {
        logmsg("[UNITY_PROBE][ERROR] fase desconhecida: %s. V2.7.24 aceita scan, map, jni-main, native-load-unity, native-load, unity-initjni, unity-lifecycle, unity-surface, unity-render ou unity-loop.\n", phase);
        cleanup(); if (g_log) fclose(g_log); return 3;
    }

    /* Carrega libs host apenas para aumentar visibilidade de imports seguros. */
    const char *hostlibs[] = {"libc.so.6", "libm.so.6", "libdl.so.2", "libstdc++.so.6", "libz.so.1", "libEGL.so.1", "libGLESv2.so.2"};
    for (size_t i = 0; i < sizeof(hostlibs)/sizeof(hostlibs[0]); ++i) (void)dlopen(hostlibs[i], RTLD_LAZY | RTLD_GLOBAL);

    /* V2.7.34: crash diagnostics ja ativos no bootstrap de map/relocation.
       Firebase fica apenas parseada e e preparada quando o jogo chama dlopen. */
    install_crash_handlers();
    for (int i = 0; i < g_module_count; ++i) {
        if(g_modules[i].lazy_module){
            logmsg("[BENDER_LAZY_MODULE][DEFER_MAP] module=%s raw=%zu policy=dlopen-on-demand\n",g_modules[i].name,g_modules[i].raw_size);
            continue;
        }
        set_checkpoint("bootstrap map module=%s",g_modules[i].name);
        int rc = map_module(&g_modules[i]);
        if (rc != 0) { logmsg("[UNITY_PROBE][ERROR] map module=%s rc=%d\n", g_modules[i].name, rc); cleanup(); if (g_log) fclose(g_log); return 30 + i; }
    }
    /* Somente modulos bootstrap entram no link inicial; lazy nao contam como missing. */
    size_t total_missing = 0, total_unsupported = 0;
    for (int i = 0; i < g_module_count; ++i) {
        if(g_modules[i].lazy_module)continue;
        set_checkpoint("bootstrap relocate module=%s ranges=%d symcount=%zu",g_modules[i].name,g_modules[i].range_count,g_modules[i].symcount);
        relocate_module(&g_modules[i], verbose);
        g_modules[i].relocate_state=2;
        total_missing += g_modules[i].missing_count;
        total_unsupported += g_modules[i].unsupported_count;
    }
    for (int i = 0; i < g_module_count; ++i) {
        if(g_modules[i].lazy_module)continue;
        protect_module(&g_modules[i]);
        g_modules[i].protect_state=2;
    }

    for (int i = 0; i < g_module_count; ++i) {
        if(g_modules[i].lazy_module){
            logmsg("[UNITY_PROBE][ENTRY][LAZY] module=%s JNI_OnLoad=deferred init_array=%zu\n",g_modules[i].name,g_modules[i].init_array_count);
            continue;
        }
        uintptr_t jni = module_symbol(&g_modules[i], "JNI_OnLoad");
        logmsg("[UNITY_PROBE][ENTRY] module=%s JNI_OnLoad=%p init_array=%zu\n", g_modules[i].name, (void *)jni, g_modules[i].init_array_count);
    }
    logmsg("[UNITY_PROBE][OK] bootstrap-map complete missing=%zu unsupported_reloc=%zu lazy_firebase=deferred\n", total_missing, total_unsupported);
    if (want_jni_flow) {
#if defined(__aarch64__)
        if (total_missing || total_unsupported) {
            logmsg("[UNITY_PROBE][JNI_FLOW][BLOCKED] imports/relocations pendentes; codigo guest nao executado.\n");
        } else {
            init_fake_jni();
            Module *mainm=NULL; for(int i=0;i<g_module_count;i++) if(!strcmp(g_modules[i].name,"libmain.so")) mainm=&g_modules[i];
            uintptr_t ep=mainm?module_symbol(mainm,"JNI_OnLoad"):0;
            if(!ep) logmsg("[UNITY_PROBE][JNI_MAIN][ERROR] JNI_OnLoad de libmain.so nao encontrado.\n");
            else {
                typedef bjint (*OnLoadFn)(BVM*,void*);
                set_checkpoint("libmain JNI_OnLoad fn=%p",(void*)ep); install_crash_handlers();
                logmsg("[UNITY_PROBE][JNI_MAIN] chamando libmain.so JNI_OnLoad=%p com JavaVM fake...\n",(void*)ep);
                bjint ver=((OnLoadFn)ep)(&g_vm,NULL);
                logmsg("[UNITY_PROBE][JNI_MAIN][RETURN] version=0x%08x load=%p unload=%p exception=%d\n",(unsigned)ver,g_native_load_fn,g_native_unload_fn,g_jni_exception);
                if(ver==0x00010006 && g_native_load_fn) {
                    logmsg("[UNITY_PROBE][JNI_MAIN][OK] NativeLoader registrado.\n");
                    if(want_native_unity||want_native_all){
                        g_guest_allow_unity_init=1; g_guest_allow_il2cpp_init=want_native_all?1:0; g_guest_block_il2cpp=want_native_unity?1:0;
                        const char *native_dir=dir; void *jpath=jni_NewStringUTF(&g_env,native_dir);
                        typedef bjboolean (*NativeLoadFn)(BEnv*,void*,void*);
                        set_checkpoint("NativeLoader.load dir=%s mode=%s",native_dir,want_unity_init_stage?phase:(want_native_all?"unity+il2cpp":"unity-only")); install_crash_handlers();
                        logmsg("[UNITY_PROBE][NATIVE_LOAD] chamando NativeLoader.load dir=%s mode=%s fn=%p\n",native_dir,want_unity_init_stage?phase:(want_native_all?"unity+il2cpp":"unity-only"),g_native_load_fn);
                        bjboolean loaded=((NativeLoadFn)g_native_load_fn)(&g_env,g_native_loader_class,jpath);
                        Module *um=guest_module_from_path("libunity.so"),*im=guest_module_from_path("libil2cpp.so");
                        set_checkpoint("NativeLoader.load returned result=%u unity=%d il2cpp=%d",(unsigned)loaded,um?um->init_state:-1,im?im->init_state:-1);
                        logmsg("[UNITY_PROBE][NATIVE_LOAD][RETURN] result=%u unity_init=%d il2cpp_init=%d exception=%d fatal_intercepted=%d\n",(unsigned)loaded,um?um->init_state:-1,im?im->init_state:-1,g_jni_exception,g_jni_fatal_count);
                        if(want_native_unity){
                            if(loaded&&um&&um->init_state==2)logmsg("[UNITY_PROBE][NATIVE_LOAD_UNITY][OK] libunity constructors + JNI_OnLoad atravessados; libil2cpp permaneceu bloqueada propositalmente.\n");
                            else logmsg("[UNITY_PROBE][NATIVE_LOAD_UNITY][WARN] libunity nao concluiu de forma limpa.\n");
                        } else {
                            /* ULTRAKILL (Unity 2019.4): libunity.so so faz dlopen
                             * de libil2cpp.so DENTRO de UnityPlayer.initJni, nao
                             * durante NativeLoader.load (no Unity 6 do port modelo
                             * ele ja vem aqui). O gate antigo exigia
                             * im->init_state==2 e derrubava a escada inteira com
                             * "retornou sem concluir os dois init_array". Aceitar
                             * init_state==0 quando o init do il2cpp esta liberado
                             * (want_native_all) - ele roda sozinho no initJni. */
                            if(loaded&&um&&um->init_state==2&&im&&(im->init_state==2||g_guest_allow_il2cpp_init)){
                                logmsg("[UNITY_PROBE][NATIVE_LOAD][OK] libunity + libil2cpp atravessaram NativeLoader.load (il2cpp_init=%d%s).\n",
                                       im->init_state,im->init_state==2?"":"; il2cpp sera carregado no initJni (Unity 2019.x)");
                                bender_application_paths_install_2772(im);
                                if(g_bender_application_paths_installed_2772)bender_content_async_install_2773(im);
                                if(g_bender_application_paths_installed_2772)bender_fix12_install_2812(im);
                                bender_fix13_install_2813(im);
                                logmsg("[BENDER_JNI][UNITY_PTRS] initJni=%p pause=%p resume=%p focus=%p done=%p render=%p recreateGfx=%p surfaceChanged=%p unityClass=%p activityClass=%p\n",
                                       g_unity_init_jni_fn,g_unity_native_pause_fn,g_unity_native_resume_fn,g_unity_native_focus_fn,g_unity_native_done_fn,
                                       g_unity_native_render_fn,g_unity_native_recreate_gfx_fn,g_unity_native_surface_changed_fn,g_unity_player_class,g_unity_activity_class);
                                if(want_unity_init_stage){
                                    if(!g_unity_init_jni_fn){
                                        logmsg("[UNITY_PROBE][INITJNI][ERROR] UnityPlayer.initJni nao foi registrado.\n");
                                    } else {
                                        BObj *ctx=new_obj(4,"android/app/Activity",BENDER_APP_PACKAGE);
                        logmsg("[BENDER_JNI][CTX] contexto sintatico entregue como android/app/Activity (Unity 2019.4 exige instanceof Activity)\n");
                                        BObj *activity=new_obj(4,"com/unity3d/player/UnityPlayerForActivityOrService","instance");
                                        g_unity_activity_instance=activity;
                                        logmsg("[BENDER_JNI][CURRENT_ACTIVITY][BIND] activity=%p class=%s\n",(void*)activity,activity?activity->a:"?");
                                        BObj *surface=new_obj(4,"android/view/Surface","bender-fake-surface");
                                        volatile int lifecycle_stage_ok=1;
                                        volatile int surface_stage_ok=1;
                                        volatile int render_stage_ok=1;
                                        size_t ab=g_guest_alloc_total,fb=g_guest_free_total,cb=g_jni_call_count;
                                        guest_heap_validate_all("before-initJni");
                                        set_checkpoint("UnityPlayer.initJni begin fn=%p context=%p flags=0",(void*)g_unity_init_jni_fn,(void*)ctx); install_crash_handlers();
                                        logmsg("[UNITY_PROBE][INITJNI][BEGIN] fn=%p class=%p context=%p flags=0 alloc=%zu free=%zu jni_calls=%zu\n",
                                               g_unity_init_jni_fn,g_unity_player_class,(void*)ctx,ab,fb,cb);
                                        typedef void (*UnityInitJniFn)(BEnv*,void*,void*,bjint);
                                        ((UnityInitJniFn)g_unity_init_jni_fn)(&g_env,g_unity_player_class?g_unity_player_class:(void*)ctx,(void*)ctx,0);
                                        set_checkpoint("UnityPlayer.initJni returned exception=%d fatal=%d",g_jni_exception,g_jni_fatal_count);
                                        guest_heap_validate_all("after-initJni");
                                        logmsg("[UNITY_PROBE][INITJNI][RETURN] exception=%d fatal_intercepted=%d alloc_delta=%zu free_delta=%zu active=%zu jni_calls_delta=%zu\n",
                                               g_jni_exception,g_jni_fatal_count,g_guest_alloc_total-ab,g_guest_free_total-fb,g_guest_alloc_active,g_jni_call_count-cb);
                                        if(g_jni_exception||g_jni_fatal_count){
                                            logmsg("[UNITY_PROBE][INITJNI][BLOCKED] initJni retornou com estado JNI/fatal pendente; lifecycle nao sera executado.\n");
                                        } else {
                                            logmsg("[UNITY_PROBE][INITJNI][OK] UnityPlayer.initJni retornou limpo.\n");
                                            if(want_unity_lifecycle) logmsg("[UNITY_PROBE][LIFECYCLE][BEGIN] initJni limpo; iniciando focus/resume instrumentados.\n");
                                            if(want_unity_surface||want_unity_render){
                                                logmsg("[UNITY_PROBE][SURFACE_STAGE][BEGIN] lifecycle anterior validado no R36S; iniciando Surface/ANativeWindow/Gfx.\n");
                                                if(!g_unity_native_recreate_gfx_fn){
                                                    surface_stage_ok=0;
                                                    logmsg("[UNITY_PROBE][SURFACE][ERROR] nativeRecreateGfxState nao registrado.\n");
                                                } else {
                                                    typedef void (*UnityRecreateGfxFn)(BEnv*,void*,bjint,void*);
                                                    guest_heap_validate_all("before-recreateGfx");
                                                    set_checkpoint("nativeRecreateGfxState begin fn=%p display=0 surface=%p",g_unity_native_recreate_gfx_fn,(void*)surface); install_crash_handlers();
                                                    logmsg("[UNITY_PROBE][SURFACE][BEGIN] recreateGfx=%p activity=%p surface=%p fakeWindow=%p %dx%d format=%d refs=%d\n",
                                                           g_unity_native_recreate_gfx_fn,(void*)activity,(void*)surface,(void*)&g_native_window,g_native_window.width,g_native_window.height,g_native_window.format,g_native_window.refs);
                                                    ((UnityRecreateGfxFn)g_unity_native_recreate_gfx_fn)(&g_env,(void*)activity,0,(void*)surface);
                                                    set_checkpoint("nativeRecreateGfxState returned exception=%d fatal=%d",g_jni_exception,g_jni_fatal_count);
                                                    guest_heap_validate_all("after-recreateGfx");
                                                    logmsg("[UNITY_PROBE][SURFACE][RETURN] exception=%d fatal_intercepted=%d active=%zu anw_from_surface=%zu acquire=%zu release=%zu geometry=%zu window=%dx%d format=%d refs=%d\n",
                                                           g_jni_exception,g_jni_fatal_count,g_guest_alloc_active,g_native_window_from_surface_calls,g_native_window_acquire_calls,g_native_window_release_calls,g_native_window_geometry_calls,g_native_window.width,g_native_window.height,g_native_window.format,g_native_window.refs);
                                                    if(g_jni_exception||g_jni_fatal_count){
                                                        surface_stage_ok=0;
                                                    } else if(!g_unity_native_surface_changed_fn){
                                                        surface_stage_ok=0;
                                                        logmsg("[UNITY_PROBE][SURFACE_CHANGED][ERROR] nativeSendSurfaceChangedEvent nao registrado.\n");
                                                    } else {
                                                        typedef void (*UnitySurfaceChangedFn)(BEnv*,void*);
                                                        guest_heap_validate_all("before-surfaceChanged");
                                                        set_checkpoint("nativeSendSurfaceChangedEvent begin fn=%p",g_unity_native_surface_changed_fn); install_crash_handlers();
                                                        logmsg("[UNITY_PROBE][SURFACE_CHANGED][BEGIN] fn=%p activity=%p\n",g_unity_native_surface_changed_fn,(void*)activity);
                                                        ((UnitySurfaceChangedFn)g_unity_native_surface_changed_fn)(&g_env,(void*)activity);
                                                        set_checkpoint("nativeSendSurfaceChangedEvent returned exception=%d fatal=%d",g_jni_exception,g_jni_fatal_count);
                                                        guest_heap_validate_all("after-surfaceChanged");
                                                        logmsg("[UNITY_PROBE][SURFACE_CHANGED][RETURN] exception=%d fatal_intercepted=%d active=%zu\n",g_jni_exception,g_jni_fatal_count,g_guest_alloc_active);
                                                        if(g_jni_exception||g_jni_fatal_count) surface_stage_ok=0;
                                                    }
                                                }
                                                if(surface_stage_ok&&!g_jni_exception&&!g_jni_fatal_count)
                                                    logmsg("[UNITY_PROBE][SURFACE_STAGE][OK] recreateGfx + SurfaceChanged retornaram limpos.\n");
                                                else
                                                    logmsg("[UNITY_PROBE][SURFACE_STAGE][BLOCKED] surface incompleta exception=%d fatal=%d; render nao sera promovido.\n",g_jni_exception,g_jni_fatal_count);
                                            }
                                            if((want_unity_lifecycle||want_unity_surface||want_unity_render)&&!g_jni_exception&&!g_jni_fatal_count){
                                                if(g_unity_native_focus_fn){
                                                    typedef void (*UnityFocusFn)(BEnv*,void*,bjboolean);
                                                    guest_heap_validate_all("before-focus");
                                                    set_checkpoint("nativeFocusChanged begin focused=1 fn=%p",g_unity_native_focus_fn); install_crash_handlers();
                                                    logmsg("[UNITY_PROBE][FOCUS][BEGIN] fn=%p activity=%p focused=1\n",g_unity_native_focus_fn,(void*)activity);
                                                    ((UnityFocusFn)g_unity_native_focus_fn)(&g_env,(void*)activity,1);
                                                    set_checkpoint("nativeFocusChanged returned exception=%d fatal=%d",g_jni_exception,g_jni_fatal_count);
                                                    guest_heap_validate_all("after-focus");
                                                    logmsg("[UNITY_PROBE][FOCUS][RETURN] exception=%d fatal_intercepted=%d\n",g_jni_exception,g_jni_fatal_count);
                                                } else { lifecycle_stage_ok=0; logmsg("[UNITY_PROBE][FOCUS][ERROR] nativeFocusChanged nao registrado.\n"); }
                                                if(!g_jni_exception&&!g_jni_fatal_count&&lifecycle_stage_ok){
                                                    if(!g_unity_native_resume_fn){
                                                        lifecycle_stage_ok=0;
                                                        logmsg("[UNITY_PROBE][RESUME][ERROR] nativeResume nao registrado.\n");
                                                    } else {
                                                        typedef void (*UnityResumeFn)(BEnv*,void*);
                                                        guest_heap_validate_all("before-resume");
                                                        set_checkpoint("nativeResume begin fn=%p",g_unity_native_resume_fn); install_crash_handlers();
                                                        logmsg("[UNITY_PROBE][RESUME][BEGIN] fn=%p activity=%p\n",g_unity_native_resume_fn,(void*)activity);
                                                        ((UnityResumeFn)g_unity_native_resume_fn)(&g_env,(void*)activity);
                                                        set_checkpoint("nativeResume returned exception=%d fatal=%d",g_jni_exception,g_jni_fatal_count);
                                                        guest_heap_validate_all("after-resume");
                                                        logmsg("[UNITY_PROBE][RESUME][RETURN] exception=%d fatal_intercepted=%d active=%zu\n",g_jni_exception,g_jni_fatal_count,g_guest_alloc_active);
                                                        if(g_jni_exception||g_jni_fatal_count) lifecycle_stage_ok=0;
                                                    }
                                                } else if(g_jni_exception||g_jni_fatal_count) lifecycle_stage_ok=0;
                                                if(want_unity_lifecycle){
                                                    if(lifecycle_stage_ok&&!g_jni_exception&&!g_jni_fatal_count) logmsg("[UNITY_PROBE][LIFECYCLE][OK] focus/resume retornaram limpos; heap validado.\n");
                                                    else logmsg("[UNITY_PROBE][LIFECYCLE][BLOCKED] lifecycle incompleto exception=%d fatal=%d; nao promover automaticamente para Surface.\n",g_jni_exception,g_jni_fatal_count);
                                                }
                                            }
                                            if(want_unity_render&&!g_jni_exception&&!g_jni_fatal_count&&lifecycle_stage_ok&&surface_stage_ok){
                                                logmsg("[UNITY_PROBE][RENDER_STAGE][BEGIN] Surface + lifecycle validados; iniciando primeira chamada nativeRender. loop=%d continuous=%d\n",want_unity_loop?1:0,want_unity_run?1:0);
                                                if(!g_unity_native_render_fn){
                                                    render_stage_ok=0;
                                                    logmsg("[UNITY_PROBE][RENDER][ERROR] nativeRender nao registrado.\n");
                                                } else {
                                                    typedef bjboolean (*UnityRenderFn)(BEnv*,void*);
                                                    size_t rab=g_guest_alloc_total, rfb=g_guest_free_total, ractive=g_guest_alloc_active;
                                                    unsigned long first_swaps_before=g_bender_egl_swaps;
                                                    guest_heap_validate_all("before-render");
                                                    guest_heap_set_fast_mode("before-first-nativeRender");
                                                    set_checkpoint("nativeRender first-frame begin fn=%p",g_unity_native_render_fn); install_crash_handlers();
                                                    logmsg("[UNITY_PROBE][RENDER][BEGIN] primeira chamada fn=%p activity=%p alloc=%zu free=%zu active=%zu\n",g_unity_native_render_fn,(void*)activity,rab,rfb,ractive);
                                                    volatile bjboolean rr=0; g_jni_kill_requested=0;
                                                    int rjump=sigsetjmp(g_render_jmp,1);
                                                    if(rjump==0){
                                                        g_render_jmp_active=1; bender_render_watchdog_start();
                                                        rr=((UnityRenderFn)g_unity_native_render_fn)(&g_env,(void*)activity);
                                                        g_render_jmp_active=0; bender_render_watchdog_done("nativeRender-return");
                                                        set_checkpoint("nativeRender returned result=%u exception=%d fatal=%d",(unsigned)rr,g_jni_exception,g_jni_fatal_count);
                                                        guest_heap_validate_all("after-render");
                                                        logmsg("[UNITY_PROBE][RENDER][RETURN] result=%u exception=%d fatal_intercepted=%d alloc_delta=%zu free_delta=%zu active=%zu kill_requested=%d guest_swaps_delta=%lu\n",(unsigned)rr,g_jni_exception,g_jni_fatal_count,g_guest_alloc_total-rab,g_guest_free_total-rfb,g_guest_alloc_active,(int)g_jni_kill_requested,g_bender_egl_swaps-first_swaps_before);
                                                        if(g_jni_exception||g_jni_fatal_count||g_jni_kill_requested) render_stage_ok=0;
                                                    } else {
                                                        g_render_jmp_active=0; bender_render_watchdog_done("nativeRender-kill-intercept"); render_stage_ok=0;
                                                        set_checkpoint("nativeRender aborted by UnityPlayer.kill request");
                                                        guest_heap_validate_all("after-render-kill-intercept");
                                                        logmsg("[UNITY_PROBE][RENDER][KILL_INTERCEPTED] Unity solicitou kill durante primeiro frame; probe preservado para diagnostico. alloc_delta=%zu free_delta=%zu active=%zu\n",g_guest_alloc_total-rab,g_guest_free_total-rfb,g_guest_alloc_active);
                                                    }
                                                    if(render_stage_ok) logmsg("[UNITY_PROBE][RENDER_STAGE][OK] nativeRender retornou sem crash/excecao/fatal/kill; result=%u.\n",(unsigned)rr);
                                                    else logmsg("[UNITY_PROBE][RENDER_STAGE][BLOCKED] primeiro frame incompleto exception=%d fatal=%d kill_requested=%d.\n",g_jni_exception,g_jni_fatal_count,(int)g_jni_kill_requested);

                                                    if(want_unity_loop&&render_stage_ok&&!g_jni_exception&&!g_jni_fatal_count&&!g_jni_kill_requested){
                                                        unsigned frame_limit=bender_frame_loop_frames();
                                                        unsigned delay_us=bender_frame_loop_delay_us();
                                                        volatile unsigned frame=2u;
                                                        volatile unsigned frames_done=1u;
                                                        volatile int loop_ok=1;
                                                        if(g_bender_egl_swaps==first_swaps_before){
                                                            bender_host_present_fallback(1u,"first-nativeRender-no-eglSwapBuffers");
                                                        }
                                                        logmsg("[BENDER_FRAME_PACING][BEGIN] source=fix69 mode=%s target_us=%u legacy_delay_us=%u runtime_select=1 policy=compat-or-measured-auto60-or-uncapped code_patch=0 forced_completion=0\n",bender_fix69_frame_mode_name_2869(),bender_fix61_target_us_2861(),delay_us);
                                                        if(want_unity_run)logmsg("[BENDER_FRAME_LOOP][BEGIN] mode=continuous target_frames=unbounded delay_us=%u first_result=%u guest_swaps=%lu host_presents=%lu input_hook=%p\n",delay_us,(unsigned)rr,g_bender_egl_swaps,g_bender_host_presents,g_unity_native_inject_event_fn);
                                                        else logmsg("[BENDER_FRAME_LOOP][BEGIN] mode=diagnostic target_frames=%u delay_us=%u first_result=%u guest_swaps=%lu host_presents=%lu\n",frame_limit,delay_us,(unsigned)rr,g_bender_egl_swaps,g_bender_host_presents);
                                                        if((want_unity_run||frame_limit>1u)&&(unsigned)rr!=0u){
                                                            bender_render_watchdog_start();
                                                            while(want_unity_run?!g_bender_run_stop_requested:(frame<=frame_limit)){
                                                                volatile size_t fab=g_guest_alloc_total, ffb=g_guest_free_total;
                                                                volatile unsigned long swaps_before=g_bender_egl_swaps;
                                                                const uint64_t bender_fix61_frame_start_us_2861=bender_fix61_now_us_2861();
                                                                if(want_unity_run)set_checkpoint_quiet("nativeRender continuous frame=%u begin",frame);
                                                                else set_checkpoint_quiet("nativeRender loop frame=%u/%u begin",frame,frame_limit);
                                                                g_jni_kill_requested=0;
                                                                bender_input_poll(frame);
                                                                bender_jni_choreographer_pump(frame);
                                                                if(g_jni_exception||g_jni_fatal_count||g_jni_kill_requested){if(!g_bender_exit_reason_code)g_bender_exit_reason_code=4;g_bender_exit_frame=frame;loop_ok=0;logmsg("[BENDER_FRAME_LOOP][CHOREOGRAPHER_BLOCKED] frame=%u exception=%d fatal=%d kill=%d\n",frame,g_jni_exception,g_jni_fatal_count,(int)g_jni_kill_requested);break;}
                                                                volatile bjboolean fr=0;
                                                                int fjump=sigsetjmp(g_render_jmp,1);
                                                                if(fjump!=0){
                                                                    g_render_jmp_active=0;loop_ok=0;
                                                                    set_checkpoint("nativeRender frame=%u kill-intercept",frame);
                                                                    logmsg("[BENDER_FRAME_LOOP][KILL_INTERCEPTED] frame=%u alloc_delta=%zu free_delta=%zu active=%zu\n",frame,g_guest_alloc_total-fab,g_guest_free_total-ffb,g_guest_alloc_active);
                                                                    break;
                                                                }
                                                                g_render_jmp_active=1;
                                                                fr=((UnityRenderFn)g_unity_native_render_fn)(&g_env,(void*)activity);
                                                                g_render_jmp_active=0;frames_done=frame;
                                                                bender_fix13_frame_tick_2813(frame);
                                                                if(want_unity_run)set_checkpoint_quiet("nativeRender continuous frame=%u returned=%u",frame,(unsigned)fr);
                                                                else set_checkpoint_quiet("nativeRender loop frame=%u/%u returned=%u",frame,frame_limit,(unsigned)fr);
                                                                if(g_jni_exception||g_jni_fatal_count||g_jni_kill_requested){if(!g_bender_exit_reason_code)g_bender_exit_reason_code=5;g_bender_exit_frame=frame;loop_ok=0;logmsg("[BENDER_FRAME_LOOP][BLOCKED] frame=%u result=%u exception=%d fatal=%d kill=%d\n",frame,(unsigned)fr,g_jni_exception,g_jni_fatal_count,(int)g_jni_kill_requested);break;}
                                                                if(g_bender_egl_swaps==swaps_before)bender_host_present_fallback(frame,"nativeRender-no-eglSwapBuffers");
                                                                if(frame<=10u||frame%300u==0u||(!want_unity_run&&frame==frame_limit)){
                                                                    logmsg("[BENDER_FRAME_LOOP][FRAME] mode=%s frame=%u%s result=%u alloc_delta=%zu free_delta=%zu active=%zu guest_swaps=%lu host_presents=%lu jni_handles=%d/%d/%d\n",want_unity_run?"continuous":"diagnostic",frame,want_unity_run?"":"/target",(unsigned)fr,g_guest_alloc_total-fab,g_guest_free_total-ffb,g_guest_alloc_active,g_bender_egl_swaps,g_bender_host_presents,g_class_count,g_method_count,g_object_count);
                                                                    Module*mcprime=guest_module_from_path("libmcprime.so");Module*mcservices=guest_module_from_path("libmcservices.so");
                                                                    if(!g_football_native_autobootstrap_2763 && frame>=600u && bender_pad_completed_2764()>=2ul && !strcmp(BENDER_APP_PACKAGE,"com.miniclip.minifootball") && mcprime && mcservices && g_mcprime_dlopen_requests_2762==0ul && g_mcservices_dlopen_requests_2762==0ul){
                                                                        g_football_native_autobootstrap_2763=1;
                                                                        logmsg("[BENDER_FOOTBALL_NATIVE][WAIT] frame=%u pad_complete=%lu mcprime_req=0 mcservices_req=0 action=diagnostic-only-no-force\n",frame,bender_pad_completed_2764());
                                                                    }
                                                                    bender_pad_refresh_installed_2811();
                                                                    logmsg("[BENDER_LOADING_GATE] frame=%u pad_completed=%lu pad_data=%d pad_stream=%d dns=%lu connect=%lu firebase_tasks=%lu/%lu/%lu remote_tasks=%lu clevertap_boot=%d clevertap_msgs=%lu applovin=%d media_calls=%lu mcprime_req=%lu mcprime_init=%d mcprime_jni=%d mcservices_req=%lu mcservices_init=%d mcservices_jni=%d pad_listener=%p pad_listener_regs=%lu\n",frame,bender_pad_completed_2764(),g_pad_data_complete_2764,g_pad_stream_complete_2764,g_bender_net_dns_calls_2750,g_bender_net_connect_calls_2750,g_firebase_task_created_2757,g_firebase_task_completed_2757,g_firebase_task_failed_2757,g_remote_config_task_count_2761,g_clevertap_bootstrap_sent_2758,g_clevertap_unity_message_count_2758,g_applovin_initialized_2762,g_bender_media_calls,g_mcprime_dlopen_requests_2762,mcprime?mcprime->init_state:-1,mcprime?mcprime->jni_onload_state:-1,g_mcservices_dlopen_requests_2762,mcservices?mcservices->init_state:-1,mcservices?mcservices->jni_onload_state:-1,g_pad_download_listener_2765,g_pad_download_listener_regs_2765);
                                                                    bender_application_paths_health_2772(frame);
                                                                    bender_content_async_health_2773(frame);
                                                                    bender_fix12_health_2812(frame);
                                                                }
                                                                if((unsigned)fr==0u){if(!g_bender_exit_reason_code)g_bender_exit_reason_code=3;g_bender_exit_frame=frame;logmsg("[BENDER_FRAME_LOOP][STOP] frame=%u reason=nativeRender-returned-0\n",frame);break;}
                                                                bender_fix69_frame_pace_2869(frame,bender_fix61_frame_start_us_2861,delay_us);
                                                                ++frame;if(frame==0u)frame=2u;
                                                            }
                                                            if(want_unity_run)bender_fmod_audio_stop_fix5("frame-loop-end");
                                                            bender_render_watchdog_done(loop_ok?(want_unity_run?"continuous-run-stop":"frame-loop-complete"):"frame-loop-blocked");
                                                        }
                                                        if(want_unity_run&&g_bender_run_stop_requested)logmsg("[BENDER_RUN][STOP_REQUESTED] frames_done=%u action=clean-exit\n",frames_done);
                                                        if(want_unity_run){if(g_bender_exit_frame==0u)g_bender_exit_frame=frames_done;bender_emit_exit_reason();}
                                                        guest_heap_validate_all(want_unity_run?"after-continuous-run":"after-frame-loop");
                                                        if(want_unity_run)logmsg("[BENDER_FRAME_LOOP][DONE] mode=continuous frames_done=%u loop_ok=%d guest_swaps=%lu host_presents=%lu exception=%d fatal=%d kill=%d\n",frames_done,loop_ok,g_bender_egl_swaps,g_bender_host_presents,g_jni_exception,g_jni_fatal_count,(int)g_jni_kill_requested);
                                                        else logmsg("[BENDER_FRAME_LOOP][DONE] mode=diagnostic frames_done=%u target=%u loop_ok=%d guest_swaps=%lu host_presents=%lu exception=%d fatal=%d kill=%d\n",frames_done,frame_limit,loop_ok,g_bender_egl_swaps,g_bender_host_presents,g_jni_exception,g_jni_fatal_count,(int)g_jni_kill_requested);
                                                        if(!loop_ok)render_stage_ok=0;
                                                    }
                                                }
                                            } else if(want_unity_render){
                                                render_stage_ok=0;
                                                logmsg("[UNITY_PROBE][RENDER_STAGE][BLOCKED] prerequisitos incompletos lifecycle=%d surface=%d exception=%d fatal=%d.\n",lifecycle_stage_ok,surface_stage_ok,g_jni_exception,g_jni_fatal_count);
                                            }
                                            if(want_unity_initjni) logmsg("[UNITY_PROBE][INITJNI][NEXT] initJni concluido; teste phase=unity-lifecycle.\n");
                                            else if(want_unity_lifecycle&&lifecycle_stage_ok&&!g_jni_exception&&!g_jni_fatal_count) logmsg("[UNITY_PROBE][LIFECYCLE][NEXT] focus/resume atravessados; teste phase=unity-surface.\n");
                                            else if(want_unity_surface&&surface_stage_ok&&lifecycle_stage_ok&&!g_jni_exception&&!g_jni_fatal_count) logmsg("[UNITY_PROBE][SURFACE][NEXT] Surface + lifecycle atravessados limpos; teste phase=unity-render.\n");
                                            else if(want_unity_surface) logmsg("[UNITY_PROBE][SURFACE][BLOCKED] nao promover para unity-render; envie este log.\n");
                                            else if(want_unity_run&&render_stage_ok&&!g_jni_exception&&!g_jni_fatal_count) logmsg("[UNITY_PROBE][RUN][DONE] modo continuo encerrou limpo por solicitacao externa.\n");
                                            else if(want_unity_loop&&render_stage_ok&&!g_jni_exception&&!g_jni_fatal_count) logmsg("[UNITY_PROBE][FRAME_LOOP][DONE] bootstrap + frame loop finito atravessaram limpos.\n");
                                            else if(want_unity_render&&render_stage_ok&&!g_jni_exception&&!g_jni_fatal_count) logmsg("[UNITY_PROBE][RENDER][DONE] primeira chamada de render atravessou limpa; use phase=unity-loop para o frame loop finito.\n");
                                            else if(want_unity_render) logmsg("[UNITY_PROBE][RENDER][BLOCKED] primeira chamada de render nao atravessou limpa; envie este log.\n");
                                        }
                                    }
                                } else logmsg("[UNITY_PROBE][NATIVE_LOAD][NEXT] use phase=unity-initjni para executar UnityPlayer.initJni de forma controlada.\n");
                            } else logmsg("[UNITY_PROBE][NATIVE_LOAD][WARN] NativeLoader retornou sem concluir os dois init_array; enviar log.\n");
                        }
                    } else logmsg("[UNITY_PROBE][JNI_MAIN][NEXT] use phase=native-load para liberar Unity + IL2CPP; native-load-unity continua disponivel para diagnostico isolado.\n");
                } else logmsg("[UNITY_PROBE][JNI_MAIN][WARN] registro incompleto; enviar log antes de avancar.\n");
            }
        }
#else
        logmsg("[UNITY_PROBE][JNI_FLOW][SKIP] execucao de codigo AArch64 somente no R36S/AArch64.\n");
#endif
    } else {
        logmsg("[UNITY_PROBE][NEXT] fase map concluida; use phase=jni-main, native-load-unity, native-load, unity-initjni, unity-lifecycle, unity-surface, unity-render ou unity-loop.\n");
    }
    guest_heap_summary("phase-end");
    logmsg("[BENDER_CXA][SUMMARY] registered=%zu dropped=%zu finalize_calls=%zu\n",g_guest_atexit_count,g_guest_atexit_dropped,g_guest_finalize_calls);
    if(want_jni_flow) {
        int exit_rc=total_unsupported?4:0;
        set_checkpoint("safe exit rc=%d guest_atexit=%zu",exit_rc,g_guest_atexit_count);
        logmsg("[UNITY_PROBE][EXIT][SAFE] codigo guest executado; mantendo imagens mapeadas ate o kernel encerrar o probe. rc=%d\n",exit_rc);
        fflush(NULL);
        _exit(exit_rc);
    }
    set_checkpoint("cleanup begin guest_atexit=%zu",g_guest_atexit_count);
    cleanup();
    set_checkpoint("cleanup complete guest mappings released");
    logmsg("[UNITY_PROBE][EXIT][OK] cleanup concluido; destructors guest nao foram entregues a glibc.\n");
    if (g_log) { g_log_fd=-1; fclose(g_log); g_log=NULL; }
    return total_unsupported ? 4 : 0;
}

