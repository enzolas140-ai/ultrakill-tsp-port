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
| `libOpenSLES.so`   | `slCreateEngine` e familia, todos no-op com `SL_RESULT_SUCCESS` | jogo abre **sem audio** (aceitavel p/ smoke-test) |
| `libmediandk.so`   | `AMediaExtractor/Codec` no-op | cutscenes `.webm` nao tocam (aceitavel: UK-Death_MARK e o unico level que usa) |
| `libandroid` NativeActivity | `ANativeActivity_onCreate` — o bridge chama **para dentro** do `libunity.so`, nao vice-versa | |

## Arquivos

```
wrapper/
├── README.md                    <- este arquivo
├── build.sh                     <- wrapper em volta do cmake
├── CMakeLists.txt               <- constrói 4 .so + 1 ELF
├── bridge/
│   ├── bridge_main.c            <- ELF: dlopen libunity.so, chama UnityMain
│   ├── bridge_input.c           <- SDL event thread (gamepad/teclado/mouse)
│   ├── bridge_input.h
│   └── bridge_unity.h           <- typedefs NDK/Unity minimais
├── include/
│   └── android_shim.h           <- cabecalho compartilhado
├── jni/
│   ├── bridge_jni.h             <- structs JNI vtable (ABI exato com jni.h NDK 21)
│   └── bridge_jni.c             <- mini-JVM: ~25 métodos implementados + tabela de valores
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
| OpenSL ES no-op | ✅ completo | áudio silencioso |
| Media NDK no-op | ✅ completo | cutscenes ignoradas |
| JavaVM/JNIEnv | 🟡 esqueleto | ~25 métodos; tabela `known_ints` precisa crescer conforme Player.log reclamar |
| SDL2 window + EGL context | ✅ completo | `gl/bridge_gl.c` |
| **libEGL.so shim** | ✅ | `gl/bridge_egl_shim.c` - 30+ forwarders + special cases |
| **Input loop** | ✅ | SDL2 gamepad/keyboard -> AInputQueue + UnitySend* dual path |
| **Shader discovery + SIGSEGV guard** | ✅ novo (R1-b passo 0) | `gl/bridge_shader_stub.c` - enumera .dynsym, handler com backtrace + PC + dladdr, registry de hooks |
| **symbol_probe CLI** | ✅ novo | `tests/symbol_probe.c` - roda offline contra qualquer libunity.so e revela se `Shader::CreateProgram` esta exportado ou nao |
| **GOT-patch real** (substituir funcao) | ❌ pendente | `patch_got_for()` retorna NAO-IMPLEMENTADO; preenchido apos ver o probe output real no device |
| Re-export das Addressables p/ Android/GLES | ❌ bloqueio real | se simbolo nao exportar nem via pattern-scan, este e o fallback ultimo |

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
