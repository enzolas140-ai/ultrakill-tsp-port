# tests/

Bateria de validação do wrapper que roda SEM o payload do jogo. Serve pra
isolar regressões de ABI, exports dos `.so` stub, e wiring entre `bridge_input.c`
e o `libandroid.so` em runtime.

## Arquivos

- **`input_probe.c`** — único `.c` aqui. Um ELF standalone que:
  - **(A)** usa `_Static_assert` no `offsetof(struct shim_native_window, magic)`
    e em `sizeof(AInputEvent)`. Falha no compile se alguém reordenar campos.
  - **(B)** `dlopen` os `.so` que vão no pacote (`libandroid.so`, `liblog.so`,
    `libEGL.so`) e checa `dlsym` para 23 símbolos que o Unity chama. Pega
    regressão de `visibility("default")` → `hidden`.
  - **(C)** injeta três eventos sintéticos (KEY, MOTION com 2 pointers,
    MOTION com axis de joystick) via `shim_input_queue_push`, drene pela
    API pública (`AInputQueue_getEvent` + `AKeyEvent_getKeyCode` +
    `AMotionEvent_getAxisValue`), confere byte-a-byte.
  - **(D)** spawn `shim_input_start` real com SDL2. Se a devbox tiver
    gamepad plugado, os botões aparecem logados durante `--dur N` segundos.
    Sem gamepad, o check D apenas prova que o loop roda sem crash.

## Rodar

### Na devbox (build de host, mais rápido)

```sh
cd ports/ultrakill/wrapper
cmake -S . -B build-host -DTARGET_ARCH=arm64 -DCMAKE_C_COMPILER=gcc
cmake --build build-host -j$(nproc)
./build-host/input_probe --headless --skip-real-loop
```

Nesta configuração A+B+C rodam, D é pulado. Serve pra CI.

### Cross-compile pro TSP (validação no device)

```sh
./build.sh arm64 --cross
scp build-arm64/input_probe libs/arm64/*.so root@tsp:/tmp/probe/
ssh root@tsp 'cd /tmp/probe && \
  LD_LIBRARY_PATH=. ./input_probe --libdir . --headless --dur 5'
```

Aperte A/B/start no gamepad do TSP durante os 5s; deve ver linhas
`KEY src=GAMEPAD action=DOWN keycode=97` no stdout.

### Com janela EGL completa (valida shim + input juntos)

Sem `--headless`:

```sh
LD_LIBRARY_PATH=../libs/arm64 \
ULTRAKILL_REAL_LIBEGL=/usr/lib/aarch64-linux-gnu/libEGL.so.1 \
./build-arm64/input_probe --dur 5
```

Se `shim_gl_init` falhar (sem display), cai automaticamente pra headless.

## Flags

```
--dur S              segundos de loop real (default 5)
--headless           nao abre janela SDL
--skip-real-loop     roda so A+B+C
--libdir DIR         prepend DIR em LD_LIBRARY_PATH
```

## Códigos de saída

| rc | Significado |
|----|-------------|
| 0  | todos os checks rodaram e passaram |
| 1  | pelo menos um check falhou (linhas `[X] FAIL ...` no stderr) |
| 2  | argumento inválido na CLI |

## O que NÃO é este teste

- Não valida se o `libunity.so` consegue dlopenar `libEGL.so` shim e rodar —
  isso exige o payload do jogo. O teste só checa exports.
- Não valida a `bridge_jni.c` (`known_ints`). Se quiser um `jni_probe.c`,
  a estrutura é análoga; falta motivo porque o Unity SIGSEGV-ava antes
  disso e a cobertura atual pega os 15 métodos que ele chama no boot.
- Não testa o `bridge_main.c` (que faz `UnityMain`); esse exige o `.so`
  real do jogo e cai no bloqueio DXBC.

## Integrar com CI

Exemplo para GitHub Actions num runner arm64 (Ubuntu 22.04):

```yaml
- name: build wrapper
  run: |
    sudo apt install -y libsdl2-dev libegl-dev pkg-config cmake
    cd ports/ultrakill/wrapper && cmake -S . -B build && cmake --build build -j
- name: input_probe (headless, sem gamepad)
  run: |
    ports/ultrakill/wrapper/build/input_probe \
      --headless --skip-real-loop --libdir ports/ultrakill/libs/arm64
```

Se rodar numa imagem x86_64, cross-compile e use `qemu-aarch64 -L /usr/aarch64-linux-gnu`.
