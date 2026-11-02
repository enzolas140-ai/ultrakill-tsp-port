/* symbol_probe.c - CLI standalone que inspeciona um libunity.so.
 *
 * Uso:
 *   ./symbol_probe /caminho/para/libunity.so [substring ...]
 *
 * Se nenhuma substring for passada, usa SHIM_SHADER_DEFAULT_FILTERS.
 *
 * Roda o shim_shader_probe() contra a lib e imprime:
 *   - total de simbolos .dynsym
 *   - matches do filtro (com enderecos)
 *   - resultado do scan de .rodata para os candidatos mais provaveis
 *   - lista de todos os simbolos (opcional: --all)
 *
 * Isto e o PASSO 0 da Rota 1-b: antes de escrever hook code, descobrir
 * se o simbolo alvo existe na .dynsym. Se nao existir, o hook precisa
 * de pattern-match em .text (proxima iteracao).
 *
 * Compilar em Linux (host ou device), rodar com a libunity.so real:
 *   # extrair libunity.so do APK:
 *   cd ultrakill_port/
 *   unzip -o ULTRADROID.apk 'lib/arm64-v8a/libunity.so' -o /tmp/
 *   ./build-arm64/symbol_probe /tmp/lib/arm64-v8a/libunity.so
 */
#define _GNU_SOURCE
#include <bridge_shader_stub.h>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Precisamos do dlopen ter sucesso para o dl_iterate_phdr encontrar
 * libunity.so - por isso copiamos a lib para um path temporario e
 * chamamos dlopen. Nao executamos nada dela. */

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr,
      "uso: %s /caminho/para/libunity.so [--all] [substring ...]\n"
      "\n"
      "  --all       imprime TODOS os simbolos (default: so matches)\n"
      "  substring   filtras por substring (case insensitive)\n"
      "\n"
      "Exemplo:\n"
      "  %s /tmp/lib/arm64-v8a/libunity.so Shader Program GLES\n",
      argv[0], argv[0]);
    return 2;
  }
  const char *libpath = argv[1];
  int show_all = 0;
  const char *filters[32];
  int nf = 0;
  for (int i = 2; i < argc && nf < 32; i++) {
    if (!strcmp(argv[i], "--all")) { show_all = 1; continue; }
    filters[nf++] = argv[i];
  }

  /* dlopen exige que o path esteja completo ou o loader ache via
   * LD_LIBRARY_PATH. Normaliza. */
  char abspath[4096];
  if (libpath[0] == '/') snprintf(abspath, sizeof abspath, "%s", libpath);
  else snprintf(abspath, sizeof abspath, "./%s", libpath);

  void *h = dlopen(abspath, RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    fprintf(stderr, "dlopen(%s) falhou: %s\n", abspath, dlerror());
    fprintf(stderr, "Dica: dl_iterate_phdr so ve lib se ela estiver de fato\n");
    fprintf(stderr, "carregada no processo. Tente 'sudo apt install libelf1'\n");
    fprintf(stderr, "ou rode com LD_LIBRARY_PATH apontando pro dir da .so.\n");
    return 3;
  }

  printf("== symbol_probe: %s ==\n", abspath);
  fflush(stdout);

  const char *const *use_filters = (const char *const *)filters;
  int use_nf = nf;
  if (nf == 0 && !show_all) {
    /* usa defaults do header */
    use_filters = SHIM_SHADER_DEFAULT_FILTERS;
    use_nf = SHIM_SHADER_DEFAULT_FILTERS_N;
  } else if (show_all) {
    use_filters = NULL;
    use_nf = 0;   /* aceita todos */
  }

  shim_shader_probe_result *r = shim_shader_probe(h, use_filters, use_nf);
  if (!r->dl_link_map_available) {
    printf("\nERRO: libunity.so nao apareceu via dl_iterate_phdr. Possivel causa:\n");
    printf("  - dl_link_map_available=0 (glibc muito antiga, ou dlopen falhou silencioso)\n");
    printf("  - nome do arquivo nao contem 'libunity.so' (checar dli_name)\n");
    shim_shader_probe_free(r);
    dlclose(h);
    return 4;
  }
  printf("\n.dynsym total=%d, matches=%d\n",
         r->total_dynsym, r->count);
  printf("================================================================\n");
  for (int i = 0; i < r->count; i++) {
    printf("  %-6s  %8zu B  %s\n",
           "", r->symbols[i].size, r->symbols[i].raw_name);
    fflush(stdout);
  }

  /* scan de .rodata p/ as strings classicas. Mesmo sem simbolo
   * exportado, a string estar presente indica que Unity loga em algum
   * ponto - e a partir dai da pra fazer pattern-match em .text. */
  static const char *needles[] = {
    "Shader::CreateProgram",
    "GfxDeviceGLES3X::CreateGpuProgram",
    "GfxDeviceGLES20::CreateGpuProgram",
    "GfxDeviceGLES31::CreateGpuProgram",
    "CompileProgram",
    "LinkProgram",
    "CreateProgram",
    "glCompileShader",
    "glLinkProgram",
    "Shaderlab error",
    "Unsupported graphics API",
    "Cannot compile",
    "GraphicsCaps",
    NULL
  };
  printf("\n== scan .rodata (strings de log/error que Unity usaria) ==\n");
  char ctx[300];
  int found_str = 0;
  for (int i = 0; needles[i]; i++) {
    if (shim_shader_scan_rodata(h, needles[i], ctx, sizeof ctx)) {
      printf("  SIM: '%s'\n    contexto: %s\n", needles[i], ctx);
      found_str++;
    }
  }
  if (!found_str)
    printf("  (nenhuma das strings classicas encontrada em .rodata)\n");

  shim_shader_probe_free(r);
  dlclose(h);
  printf("\n== symbol_probe done ==\n");
  return 0;
}
