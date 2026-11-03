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
  /* stderr SEM buffer: sem isso, um SIGSEGV perde a ultima linha impressa e
   * ficamos sem saber em que passo caiu. */
  setvbuf(stderr, NULL, _IONBF, 0);
  /* Guarda de sinal o MAIS CEDO possivel - antes de qualquer dlopen/constructor
   * do libunity.so, senao o crash nos init_array nao tem backtrace. */
  shim_shader_install_guard();
  fprintf(stderr, "[main] passo 1: portdir\n");

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
  fprintf(stderr, "[main] passo C: monta ANativeActivity + FindClass\n");
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
  act.clazz             = (*env)->FindClass(env,
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
  fprintf(stderr, "[main] passo D: dlopen libunity (%s)\n", absli ? absli : "(LD_LIBRARY_PATH)");
  void *hu = try_dlopen(cands, absli ? 3 : 2, err, sizeof err);
  fprintf(stderr, "[main] passo D: dlopen retornou %p\n", hu);
  if (!hu) {
    fprintf(stderr, "dlopen libunity.so falhou: %s\n", err);
    fprintf(stderr, "LD_LIBRARY_PATH=%s\n", getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : "(unset)");
    fprintf(stderr, "você extraiu lib/arm64-v8a/libunity.so para %s?\n", gamedir);
    return 3;
  }

  /* ---- 5. resolve entrypoint ---- */
  /* Android libunity.so NAO exporta UnityMain (dynsym: so JNI_OnLoad,
   * JNI_OnUnload, UnitySendMessage, UnitySwappy_*). E neste glibc do
   * SpruceOS, dlsym() de nome AUSENTE numa lib grande derruba o loader
   * (SIGSEGV ld+0x9048, visto no log). Entao NAO tentamos UnityMain nem
   * ANativeActivity_onCreate; vamos direto no JNI_OnLoad que EXISTE. */
  UnityMain_fn unityMain = NULL;

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
      /* Mapeado pelo proprio JNI_OnLoad (log do device): nao ha
       * nativeSetup/nativeRun neste Unity 2019.4 Android. O que a
       * UnityPlayer.java faz apos registrar os natives e:
       *   initJni(context)  -> cria engine + main thread
       *   nativeResume()    -> reativa após pause
       *   loop: nativeRender()Z  (retorna !QuitRequested)
       *         + window focus etc.
       * Emulamos essa sequencia AQUI na thread principal (GL context ja
       * current nela; Unity cria o próprio EGLSurface via nosso shim). */
      typedef int (*Fn_Z_noargs)(JNIEnv *, void *);
      typedef void (*Fn_V_noargs)(JNIEnv *, void *);
      typedef void (*Fn_V_ctx)(JNIEnv *, void *, void *);
      typedef void (*Fn_V_bool)(JNIEnv *, void *, unsigned char);

      void *fnInit   = shim_jni_find_native(UP, "initJni");
      void *fnResume = shim_jni_find_native(UP, "nativeResume");
      void *fnRender = shim_jni_find_native(UP, "nativeRender");
      void *fnFocus  = shim_jni_find_native(UP, "nativeFocusChanged");
      void *fnDone   = shim_jni_find_native(UP, "nativeDone");
      void *fnLowMem = shim_jni_find_native(UP, "nativeLowMemory");
      (void)fnLowMem;

      /* Objeto-Contexto fake (ref do nosso pool; initJni so quer um
       * jobject pra cachar o Context). */
      void *ctx = (*env)->FindClass(env, "android/content/res/UnityPlayerActivity");

      if (fnInit)   { fprintf(stderr, "chamando initJni(ctx)\n");
                      ((Fn_V_ctx)fnInit)(env, NULL, ctx);
                      fprintf(stderr, "initJni voltou\n");
                      /* O initJni do Unity 2019.4 REGISTRA OS PROPRIOS
                       * handlers de sinal (crash reporter dele) e ROUBA o
                       * nosso guard - por isso o SIGSEGV do device caiu sem
                       * imprimir nada. Rearma o guard AGORA, depois que a
                       * engine ja instalou os dela, para termos PC/si_addr/
                       * dladdr do crash real no log do SD. */
                      shim_shader_install_guard();
                      fprintf(stderr, "[guard] rearmando apos initJni: ok\n"); }
      if (fnFocus && !getenv("ULTRAKILL_SKIP_FOCUS")) {
                      fprintf(stderr, "chamando nativeFocusChanged(1)\n");
                      ((Fn_V_bool)fnFocus)(env, NULL, 1);
                      fprintf(stderr, "focus voltou\n");
      } else if (fnFocus) {
                      fprintf(stderr, "nativeFocusChanged PULADO (ULTRAKILL_SKIP_FOCUS)\n");
      }
      if (fnResume) { fprintf(stderr, "chamando nativeResume()\n");
                      shim_shader_install_guard();
                      /* O device mostrou: SIGBUS DENTRO do nativeResume sem
                       * nenhuma linha de [guard] - o Unity repoe os handlers
                       * dele na thread de render logo ali. O watchdog
                       * re-escreve os nossos a cada 40ms, entao o proximo
                       * crash imprime pc/si_addr/modulo+offset (e o offset
                       * resolve offline no nosso libunity.so). */
                      shim_shader_start_guard_watchdog();
                      ((Fn_V_noargs)fnResume)(env, NULL);
                      fprintf(stderr, "resume voltou\n"); }

      if (fnRender) {
        fprintf(stderr, ">> loop nativeRender comecando\n");
        long frames = 0;
        while (frames < 600) {   /* ~10 min de teste; quit via nativeDone */
          int cont = ((Fn_Z_noargs)fnRender)(env, NULL);
          frames++;
          /* NO_SDL_GL (R36S): SDL nao viu GL, enta copiamos o pbuffer
           * pro KMS por conta propria a cada frame. */
          shim_gl_present(&glc);
          if (frames % 300 == 0)
            fprintf(stderr, "[render] frames=%ld cont=%d\n", frames, cont);
          if (!cont) { fprintf(stderr, "[render] QuitRequested -> sai\n"); break; }
          usleep(16000);   /* ~60fps; gamepad/teclado via input thread */
        }
        fprintf(stderr, "[render] loop encerrado apos %ld frames\n", frames);
      } else {
        fprintf(stderr, "AVISO: nativeRender nao registrado; esperando 30s\n");
        for (volatile int s = 0; s < 3000; s++) { usleep(10000); }
      }
      if (fnDone) { fprintf(stderr, "chamando nativeDone()\n"); ((Fn_Z_noargs)fnDone)(env, NULL); }
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
