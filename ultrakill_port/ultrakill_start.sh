#!/bin/bash
# ultrakill_start.sh - pre-flight + tuning + exec.
#
# Auto-configura o port na primeira execucao:
#   1. se gamedata/aa/ estiver vazio, extrai do ULTRADROID.apk bundlado
#   2. se libs/$ARCH/ nao tiver os .so, tenta compilar o wrapper no device
#   3. aplica tuning de CPU / memoria / GL
#   4. exec gamedata/bin/ultrakill (bridge)
#
# Premissas de hardware (conferidas no ULTRADROID.apk):
#   - Trimui Smart Pro: Allwinner A133P (4x Cortex-A53 @1.8GHz),
#     GPU Imagination PowerVR GE8300 @660MHz, 1GB LPDDR4x unificada,
#     display 1280x720. SpruceOS (base Debian/Armbian).
#   - driver GPU = PowerVR DDK proprietary. MESA_* env vars nao tem efeito.
#   - Os bundles do APK sao StandaloneWindows64 (DXBC). Um runtime GLES
#     NAO instancia DXBC - por isso o bridge morre em Shader::CreateProgram.
#     Este launcher prepara o terreno mas nao contorna o bloqueio.

set -u
PORTDIR="${OD:-$(cd "$(dirname "$0")" && pwd)}"
GD="$PORTDIR/gamedata"
LIBDIR="$PORTDIR/libs"
LOG="${ULTRAKILL_LOG:-$PORTDIR/ultrakill.log}"
STATEDIR="${TD:-$HOME/.ultrakill}"
mkdir -p "$STATEDIR"

# --------------------------------------------------------------- BANNER
# Imprime ANTES do exec > LOG redirect pra aparecer na tela do ES
# (ES mostra stdout do launcher ao vivo ate o jogo assumir a tela).
BANNER=$(cat <<'ART'

  /\  /\/ _ |\/|_  _/ _/ |_)| _|/_ _)/ _/ / _/_/_)
 /__\| (_| |  | (_| (_| | \|(_|| \|(_\ (_|(_| (_| |
                                    _\              |
   Trimui Smart Pro port - experimental

ART
)
printf '%s\n' "$BANNER"
printf '%s\n' "$BANNER" > /tmp/status_message.txt 2>/dev/null || true

current_phase=0
total_phases=5
phase() {
  current_phase=$((current_phase + 1))
  local msg="[$current_phase/$total_phases] $*"
  printf '%s\n' "$msg"
  printf 'ULTRAKILL - %s\n' "$msg" >> /tmp/status_message.txt 2>/dev/null || true
  # nao tentamos /dev/tty - SpruceOS nao expoe este device pros
  # processos do ES; em vez disso tentamos /dev/console em subshell
  # pra nao vazar erro pro log.
  (echo "$msg" > /dev/console) 2>/dev/null || true
}

exec > "$LOG" 2>&1

echo "== ultrakill_start.sh $(date -Is) =="
echo "portdir=$PORTDIR gd=$GD arch=$(uname -m) kernel=$(uname -r)"

status() {
  echo "$*" >> /tmp/status_message.txt 2>/dev/null || true
  (echo "$*" > /dev/console) 2>/dev/null || true
  echo "$*"
}

# ---------------------------------------------------------------- ARCH
ARCH="$(uname -m)"
case "$ARCH" in
  aarch64|arm64) LIBSUB=arm64 ;;
  armv7*|armhf)  LIBSUB=armv7 ;;
  *)             LIBSUB=arm64 ;;
esac
SHIMDIR="$LIBDIR/$LIBSUB"

# ---------------------------------------------------------------- 0a. PREFLIGHT: gamedata
phase "checando payload"
# Detecta se o payload ja foi extraido. Se nao, roda extract_payload.py
# contra o APK bundlado (ou um dos paths alternativos).
extract_needed=1
if [ -d "$GD/aa/StandaloneWindows64" ] || [ -d "$GD/aa/Android" ]; then
  extract_needed=0
fi

if [ "$extract_needed" = 1 ]; then
  APK=""
  for c in "$PORTDIR/ULTRADROID.apk" "$PORTDIR/../ULTRADROID.apk" \
           "$HOME/ULTRADROID.apk" "$STATEDIR/ULTRADROID.apk"; do
    [ -f "$c" ] && APK="$c" && break
  done
  if [ -z "$APK" ]; then
    status "ERRO: gamedata/ vazio E ULTRADROID.apk nao encontrado."
    echo "  colocou o APK em $PORTDIR/?" >&2
    exit 10
  fi
  phase "extraindo APK - bundles"
  if ! command -v python3 >/dev/null; then
    status "ERRO: python3 ausente"
    exit 11
  fi
  python3 "$PORTDIR/tools/extract_payload.py" "$APK" "$GD" --only bundles \
    || { status "extract bundles falhou"; exit 12; }
  python3 "$PORTDIR/tools/extract_payload.py" "$APK" "$GD" --only native \
    || status "extract native-arm falhou (continua; se libunity.so nao existir o bridge nao roda)"
  status "OK: payload extraido."
fi

# ---------------------------------------------------------------- 0c. PREFLIGHT: prepare_payload
# Rodes so o path fixup (nao precisa UnityPy). Se Android/ ja foi
# populado manualmente (com re-export), pula sem tocar.
if [ -d "$GD/aa/StandaloneWindows64" ] && [ -d "$PORTDIR/tools" ]; then
  if [ ! -e "$GD/aa/Android/" ]; then
    phase "preparando payload"
    python3 "$PORTDIR/tools/prepare_payload.py" --root "$GD" 2>&1 | tee -a "$LOG" || \
      status "prepare_payload reportou erro (continuando)"
  fi
fi

# ---------------------------------------------------------------- 0b. PREFLIGHT: libs
if [ ! -e "$SHIMDIR/libandroid.so" ] || [ ! -e "$SHIMDIR/libEGL.so" ]; then
  echo ">> stubs .so ausentes em $SHIMDIR"
  NEED_BUILD=1
else
  NEED_BUILD=0
fi
if [ "$NEED_BUILD" = 1 ]; then
  if command -v gcc >/dev/null && command -v cmake >/dev/null; then
    phase "compilando wrapper no device"
    # spinner em background enquanto cmake roda - senao a tela fica em
    # preto por 5-10 min
    (
      i=0
      while sleep 0.4; do
        i=$((i+1))
        sp=( '|' '/' '-' '\\' )
        printf 'ULTRAKILL - [2/5] compiland %s (%ds)\n' "${sp[$((i%4))]}" "$((i*40/100))" \
          > /tmp/status_message.txt 2>/dev/null || true
      done
    ) & SPIN=$!
    ( cd "$PORTDIR/wrapper" && ./build.sh "$LIBSUB" ) \
      || { kill $SPIN 2>/dev/null; status "build nativo FALHOU - ver log"; }
    kill $SPIN 2>/dev/null
    wait $SPIN 2>/dev/null || true
    status "build done."
  else
    phase "ERRO: toolchain ausente no device"
    {
      echo
      echo "==========================================="
      echo " ULTRAKILL nao pode compilar no device"
      echo "==========================================="
      echo
      echo "  O SpruceOS do TSP nao tem gcc/cmake/libsdl2-dev,"
      echo "  e portanto nao pode construir os stubs .so e o bridge"
      echo "  ELF localmente."
      echo
      echo "  >>> Caminhos para obter os binarios prontos:"
      echo
      echo "   (1) GitHub Actions (zero-setup local):"
      echo "       - faca upload da pasta ultrakill_port/ pra um repo"
      echo "       - va em Actions -> 'build wrapper (arm64)' -> Run"
      echo "       - baixe o artifact 'ultrakill-arm64-binaries'"
      echo "       - descompacte em cima de /roms/ports/ultrakill/"
      echo "       - re-execute este launcher - os .so vao estar la"
      echo
      echo "   (2) WSL2 Ubuntu na sua maquina Windows:"
      echo "       wsl --install -d Ubuntu"
      echo "       # dentro do WSL:"
      echo "       sudo apt install build-essential cmake pkg-config libsdl2-dev"
      echo "       cd /mnt/c/Users/User/Documents/port\\ de\\ ultrakill\\ linux/ultrakill_port"
      echo "       ./bootstrap.sh ./ULTRADROID.apk"
      echo "       # depois copie libs/arm64/*.so + gamedata/bin/ultrakill"
      echo "       # para o SD"
      echo
      echo "   (3) Docker Desktop com imagem multi-arch:"
      echo "       docker run --rm -v \$PWD:/w -w /w/ultrakill_port/wrapper "
      echo "         --platform linux/arm64 ubuntu:24.04 "
      echo "         bash -c 'apt update && apt install -y build-essential "
      echo "         cmake pkg-config libsdl2-dev && ./build.sh arm64'"
      echo
      echo "==========================================="
      echo
    } | tee -a /tmp/status_message.txt >&2
    exit 13
  fi
fi

# ---------------------------------------------------------------- 1. CPU governor
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
  [ -w "$g" ] && echo performance > "$g" 2>/dev/null
done
POL=$(ls -d /sys/devices/system/cpu/cpufreq/policy* 2>/dev/null | head -1)
if [ -n "$POL" ]; then
  TOPFREQ=$(tr ' ' '\n' < "$POL/scaling_available_frequencies" 2>/dev/null \
            | grep -E '^[0-9]+$' | sort -n | tail -1)
  [ -n "$TOPFREQ" ] && echo "$TOPFREQ" > "$POL/scaling_max_freq" 2>/dev/null
  echo "cpu: performance, max=${TOPFREQ:-?} kHz"
fi

# ---------------------------------------------------------------- 2. MEMORIA
if [ -w /proc/sys/vm/swappiness ]; then
  echo 180 > /proc/sys/vm/swappiness
  [ -w /proc/sys/vm/vfs_cache_pressure ] && echo 500 > /proc/sys/vm/vfs_cache_pressure
  sync
  echo 3 > /proc/sys/vm/drop_caches
fi
for d in /sys/block/zram*; do
  [ -e "$d/comp_algorithm" ] && echo "zram $(basename "$d"): $(cat "$d/comp_algorithm") size=$(cat "$d/disksize")"
done
if [ -f /sys/fs/cgroup/cgroup.controllers ]; then
  CG=/sys/fs/cgroup/ultrakill
  [ -d "$CG" ] || mkdir -p "$CG" 2>/dev/null
  if [ -d "$CG" ]; then
    echo $((830*1024*1024)) > "$CG/memory.max"      2>/dev/null
    echo $((128*1024*1024)) > "$CG/memory.swap.max" 2>/dev/null
    echo $$ > "$CG/cgroup.procs" 2>/dev/null \
      && echo "cgroup: ultrakill (memory.max=830M)"
  fi
fi

# ---------------------------------------------------------------- 3. GPU/GL env
export PVRSRVHINTS="${PVRSRVHINTS:-CompressTextures=3}"
export UNITY_GFX_TECHNOLOGY="${UNITY_GFX_TECHNOLOGY:-OpenGLTypesOpenGLES2,OpenGLTypesOpenGLES3}"

# ---------------------------------------------------------------- 4. BOOT.CONFIG
mkdir -p "$GD/Data"
cat > "$GD/Data/boot.config" <<'CFG'
wait-for-native-debugger=0
hdr-display-enabled=0
gc-max-time-slice=3
gfx-enable-gfx-jobs=0
gfx-enable-native-gfx-jobs=0
gfx-threaded-processing=0
androidStartInFullscreen=1
androidRenderOutsideSafeArea=0
disable-gpu-preloading=1
CFG

# ---------------------------------------------------------------- 5. RESOLUCAO
INT_W=${INT_W:-480}
INT_H=${INT_H:-270}

# ---------------------------------------------------------------- 6. LD_LIBRARY_PATH
# Ordem: stubs do wrapper primeiro, depois libs nativos extraidos do
# APK (para libunity.so + libil2cpp.so), depois o resto do sistema.
if [ -d "$SHIMDIR" ]; then
  export LD_LIBRARY_PATH="$SHIMDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  echo "shim dir: $SHIMDIR"
fi
# Forca nosso shim libEGL.so.1 a ser o PRIMEIRO EGL carregado no processo.
# Sem isso, SDL2 pode dlopen('libEGL.so.1') e acabar pegando /usr/lib/libEGL.so.1
# (driver real, sem EXT_device_enumeration) em vez do nosso shim.
if [ ! -f "$SHIMDIR/libEGL.so.1" ] && [ -f "$SHIMDIR/libEGL.so" ]; then
  # SD pode nao ter a versao .1 - cria agora (cp, nao symlink, p/ evitar
  # problemas em FAT32/exFAT que nao suportam symlink)
  cp "$SHIMDIR/libEGL.so" "$SHIMDIR/libEGL.so.1" 2>/dev/null || true
fi
if [ -f "$SHIMDIR/libEGL.so.1" ]; then
  export LD_PRELOAD="$SHIMDIR/libEGL.so.1${LD_PRELOAD:+:$LD_PRELOAD}"
  echo "LD_PRELOAD: $SHIMDIR/libEGL.so.1"
else
  echo "AVISO: libEGL.so.1 nao encontrado, nao da pra LD_PRELOAD - SDL pode"
  echo "       pegar o driver real sem EXT_device_enumeration"
fi
# Native Android .so extraidos do APK:
for d in "$GD/lib/arm64-v8a" "$GD/lib/armeabi-v7a" "$GD/lib/$LIBSUB"; do
  if [ -d "$d" ]; then
    export LD_LIBRARY_PATH="$d${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    echo "native dir: $d"
  fi
done

# ------------------------------------------------------- 6b. Aliases Android->glibc
# libunity.so / libil2cpp.so (Bionic) tem DT_NEEDED libc.so, libm.so,
# libdl.so, libz.so (SEM versao). No glibc do SpruceOS so existem
# libc.so.6 / libm.so.6 / libdl.so.2 / libz.so.1. Sem o alias, dlopen
# falha com "libm.so: cannot open shared object".
# Copiamos o arquivo REAL do proprio device para o nome Android. O loader
# deduplica via DT_SONAME (ex: libc.so copiado tem SONAME=libc.so.6, ja
# carregado pelo processo) => nenhum simbolo duplicado, nenhum risco.
android_alias() {
  want="$1"; shift
  [ -e "$SHIMDIR/$want" ] && return 0
  for real in "$@"; do
    if [ -e "$real" ]; then
      if cp "$real" "$SHIMDIR/$want" 2>/dev/null; then
        echo "alias: $real -> $SHIMDIR/$want"
        return 0
      fi
    fi
  done
  echo "AVISO: nenhuma lib real encontrada para alias $want (esperados: $@)"
  return 1
}
GLIBC_GLOBS="/lib /usr/lib /usr/lib64 /lib/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu"
for w in libc libm libdl; do
  for g in $GLIBC_GLOBS; do
    cand="$g/$w.so.6"
    [ -e "$cand" ] || cand="$g/$w.so.2"
    if [ -e "$cand" ]; then
      [ -e "$SHIMDIR/$w.so" ] || cp "$cand" "$SHIMDIR/$w.so" 2>/dev/null && \
        [ -e "$SHIMDIR/$w.so" ] && echo "alias: $cand -> $SHIMDIR/$w.so"
      break
    fi
  done
done
android_alias libz.so /usr/lib/libz.so.1 /lib/libz.so.1 /usr/lib/aarch64-linux-gnu/libz.so.1
# GLESv1_CM (alguns builds do Unity pedem):
android_alias libGLESv1_CM.so /usr/lib/libGLESv1_CM.so /usr/lib/libGLESv1_CM.so.1

# ------------------------------------------------------------------ 6a. Diagnosticos libSDL2
# Nao alteramos mais LD_LIBRARY_PATH aqui - ja embarcamos nosso proprio
# libSDL2.so (copia do portmaster) em $SHIMDIR. So lista o que existe p/
# diagnostico.
{
  if [ -f "$SHIMDIR/libSDL2-2.0.so.0" ]; then
    drv=$(strings "$SHIMDIR/libSDL2-2.0.so.0" 2>/dev/null | grep -E '^(x11|wayland|kmsdrm|fbcon|directfb|offscreen|dummy)$' | sort -u | tr '\n' ' ')
    echo "libSDL2 embarcada no port ($SHIMDIR): drivers=[$drv]"
  else
    echo "AVISO: libSDL2 embarcada NAO encontrada em $SHIMDIR - bridge pode falhar"
  fi
} 2>&1 | tee -a "$LOG"

# Persistencia
export ULTRAKILL_PERSIST="$STATEDIR"

# ---------------------------------------------------------------- 6b. DESCOBRE DRIVER EGL REAL
# SpruceOS nao usa os paths padrao do Debian para o DDK PowerVR. Varre
# /usr/lib e subpasta, escolhe o candidato mais provavel, exporta em
# ULTRAKILL_REAL_LIBEGL (o bridge_egl_shim.c pe esse env var primeiro).
{
  echo "== procurando driver EGL real no sistema =="
  found=""
  for pat in \
      /usr/lib/libEGL.so.1 /usr/lib/libEGL.so \
      /usr/lib/aarch64-linux-gnu/libEGL.so.1 \
      /usr/lib/arm-linux-gnueabihf/libEGL.so.1 \
      /usr/lib/egl/libEGL.so.1 /usr/lib/egl/libEGL.so \
      /usr/lib/egl/libEGL*POWER* /usr/lib/egl/libEGL*Mali* \
      /usr/lib/*/libEGL.so.1 /usr/lib/*/libEGL.so \
      /usr/local/lib/libEGL.so.1 /opt/*/libEGL.so.1 \
      /usr/lib/libEGL.pvr* /usr/lib/libEGLIMG* \
      /usr/lib/hwdefault*/*/libEGL.so*; do
    for f in $pat; do
      [ -f "$f" ] || continue
      # ignora NOSSO shim (esta em $SHIMDIR)
      case "$f" in "$SHIMDIR"/*) continue ;; esac
      echo "  achou: $f"
      [ -z "$found" ] && found="$f"
    done
  done
  if [ -n "$found" ]; then
    echo "  usando como ULTRAKILL_REAL_LIBEGL: $found"
    export ULTRAKILL_REAL_LIBEGL="$found"
  else
    echo "  NENHUM driver EGL real encontrado! shim vai falhar em abrir"
  fi
  # GLESv2 tambem:
  for pat in /usr/lib/*/libGLESv2.so* /usr/lib/libGLESv2.so* \
             /usr/lib/egl/libGLES* /usr/lib/libIMG*GLES* /usr/lib/hwdefault*/*/libGLESv2*; do
    for f in $pat; do
      [ -f "$f" ] || continue
      echo "  GLESv2: $f"
      export ULTRAKILL_REAL_LIBGLESV2="$f"
      break 2
    done
  done
} 2>&1 | tee -a "$LOG"

# Detecta SDL_VIDEODRIVER. Em vez de chutar, lista o que o SDL2 do
# SpruceOS realmente suporta (via strings na lib) e testa em ordem.
{
  # procura TODAS as libSDL2.so no sistema (SpruceOS pode ter mais de uma)
  SDL_LIBS=$(find /usr/lib /lib /opt /userdata /mnt -maxdepth 4 \
             \( -name 'libSDL2-2.0.so*' -o -name 'libSDL2.so*' \) 2>/dev/null | sort -u)
  echo "== libSDL2 encontradas: =="
  for L in $SDL_LIBS; do
    echo "  $L"
    if command -v strings >/dev/null; then
      # VIDEODRIVERS validos do SDL2
      drv=$(strings "$L" 2>/dev/null | grep -E '^(x11|wayland|wayland-egl|kmsdrm|fbcon|directfb|offscreen|dummy|rpi|android|uikit|cocoa|windows|ps2|psp|vita|emscripten|netbsdconsol|qnx|riscos)$' | sort -u | tr '\n' ' ')
      echo "    drivers: $drv"
    fi
  done
} 2>&1 | tee -a "$LOG"

# Ordem: primeiro o que o proprio SDL escolhe (vazio), depois
# explicitos. Se DISPLAY/WAYLAND_DISPLAY ja estao no env (ES
# exportando p/ port), tenta-os primeiro. Se socket wayland existir,
# forca WAYLAND_DISPLAY.
cand=""
if [ -n "${DISPLAY:-}" ]; then cand="$cand x11"; fi
if [ -n "${WAYLAND_DISPLAY:-}" ]; then cand="$cand wayland"; fi
# detecta socket wayland sem depender da env var
for w in /run/wayland-* /tmp/wayland-* /run/user/$(id -u)/wayland-*; do
  if [ -S "$w" ]; then
    export WAYLAND_DISPLAY=$(basename "$w")
    export XDG_RUNTIME_DIR=$(dirname "$w")
    echo "socket wayland achado: $w -> WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
    cand="$cand wayland"
    break
  fi
done
if [ -e /dev/dri/card0 ]; then cand="$cand kmsdrm"; fi
if [ -e /dev/fb0 ]; then cand="$cand fbcon"; fi
# wayland SEMPRE no fim tbem, na falta de melhor
if ! echo "$cand" | grep -q ' wayland '; then cand="$cand wayland"; fi
cand="$cand offscreen dummy"
CANDIDATES="$cand"
echo "candidatos SDL_VIDEODRIVER a testar: $CANDIDATES"
echo "env: WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>}  DISPLAY=${DISPLAY:-<unset>}  XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-<unset>}"

# ---------------------------------------------------------------- 7. EXEC
BIN=""
for c in "$GD/bin/ultrakill-arm64" "$GD/bin/ultrakill-arm" "$GD/bin/ultrakill"; do
  [ -x "$c" ] && BIN="$c" && break
done

phase "iniciando bridge"

if [ -z "$BIN" ]; then
  cat <<HELP
NAO HAH binario nativo em $GD/bin/.

O pre-flight extract/build rodou mas o bridge ELF ainda nao esta la.
Causas possiveis:
  * wrapper/build.sh nao linkou o alvo 'bridge' (cheque log de build)
  * ARCH_TAG ($LIBSUB) nao bate com o target buildado
  * o APK foi copiado mas a build nativa falhou (SDL2 dev nao instalado)

Reesecute manualmente:
  cd $PORTDIR/wrapper && ./build.sh $LIBSUB
HELP
  exit 14
fi

echo ">> exec: $BIN (${INT_W}x${INT_H})"
chmod +x "$BIN" 2>/dev/null || true
RUN_BIN="$BIN"
if ! test -x "$BIN" 2>/dev/null; then
  echo ">> $BIN sem +x, tentando copia p/ /tmp (SD noexec?)"
  cp "$BIN" /tmp/ultrakill 2>/dev/null && chmod +x /tmp/ultrakill && RUN_BIN=/tmp/ultrakill
fi
for so in "$SHIMDIR"/lib*.so; do chmod +x "$so" 2>/dev/null; done

# Tenta cada SDL_VIDEODRIVER em ordem. O bridge retorna 6 quando
# shim_gl_init falhou; tenta proximo. Qualquer outro exit code
# (0, SIGSEGV, SIGABRT, 14, ...) para o loop.
RC=6
for drv in $CANDIDATES; do
  echo ">> tentando SDL_VIDEODRIVER=$drv" | tee -a "$LOG"
  export SDL_VIDEODRIVER="$drv"
  if command -v taskset >/dev/null 2>&1; then
    taskset -c 0-3 "$RUN_BIN" \
      -screen-width "$INT_W" -screen-height "$INT_H" -screen-fullscreen 1 \
      -logFile "$STATEDIR/player.log"
  else
    "$RUN_BIN" \
      -screen-width "$INT_W" -screen-height "$INT_H" -screen-fullscreen 1 \
      -logFile "$STATEDIR/player.log"
  fi
  RC=$?
  if [ $RC -ne 6 ] && [ $RC -ne 1 ]; then
    echo ">> driver $drv deu exit $RC (nao e mais problema de video, para loop)" | tee -a "$LOG"
    break
  fi
  echo ">> driver $drv falhou (exit $RC), proximo..." | tee -a "$LOG"
done
echo "== final exit $RC, driver usado: $SDL_VIDEODRIVER $(date -Is) ==" | tee -a "$LOG"
[ "$RUN_BIN" != "$BIN" ] && rm -f /tmp/ultrakill

# ---------------------------------------------------------------- 8. RESTAURA
[ -w /proc/sys/vm/swappiness ] && echo 60 > /proc/sys/vm/swappiness
[ -w /proc/sys/vm/vfs_cache_pressure ] && echo 100 > /proc/sys/vm/vfs_cache_pressure
exit $RC
