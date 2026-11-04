# wrapper/

Shim nativo que faz `libunity.so` do ULTRADROID.apk carregar em Linux ARM
(SpruceOS) sem Android. Um shim completo levaria semanas. Isto aqui e o
esqueleto correto, com as assinaturas reais do NDK que o `libunity.so`
importa (verificado com `readelf -d lib/arm64-v8a/libunity.so`):

```
NEEDED: liblog.so  libandroid.so  libEGL.so  libGLESv2.so
        libOpenSLES.so  libmediandk.so  libvulkan.so  libdl.so
```

Nao podemos stubbar `libEGL`/`libGLESv2` — o jogo depende do driver PowerVR
real (GE8300) via essas libs. Nao precisamos stubbar `libvulkan` — Unity
2019.4 so o usa se o boot.config escolher Vulkan; aqui forçamos GLES.

O que stubamos:

| lib | o que fornecemos | efeito no jogo |
|-----|------------------|----------------|
| `liblog.so`        | `__android_log_print/write/vprint/assert` → `vfprintf(stderr)` | log visivel |
| `libandroid.so`    | `AAssetManager_*`, `ALooper_*`, `AMemory_*`, `AConfiguration_*` | Unity le `gamedata/` como se fosse APK assets |
| `libOpenSLES.so`   | **bridge real OpenSL ES → SDL2** (`slCreateEngine` + vtables de object/engine/play/bufferqueue/androidconfig + globais `SL_IID_*`) | **SOM**: o FMOD embutido no `libunity.so` faz `dlopen("libOpenSLES.so")`; sem vtable correta ele saltava em lixo (SIGBUS no `nativeResume`) |
| `libmediandk.so`   | `AMediaExtractor/Codec` no-op | cutscenes `.webm` nao tocam (aceitavel: UK-Death_MARK e o unico level que usa) |
| `libandroid` NativeActivity | `ANativeActivity_onCreate` — o bridge chama **para dentro** do `libunity.so`, nao vice-versa | |

> **Prova (nao chute)**: `python3 ../tools/dumpelf.py ../unity_payload/lib/arm64-v8a/libunity.so --undef`
> lista **zero** simbolo `sl*`/`AInputQueue*`, e a `.rodata` traz
> `libOpenSLES.so`, `slCreateEngine`, `SL_IID_ENGINE`, `SL_IID_PLAY`,
> `SL_IID_BUFFERQUEUE`, `SL_IID_ANDROIDSIMPLEBUFFERQUEUE`,
> `SL_IID_ANDROIDCONFIGURATION`, `SL_IID_RECORD`, `disableOpenSL` e
> `../android/src/fmod_output_opensl.cpp` → audio e carregado em runtime
> (`dlopen`/`dlsym`), e o input entra **somente** por
> `nativeInjectEvent(Landroid/view/InputEvent;)Z`.

## Arquivos

```
wrapper/
├── README.md                    <- este arquivo
├── build.sh                     <- wrapper em volta do cmake
├── CMakeLists.txt               <- constrói 4 .so + 1 ELF
├── bridge/
│   ├── bridge_main.c            <- ELF: dlopen libunity.so, chama UnityMain
│   ├── bridge_input.c           <- SDL event thread + MOUSE VIRTUAL + inject JNI
│   ├── bridge_input.h
│   └── bridge_unity.h           <- typedefs NDK/Unity minimais
├── include/
│   ├── android_shim.h           <- cabecalho compartilhado
│   └── sles_shim.h              <- ABI OpenSL ES 1.0.1 + extensoes Android (NOVO)
├── jni/
│   ├── bridge_jni.h             <- structs JNI vtable (ABI exato com jni.h NDK 21)
│   └── bridge_jni.c             <- mini-JVM: vtable COMPLETA + objetos
│                                   MotionEvent/KeyEvent virtuais (input)
├── gl/
│   ├── bridge_gl.h              <- SDL2+EGL: shim_gl_ctx
│   ├── bridge_gl.c              <- cria janela, EGL context GLES3
│   ├── shim_native_window.h     <- struct compartilhada bridge_gl<->egl_shim
│   ├── bridge_egl_shim.c        <- libEGL.so transparente (forwarder + intercepta
│   │                               eglCreateWindowSurface / eglDestroySurface)
│   ├── bridge_shader_stub.h     <- API de discovery + guard + hooks (R1-b passo 0)
│   └── bridge_shader_stub.c     <- probe .dynsym, SIGSEGV handler com backtrace,
│                                   registry de GOT-patches
├── tests/
│   ├── README.md
│   ├── input_probe.c            <- validacao standalone A/B/C/D de input
│   └── symbol_probe.c           <- inspeciona libunity.so: lista simbolos
│                                   .dynsym + scan de strings em .rodata
└── stubs/
    ├── android_log_stub.c       <- liblog
    ├── android_asset_stub.c     <- AAsset* (parte do libandroid)
    ├── android_nativeactivity_stub.c   <- ALooper/AMemory/AConfig (parte do libandroid)
    ├── android_input_stub.c     <- AInputQueue/AInputEvent/AMotionEvent/AKeyEvent
    ├── opensles_stub.c          <- OpenSL ES (no-op)
    └── mediandk_stub.c          <- Media NDK (no-op)
```

## Status atual

| Bloco | Estado | Observação |
|-------|--------|-----------|
| AAssetManager → filesystem | ✅ completo | `android_asset_stub.c` |
| ALooper / AConfiguration | ✅ completo | `android_nativeactivity_stub.c` |
| liblog → stderr | ✅ completo | `android_log_stub.c` |
| **OpenSL ES → SDL2 (som)** | ✅ **reescrito (rev -L)** | `stubs/opensles_stub.c` + `include/sles_shim.h`; antes era no-op SEM vtable (causa do SIGBUS no audio) |
| Media NDK no-op | ✅ completo | cutscenes ignoradas |
| JavaVM/JNIEnv | 🟡 esqueleto | vtable **completa** (rev -L); tabela `known_ints` ainda cresce conforme Player.log reclamar |
| **objetos MotionEvent/KeyEvent falsos** | ✅ novo (rev -L) | `shim_jni_make_event()` + dispatcher `ev_get_int/ev_get_float` em `bridge_jni.c` |
| SDL2 window + EGL context | ✅ completo | `gl/bridge_gl.c`; **reaproveita a window surface do SDL** (PowerVR) |
| **libEGL.so shim** | ✅ + SONAME corrigido | `gl/bridge_egl_shim.c`; devolve a surface de JANELA ao Unity em vez de pbuffer |
| **Input loop** | ✅ reescrito (rev -L) | SDL2 → `nativeInjectEvent` (JNI) **+ AInputQueue** (fallback) |
| **Mouse virtual** | ✅ novo (rev -L) | START+R1 liga/desliga, B = clique, stick = ponteiro; `bridge/bridge_input.c` |
| **Shader discovery + SIGSEGV guard** | ✅ | `gl/bridge_shader_stub.c` - enumera .dynsym, handler com backtrace + PC + dladdr, registry de hooks |
| **symbol_probe CLI** | ✅ | `tests/symbol_probe.c` - roda offline contra qualquer libunity.so |
| **dumpelf.py / ccheck.py** | ✅ novo | `../tools/` - inspecao de ELF e checagem de fontes sem compilador |
| GOT-patch real (substituir funcao) | ❌ pendente | `patch_got_for()` retorna NAO-IMPLEMENTADO |
| Re-export das Addressables p/ Android/GLES | ✅ **nao precisa** | o payload em uso e o build Android (`aa/Android/*.bundle` com ESSL/ETC2); o mito do DXBC valia so para o APK Windows antigo |

## Como o `bridge_egl_shim.c` fecha o gap gráfico

```
libunity.so (NEEDED: libEGL.so)
      │
      ▼
libs/arm64/libEGL.so            ← nosso shim (primeiro no LD_LIBRARY_PATH)
      │
      │ eglGetDisplay / eglInitialize / eglBindAPI / ... → dlsym + foward
      │
      ├─► eglCreateWindowSurface(dpy, cfg, win, attrs)
      │       │
      │       ├── win->magic == SHIM_ANW_MAGIC?  ← shim_native_window*
      │       │       └─ retorna w->egl_surface   (surface pré-criada
      │       │                                    pelo bridge_gl)
      │       │
      │       └── senao → repassa ao driver real
      │
      ├─► eglDestroySurface(dpy, surf)
      │       └─ surf == w->egl_surface? SKIP (protege a do bridge)
      │
      ▼
dlopen("libEGL.so.1")  [RTLD_LOCAL]
      │
      ▼
PowerVR DDK real no sistema (GE8300)
```

**Por que retornar a surface pré-criada em vez de chamar o real de novo?**
Porque `bridge_gl.c` já fez `eglMakeCurrent(dpy, surf, surf, ctx)` com
aquela surface. Se o shim criasse uma segunda, o contexto atual ficaria
apontando para a antiga e o primeiro `eglSwapBuffers` do Unity daria
`EGL_BAD_NATIVE_WINDOW` (a segunda superfície não está bound no context).
Retornar a mesma EGLSurface mantém tudo coerente.

**Por que o bridge_gl.c não cai no próprio shim?** Ele faz `dlopen` em
`libEGL.so.1` explicitamente (nunca `libEGL.so`). O shim instala-se com
o nome de arquivo exato `libEGL.so` — sem .1 — para casar com o `NEEDED`
do `libunity.so`. Os dois nomes nunca se encontram.

## Input: como o ULTRAKILL realmente recebe eventos (rev -L)

```
SDL_WaitEventTimeout (thread de input)
        |
        +-- botao (gamepad/teclado)
        |      |
        |      +-- combo START+R1?  -> TOGGLE mouse virtual (NENHUM dos 2 vai ao jogo)
        |      +-- mouse ON + B?    -> clique (touch down/up no ponteiro)
        |      +-- senao -> shim_jni_event (is_key=1) --> UnityPlayer.nativeInjectEvent(ev)
        |                            (mini-JVM responde getKeyCode/getAction/getDownTime/...)
        |      [e em paralelo empilha AInputQueue - fallback p/ NativeActivity]
        |
        +-- analogico
        |      +-- mouse ON + stick do ponteiro -> move ponteiro (+ touch MOVE se clicado)
        |      +-- senao -> shim_jni_event motion (source=JOYSTICK, axis[] completos)
        |                    --> nativeInjectEvent(ev); Unity le getAxisValue(AXIS_RX, 0) etc
        |
        +-- toque/mouse fisicos (devbox) -> motion TOUCHSCREEN direto
```

Os knobs (todos no `../ultrakill.conf`, lidos por `getenv` em
`bridge/bridge_input.c`): `ULTRAKILL_MOUSE`, `ULTRAKILL_MOUSE_TOGGLE`,
`ULTRAKILL_MOUSE_CLICK`, `ULTRAKILL_MOUSE_STICK`, `ULTRAKILL_MOUSE_SPEED`,
`ULTRAKILL_MOUSE_DEADZONE`, `ULTRAKILL_MOUSE_MODE`.

Por que nao `UnitySendTouchEvent`? Porque **esses simbolos nao existem** no
build Android do Unity 2019.4 (`.dynsym` so exporta `JNI_OnLoad`,
`UnitySendMessage`, `UnitySwappy_*`); e um `dlsym` de nome ausente numa lib
grande derrubava o loader desta glibc (SIGSEGV em `ld+0x9048`, visto no log).
`shim_input_start()` por isso nao tenta mais nenhum `UnitySend*`.

## Som: o caminho OpenSL ES -> SDL2 (rev -L)

```
libunity.so (FMOD estatico) -- dlopen --> libOpenSLES.so (nosso shim)
   slCreateEngine(&engine,...)          -> objeto com SLObjectItf_ de verdade
   (*engine)->GetInterface(SL_IID_ENGINE)
   (*engine)->CreateOutputMix / CreateAudioPlayer(source=ASBQ+PCM, sink=Mix)
   (*player)->GetInterface(SL_IID_PLAY | SL_IID_ANDROIDSIMPLEBUFFERQUEUE)
   (*bq)->RegisterCallback(cb, ctx)     -> guardamos cb
   (*bq)->Enqueue(buf, n)               -> SDL_QueueAudio(dev, buf, n)
   pump thread (4 ms):                   consumido = total_enfileirado
                                         - SDL_GetQueuedAudioSize(dev)
     para CADA buffer cujo fim cumulativo <= consumido -> cb(bq, ctx)
```

Sem dispositivo SDL (sem `/dev/snd`, permissao, SDL < 2.0.4) o shim entra em
modo **relogio**: consome bytes na taxa nominal, o mixer do FMOD continua
girando e o unico sintoma e o silencio — **nunca** trava o boot. Forcar esse
modo: `ULTRAKILL_AUDIO=0`.

ABI conferida contra `../audio_bridge/libOpenSLES.so` (o bridge de referencia
ja embarcado no port): `nm` mostra `slCreateEngine` + 7 globais `SL_IID_*` de
8 bytes — nosso shim exporta os mesmos nomes no mesmo formato, e compara IID
por identidade de ponteiro (aceitando tanto o valor do global quanto o endereco
dele, porque headers diferentes resolvem `SL_IID_x` de jeitos diferentes).

## Variáveis de ambiente úteis no shim

- `ULTRAKILL_REAL_LIBEGL=/abs/path/libEGL.so.1` — força um driver
  específico (útil se o DDK estiver fora do path padrão do ld.so).
- `ULTRAKILL_EGL_STATS=1` — no encerramento do processo, imprime quantos
  `eglSwapBuffers` passaram pelo shim. Diagnóstico de “o jogo chegou a
  renderizar?”. Sem isto o shim é silencioso sobre forwards.

## Próximo passo técnico (em ordem de prioridade)

1. Adicionar métodos missing na tabela `known_ints` conforme aparecem
   no log do shim. Unity 2019.4 tipicamente consulta: `Build$VERSION.SDK_INT`,
   `UnityPlayer.getConfiguration*`, `SwappyDisplayManager.getFrameInterval`.
   Cada um aparece em `~/.ultrakill/player.log` como
   `JVM-stub: CallStaticIntMethod nao mapeado: name=...`.
2. **Validar compilacao real no dev Linux**. Instrucoes em
   [`../README.md`](../README.md). Sem isso, erros de sintaxe/ABI ficam
   ocultos.
3. Re-export das Addressables (fora do wrapper; precisa de Unity editor).

## Build

```sh
# para o proprio TSP (aarch64 nativo):
cd wrapper
./build.sh arm64            # produz ../libs/arm64/*.so + ../gamedata/bin/ultrakill-arm64

# para cross-compile aarch64 num devbox com toolchain instalada:
./build.sh arm64 --toolchain /usr/bin/aarch64-linux-gnu-

# armv7 (fallback caso SpruceOS rode userspace 32-bit):
./build.sh armv7 --toolchain /usr/bin/arm-linux-gnueabihf-
```

## Como testar

Aps o build (todos os caminhos abaixo sao relativos a `wrapper/`):

1. `../tools/extract_payload.py ../../ULTRADROID.apk ../gamedata --only bundles`
   (ou rode `../bootstrap.sh` do root do port - ve
   [`../README.md`](../README.md)).
2. Confirme que `../gamedata/aa/Android/*.bundle` existem (nao
   `StandaloneWindows64`). Se nao existirem, o port nao roda — re-export
   as Addressables antes (ver abaixo).
3. Valide o wrapper standalone: `build-arm64/input_probe --headless --skip-real-loop`.
4. Rode `../ultrakill.sh` do diretorio do port. `Player.log` aparece em
   `~/.ultrakill/`.

## Limitacao estrutural (leia antes de codar)

Este shim **nao faz mágica**. Ele resolve o problema de linkage (o
`libunity.so` ARM64 nao encontra `liblog.so`/`libandroid.so` em SpruceOS),
mas o **payload** (`aa/StandaloneWindows64/*.bundle`) contem shaders
compilados para DXBC, que o backend GLES nao sabe instanciar. Sem um
re-export das Addressables para `Android` (GLSL/ESSL), o processo morre em
`Shader::CreateProgram()` — mesmo que os stubs estejam perfeitos.

Re-export exige:
- project files originais do Arkbox (nao disponiveis), **ou**
- AssetRipper/UABE p/ abrir os bundles StandaloneWindows64 + Unity 2019.4.40f1
  editor local + re-import + rebuild `Addressables` com `Build Target = Android`
  (GLES3, texturas ETC2, audio Vorbis) — trabalho de dias por bundle.

## Referencia de assinaturas

As signatures abaixo vem do Android NDK r21 (o que o Unity 2019.4 embarca).
Se você atualizar para NDK mais novo, **nao** troque as assinaturas: o ABI
precisa bater com o que o `libunity.so` foi compilado contra.

- `android/log.h`
- `android/asset_manager.h`, `android/looper.h`, `android/native_activity.h`
- `SLES/sLESAll.h`
- `media/NdkMediaExtractor.h`, `media/NdkMediaCodec.h`
