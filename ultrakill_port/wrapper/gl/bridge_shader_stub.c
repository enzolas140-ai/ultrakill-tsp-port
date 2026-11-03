/* bridge_shader_stub.c - ver bridge_shader_stub.h.
 *
 * Nota importante: este modulo roda ANTES do UnityMain e produz diagnostico
 * no log para guiar a iteracao. As funcoes "hook" ainda nao substituem
 * nada automaticamente - registram apenas o que DEVERIAM substituir, e
 * validam que o alvo existe. Isso e intencional: sem primeiro rodar e
 * ver o resultado real do probe no TSP, nao temos como saber se o
 * simbolo alvo esta visivel na .dynsym ou se precisa ser achado via
 * pattern match em .text. */
#define _GNU_SOURCE
#include <bridge_shader_stub.h>

#include <dlfcn.h>
#include <elf.h>
#include <execinfo.h>
#include <link.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef ElfW
#  if __ELF_NATIVE_CLASS == 32
#    define ElfW(type) Elf32_##type
#  else
#    define ElfW(type) Elf64_##type
#  endif
#endif

static FILE *g_logfp;
static void log_init(void) {
  if (g_logfp) return;
  const char *p = getenv("ULTRAKILL_SHADER_LOG");
  g_logfp = fopen(p ? p : "/tmp/ultrakill_shader.log", "a");
  if (!g_logfp) g_logfp = stderr;
}
#define LOG(...) do { log_init(); fprintf(g_logfp, __VA_ARGS__); fflush(g_logfp); } while (0)

/* =========================================================================
 *  DISCOVERY: enumerar .dynsym de libunity.so
 * ========================================================================= */

struct phdr_ctx {
  const char *want_name_sub;
  uintptr_t   base;
  const ElfW(Ehdr) *ehdr;
  const ElfW(Phdr) *phdr;
  int found;
};

/* Detecta pelo path que o dlpi_name. Passa se contiver a substring. */
static int phdr_cb(struct dl_phdr_info *info, size_t size, void *vctx) {
  (void)size;
  struct phdr_ctx *c = (struct phdr_ctx*)vctx;
  if (!info->dlpi_name) return 0;
  if (!strstr(info->dlpi_name, c->want_name_sub)) return 0;
  c->base = (uintptr_t)info->dlpi_addr;
  c->ehdr = (const ElfW(Ehdr)*)info->dlpi_addr;
  c->phdr = info->dlpi_phdr;
  c->found = 1;
  return 1;   /* para a iteracao */
}

static const ElfW(Dyn) *find_dynamic(const struct phdr_ctx *c) {
  for (int i = 0; i < c->ehdr->e_phnum; i++)
    if (c->phdr[i].p_type == PT_DYNAMIC)
      return (const ElfW(Dyn)*)(c->base + c->phdr[i].p_vaddr);
  return NULL;
}

static const ElfW(Sym) *find_symtab(const ElfW(Dyn) *dyn, const char **strtab_out, size_t *nsyms_out) {
  const ElfW(Sym) *sym = NULL;
  const char *str = NULL;
  size_t n = 0;
  const ElfW(Dyn) *hashtab = NULL;
  for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; d++) {
    switch (d->d_tag) {
      case DT_SYMTAB: sym = (const ElfW(Sym)*)d->d_un.d_ptr; break;
      case DT_STRTAB: str = (const char*)d->d_un.d_ptr; break;
      case DT_HASH:   hashtab = (const ElfW(Dyn)*)d->d_un.d_ptr; break;
      default: break;
    }
  }
  if (hashtab) n = ((const uint32_t*)hashtab)[1];   /* DT_HASH->nchain */
  *strtab_out = str;
  *nsyms_out  = n;
  return sym;
}

static int contains_caseins(const char *hay, const char *needle) {
  if (!needle || !*needle) return 1;
  for (const char *h = hay; *h; h++) {
    const char *a = h, *b = needle;
    while (*a && *b) {
      char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
      char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
      if (ca != cb) break;
      a++; b++;
    }
    if (!*b) return 1;
  }
  return 0;
}

shim_shader_probe_result *shim_shader_probe(void *unity_handle,
                                             const char *const *filters,
                                             int n_filters) {
  (void)unity_handle;   /* poderíamos usar dlopen-based info, mas dl_iterate_phdr é mais rico */
  struct phdr_ctx c = { .want_name_sub = "libunity.so" };
  dl_iterate_phdr(phdr_cb, &c);

  shim_shader_probe_result *r = calloc(1, sizeof(*r));
  if (!c.found) {
    LOG("[shader_stub] libunity.so ainda NAO carregado - shim_shader_probe() chamado antes do dlopen?\n");
    r->dl_link_map_available = 0;
    return r;
  }
  r->dl_link_map_available = 1;
  LOG("[shader_stub] libunity.so base=0x%lx ehdr@%p phnum=%d\n",
      (unsigned long)c.base, (void*)c.ehdr, c.ehdr->e_phnum);

  const ElfW(Dyn) *dyn = find_dynamic(&c);
  if (!dyn) { LOG("[shader_stub] PT_DYNAMIC ausente\n"); return r; }
  const char *strtab = NULL;
  size_t nsyms = 0;
  const ElfW(Sym) *symtab = find_symtab(dyn, &strtab, &nsyms);
  if (!symtab || !strtab) {
    /* Algumas libc expõem d_un.d_ptr já absoluto; outras relativo. Tenta os dois. */
    LOG("[shader_stub] SYMTAB/STRTAB lookup falhou - dyn nao resolvido\n");
    return r;
  }
  r->total_dynsym = (int)nsyms;

  /* Filtra e guarda */
  r->symbols = calloc(nsyms + 1, sizeof(*r->symbols));
  for (size_t i = 0; i < nsyms; i++) {
    const ElfW(Sym) *s = &symtab[i];
    const char *nm = strtab + s->st_name;
    if (!*nm) continue;
    int matched = 0;
    if (n_filters <= 0) matched = 1;
    else for (int k = 0; k < n_filters; k++)
      if (contains_caseins(nm, filters[k])) { matched = 1; break; }
    if (!matched) continue;

    shim_shader_symbol *out = &r->symbols[r->count++];
    out->raw_name = strdup(nm);
    out->name     = out->raw_name;   /* sem demangle - sem libiberty linkada */
    out->addr     = (void*)(c.base + s->st_value);
    out->size     = s->st_size;
    out->info     = s->st_info;
    out->matched_filter = 1;
  }
  LOG("[shader_stub] .dynsym total=%zu, matchei=%d candidatos de shader\n",
      nsyms, r->count);
  for (int i = 0; i < r->count && i < 60; i++) {
    LOG("   [%2d] %p  size=%-6zu  %s\n",
        i, r->symbols[i].addr, r->symbols[i].size, r->symbols[i].raw_name);
  }
  if (r->count > 60) LOG("   ... (%d mais)\n", r->count - 60);
  return r;
}

void shim_shader_probe_free(shim_shader_probe_result *r) {
  if (!r) return;
  if (r->symbols) {
    for (int i = 0; i < r->count; i++) free((void*)r->symbols[i].raw_name);
    free(r->symbols);
  }
  free(r);
}

/* =========================================================================
 *  .rodata scan: achar strings tipo "Shader::CreateProgram" dentro
 *  do libunity.so. Isso nao prova que o simbolo esta exportado, mas
 *  quando a string esta presente significa que o build foi feito com
 *  logging ativo na funcao - ai sabemos que ela existe.
 * ========================================================================= */

struct segs_ctx {
  uintptr_t base;
  const ElfW(Phdr) *phdr;
  int nph;
};
static int segs_cb(struct dl_phdr_info *info, size_t sz, void *vctx) {
  (void)sz;
  struct segs_ctx *c = (struct segs_ctx*)vctx;
  if (!info->dlpi_name || !strstr(info->dlpi_name, "libunity.so")) return 0;
  c->base = (uintptr_t)info->dlpi_addr;
  c->phdr = info->dlpi_phdr;
  c->nph  = info->dlpi_phnum;
  return 1;
}

int shim_shader_scan_rodata(void *h, const char *needle,
                            char *out_ctx, size_t out_len) {
  struct segs_ctx c = {0};
  dl_iterate_phdr(segs_cb, &c);
  if (!c.phdr) { LOG("[shader_stub] libunity.so nao mapeado ainda\n"); return 0; }
  size_t n = strlen(needle);
  for (int i = 0; i < c.nph; i++) {
    if (c.phdr[i].p_type != PT_LOAD) continue;
    if (!(c.phdr[i].p_flags & PF_R)) continue;
    unsigned char *p = (unsigned char*)(c.base + c.phdr[i].p_vaddr);
    size_t len = c.phdr[i].p_filesz;
    for (size_t j = 0; j + n < len; j++) {
      if (memcmp(p + j, needle, n) == 0) {
        size_t sstart = (j > 40) ? j - 40 : 0;
        size_t send   = (j + n + 40 < len) ? j + n + 40 : len;
        char buf[256] = {0};
        size_t k = 0;
        for (size_t z = sstart; z < send && k + 1 < sizeof buf; z++) {
          unsigned char ch = p[z];
          buf[k++] = (ch >= 32 && ch < 127) ? (char)ch : '.';
        }
        LOG("[shader_stub] .rodata match @ +%zu (segmento %d): %s\n", j, i, buf);
        if (out_ctx && out_len) snprintf(out_ctx, out_len, "%s", buf);
        return 1;
      }
    }
  }
  LOG("[shader_stub] .rodata NAO contem '%s'\n", needle);
  return 0;
}

/* =========================================================================
 *  GUARD: SIGSEGV/SIGABRT/SIGILL handler com backtrace + PC
 * ========================================================================= */

static volatile sig_atomic_t in_handler = 0;
static const char *signame(int s) {
  switch (s) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGILL:  return "SIGILL";
    case SIGTRAP: return "SIGTRAP";
    case SIGBUS:  return "SIGBUS";
    case SIGFPE:  return "SIGFPE";
    default: return "SIG?";
  }
}

/* Extrai PC do ucontext de forma portavel ARM/AArch64. */
static uintptr_t pc_from_uc(void *u) {
  ucontext_t *uc = (ucontext_t*)u;
#if defined(__aarch64__)
  return (uintptr_t)uc->uc_mcontext.pc;
#elif defined(__arm__)
  return (uintptr_t)uc->uc_mcontext.arm_pc;
#elif defined(__x86_64__)
  return (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#else
  return 0;
#endif
}
static uintptr_t fault_addr_from_si(siginfo_t *si, int sig) {
  (void)sig;
#if defined(SIGSEGV)
  return (uintptr_t)si->si_addr;
#else
  return 0;
#endif
}

/* Escreve para stderr sem malloc (async-signal-safe): */
static void safe_puts(const char *s) { ssize_t r = write(2, s, strlen(s)); (void)r; }
static void safe_putx(uintptr_t v) {
  char buf[24];
  int n = snprintf(buf, sizeof buf, "0x%lx", (unsigned long)v);
  if (n > 0) { ssize_t r = write(2, buf, n); (void)r; }
}

/* --- espelho do crash para um FICHEIRO no SD -----------------------------
 * O Unity 2019 redireciona/fecha o stderr dele durante o boot do player, e
 * ai o handler escreve num fd que ja nao e o nosso log (foi assim que o
 * SIGBUS do device ficou mudo). Abrimos NOSSO proprio fd (append, sem
 * stdio, sem malloc) e escrevemos nos DOIS. */
static volatile int g_crashfd = -1;
static void crash_w(const char *s, size_t n) {
  ssize_t r;
  int fd;
  if (s && n) {
    r = write(2, s, n); (void)r;
    fd = g_crashfd;
    if (fd >= 0) { r = write(fd, s, n); (void)r; }
  }
}
static void crash_puts(const char *s) { if (s) crash_w(s, strlen(s)); }
static void crash_putx(uintptr_t v) {
  char buf[32];
  int n = snprintf(buf, sizeof buf, "0x%lx\n", (unsigned long)v);
  if (n > 0) crash_w(buf, (size_t)n);
}

/* Linha de /proc/self/maps que contem o modulo indicado (so syscalls). */
static void crash_dump_map(const char *fname) {
  char buf[1024];
  size_t used = 0;
  int fd;
  const char *base;
  ssize_t r;
  int hits = 0;
  if (!fname || !*fname) return;
  fd = open("/proc/self/maps", O_RDONLY);
  if (fd < 0) return;
  base = strrchr(fname, '/');
  base = base ? base + 1 : fname;
  while ((r = read(fd, buf + used, sizeof(buf) - 1 - used)) > 0) {
    char *nl;
    used += (size_t)r;
    buf[used] = 0;
    while ((nl = memchr(buf, '\n', used)) != NULL) {
      size_t linelen = (size_t)(nl - buf);
      *nl = 0;
      if (linelen < sizeof(buf) && strstr(buf, base) && hits < 4) {
        crash_puts("  maps: "); crash_puts(buf); crash_puts("\n");
        hits++;
      }
      used -= linelen + 1;
      memmove(buf, nl + 1, used);
      buf[used] = 0;
    }
    if (used >= sizeof(buf) - 1) { used = 0; buf[0] = 0; }  /* linha gigante */
  }
  if (used && strstr(buf, base) && hits < 4) {
    crash_puts("  maps: "); crash_puts(buf); crash_puts("\n");
  }
  close(fd);
}

static void guard_handler(int sig, siginfo_t *si, void *u) {
  if (in_handler) _exit(128 + sig);   /* no recursion */
  in_handler = 1;

  crash_puts("\n[guard] *** ");
  crash_puts(signame(sig));
  crash_puts(" em pc=");
  crash_putx(pc_from_uc(u));
  crash_puts("[guard] si_addr=");
  crash_putx(fault_addr_from_si(si, sig));
  crash_puts("  ***\n");

  /* backtrace com libunwind/execinfo */
  void *bt[32];
  int n = backtrace(bt, 32);
  crash_puts("[guard] backtrace:\n");
  backtrace_symbols_fd(bt, n, 2);
  if (g_crashfd >= 0) backtrace_symbols_fd(bt, n, g_crashfd);

  /* lista de DSOs (dl_iterate_phdr nao eh async-safe, mas em crash
   * normalmente da pra chamar sem dead-lock) */
  safe_puts("[guard] --- DSO map (resolve PC -> file+offset offline) ---\n");
  struct link_map *lm = NULL;
  Dl_info info;
  uintptr_t pc = pc_from_uc(u);
  if (dladdr((void*)pc, &info)) {
    crash_puts("  pc esta em: ");
    crash_puts(info.dli_fname ? info.dli_fname : "(anon)");
    crash_puts("  sym=");
    crash_puts(info.dli_sname ? info.dli_sname : "(nao exportado)");
    crash_puts("  symbase=");
    crash_putx((uintptr_t)info.dli_saddr);
    crash_puts("  loadbase=");
    crash_putx((uintptr_t)info.dli_fbase);
    crash_puts("  -> offset no arquivo: ");
    crash_putx(pc - (uintptr_t)info.dli_fbase);
    crash_puts("  offline: aarch64-linux-gnu-objdump -d --start-address=0x");
    crash_puts(info.dli_fname ? info.dli_fname : "libunity.so");
    crash_puts("\n");
    crash_dump_map(info.dli_fname);
  }
  (void)lm;

  /* re-raise com default para core dump (se habilitado) */
  signal(sig, SIG_DFL);
  raise(sig);
}

int shim_shader_install_guard(void) {
  struct sigaction sa = {0};
  sa.sa_sigaction = guard_handler;
  sa.sa_flags     = SA_SIGINFO | SA_NODEFER;
  sigemptyset(&sa.sa_mask);
  int rc = 0;
  int sigs[] = { SIGSEGV, SIGABRT, SIGILL, SIGTRAP, SIGBUS, SIGFPE };
  if (g_crashfd < 0) {
    const char *cf = getenv("ULTRAKILL_CRASHFILE");
    if (cf && *cf) g_crashfd = open(cf, O_WRONLY | O_APPEND | O_CREAT, 0644);
  }
  for (size_t i = 0; i < sizeof sigs / sizeof *sigs; i++) {
    if (sigaction(sigs[i], &sa, NULL) != 0) {
      LOG("[shader_stub] sigaction(%s) falhou: %s\n",
          strsignal(sigs[i]), strerror(errno));
      rc--;
    }
  }
  LOG("[shader_stub] guard instalado em %zu signals (rc=%d)\n",
      sizeof sigs / sizeof *sigs, rc);
  log_init();
  return rc;
}

/* O Unity re-instala os handlers dele na thread do RENDER (depois do nosso
 * re-arm), entao rearmar uma vez nao basta. Esta thread re-escreve os nossos
 * sigaction a cada 40ms ate o processo morrer - quem ganhar a corrida e quem
 * imprime o PC do crash, e 40ms e muito menos que o tempo entre o resume e o
 * crash. */
static volatile int g_wd_stop = 0;
static void *guard_wd(void *v) {
  (void)v;
  while (!g_wd_stop) {
    shim_shader_install_guard();
    usleep(40000);
  }
  return NULL;
}

int shim_shader_start_guard_watchdog(void) {
  pthread_t t;
  pthread_attr_t a;
  pthread_attr_init(&a);
  pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
  int r = pthread_create(&t, &a, guard_wd, NULL);
  pthread_attr_destroy(&a);
  LOG("[shader_stub] watchdog de guard: %s\n", r == 0 ? "rodando" : "FALHOU");
  return r;
}

/* =========================================================================
 *  HOOK REGISTRY: framework de GOT-patch
 *
 *  A ideia: para cada simbolo que queiramos substituir, encontramos o
 *  endereco do original via dlsym(handle, name). Isso funciona apenas
 *  para simbolos EXPORTADOS na .dynsym (Unity Android raramente tem
 *  Shader::CreateProgram aqui - por isso este modulo tambem expoe o
 *  probe acima).
 *
 *  Uma vez com target_addr, fazemos:
 *   - mprotect na pagina
 *   - scan .rela.plt (GOT relocation table) do nosso bridge ELF
 *     e de libunity.so, procurando um reloc que aponte para o alvo
 *   - substitui o valor resolved da PLT para apontar para `replacement`
 *   - salva o valor antigo em *out_original (para o stub chamar depois)
 *
 *  Simbolo NAO-exportado: shim_hook_register() registra mas
 *  shim_hook_apply_all() falha com NO_SYMBOL - e o report diz exatamente
 *  isso para orientar o proximo passo.
 * ========================================================================= */

#define SHIM_HOOK_MAX 32
static shim_hook g_hooks[SHIM_HOOK_MAX];
static int g_nhooks = 0;

shim_hook *shim_hook_register(void *unity_handle, const char *symbol_name,
                              void *replacement, void **out_original) {
  if (g_nhooks >= SHIM_HOOK_MAX) {
    LOG("[hook] registry cheio (%d)\n", SHIM_HOOK_MAX);
    return NULL;
  }
  shim_hook *h = &g_hooks[g_nhooks++];
  memset(h, 0, sizeof *h);
  strncpy(h->sym_name, symbol_name, sizeof h->sym_name - 1);
  h->replacement_addr = replacement;

  /* NAO fazemos dlsym(unity_handle, symbol_name) aqui: neste glibc do
   * SpruceOS, dlsym de nome AUSENTE dentro de libunity.so derruba o
   * loader (SIGSEGV ld+0x9048). E o build Android e stripped de qqr
   * modo - esses candidatos C++ nunca estariam no .dynsym. */
  __attribute__((unused)) void *target = NULL;
  if (1) {
    LOG("[hook] %s: dlsym pulado (loader instavel p/ nome ausente) - NO_SYMBOL\n",
        symbol_name);
    h->status = SHIM_HOOK_FAILED_NO_SYMBOL;
    if (out_original) *out_original = NULL;
    return h;
  }
  h->target_addr = target;
  LOG("[hook] %s @ %p - pronto pra apply\n", symbol_name, target);
  if (out_original) *out_original = target;
  return h;
}

/* Walks our own ELF's PLT relocations and libunity.so's, looking for
 * one whose symbol equals `h->sym_name`. If found, patches the GOT
 * slot.
 *
 * IMPLEMENTACAO PENDENTE. Para a iteracao atual do scaffold isto e
 * deliberadamente um stub que reporta NAO-IMPLEMENTADO. Assim que o
 * probe do shim_shader_probe() rodar em device real e a gente vir
 * quais simbolos Unity de fato exporta, preenchemos aqui com um
 * walker de PT_DYNAMIC -> DT_JMPREL que:
 *   1. Itera dl_iterate_phdr (para cada DSO carregado)
 *   2. Localiza a tabela ElfW(Rela)* em DT_JMPREL, com nent = DT_JMPRELCNT
 *   3. Para cada reloc: resolve o nome via DT_SYMTAB+DT_STRTAB
 *   4. Se r_info sym == h->sym_name: mprotect(base + r_offset, PROT_RW),
 *      escreve (void*)h->replacement_addr, mprotect volta pra PROT_R
 *   5. Guarda o valor antigo em h->original_addr_saved
 *
 * Retorna 0 no sucesso, negativo em falha. */
static int patch_got_for(shim_hook *h) {
  LOG("[hook] patch_got_for(%s): walker de DT_JMPREL ainda NAO implementado - "
      "ver comentario em bridge_shader_stub.c\n", h->sym_name);
  (void)h;
  return -1;
}

int shim_hook_apply_all(void) {
  int ok = 0;
  for (int i = 0; i < g_nhooks; i++) {
    shim_hook *h = &g_hooks[i];
    if (h->status == SHIM_HOOK_INSTALLED) { ok++; continue; }
    if (h->status == SHIM_HOOK_FAILED_NO_SYMBOL) continue;   /* nada pra patchar */
    if (!h->target_addr) continue;
    int r = patch_got_for(h);
    if (r == 0) { h->status = SHIM_HOOK_INSTALLED; ok++; }
    else        { h->status = SHIM_HOOK_FAILED_NO_PLT; }
  }
  return ok;
}

void shim_hook_report(void) {
  LOG("[hook] registry (%d entradas):\n", g_nhooks);
  for (int i = 0; i < g_nhooks; i++) {
    const char *st = "?";
    switch (g_hooks[i].status) {
      case SHIM_HOOK_NOT_INSTALLED:      st = "not-installed"; break;
      case SHIM_HOOK_INSTALLED:          st = "INSTALLED"; break;
      case SHIM_HOOK_FAILED_NO_SYMBOL:   st = "no-symbol (stripped?)"; break;
      case SHIM_HOOK_FAILED_NO_PLT:      st = "no-PLT-match"; break;
      case SHIM_HOOK_FAILED_MPROTECT:    st = "mprotect-fail"; break;
    }
    LOG("   %-40s  target=%p  repl=%p  [%s]\n",
        g_hooks[i].sym_name, g_hooks[i].target_addr,
        g_hooks[i].replacement_addr, st);
  }
}
