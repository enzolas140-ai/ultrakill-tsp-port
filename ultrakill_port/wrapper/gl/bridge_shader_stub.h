/* bridge_shader_stub.h - API de descoberta de simbolos + guard + hooks.
 *
 * Este modulo NAO faz magic de shader. Ele:
 *   1. Enumera os simbolos EXPORTADOS do libunity.so real (para sabermos
 *      com o que podemos hookar - o plano "Shader::CreateProgram via dlsym"
 *      falha se o simbolo nao esta na .dynsym, e Unity Android release
 *      build costuma strip. Este probe diz a verdade).
 *   2. Instala um SIGSEGV/SIGABRT handler que da backtrace + fault addr
 *      no log ANTES do processo morrer - ai sabemos ONDE o Unity quebrou
 *      (Shader::CreateProgram, GfxDeviceGLES3X::CreateGpuProgram, etc.).
 *   3. Mantem uma registry de GOT-patches que, uma vez descoberto o
 *      simbolo alvo, substitui por nossa stub function. Framework para
 *      a R1-b de verdade.
 *
 * Chamar shim_shader_probe() e shim_shader_install_guard() no main do
 * bridge, ANTES de UnityMain(). */
#ifndef BRIDGE_SHADER_STUB_H
#define BRIDGE_SHADER_STUB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------- discovery -------- */
typedef struct shim_shader_symbol {
  const char *name;         /* demangled se possivel, senao raw */
  const char *raw_name;     /* como esta na .dynstr */
  void       *addr;         /* valor de st_value ajustado por base */
  size_t      size;         /* st_size (nem sempre preenchido) */
  int         info;         /* st_info */
  int         matched_filter;
} shim_shader_symbol;

typedef struct shim_shader_probe_result {
  shim_shader_symbol *symbols;
  int count;
  int total_dynsym;
  int dl_link_map_available;
} shim_shader_probe_result;

/* Enumera TODOS os simbolos de .dynsym do libunity.so identificado pelo
 * `handle` (retorno do dlopen). Se `filter_pattern` for NULL ou "",
 * aceita todos. Caso contrario, so reporta simbolos contendo alguma das
 * substrings (case-insensitive).
 * Result: shim_shader_probe_result - free() com shim_shader_probe_free().
 */
shim_shader_probe_result *shim_shader_probe(void *unity_handle,
                                             const char *const *filter_substrings,
                                             int n_substrings);
void shim_shader_probe_free(shim_shader_probe_result *r);

/* Conveniencia: lista de candidatos tipicos de shader em Unity 2019.4.
 * Passa pra shim_shader_probe() com estes filters e devolve o resultado. */
#define SHIM_SHADER_DEFAULT_FILTERS \
  (const char *[]){ \
    "Shader", "shader", "CreateProgram", "create_program", \
    "GfxDevice", "gfxdevice", "Program", "program", \
    "GLES3X", "gles", "CompileProgram", "LinkProgram", \
    "CreateGpuProgram", "GraphicsCaps", "RenderSurface", \
    "ShaderGpuProgram", "GpuProgram", "Material", \
    "SerializedFile", "LoadFile", "LoadGpuProgramFromAsset", \
    NULL }
#define SHIM_SHADER_DEFAULT_FILTERS_N 21

/* -------- guard (SIGSEGV/SIGABRT/SIGILL) -------- */
/* Instala handlers que logam:
 *   - numero do signal
 *   - si_addr (fault address)
 *   - PC do ucontext (onde exatamente o crash aconteceu)
 *   - backtrace via <execinfo.h> (ate 32 frames)
 *   - lista de DSOs (dl_iterate_phdr) com base address - ai o dev
 *     resolve PC para file+offset offline com addr2line.
 *
 * NAO tenta longjmp nem recover - o estado depois de SIGSEGV em
 * Unity e irrecuperavel. O handler loga e re-raise o signal pro
 * default (core dump / exit). */
int shim_shader_install_guard(void);

/* -------- hook registry -------- */
typedef enum {
  SHIM_HOOK_NOT_INSTALLED = 0,
  SHIM_HOOK_INSTALLED,
  SHIM_HOOK_FAILED_NO_SYMBOL,
  SHIM_HOOK_FAILED_NO_PLT,
  SHIM_HOOK_FAILED_MPROTECT,
} shim_hook_status;

typedef struct shim_hook {
  char  sym_name[128];
  void *target_addr;         /* endereco do symbol original */
  void *replacement_addr;    /* nossa funcao */
  void *original_addr_saved; /* trampolin para chamar o original */
  shim_hook_status status;
} shim_hook;

/* Registra um hook por nome. Aplica imediatamente se o simbolo existir.
 * Retorna NULL se falhou; senao retorna o handle da entrada. */
shim_hook *shim_hook_register(void *unity_handle, const char *symbol_name,
                              void *replacement, void **out_original);

/* Aplica todos os hooks registrados. Retorna quantos foram instalados. */
int shim_hook_apply_all(void);

/* Reporta status de cada hook no log. */
void shim_hook_report(void);

/* Busca uma substring em strings de `.rodata` do libunity.so.
 * Unity as vezes referencia simbolos por nome em texto (p/ debug, ou
 * via RTTI). Achar "Shader::CreateProgram" em .rodata e um indicio de
 * que o dev de Unity usava aquele nome em log, e ai a gente sabe onde
 * caçar o entry point real em .text. */
int shim_shader_scan_rodata(void *unity_handle, const char *needle,
                            char *out_context, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* BRIDGE_SHADER_STUB_H */
