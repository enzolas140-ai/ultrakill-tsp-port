/* bridge_main.c - ELF que carrega libunity.so como se fosse Android.
 *
 * Fluxo:
 *   1. shim_jni_install()  -> cria JavaVM/JNIEnv fake (mini-JVM em ../jni)
 *   2. shim_gl_init()      -> SDL2 window + EGL context + ANativeWindow
 *   3. ALooper_prepare     -> TLS looper que Unity usa na main
 *   4. AAssetManager       -> filesystem em $OD/gamedata
 *   5. dlopen("libunity.so") + dlopen do shim libEGL.so via NEEDED
 *   6. shim_input_start()  -> thread SDL_event -> AInputQueue/UnitySend*
 *   7. UnityMain(&fake_ANativeActivity, NULL, 0)
 *
 * Status: bloqueio estrutural restante e NAO de linkage - os bundles
 * StandaloneWindows64 carregam DXBC. Ver ports/ultrakill/README.md. */
#include <android_shim.h>
#include <bridge_unity.h>
#include <bridge_jni.h>
#include <bridge_gl.h>
#include <bridge_input.h>
#include <bridge_shader_stub.h>

#include <dlfcn.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

extern AAssetManager *shim_AAssetManager_fromPath(const char *basepath);
extern ANativeWindow *ANativeWindow_get_shim(void);

static char *xstrdup(const char *s) {
  char *r = strdup(s);
  if (!r) { perror("strdup"); exit(2); }
  return r;
}

static void exe_dir(char *out, size_t n) {
  ssize_t r = readlink("/proc/self/exe", out, n-1);
  if (r <= 0) { snprintf(out, n, "."); return; }
  out[r] = 0;
  char *d = dirname(out);
  memmove(out, d, strlen(d)+1);
}

static void *try_dlopen(const char *const *candidates, int n, char *err, size_t elen) {
  if (err && elen) err[0] = 0;
  /* RTLD_LAZY: libunity.so (Bionic) importa ~34 simbolos Android que ainda
   * podem faltar. Com RTLD_NOW o dlopen falha no PRIMEIRO ausente antes de
   * qualquer coisa rodar. Com RTLD_LAZY a lib carrega e so explode (via nosso
   * guard SIGSEGV) quando um simbolo realmente for chamado - ai vemos qual. */
  for (int i = 0; i < n; i++) {
    dlerror();   /* limpa estado anterior */
    void *h = dlopen(candidates[i], RTLD_LAZY | RTLD_GLOBAL);
    if (h) return h;
    const char *e = dlerror();   /* UMA so chamada - dlerror() consome a msg */
    if (err && elen && err[0] == 0)   /* guarda o PRIMEIRO erro, nao o ultimo */
      snprintf(err, elen, "dlopen(\"%s\"): %s", candidates[i],
               e ? e : "(sem mensagem)");
  }
  return NULL;
}

int main(int argc, char **argv, char **envp) {
  /* ---- 0. localizar port root (../gamedata a partir do binario) ---- */
  char exedir[PATH_MAX]; exe_dir(exedir, sizeof exedir);
  char portdir[PATH_MAX];
  snprintf(portdir, sizeof portdir, "%s/..", exedir);   /* gamedata/bin -> port */
  char gamedir[PATH_MAX];
  snprintf(gamedir, sizeof gamedir, "%s/gamedata", portdir);

  /* ---- 1. AAssetManager que le do filesystem ---- */
  setenv("ULTRAKILL_ASSET_ROOT", gamedir, 1);
  AAssetManager *mgr = shim_AAssetManager_fromPath(gamedir);
  if (!mgr) { fprintf(stderr, "asset mgr fail\n"); return 2; }

  /* ---- 2. ALooper na thread main ---- */
  ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
  if (!looper) { fprintf(stderr, "looper fail\n"); return 2; }

  /* ---- 2b. JavaVM/JNIEnv mini (mata o SIGSEGV em activity->vm) ---- */
  shim_jni_install();
  JavaVM *vm  = shim_jni_get_vm();
  JNIEnv *env = shim_jni_new_env_for_thread("main");
  if (!vm || !env) { fprintf(stderr, "jni fail\n"); return 2; }

  /* ---- 2c. SDL2 + EGL + ANativeWindow real ---- */
  int w = 1280, h = 720;
  const char *ws = getenv("ULTRAKILL_INT_W"); if (ws) w = atoi(ws);
  const char *hs = getenv("ULTRAKILL_INT_H"); if (hs) h = atoi(hs);
  static shim_gl_ctx glc;
  shim_gl_status gsr = shim_gl_init(&glc, w, h, "ULTRAKILL");
  if (gsr != SHIM_GL_OK) {
    fprintf(stderr, "shim_gl_init falhou (code %d) - sem display?\n", (int)gsr);
    fprintf(stderr, "  tente: SDL_VIDEODRIVER=x11 %s\n", argv[0]);
    return 6;
  }

  /* ---- 3. monta ANativeActivity com vm/env/clazz/window reais ---- */
  static ANativeActivity act;
  act.packageName       = "arkboxgames.ultrakill";
  act.activityName      = "com.unity3d.player.UnityPlayerNativeActivity";
  act.className         = "ULTRAKILL";
  act.internalDataPath  = gamedir;
  act.externalDataPath  = getenv("ULTRAKILL_PERSIST") ? getenv("ULTRAKILL_PERSIST") : "/tmp";
  act.instanceState     = NULL;
  act.savedStateSize    = 0;
  act.looper            = looper;
  act.contentScale      = 160;
  act.debuggable        = 0;
  act.clazz             = env->FindClass(env,
                            "com/unity3d/player/UnityPlayerNativeActivity");
  act.env               = env;
  act.vm                = vm;
  act.userData          = &glc;   /* Unity pode recuperar via callbacks */
  /* (O ANativeWindow em si passa via eglCreateWindowSurface interceptado.) */

  /* ---- 4. dlopen libunity.so ---- */
  const char *absli = getenv("ULTRAKILL_LIBUNITY");
  const char *cands[] = {
    absli,                                      /* caminho absoluto do launcher */
    "libunity.so",                              /* achado via LD_LIBRARY_PATH */
    "./libunity.so",
    NULL
  };
  char err[256] = "";
  void *hu = try_dlopen(cands, absli ? 3 : 2, err, sizeof err);
  if (!hu) {
    fprintf(stderr, "dlopen libunity.so falhou: %s\n", err);
    fprintf(stderr, "LD_LIBRARY_PATH=%s\n", getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "(unset)");
    fprintf(stderr, "você extraiu lib/arm64-v8a/libunity.so para %s?\n", gamedir);
    return 3;
  }

  /* ---- 5. resolve entrypoint ---- */
  dlerror();
  UnityMain_fn unityMain = (UnityMain_fn)dlsym(hu, "UnityMain");

  /* ---- 5a. shader discovery + guard (Rota 1-b, passo 0) ----
   * shim_shader_probe() enumera .dynsym de libunity.so e loga
   * candidatos com substring de "Shader"/"CreateProgram"/etc em
   * /tmp/ultrakill_shader.log. shim_shader_install_guard() captura
   * SIGSEGV e da backtrace + PC - assim quando o boot quebrar
   * em Shader::CreateProgram ou similar, temos a evidencia.
   *
   * shim_hook_register() registra hooks FUTUROS - o patch_got_for
   * de verdade esta pendente, mas o registry ja loga o status
   * (NO_SYMBOL = esperado em Unity Android stripped). */
  shim_shader_probe_result *probe =
    shim_shader_probe(hu, SHIM_SHADER_DEFAULT_FILTERS,
                      SHIM_SHADER_DEFAULT_FILTERS_N);
  shim_shader_install_guard();

  /* Candidatos classicos; a maioria retorna NO_SYMBOL, o que e o
   * resultado honesto e orienta a proxima iteracao (pattern-scan
   * em .text via bytes magic). */
  static const char *hook_candidates[] = {
    "_ZN6Shader13CreateProgramEv",
    "_ZN6Shader13CreateProgramEP8ShaderPass",
    "UnityShaderCreateProgram",
    "LoadGpuProgramFromAsset",
    NULL
  };
  for (int i = 0; hook_candidates[i]; i++) {
    /* replacement NULL: so discovery por enquanto */
    shim_hook_register(hu, hook_candidates[i], NULL, NULL);
  }
  shim_hook_report();

  /* Guardou no probe result; libera depois do UnityMain retornar (ou crash). */
  /* shim_shader_probe_free(probe); */

  /* ---- 5b. start input thread ----
   * shim_input_start faz dlsym em `hu` pra UnitySend{Touch,Key,Joystick}.
   * Se nao achar (esperado em libunity.so 2019.4, onde os simbolos sao
   * private), cai em AInputQueue via libandroid.so stub. */
  static shim_input_ctx *input_ctx;
  shim_input_status isr = shim_input_start(&input_ctx, hu, w, h);
  if (isr != SHIM_INPUT_OK) {
    fprintf(stderr, "shim_input_start falhou (code %d) - sem gamepad?\n", (int)isr);
    /* nao aborta: o jogo ainda pode rodar com input de teclado SDL */
  }

  if (!unityMain) {
    /* Build ANDROID nao exporta UnityMain; o entry real e JNI_OnLoad(vm, res).
     * Ele cachea a JavaVM e faz RegisterNatives na classe UnityPlayer (nosso
     * mini-JVM agora ARMAZENA esses natives). Depois disso, o loop do jogo e
     * iniciado chamando os natives nativeSetup/nativeRun na ordem da Activity. */
    typedef int (*JNI_OnLoad_fn)(JavaVM *, void *);
    JNI_OnLoad_fn onload = (JNI_OnLoad_fn)dlsym(hu, "JNI_OnLoad");
    if (onload) {
      fprintf(stderr, ">> JNI_OnLoad(vm) - entry real Android\n");
      int ver = onload(vm, NULL);
      fprintf(stderr, "JNI_OnLoad retornou JNI_VERSION=0x%x\n", ver);
      extern void shim_jni_dump_natives(void);
      extern void *shim_jni_find_native(const char *, const char *);
      extern const char *shim_jni_native_sig(const char *, const char *);
      shim_jni_dump_natives();

      const char *UP = "com/unity3d/player/UnityPlayer";
      /* Ordem que a UnityPlayer.java roda o boot: */
      static const char *seq[] = {
        "nativePrepare", "nativeSetup", "nativePreInit", "nativeInit",
        "nativeAudioInit", "nativeVideoInit", "nativeStartup", "nativeRun",
        NULL
      };
      typedef void (*Native_void)(JNIEnv *, void *, ...);
      typedef void (*Native_int)(JNIEnv *, void *, int);
      for (int i = 0; seq[i]; i++) {
        void *fn = shim_jni_find_native(UP, seq[i]);
        if (!fn) continue;
        const char *sig = shim_jni_native_sig(UP, seq[i]);
        fprintf(stderr, "chamando %s (%s)\n", seq[i], sig ? sig : "?");
        if (sig && strstr(sig, "I"))       ((Native_int)fn)(env, NULL, 0);
        else                               ((Native_void)fn)(env, NULL);
      }
      /* Se nada rodou, o loop pode vir numa thread spawnada pelo OnLoad. */
      fprintf(stderr, ">> pos-JNI_OnLoad (aguardando render thread)\n");
      for (volatile int s = 0; s < 300; s++) { usleep(10000); } /* 3s de espera */
      return 0;
    }
    ANativeActivity_onCreate_fn oncreate =
        (ANativeActivity_onCreate_fn)dlsym(hu, "ANativeActivity_onCreate");
    if (!oncreate) {
      fprintf(stderr, "nem UnityMain nem JNI_OnLoad nem ANativeActivity_onCreate\n");
      fprintf(stderr, "isto indica build Unity 5.x/2017.x ou ABI mismatch\n");
      return 4;
    }
    fprintf(stderr, "usando ANativeActivity_onCreate (fallback)\n");
    oncreate(&act, NULL, 0);
    return 0;
  }

  fprintf(stderr, ">> UnityMain() - o controle passa para libunity.so\n");
  unityMain(&act, NULL, 0);
  /* UnityMain nunca retorna em builds Android; cai aqui so se crashar. */
  shim_input_stop(input_ctx);
  shim_gl_shutdown(&glc);
  return 0;
}
