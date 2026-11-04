#!/bin/bash
# ultrakill_start.sh - pre-flight + tuning + exec.
#
# Auto-configura o port na primeira execucao:
#   1. se gamedata/aa/ estiver vazio, extrai do ULTRADROID.apk bundlado
#   2. se libs/$ARCH/ nao tiver os .so, tenta compilar o wrapper no device
#   3. aplica tuning de CPU / memoria / GL
#   4. exec gamedata/bin/ultrakill (bridge)
#
# CONSOLE ALVO (2026-11-03 em diante): TRIMUI SMART PRO (TSP)
#   Allwinner A133P (4x Cortex-A53, userspace aarch64), GPU PowerVR GE8300
#   (DDK Rogue), painel 1280x720, 1 GB RAM, SEM touch screen e SEM mouse.
#   O port e o wrapper (wrapper/*.c) foram escritos/revistos para este box:
#   loader 'bridge' + EGL do SISTEMA (PowerVR) + SDL2 window real + som via
#   libOpenSLES.so (bridge OpenSL->SDL2) + MOUSE VIRTUAL (START+R1 liga,
#   B clica, analogico direito move)
#
# Hardware antigo (R36S / ArkOS4Clone, RK3326 + Mali-G31) continua suportado
# pelo caminho 'mali' + loader BENDER, mas agora DETECTADO em runtime e nao
# mais assumido. Ver bloco "DETECTA CONSOLE".

set -u
PORTDIR="${OD:-$(cd "$(dirname "$0")" && pwd)}"
GD="$PORTDIR/gamedata"
# Dois layouts sao aceitos, na ordem de preferencia:
#   (A) unity_payload/assets/{aa,bin/Data,Videos} + unity_payload/lib/arm64-v8a
#       = layout dos ports BENDER que FUNCIONAM nesta firmware (60 Parsecs,
#       Cuphead). O bridge legado aponta pro mesmo lugar via
#       ULTRAKILL_ASSET_ROOT.
#   (B) gamedata/{aa,Data,lib}  = layout historico deste port.
if [ ! -d "$GD/aa" ] && [ -d "$PORTDIR/unity_payload/assets/aa" ]; then
  GD_AA="$PORTDIR/unity_payload/assets/aa"
  GD_DATA="$PORTDIR/unity_payload/assets/bin/Data"
  GD_LIB="$PORTDIR/unity_payload/lib"
  export ULTRAKILL_ASSET_ROOT="$PORTDIR/unity_payload/assets"
  echo "layout A (unity_payload) ativo"
else
  GD_AA="$GD/aa"; GD_DATA="$GD/Data"; GD_LIB="$GD/lib"
fi
LIBDIR="$PORTDIR/libs"
LOG="${ULTRAKILL_LOG:-$PORTDIR/ultrakill.log}"
# Persistencia DENTRO do port (receita do Cuphead: HOME/XDG no diretorio do
# port). Na caixa o HOME da ES e o rootfs - que e pequeno e e gravado fora em
# firmware update. TD=<outro dir> continua mandando.
STATEDIR="${TD:-$PORTDIR/home}"
mkdir -p "$STATEDIR" "$STATEDIR/.config" "$STATEDIR/.local/share" 2>/dev/null
export HOME="$STATEDIR"
export XDG_CONFIG_HOME="$STATEDIR/.config"
export XDG_DATA_HOME="$STATEDIR/.local/share"
export XDG_CACHE_HOME="$PORTDIR/data/cache"

# --------------------------------------------------------------- BANNER
# Imprime ANTES do exec > LOG redirect pra aparecer na tela do ES
# (ES mostra stdout do launcher ao vivo ate o jogo assumir a tela).
BANNER=$(cat <<'ART'

  /\  /\/ _ |\/|_  _/ _/ |_)| _|/_ _)/ _/ / _/_/_)
 /__\| (_| |  | (_| (_| | \|(_|| \|(_\ (_|(_| (_| |
                                    _\              |
   TRIMUI SMART PRO / SpruceOS (PowerVR GE8300) - experimental

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

exec 3>&1
# Rotaciona o log do boot anterior ANTES de sobrescrever: assim, quando o port
# morre e a tela volta ao ES, ainda ha DOIS logs no SD (o deste boot e o
# anterior) para comparar. Sem isso o usuario perdia a evidencia.
if [ -f "$LOG" ] && [ -s "$LOG" ]; then
  mv -f "$LOG" "$LOG.prev" 2>/dev/null || true
fi
if [ -f "$PORTDIR/player.log" ] && [ -s "$PORTDIR/player.log" ]; then
  mv -f "$PORTDIR/player.log" "$PORTDIR/player.log.prev" 2>/dev/null || true
fi
exec > "$LOG" 2>&1

echo "== ultrakill_start.sh $(date -Is) =="
echo ">> LAUNCHER_REV 2026-11-03-L (revisao TSP: backend pvr detectado em runtime, audio OpenSL->SDL2, mouse virtual START+R1, entrada real por nativeInjectEvent, log rotacionado + resumo)"
echo "   se voce NAO ve esta linha num log novo, o ultrakill_start.bin do cartao"
echo "   ainda e o ANTIGO - ressincronize a pasta e ejete bem o cartao."
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

# ---------------------------------------------------------------- DETECTA CONSOLE
# O port ja foi publicado apontando para o HARDWARE ERRADO (R36S/Mali). Aqui a
# caixa e decidida em runtime, pelo que o device realmente expoe - e nao por
# chute. GPU de fato:
#   Mali-G31  -> /dev/mali0 existe (kernel mali_kbase). R36S/ArkOS4Clone.
#   PowerVR   -> /dev/mali0 NAO existe e ha driver pvrsrvkm / libs do DDK IMG.
#                TrimUI Smart Pro (A133P) / SpruceOS.
BOX=""; GPU=""
if [ -e /dev/mali0 ]; then
  BOX="r36s"; GPU="mali"
elif lsmod 2>/dev/null | grep -qiE 'pvrsrvkm|pvr_android|ankerux' \
  || [ -d /sys/module/pvrsrvkm ] || [ -e /sys/kernel/debug/pvrsrvkm ] \
  || ls /usr/lib/*/libEGL* 2>/dev/null | grep -qiE 'powervr|pvr|img' \
  || grep -qiE 'powervr|rogue|ge8300' /proc/cpuinfo /sys/class/devfreq/*/of_node/name 2>/dev/null; then
  BOX="tsp"; GPU="pvr"
else
  # sem /dev/mali0 e sem marca PowerVR: TSP mesmo assim (e o console alvo do
  # port) porque o caminho 'pvr' so usa o libEGL do SISTEMA, que funciona em
  # qualquer box com EGL real; o caminho 'mali' e que exige blob extra.
  BOX="tsp"; GPU="pvr"
fi
# Painel real (para os defaults de resolucao), sem quebrar se nao houver.
PANEL=$( (cat /sys/class/drm/card0-*/modes 2>/dev/null || cat /sys/class/drm/card0/modes 2>/dev/null || true) | head -1)
case "$PANEL" in
  1280*) PANEL_W=1280; PANEL_H=720 ;;
  854*)  PANEL_W=854;  PANEL_H=480  ;;
  640*)  PANEL_W=640;  PANEL_H=480  ;;
  *)     PANEL_W=1280; PANEL_H=720  ;;
esac
if [ "$BOX" = "tsp" ]; then
  DEF_INT_W=$(( PANEL_W / 2 )); DEF_INT_H=$(( PANEL_H / 2 ))
else
  DEF_INT_W=480; DEF_INT_H=360
fi
echo ">> CONSOLE detectado: BOX=$BOX GPU=$GPU painel=${PANEL:-?} ($PANEL_W${PANEL_H:+x}$PANEL_H) -> interno default ${DEF_INT_W}x${DEF_INT_H}"
# Permite forcar no conf (ULTRAKILL_BOX=tsp|r36s) se a deteccao errar.
if [ -n "${ULTRAKILL_BOX:-}" ]; then
  BOX="$ULTRAKILL_BOX"
  case "$BOX" in r36s) GPU=mali ;; *) GPU=pvr ;; esac
  echo ">> BOX forcado por ultrakill.conf: $BOX (gpu=$GPU)"
fi

# ultrakill.conf (opcional, na raiz do port): tune sem recompilar nada.
# Ex.: echo 'INT_W=400' > /roms/ports/ultrakill/ultrakill.conf
# (movido para BEFORE das sondas na rev -L: senao knobs como ULTRAKILL_TARGET_FPS,
#  SDL_VIDEODRIVER_TSP ou ULTRAKILL_BOX eram lidos depois de ja terem sido usados.)
if [ -f "$PORTDIR/ultrakill.conf" ]; then
  echo ">> lendo $PORTDIR/ultrakill.conf"
  # set -a: o que o conf define sai exportado (senao ULTRAKILL_NO_FB etc.
  # ficariam variaveis de shell e o bridge nunca veria via getenv)
  # Sanitiza CRLF: se o conf foi salvo no Windows (\r\n), o '. source' engole o
  # \r e os valores viram "mali\r", "1\r" etc. - isso DESATIVAVA todo o backend
  # escolhido. Sempre sourceamos de uma copia \r-free.
  _UKC="$(mktemp 2>/dev/null || echo "/tmp/ultrakill.conf.$$")"
  if tr -d '\r' < "$PORTDIR/ultrakill.conf" > "$_UKC" 2>/dev/null; then
    set -a; . "$_UKC"; set +a
  else
    set -a; . "$PORTDIR/ultrakill.conf"; set +a
  fi
  rm -f "$_UKC" 2>/dev/null || true
fi

# ---------------------------------------------------------------- 0d. RECEITA SPRUCEOS/TSP
# Engenharia reversa dos ports QUE RODAM nesta caixa (Cuphead R24, GTA SA,
# McpeLauncher, Dead Space). Fatos comprovados, lidos nos launchers deles:
#  * O SDL2 do firmware do spruceOS conduz o painel pelo backend **"mali"** - e e
#    ele que os ports usam; o "kmsdrm" da R36S FALHA aqui com "kmsdrm not
#    available" / "Can't window GBM/EGL surfaces" porque o frontend (ES) segura
#    DRM master. (GTA SA.sh:51-58 diz exatamente isso: spruceOS/TSP -> mali.)
#  * O runtime do firmware mora em /mnt/SDCARD/spruce/flip[/lib] e
#    /usr/trimui/lib; la estao os sonames libEGL.so/libGLESv2.so/libSDL2
#    (Dead Space.sh comenta que sem o flip o dlopen nao acha nada).
#  * Audio: SDL_AUDIODRIVER=alsa (+ ALSA_* do runtime).
#  * Pad: o pad virtual do trimui_inputd traz ids de Xbox 360 mas esta ligado
#    no estilo Nintendo (BTN_SOUTH=304 e o botao marcado B). Sem a linha certa
#    no gamecontrollerdb, A e B ficam trocados (Cuphead: 'botoes
#    desconfigurados').
#  * Cuphead ancora o display com flock /tmp/ports-android-display.lock, tira o
#    console do vtcon, trava CPU/devfreq e restaura ao sair.
# Nada aqui depende de recompilar: e so ambiente do loader.
if [ -d /mnt/SDCARD/spruce ] || [ -d /usr/trimui/lib ] || [ -e /mnt/SDCARD/spruce/flip/lib/libEGL.so ]; then
  SPRUCE=1
else
  SPRUCE=0
fi
if [ "$SPRUCE" = 1 ]; then
  echo ">> spruceOS detectado: aplicando receita dos ports que rodam (SDL_VIDEODRIVER=mali + flip/lib + /usr/trimui/lib)"
  FLIP=""
  for d in /mnt/SDCARD/spruce/flip/lib /mnt/SDCARD/spruce/flip /usr/trimui/lib /usr/trimui/lib/egl; do
    [ -d "$d" ] && FLIP="$FLIP:$d"
  done
  export LD_LIBRARY_PATH="${FLIP#:}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export SDL_VIDEODRIVER="${SDL_VIDEODRIVER_TSP:-mali}"
  export SDL_VIDEO_EGL_DRIVER="${SDL_VIDEO_EGL_DRIVER:-libEGL.so}"
  export SDL_VIDEO_GL_DRIVER="${SDL_VIDEO_GL_DRIVER:-libGLESv2.so}"
  export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-alsa}"
  export ALSOFT_DRIVERS="${ALSOFT_DRIVERS:-alsa}"
  export MALLOC_CHECK_=0
  # O probe fecha a lib SDL2 por si: aponta para a do firmware para ele ganhar
  # o backend 'mali' (senao ele usa a SDL bundled/sem backend de janela).
  for s in /usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0 /usr/trimui/lib/libSDL2-2.0.so.0 \
           /usr/lib/libSDL2-2.0.so.0 /mnt/SDCARD/spruce/flip/lib/libSDL2-2.0.so.0; do
    if [ -f "$s" ]; then export BENDER_SDL_LIBRARY="$s"; echo "   BENDER_SDL_LIBRARY=$s"; break; fi
  done
  # EGL/GLES 'real' para o shim do bridge e para o bridge_gl (procura no flip
  # primeiro - os sonames de la que o DDK PowerVR do TSP aceita).
  if [ -z "${ULTRAKILL_REAL_LIBEGL:-}" ]; then
    for e in /mnt/SDCARD/spruce/flip/lib/libEGL.so /mnt/SDCARD/spruce/flip/lib/libEGL.so.1 \
             /usr/trimui/lib/libEGL.so /usr/lib/aarch64-linux-gnu/libEGL.so.1 /usr/lib/libEGL.so.1; do
      [ -f "$e" ] && export ULTRAKILL_REAL_LIBEGL="$e" && echo "   ULTRAKILL_REAL_LIBEGL=$e" && break
    done
  fi
  # ---- pad: A/B no rotulo fisico (receita do Cuphead R24) ----
  # Escreve uma copia privada do db com a linha do pad do TSP por ULTIMA: o SDL
  # aplica as linhas na ordem, entao a ultima para um GUID vence a mapa Xbox
  # embutida. a:b1,b:b0 == devolve aos botoes as letras que o firmware mostra.
  UK_DB="$STATEDIR/gamecontrollerdb-tsp.txt"
  SRC_DB=""
  if [ "${ULTRAKILL_TSP_PAD_FIX:-1}" = 0 ]; then
    echo "   pad TSP: correcao desligada (ULTRAKILL_TSP_PAD_FIX=0)"
    SRC_DB=""
  fi
  for c in "${SDL_GAMECONTROLLERCONFIG_FILE:-}" "$PORTDIR/../PortMaster/gamecontrollerdb.txt" \
           /roms/ports/PortMaster/gamecontrollerdb.txt \
           /mnt/SDCARD/Roms/ports/PortMaster/gamecontrollerdb.txt \
           "$HOME/.config/PortMaster/PortMaster/gamecontrollerdb.txt" \
           /opt/tools/PortMaster/gamecontrollerdb.txt; do
    if [ -n "$c" ] && [ -f "$c" ]; then SRC_DB="$c"; break; fi
  done
  if [ -n "$SRC_DB" ] && cp -f "$SRC_DB" "$UK_DB" 2>/dev/null; then
    printf '%s\n' '0300a3845e0400008e02000014010000,Xbox 360 Controller,a:b1,b:b0,back:b6,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,dpup:h0.1,guide:b8,leftshoulder:b4,leftstick:b9,lefttrigger:a2,leftx:a0,lefty:a1,rightshoulder:b5,rightstick:b10,righttrigger:a5,rightx:a3,righty:a4,start:b7,x:b2,y:b3,platform:Linux,' >> "$UK_DB" 2>/dev/null
    export SDL_GAMECONTROLLERCONFIG_FILE="$UK_DB"
    export SDL_GAMECONTROLLER_USE_BUTTON_LABELS=0
    echo "   pad TSP: A/B pelo rotulo fisico (db privado em $UK_DB)"
  fi
  # ---- trava do display / console (receita Cuphead + GTA SA) ----
  if command -v flock >/dev/null 2>&1; then
    exec 8>/tmp/ports-android-display.lock
    flock -n 8 || echo "   AVISO: outra janela de port segura o display (lock ocupado)"
  fi
  # UK_DEVFREQ_RESTORE e o lock tem que ser desfeitos em QUALQUER saida (o
  # Cuphead faz igual no finish/trap EXIT): sem isto, um exit antecipado
  # (ex.: sem binario) deixava o SoC com min_freq no teto para a ES toda.
  uk_cleanup_done=0
  uk_cleanup() {
    [ "${uk_cleanup_done:-0}" = 1 ] && return 0
    uk_cleanup_done=1
    if [ -n "${UK_DEVFREQ_RESTORE:-}" ] && [ -f "$UK_DEVFREQ_RESTORE" ]; then
      while read -r upath uold; do
        [ -n "$upath" ] && [ -n "$uold" ] && echo "$uold" > "$upath" 2>/dev/null
      done < "$UK_DEVFREQ_RESTORE"
      rm -f "$UK_DEVFREQ_RESTORE" 2>/dev/null || true
    fi
  }
  trap uk_cleanup EXIT TERM INT HUP
  for vt in /sys/class/vtconsole/vtcon0/bind /sys/class/vtconsole/vtcon1/bind; do
    [ -w "$vt" ] && echo 0 > "$vt" 2>/dev/null
  done
  # ---- GPU/RAM clocks do spruceOS (devfreq; Cuphead faz o mesmo) ----
  # Guarda o valor ANTES de subir e restaura no fim (arquivo em /tmp, porque
  # /sys nao aceita criar ficheiros). min_freq no teto = sem DVFS no meio da
  # partida, que e o que dá o ' stutter' de 30->18 fps do TSP sob aquecimento.
  UK_DEVFREQ_RESTORE="/tmp/ultrakill-devfreq-$$"
  : > "$UK_DEVFREQ_RESTORE" 2>/dev/null
  for df in /sys/class/devfreq/*gpu* /sys/class/devfreq/*dmc* /sys/class/devfreq/*pmu* /sys/class/devfreq/ff400000.gpu; do
    [ -d "$df" ] || continue
    TOP=$(cat "$df/max_available_frequency" 2>/dev/null)
    [ -z "$TOP" ] && TOP=$(cat "$df/max_freq" 2>/dev/null)
    [ -z "$TOP" ] && continue
    for k in min_freq max_freq; do
      [ -w "$df/$k" ] || continue
      OLD=$(cat "$df/$k" 2>/dev/null)
      if echo "$TOP" > "$df/$k" 2>/dev/null; then
        echo "$df/$k $OLD" >> "$UK_DEVFREQ_RESTORE" 2>/dev/null
      fi
    done
  done
  echo "   devfreq: $(grep -c . "$UK_DEVFREQ_RESTORE" 2>/dev/null || echo 0) ajuste(s) de clock (restaurados ao sair)"
fi
# ---- ORCAMENTO DE 30 FPS ----
# Pedido do usuario: ~30 FPS jogaveis no TSP. Os tres parafusos que importam
# (e o Cuphead usa os mesmos):
#   1) resolucao interna = metade do painel  -> 640x360 (menos 4x fragmentos)
#   2) pacing do loader  -> alvo 33333 us por frame (30 FPS) em vez de 16666
#   3) qualidade minima  -> variantes de shader baratas (UnityGraphicsQuality=0)
ULTRAKILL_TARGET_FPS="${ULTRAKILL_TARGET_FPS:-30}"
case "$ULTRAKILL_TARGET_FPS" in
  30) UK_FRAME_US=33333 ;;
  45) UK_FRAME_US=22222 ;;
  60) UK_FRAME_US=16666 ;;
  0)  UK_FRAME_US=0 ;;
  *)  UK_FRAME_US=33333 ;;
esac
if [ "$ULTRAKILL_TARGET_FPS" = 0 ] || [ "$UK_FRAME_US" = 0 ]; then
  ULTRAKILL_FRAME_PACING="${ULTRAKILL_FRAME_PACING:-uncapped}"; UK_FRAME_US=0
else
  ULTRAKILL_FRAME_PACING="${ULTRAKILL_FRAME_PACING:-auto60}"
fi
echo ">> FPS alvo: ${ULTRAKILL_TARGET_FPS} (frame US=${UK_FRAME_US}, pacing=${ULTRAKILL_FRAME_PACING}) | interno default ${DEF_INT_W}x${DEF_INT_H}"

# ---------------------------------------------------------------- 0a. PREFLIGHT: gamedata
phase "checando payload"
# Detecta se o payload ja foi extraido. Se nao, roda extract_payload.py
# contra o APK bundlado (ou um dos paths alternativos).
extract_needed=1
if [ -d "$GD_AA/StandaloneWindows64" ] || [ -d "$GD_AA/Android" ]; then
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
if [ -d "$GD_AA/StandaloneWindows64" ] && [ -d "$PORTDIR/tools" ]; then
  if [ ! -e "$GD_AA/Android/" ]; then
    phase "preparando payload"
    python3 "$PORTDIR/tools/prepare_payload.py" --root "$(dirname "$GD_AA")" 2>&1 || \
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
    ( cd "$PORTDIR/wrapper" && bash build.txt "$LIBSUB" ) \
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
      echo "         cmake pkg-config libsdl2-dev && bash build.txt arm64'"
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
# 1 GB de RAM no TSP. Reduz o overhead do glibc/IL2CPP (mesmo receituário dos
# ports Unity que rodam nesta caixa - HollowKnight.sh):
export MALLOC_ARENA_MAX="${MALLOC_ARENA_MAX:-2}"
export MALLOC_TRIM_THRESHOLD_="${MALLOC_TRIM_THRESHOLD_:-131072}"
export MALLOC_MMAP_THRESHOLD_="${MALLOC_MMAP_THRESHOLD_:-131072}"
# IL2CPP usa Boehm GC: devolve páginas livres ao SO em vez de segurá-las.
export GC_FORCE_UNMAP_ON_GCOLLECT="${GC_FORCE_UNMAP_ON_GCOLLECT:-1}"
export GC_UNMAP_THRESHOLD="${GC_UNMAP_THRESHOLD:-1}"
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
mkdir -p "$GD_DATA"
cat > "$GD_DATA/boot.config" <<'CFG'
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
# O bridge le ULTRAKILL_INT_W/H (NAO os argumentos -screen-*: esses vao para
# dentro do Unity, que nessa build nem chegou a inicializar o GFX ainda).
# Sem exportar, o bridge ficava em 1280x720 -> pbuffer/leitura 6x maior que a
# tela da R36S (llvmpipe por CPU = 2 fps e mais RAM).
INT_W=${INT_W:-$DEF_INT_W}
INT_H=${INT_H:-$DEF_INT_H}

# ultrakill.conf ja foi lido no inicio (secao 0c) - o que ele define vale aqui.
if [ -f "$PORTDIR/ultrakill.conf" ]; then
  echo ">> (conf ja aplicado; INT_W=${INT_W:-auto} INT_H=${INT_H:-auto})"
fi
export ULTRAKILL_INT_W="$INT_W"
export ULTRAKILL_INT_H="$INT_H"
echo "resolucao interna: ${INT_W}x${INT_H} (bridge), tela do ES: $( (cat /sys/class/drm/card0*/modes 2>/dev/null || true) | head -1)"

# ---------------------------------------------------------------- 6. LD_LIBRARY_PATH
# Ordem: stubs do wrapper primeiro, depois libs nativos extraidos do
# APK (para libunity.so + libil2cpp.so), depois o resto do sistema.
if [ -d "$SHIMDIR" ]; then
  export LD_LIBRARY_PATH="$SHIMDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  echo "shim dir: $SHIMDIR"
fi
# ------------------------------------------------- 6a. SHIM EGL: nome certo, sem veneno
# O shim do port existe para interceptar eglCreateWindowSurface (o Unity deste
# build nunca tem uma Surface Java, entao passa NULL/lixo e o driver real
# recusaria). Para isso funcionar, o shim e o driver real tem que ser OBJETOS
# DIFERENTES no ld.so - e o ld.so deduplica objetos comparando o nome do
# arquivo E o SONAME. O build antigo dava ao shim o SONAME "libEGL.so.1", o
# MESMO do driver do sistema: dois objetos reivindicando o mesmo nome, e qual
# dos dois o shim encontra no seu dlopen depende da ordem de carga (frágil, e
# o motivo de nao haver NENHUMA linha "shim/egl" no log do device - a
# interceptacao pode nunca ter pegado). O build novo (wrapper/CMakeLists.txt)
# deu ao shim o SONAME "libEGL.so", que e exatamente o DT_NEEDED do
# libunity.so e nada colide com o driver real.
# Regras daqui: shim vive SO como libEGL.so; SDL/bridge_gl usam o libEGL.so.1
# do SISTEMA; um libEGL.so.1 que seja copia do shim e removido.
if [ ! -f "$SHIMDIR/libEGL.so" ] && [ -f "$SHIMDIR/libEGL.so.1" ]; then
  echo ">> shim: artifact antigo (.so.1); renomeando p/ libEGL.so"
  cp -f "$SHIMDIR/libEGL.so.1" "$SHIMDIR/libEGL.so" 2>/dev/null || true
fi
if [ -f "$SHIMDIR/libEGL.so.1" ] && cmp -s "$SHIMDIR/libEGL.so" "$SHIMDIR/libEGL.so.1" 2>/dev/null; then
  echo ">> shim: removendo libEGL.so.1 duplicado (era copia do shim; faria o SDL cair no shim e o shim em si mesmo)"
  rm -f "$SHIMDIR/libEGL.so.1" 2>/dev/null || true
fi
if [ -f "$SHIMDIR/libEGL.so" ]; then
  _sonm=$(strings -a "$SHIMDIR/libEGL.so" 2>/dev/null | grep -m1 -x 'libEGL\.so\(\.1\)\?$')
  echo "shim EGL ativo via LD_LIBRARY_PATH (sem LD_PRELOAD, SONAME visto=${_sonm:-?})"
  [ "${_sonm:-}" = "libEGL.so.1" ] && \
    echo "  INFO: shim com SONAME velho (libEGL.so.1). Ainda funciona porque o driver real e carregado antes, mas refaca o build do wrapper para tirar a ambiguidade."
else
  echo "AVISO: $SHIMDIR/libEGL.so AUSENTE - o Unity vai falar direto com o EGL do sistema sem shim"
fi
# Native Android .so extraidos do APK:
for d in "$GD_LIB/arm64-v8a" "$GD_LIB/armeabi-v7a" "$GD_LIB/$LIBSUB"; do
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

# ------------------------------------------- 6c-GPU. PILHA DE GPU DO BOX
# pvr    (PADRAO no TrimUI Smart Pro): NADA de blob Mali nem Mesa software. O
#        DDK PowerVR do proprio firmware serve libEGL/libGLESv2 (via glvnd ou
#        direto), e o SDL abre janela real (kmsdrm/x11/wayland). O bridge cria o
#        contexto EGL nessa pilha. Como nao ha /dev/mali0 aqui, nada de
#        "not of a compatible version".
# mali   (R36S/ArkOS4Clone): blob Mali EMPACOTADO (graphics/libMali.so = o do
#        cuphead, r13p0) + GBM/KMS em /dev/dri/card0. O blob do SISTEMA e r10.6
#        e nao bate com o kernel mali_kbase r11.7.
# swrast : Mesa/llvmpipe SOFTWARE. Fallback lento; exige /dev/dri/renderD128
#        (negado p/ user "ark" em varios firmwares) e costuma dar exit 124.
GL_BACKEND="${ULTRAKILL_GL_BACKEND:-$GPU}"
if [ "$GL_BACKEND" = "$GPU" ]; then
  echo ">> GPU backend = $GL_BACKEND (detectado p/ o box)"
fi
MALI_BLOB=""
if [ "$GL_BACKEND" = "mali" ]; then
  # NUNCA usamos o blob Mali do SISTEMA (/usr/lib/.../libmali-bifrost-g31-rxp0-
  # wayland-gbm.so): ele e DDI r10.6 e o kernel mali_kbase deste firmware e
  # r11.7, entao o /dev/mali0 recusa -> "file /dev/mali0 is not of a compatible
  # version (user 10.6, kernel 11.7)" -> eglInitialize falha (err 0x3001).
  # O blob que BATE com o kernel e o EMPACOTADO nos ports (graphics/libMali.so,
  # provado pelo cuphead rodando ~30 FPS com ele). Entao so procuramos a copia
  # empacotada; o blob do sistema so entra se ULTRAKILL_GL_ALLOW_SYSTEM_MALI=1.
  for cand in \
      "$PORTDIR/graphics/libMali.so" \
      "$STATEDIR/graphics/libMali.so" \
      /roms/ports/cuphead/graphics/libMali.so \
      /roms2/ports/cuphead/graphics/libMali.so \
      /roms/ports/*/graphics/libMali.so \
      /roms2/ports/*/graphics/libMali.so; do
    [ -f "$cand" ] && MALI_BLOB="$cand" && break
  done
  if [ -z "$MALI_BLOB" ] && [ "${ULTRAKILL_GL_ALLOW_SYSTEM_MALI:-0}" = "1" ]; then
    echo "mali: sem blob empacotado; tentando o do sistema (RISCO de /dev/mali0 incompativel)..."
    for cand in /usr/lib/aarch64-linux-gnu/libmali*bifrost*gbm*.so /usr/lib/aarch64-linux-gnu/libmali*.so; do
      [ -f "$cand" ] && MALI_BLOB="$cand" && break
    done
  fi
  if [ -z "$MALI_BLOB" ]; then
    echo "mali: nenhum blob Mali empacotado (graphics/libMali.so) achado."
    if [ "$GPU" = "pvr" ] || [ "$BOX" = "tsp" ]; then
      echo "      box=$BOX: SEM Mali mesmo - trocando backend p/ 'pvr' (EGL do sistema)."
      GL_BACKEND=pvr
    else
      echo "      Copie um libMali.so compativel p/ $PORTDIR/graphics/ (ex.: do cuphead),"
      echo "      ou instale o cuphead, ou rode com ULTRAKILL_GL_ALLOW_SYSTEM_MALI=1."
      echo "      Caindo p/ swrast (lento)."
      GL_BACKEND=swrast
    fi
  fi
fi
if [ "$GL_BACKEND" = "pvr" ]; then
  # ---- TRIMUI SMART PRO: so usa o EGL/GLES do SISTEMA -------------------
  # Nada de libMali.so, nada de EGL_PLATFORM=surfaceless, nada de
  # ULTRAKILL_NO_SDL_GL/KMS_DIRECT (esses sao remendos do Mesa software da
  # R36S). Deixamos o SDL+PowerVR fazerem window surface de verdade, que e o
  # unico caminho em que o DDK da flip no painel.
  echo "GPU backend = pvr (PowerVR do sistema; sem blob Mali, sem Mesa software)"
  export PVRSRVHINTS="${PVRSRVHINTS:-CompressTextures=3}"
  unset EGL_PLATFORM ULTRAKILL_EGL_SURFACELESS
  unset __EGL_VENDOR_LIBRARY_FILENAMES MESA_GL_VERSION_OVERRIDE MESA_GLES_VERSION_OVERRIDE
  unset ULTRAKILL_NO_SDL_GL ULTRAKILL_KMS_DIRECT ULTRAKILL_SKIP_FOCUS
  export ULTRAKILL_FB_DEVICE="${ULTRAKILL_FB_DEVICE:-}"
  export SDL_VIDEO_EGL_DRIVER="${SDL_VIDEO_EGL_DRIVER:-libEGL.so.1}"
  export SDL_BypassDesktopEnvironmentChecker=1
  # O shim do port (libEGL.so) encaminha para o driver real; a busca do
  # 'real' e feita mais abaixo (bloco 6b) e NAO pode ser o nosso shim.
fi
export ULTRAKILL_GL_BACKEND="$GL_BACKEND"
export ULTRAKILL_BOX="$BOX" ULTRAKILL_GPU="$GPU"
if [ "$GL_BACKEND" = "mali" ]; then
  echo "GPU backend = mali (blob nativo): $MALI_BLOB"
  # Aliases em tmpfs: o SD e exFAT e nao suporta symlink; /dev/shm suporta.
  UK_GPU="/dev/shm/ultrakill-gpu-$$"
  mkdir -m 700 -p "$UK_GPU" 2>/dev/null || UK_GPU=""
  [ -z "$UK_GPU" ] && { UK_GPU="$STATEDIR/.gpu-alias-$$"; mkdir -p "$UK_GPU" 2>/dev/null; }
  if [ -d "$UK_GPU" ]; then
    for nm in libMali.so libmali.so libEGL.so libEGL.so.1 libGLESv2.so \
              libGLESv2.so.2 libGLESv1_CM.so.1 libgbm.so libgbm.so.1 \
              libmali-bifrost-g31-rxp0-gbm.so; do
      ln -sf "$MALI_BLOB" "$UK_GPU/$nm" 2>/dev/null \
        || cp -f "$MALI_BLOB" "$UK_GPU/$nm" 2>/dev/null || true
    done
  fi
  export ULTRAKILL_GPU_ALIAS_DIR="$UK_GPU"
  # Alias dir ANTES do SHIMDIR: libEGL.so.1 / libGLESv2.so.2 que o probe e o
  # SDL abrem caem no blob Mali, nao no shim Mesa.
  export LD_LIBRARY_PATH="$UK_GPU${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
  export SDL_VIDEO_EGL_DRIVER="$MALI_BLOB"
  export SDL_VIDEO_GL_DRIVER="$MALI_BLOB"
  export ULTRAKILL_REAL_LIBGLESV2="$MALI_BLOB"
  # O shim EGL embutido do probe e o interpositor SHIMDIR resolvem o driver
  # "real" por estas duas variaveis. Antes a gente as DEIXAVA soltas -> elas
  # caian no libEGL.so.1 do SISTEMA (r10.6, /dev/mali0 incompativel). Agora
  # FORCAMOS o blob bom empacotado em ambos os caminhos.
  export ULTRAKILL_REAL_LIBEGL="$MALI_BLOB"
  export BENDER_EGL_LIBRARY="$MALI_BLOB"
  # Mali GBM fala DRM/KMS direto; jamais forcamos surfaceless aqui, porque o
  # EGL_PLATFORM=surfaceless faz o eglInitialize do blob falhar.
  unset EGL_PLATFORM
  unset ULTRAKILL_EGL_SURFACELESS
  unset __EGL_VENDOR_LIBRARY_FILENAMES
  export VIDEO_KMSDRM_REQUIRE_DRM_MASTER=0
  export SDL_VIDEO_KMSDRM_NO_ATIMMUTAL_FB=1
  # Bring-up do GBM pode ser lento: damos folga ao watchdog GPU do probe.
  export BENDER_GPU_WATCHDOG_SEC="${BENDER_GPU_WATCHDOG_SEC:-45}"
  echo "mali: alias=$UK_GPU EGL/GL driver=$MALI_BLOB real_egl=$ULTRAKILL_REAL_LIBEGL bender_egl=$BENDER_EGL_LIBRARY watchdog=${BENDER_GPU_WATCHDOG_SEC}s"
elif [ "$GL_BACKEND" = "swrast" ]; then
  echo "GPU backend = swrast (Mesa/llvmpipe software - fallback lento)"
elif [ "$GL_BACKEND" = "pvr" ]; then
  echo "GPU backend = pvr (ok)"
fi

# ------------------------------------------- 6c-SWRAST. OVERRIDE EGL (fallback)
# So roda quando ULTRAKILL_GL_BACKEND=swrast. Usava glvndstack Mesa + surfaceless;
# exige /dev/dri/renderD128 (negado p/ "ark") e cai em surfaceless -> o Unity
# nunca completa eglInitialize (motivo do exit 124). Mantido como ultima opcione.
if [ "$GL_BACKEND" != "swrast" ]; then
  echo "(6c: pilha Mesa pulada, backend=$GL_BACKEND)"
else
# O probe mostrou: /usr/lib/aarch64-linux-gnu/libEGL.so.1 e symlink para o
# blob BIFROST-g31 (GPU do RG353!) - na R36S (mali400/utgard) isso explode
# "user 10.6 kernel 11.7". O GL que funciona aqui e MESA+LIMA via glvnd
# (lima_dri.so + 50_mesa.json, provado pelo doom3/cuphead rodando).
# O dispatcher glvnd e o arquivo certo: libEGL.so.1.0.0 (NUNCA o .so.1).
{
  # ---- pilha GL SOFTWARE embarcada (glvndstack) - tem PRIORIDADE ----
  GSL="$SHIMDIR/glvndstack"
  if [ -d "$GSL" ] && [ -f "$GSL/libEGL.so.1.0.0" -o "$(ls "$GSL"/libEGL.so.1* 2>/dev/null | head -1)" ]; then
    GLV=$(ls "$GSL"/libEGL.so.1.* 2>/dev/null | head -1)
    [ -z "$GLV" ] && GLV="$GSL/libEGL.so.1"
    # SHIMDIR ANTES do stack: SDL dlopen('libEGL.so.1') tem que cair no NOSSO
    # shim (que intercepta eglCreateWindowSurface p/ Unity); o shim entao
    # encaminha p/ o glvnd real via ULTRAKILL_REAL_LIBEGL abaixo.
    export LD_LIBRARY_PATH="$SHIMDIR:$GSL${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export __EGL_VENDOR_LIBRARY_FILENAMES="$GSL/glvnd/50_mesa.json"
    # DRI path = SISTEMA (mesa 19.2.8 swrast, comprovado vivo pelo egltest).
    # O swrast_dri 20.3 embarcado exige /dev/dri/renderD128 (perm denied) e
    # morria 'DRI2: failed to load driver'.
    export LIBGL_DRIVERS_PATH=/usr/lib/aarch64-linux-gnu/dri
    export SDL_VIDEO_KMSDRM_NO_ATIMMUTAL_FB=1
    # NAO forcar MESA_LOADER_DRIVER_OVERRIDE=swrast: com platform GBM/KMS o
    # 'swrast' puro nao tem winsys gbm e o loader da strcmp em ponteiro
    # lixo (crash visto no device). Sem override, mesa auto-escolhe:
    # lima (se houver) senao kms_swrast (gallium software c/ gbm ok).
    export EGL_LOG_LEVEL=debug
    # Tentativa A: platform SURFACELESS (mesa ignora gbm/window; render pra
    # offscreen buffer). SDL kmsdrm sem gbm real do mesa fica sem driver GL;
    # REQUIRE_DRM_MASTER=0 evita bloqueio. Se nativeRender precisar de
    # window surface de verdade, partimos pro loader kms-only (tentativa B).
    export EGL_PLATFORM=surfaceless
    # shim normaliza QUALQUER eglGetDisplay/PlatformDisplay p/ surfaceless
    # (egltest provou: mesa surfaceless swrast ES3.2 vive sem SDL; o loader
    # GBM do SDL-kmsdrm e o que crasha)
    export ULTRAKILL_EGL_SURFACELESS=1
    export SDL_VIDEO_EGL_GET_CLIENT_EXT=0
    export VIDEO_KMSDRM_REQUIRE_DRM_MASTER=0
    # bridge_gl cria EGL sozinho (Pbuffer) e NAO deixa o SDL tocar EGL
    # (SDL 2.32 + mesa nessa firmware crasha no loader GBM/DRM).
    export ULTRAKILL_NO_SDL_GL=1
    # present: dumb buffer KMS direto em card0 (mesmo caminho do ES); sem
    # GBM/renderD128 que o usuario ark nao tem permissao.
    export ULTRAKILL_KMS_DIRECT=1
    # nativeFocusChanged antes do resume crasha no Unity 2019.4 sem Swappy/
    # Choreographer real; pular e ir direto resume->render.
    export ULTRAKILL_SKIP_FOCUS=1
    export MESA_GL_VERSION_OVERRIDE=3.2 MESA_GLES_VERSION_OVERRIDE=3.2
    export ULTRAKILL_REAL_LIBEGL="$GLV"
    echo "EGL = glvndstack embarcado (llvmpipe software): $GLV"
  fi
  glvnd=""
  for c in /usr/lib/aarch64-linux-gnu/libEGL.so.1.0.0 \
           /usr/lib/aarch64-linux-gnu/libEGL.so.1.vnd \
           /usr/lib/libEGL.so.1.0.0 /lib/aarch64-linux-gnu/libEGL.so.1.0.0; do
    [ -f "$c" ] && glvnd="$c" && break
  done
  if [ -n "$glvnd" ] && [ -z "${ULTRAKILL_REAL_LIBEGL:-}" ]; then
    export ULTRAKILL_REAL_LIBEGL="$glvnd"
    echo "EGL real trocado p/ glvnd dispatcher: $glvnd"
  elif [ -z "${ULTRAKILL_REAL_LIBEGL:-}" ]; then
    echo "AVISO: dispatcher glvnd nao achado; shim vai cair no blob (talvez falhe)"
  fi
  vj=/usr/share/glvnd/egl_vendor.d/50_mesa.json
  [ -f "$vj" ] && export __EGL_VENDOR_LIBRARY_FILENAMES="$vj" && \
    echo "glvnd forcado p/ mesa: $vj"
  export LIBGL_DRIVERS_PATH=/usr/lib/aarch64-linux-gnu/dri
  # PROBE2 provou: libEGL.so.1/.so.1.0.0/libGLESv2/libgbm sao TODOS o MESMO
  # blob bifrost disfarçado (incompativel c/ kernel utgard 11.7). A unica
  # chance real e libEGL_mesa.so.0.0.0 ser um mesa de API publica. Se o
  # dispatcher glvnd aqui for o blob (como mostrado), escolher mesa direto.
  if [ -z "${ULTRAKILL_REAL_LIBEGL:-}" ] && command -v python3 >/dev/null 2>&1 && [ -f "$PORTDIR/tools/find_real_egl.py" ]; then
    real=$(python3 "$PORTDIR/tools/find_real_egl.py" 2>>"$LOG" | head -1)
    if [ -n "$real" ] && [ "$real" != "NONE" ]; then
      export ULTRAKILL_REAL_LIBEGL="$real"
      echo "EGL real escolhido (auto): $real"
    else
      echo "AVISO: nenhum libEGL com API publica alem do blob; mantendo $glvnd"
    fi
  fi
  # GLESv2: garante que o shim do port entrega o dispatcher glvnd, nao o
  # blob bifrost (se houver .so.2.0.0, copia p/ SHIMDIR que vem primeiro).
  for g in /usr/lib/aarch64-linux-gnu/libGLESv2.so.2.0.0 /usr/lib/libGLESv2.so.2.0.0; do
    if [ -f "$g" ]; then
      case "$(readlink -f "$g")" in
        *mali*) echo "GLESv2 $g e o blob disfarçado, pulando" ;;
        *)
          export ULTRAKILL_REAL_LIBGLESV2="$g"
          echo "GLESv2 real = $g"
          if [ ! -e "$SHIMDIR/libGLESv2.so.2" ]; then
            cp "$g" "$SHIMDIR/libGLESv2.so.2" 2>/dev/null
          fi
          break ;;
      esac
    fi
  done
}
fi

# ------------------------------------------------------ 6d. QUEM PODE ESCREVER NA TELA
# O port apresenta o quadro OU por KMS (dumb buffer em /dev/dri/card0) OU por
# /dev/fb0. O userland do ArkOS4Clone roda o port como "ark" (uid 1002), e o
# renderD128 ja provou ser negado. Chutar no escuro da tela preta ETERNA, entao
# sondamos os dois devices e logamos a resposta real (so teste -w; nunca abrimos
# com truncate um device, senao poderiamos bagunar o console).
{
  echo "== SONDA DE DISPLAY (id=$(id -u):$(id -g)) =="
  ls -l /dev/dri/ /dev/fb0 /dev/fb1 2>/dev/null | sed 's|^|  |'
  cardok=""; fbok=""
  for c in /dev/dri/card0 /dev/dri/card1; do
    if [ -w "$c" ]; then cardok="$c"; break; fi
    [ -e "$c" ] && echo "  $c EXISTE mas sem escrita"
  done
  for f in /dev/fb0 /dev/fb1; do
    if [ -w "$f" ]; then fbok="$f"; break; fi
    [ -e "$f" ] && echo "  $f EXISTE mas sem escrita"
  done
  echo "  card0 escrevivel=[${cardok:-NAO}]  fb escrevivel=[${fbok:-NAO}]"
  if [ "$GL_BACKEND" = "pvr" ]; then
    # NO TRIMUI SMART PRO quem apresenta o quadro e o PROPRIA DDK (eglSwapBuffers
    # na window surface do SDL). Assumir o CRTC por dumb buffer (KMS_DIRECT) ou
    # escrever /dev/fb0 por cima disso e o que dava tela preta/cursor. Entao:
    unset ULTRAKILL_KMS_DIRECT
    export ULTRAKILL_NO_FB=1
    echo "  -> present = EGL nativo do PowerVR (sem KMS dumb buffer, sem fb0)"
  elif [ "${KMS_MODE:-auto}" = off ]; then
    # escape room para o caso "tela preta depois de sair": escreva
    # KMS_MODE=off no ultrakill.conf e o port NUNCA assume o CRTC.
    unset ULTRAKILL_KMS_DIRECT
    echo "  -> KMS desligado por ultrakill.conf (KMS_MODE=off)"
  elif [ -n "$cardok" ]; then
    export ULTRAKILL_KMS="$cardok"
    export ULTRAKILL_KMS_DIRECT=1
    echo "  -> present via KMS ($cardok)"
  else
    unset ULTRAKILL_KMS_DIRECT
    echo "  -> KMS desligado (sem escrita em card0)"
  fi
  if [ -n "$fbok" ] && [ "$GL_BACKEND" != "pvr" ]; then
    export ULTRAKILL_FB_DEVICE="$fbok"
    echo "  -> fallback fb disponivel: $fbok"
  fi
  echo "  (se nenhum dos dois: o port roda HEADLESS e so o log interessa)"
} 2>&1

# ------------------------------------------------------------------ 6a. Diagnosticos libSDL2
{ # ======= PROBE GPU (ArkOS4Clone: descobrir p/ que dhewm3/cuphead apontam) =======
  echo "== GPU PROBE =="
  echo "-- /usr/lib*/ -> libEGL/libGLES/libMali/libGL/libgbm (symlinks reais) --"
  for dir in /usr/lib /usr/lib/aarch64-linux-gnu /usr/lib/arm-linux-gnueabihf /usr/lib/egl /lib/aarch64-linux-gnu; do
    [ -d "$dir" ] || continue
    ls -l "$dir" 2>/dev/null | grep -iE 'libEGL|libGLESv2|libMali|libmali|libGL\.|libGLX|libgbm|swrast|gallium' | sed "s|^|  |"
  done
  echo "-- libEGL.so.1 resolve para --"
  readlink -f /usr/lib/aarch64-linux-gnu/libEGL.so.1 2>/dev/null | sed "s|^|  |"
  ldd /usr/lib/aarch64-linux-gnu/libEGL.so.1 2>/dev/null | grep -iE 'mali|EGL|GLES|drm|gbm' | sed "s|^|  ldd: |"
  ls /usr/share/glvnd/egl_vendor.d/ 2>/dev/null && cat /usr/share/glvnd/egl_vendor.d/*.json 2>/dev/null | grep -E 'library|name' | sed "s|^|  glvnd: |"
  echo "-- versoes utgard nos blobs --"
  for b in /usr/lib/aarch64-linux-gnu/libMali.so /usr/lib/aarch64-linux-gnu/libmali*.so /usr/lib/libMali.so /usr/lib/aarch64-linux-gnu/mali/*.so; do
    [ -f "$b" ] || continue
    v=$(strings -a "$b" 2>/dev/null | grep -m3 -iE 'r1[0-9]p[0-9]|user [0-9]+\.[0-9]+|mali_version' | tr '\n' ' ')
    echo "  $b :: $v"
  done
  echo "-- kernel mali module --"
  for m in /sys/module/mali /sys/module/mali_kbase; do
    [ -d "$m" ] && { echo "  $m: $(cat $m/version 2>/dev/null)"; ls $m/parameters 2>/dev/null | head -3; }
  done
  ls -l /dev/mali* /dev/dri 2>/dev/null | sed "s|^|  |"
  echo "-- ldd dhewm3 (que GL ele usa de verdade) --"
  for d in /roms/ports/doom3/dhewm3 /roms2/ports/doom3/dhewm3 /usr/local/bin/dhewm3; do
    [ -f "$d" ] && ldd "$d" 2>/dev/null | grep -iE 'egl|\bgl|mali|gbm|drm' | sed "s|^|  |"
  done
  echo "-- mesa/gallium escondidos no rootfs --"
  find /usr/lib /opt /lib -maxdepth 4 \( -iname 'libEGL_*' -o -iname 'libglx_*' -o -iname '*gallium*' -o -iname 'swrast*' -o -iname 'lima_*' \) 2>/dev/null | head -12 | sed "s|^|  |"
  echo "== FIM GPU PROBE =="
} >> "$LOG" 2>&1

# ======= PROBE2: quem sao REALMENTE as libs de GL (symlinks + versoes) =======
{
  echo "== PROBE2 REAL GL STACK =="
  for f in /usr/lib/aarch64-linux-gnu/libEGL_mesa.so.0 /usr/lib/aarch64-linux-gnu/libEGL.so.1.0.0 \
           /usr/lib/aarch64-linux-gnu/libGLESv2.so.2 /usr/lib/aarch64-linux-gnu/libGLESv2.so.2.0.0 \
           /usr/lib/aarch64-linux-gnu/libgbm.so.1; do
    [ -e "$f" ] || { echo "  [ausente] $f"; continue; }
    real=$(readlink -f "$f")
    id=$(strings -a "$real" 2>/dev/null | grep -m1 -iE 'mali-[a-z]*[0-9]|Mali400|Utgard|r1[0-9]p[0-9]|glvnd|mesa [0-9]|llvmpipe' | tr -d '\r')
    echo "  $(basename $f) -> $real :: $id"
  done
  echo "-- egl_vendor.d completos --"
  ls -l /usr/share/glvnd/egl_vendor.d/ 2>/dev/null
  for j in /usr/share/glvnd/egl_vendor.d/*.json; do [ -f "$j" ] && { echo "  $j:"; cat "$j" | sed 's/^/    /'; }; done
  echo "-- modulos de kernel GPU --"
  grep -iE 'mali|lima|gpu|drm' /proc/modules 2>/dev/null | awk '{print "  "$1" "$3}'
  echo "-- TODOS os libmali*/libMali* do rootfs com versao --"
  find /usr/lib /usr/lib64 /opt /lib -maxdepth 6 \( -iname 'libmali*.so*' -o -iname 'mali*.so' \) 2>/dev/null | sort -u | while read mb; do
    rr=$(readlink -f "$mb")
    vs=$(strings -a "$rr" 2>/dev/null | grep -m1 -oE '(user|kernel) [0-9]+\.[0-9]+|r1[0-9]p[0-9]+(-[a-z]+)?|Mali-?[0-9]+' | tr -d '\r')
    echo "  $mb -> $rr :: [$vs]"
  done
  echo "-- RetroArch: qual lib ele carrega --"
  RA=$(which retroarch || ls /usr/bin/retroarch /opt/*/retroarch 2>/dev/null | head -1)
  [ -n "$RA" ] && ldd "$RA" 2>/dev/null | grep -iE 'egl|gles|mali|gl\b|drm' | sed 's/^/  /'
  echo "== FIM PROBE2 =="
}
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
}

# Persistencia
export ULTRAKILL_PERSIST="$STATEDIR"
# Ficheiro proprio do guard (escrito com open/write puros, ANTES de qualquer
# reorientacao de stderr que o Unity faz). E a unica coisa que sobrevive
# quando o crash acontece dentro do nativeResume.
export ULTRAKILL_CRASHFILE="$PORTDIR/ultrakill-crash.log"
: > "$ULTRAKILL_CRASHFILE" 2>/dev/null || true

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
      /usr/lib/libMali.so /usr/lib/libMali*.so /usr/lib/libmali*.so* \
      /usr/lib/egl/libEGL-mali.so /usr/lib/libEGL-mali.so \
      /usr/lib/*/libMali.so /opt/mali*/libEGL.so \
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
    if [ -z "${ULTRAKILL_REAL_LIBEGL:-}" ]; then   # 6c ja decidiu? nao sobrescrever
    echo "  usando como ULTRAKILL_REAL_LIBEGL: $found"
    export ULTRAKILL_REAL_LIBEGL="$found"
    else
    echo "  (mantendo ULTRAKILL_REAL_LIBEGL decidido no override 6c)"
    fi
  else
    echo "  NENHUM driver EGL real encontrado! shim vai falhar em abrir"
  fi
  # GLESv2 tambem:
  for pat in /usr/lib/*/libGLESv2.so* /usr/lib/libGLESv2.so* \
             /usr/lib/egl/libGLES* /usr/lib/libIMG*GLES* /usr/lib/hwdefault*/*/libGLESv2*; do
    for f in $pat; do
      [ -f "$f" ] || continue
      echo "  GLESv2: $f"
      [ -z "${ULTRAKILL_REAL_LIBGLESV2:-}" ] && export ULTRAKILL_REAL_LIBGLESV2="$f"
      break 2
    done
  done
}

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
}

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
# NO TRIMUI SMART PRO / spruceOS o backend que funciona e o 'mali' do SDL2 do
# firmware (GTA SA / McpeLauncher / Cuphead). kmsdrm so funciona se a ES soltar
# DRM master - e ela nao solta. Entao 'mali' ENTRA PRIMEIRO quando o flip existe.
if [ "${SPRUCE:-0}" = 1 ]; then
  if [ -d /mnt/SDCARD/spruce ] || [ -d /usr/trimui/lib ]; then
    cand="mali $cand"
    # x11/wayland vao para o fim: no TSP o compositor da ES e kmsdrm, e um
    # 'mali' que falhe deve cair em kmsdrm/wayland, nao em fbcon cego.
    echo ">> candidatos SDL: 'mali' (backend do firmware) adicionado na frente"
  fi
fi
# wayland SEMPRE no fim tbem, na falta de melhor
if ! echo "$cand" | grep -q ' wayland '; then cand="$cand wayland"; fi
cand="$cand offscreen dummy"
CANDIDATES="$cand"
echo "candidatos SDL_VIDEODRIVER a testar: $CANDIDATES"
echo "env: WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>}  DISPLAY=${DISPLAY:-<unset>}  XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-<unset>}"

# ---------------------------------------------------------------- 7. EXEC
# Diz ao bridge o caminho ABSOLUTO do libunity.so (evita qualquer ambiguidade
# de busca no LD_LIBRARY_PATH).
for lu in "$GD_LIB/arm64-v8a/libunity.so" "$GD_LIB/armeabi-v7a/libunity.so"; do
  if [ -f "$lu" ]; then export ULTRAKILL_LIBUNITY="$lu"; echo "ULTRAKILL_LIBUNITY=$lu"; break; fi
done
BIN=""
for c in "$GD/bin/ultrakill-arm64" "$GD/bin/ultrakill-arm" "$GD/bin/ultrakill"; do
  [ -x "$c" ] && BIN="$c" && break
done

# ---------------------------------------- 7. BENDER: loader Unity ja comprovado na R36S
# Engenharia reversa dos ports QUE FUNCIONAM neste cartao: 60_parsecs e cuphead
# nao usam bridge proprio - usam um loader generico ("BENDER Unity IL2CPP
# AArch64 probe") que ele mesmo mapeia libmain/libunity/libil2cpp, resolve as
# importacoes Bionic/Android, da JavaVM/JNIEnv com ABI correta, Choreographer em
# thread propria e assets materializados em unity_payload/assets.
# Nos recompilamos ESSE loader com a identidade do ULTRAKILL (bender/ no repo),
# entao o jogo entra pela mesma porta que ja levou outros Unity ate a tela.
# Se o BENDER falhar, caimos no nosso bridge original (loop de SDL_VIDEODRIVER).
PROBE="$PORTDIR/unity_probe.aarch64"
PLIB="$PORTDIR/unity_payload/lib/arm64-v8a"
[ -d "$PLIB" ] || PLIB="$GD_LIB/arm64-v8a"
# SELF-HEAL das libs nativas: o IL2CPP so funciona se libil2cpp.so + libunity.so
# forem do MESMO build do global-metadata.dat/cenas. Ja quebramos o boot com
# "referenced script is missing" porque o base.apk foi trocado e as .so extraidas
# ficaram velhas (mesmo tamanho, bytes diferentes). Aqui comparamos o libil2cpp.so
# do disco com o que esta DENTRO do base.apk (cksum, barato) e, se divergir,
# reextraímos todas as libs nativas do apk. Assim trocar de APK nunca mais
# descompassa o payload. Melhor-esforco: se falhar, segue com as libs atuais.
if [ -f "$PORTDIR/base.apk" ] && [ -d "$PLIB" ] && command -v python3 >/dev/null 2>&1; then
  _UKAPK="$PORTDIR/base.apk"; _UKL1="$PLIB/libil2cpp.so"
  if [ -f "$_UKL1" ]; then
    _UKT="$(mktemp 2>/dev/null || echo /tmp/uk_il2cpp_chk.$$)"
    if python3 -c 'import sys,zipfile; open(sys.argv[2],"wb").write(zipfile.ZipFile(sys.argv[1]).read("lib/arm64-v8a/libil2cpp.so"))' "$_UKAPK" "$_UKT" 2>/dev/null; then
      if [ -f "$_UKT" ] && [ "$(cksum < "$_UKT")" != "$(cksum < "$_UKL1")" ]; then
        echo ">> libs nativas DESCOMPASSADAS do base.apk; reextraindo de dentro do apk..."
        python3 -c 'import sys,zipfile,os,shutil
z=zipfile.ZipFile(sys.argv[1]); d=sys.argv[2]; n=0
for e in z.infolist():
    f=e.filename
    if f.startswith("lib/arm64-v8a/") and f.endswith(".so"):
        o=os.path.join(d,os.path.basename(f)); s=z.open(e); w=open(o,"wb"); shutil.copyfileobj(s,w); w.close(); s.close(); n+=1
print(">> reextraidas %d libs nativas (vao casar com o global-metadata)" % n)' "$_UKAPK" "$PLIB" 2>&1 || echo ">> AVISO: reextracao de libs falhou; continuando com as atuais"
      else
        echo ">> libs nativas OK (batem com o base.apk)"
      fi
    fi
    rm -f "$_UKT" 2>/dev/null || true
  fi
fi
BENDER_PHASE="${BENDER_PHASE:-unity-run}"
BRC=90
# ---------------------------------------------------------------- AUDIO (libOpenSLES.so)
# O som do Unity 2019.4 NAO e FMOD como lib a parte: o FMOD esta EMBUTIDO no
# libunity.so e abre "libOpenSLES.so" em runtime (dlopen+dlsym). Evidencia:
#   tools/dumpelf.py unity_payload/lib/arm64-v8a/libunity.so --str 'OpenS|slCreate'
# Consequencia pratica (corrigida nesta revisao): o stub antigo devolvia um
# objeto SEM vtable, entao (*engine)->Realize() saltava para lixo -> o SIGBUS
# dentro do nativeResume. Agora wrapper/stubs/opensles_stub.c e um bridge
# OpenSL ES -> SDL2 de verdade (audio real). Se o .so do SD ainda for o stub
# velho (8488 bytes, sem o simbolo SL_IID_ENGINE), usamos o bridge embarcado
# em audio_bridge/ em vez dele - auto-cura sem recompilar.
UK_SLES="$SHIMDIR/libOpenSLES.so"
AUDIO_LIB_DESC="(nenhuma)"
if [ -f "$UK_SLES" ]; then
  _ssz=$(wc -c < "$UK_SLES" 2>/dev/null | tr -d ' ')
  if grep -aq SL_IID_ENGINE "$UK_SLES" 2>/dev/null; then
    AUDIO_LIB_DESC="$UK_SLES (bridge OpenSL->SDL2 novo)"
  elif [ -f "$PORTDIR/audio_bridge/libOpenSLES.so" ]; then
    AUDIO_LIB_DESC="$PORTDIR/audio_bridge/libOpenSLES.so (bridge embarcado; o shim do SD e o stub velho)"
  else
    AUDIO_LIB_DESC="$UK_SLES (AVISO: stub velho SEM vtable - e ele que dá SIGBUS no audio; refaca o build do wrapper)"
  fi
fi
echo ">> AUDIO: libOpenSLES usada = $AUDIO_LIB_DESC"

# ---------------------------------------------------------------- MOUSE VIRTUAL
# TrimUI Smart Pro nao tem touch screen nem mouse. O bridge emula um ponteiro:
#   START+R1  liga/desliga | B  clique | analogico direito  ponteiro
# (tudo trocavel aqui, sem recompilar - os nomes batem com bridge_input.c)
export ULTRAKILL_MOUSE="${ULTRAKILL_MOUSE:-1}"
export ULTRAKILL_MOUSE_STICK="${ULTRAKILL_MOUSE_STICK:-right}"
export ULTRAKILL_MOUSE_SPEED="${ULTRAKILL_MOUSE_SPEED:-1100}"
export ULTRAKILL_MOUSE_DEADZONE="${ULTRAKILL_MOUSE_DEADZONE:-5000}"
export ULTRAKILL_MOUSE_MODE="${ULTRAKILL_MOUSE_MODE:-touch}"
export ULTRAKILL_MOUSE_TOGGLE="${ULTRAKILL_MOUSE_TOGGLE:-start+r1}"
export ULTRAKILL_MOUSE_CLICK="${ULTRAKILL_MOUSE_CLICK:-b}"
export ULTRAKILL_AUDIO="${ULTRAKILL_AUDIO:-1}"
echo ">> INPUT: mouse=${ULTRAKILL_MOUSE} combo=${ULTRAKILL_MOUSE_TOGGLE} clique=${ULTRAKILL_MOUSE_CLICK} stick=${ULTRAKILL_MOUSE_STICK} velocidade=${ULTRAKILL_MOUSE_SPEED} modo=${ULTRAKILL_MOUSE_MODE}"

# Persistencia do jogo (Saves/, Preferences/, il2cpp/) no SD, no mesmo layout
# que o BENDER ja usava - assim as saves e o Prefs.json continuam existindo.
# (Antes o bridge apontava p/ ~/.ultrakill, entao o jogo comecava do zero e o
# Prefs.json com volume 0 nao era aproveitado.)
export ULTRAKILL_BRIDGE_PERSIST="$PORTDIR/data/files"
mkdir -p "$ULTRAKILL_BRIDGE_PERSIST" 2>/dev/null
if [ -f "$PORTDIR/tools/tune_prefs.py" ] && command -v python3 >/dev/null 2>&1; then
  python3 "$PORTDIR/tools/tune_prefs.py" "$ULTRAKILL_BRIDGE_PERSIST" 2>&1 | sed 's/^/  prefs: /' || true
fi

# Seletor de loader.
#   bender = loader com fonte em bender/unity_probe.c (943 KB, compilado pelo
#            CI no mesmo debian bullseye/glibc 2.31). E o que CHEGA MAIS LONGE
#            de verdade no ULTRAKILL: no log do device ele passa do managed,
#            registra os natives, abre EGL/GLES e ENTRA no primeiro
#            nativeRender - e ja tem input de verdade (nativeInjectEvent com
#            KeyEvent/MotionEvent falsos), HUD, teclado virtual, keymap
#            editavel, mapeamento de sticks em toque, e o bridge de som
#            (audio_bridge/libOpenSLES.so = bender/bender_opensles.c).
#   bridge = nosso wrapper SDL nativo (wrapper/*.c + gamedata/bin/ultrakill),
#            codigo aberto e mais facil de mexer; PADRAO de fallback: se o
#            BENDER morrer por sinal, o launcher roda o bridge sozinho.
# Em ambos os loaders o MOUSE VIRTUAL esta disponivel (START+R1, clique B).
ULTRAKILL_LOADER="${ULTRAKILL_LOADER:-}"
if [ -z "$ULTRAKILL_LOADER" ]; then
  ULTRAKILL_LOADER=bender
  echo ">> LOADER padrao: bender (fonte bender/unity_probe.c); bridge fica como fallback"
fi
echo ">> LOADER selecionado: $ULTRAKILL_LOADER"
if [ -f "$PROBE" ] && [ -f "$PLIB/libunity.so" ] && [ "$ULTRAKILL_LOADER" != "bridge" ]; then
  phase "loader BENDER (Unity IL2CPP, fase $BENDER_PHASE)"
  chmod +x "$PROBE" 2>/dev/null || true
  mkdir -p "$PORTDIR/data/files" "$PORTDIR/data/external" "$PORTDIR/data/cache" "$PORTDIR/data/obb" 2>/dev/null
  [ -f "$PORTDIR/audio_bridge/libOpenSLES.so" ] && chmod +x "$PORTDIR/audio_bridge/libOpenSLES.so" 2>/dev/null
  # SDL do sistema (o probe faz dlopen de libSDL2; no device ha 2.0.18/2.0.32)
  export LD_LIBRARY_PATH="$PORTDIR/audio_bridge:$PLIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  export ULTRAKILL_PERSIST="$STATEDIR"
  # No spruceOS/TSP o backend que da janela e o 'mali' do SDL do firmware; em
  # qualquer outro box mantemos kmsdrm (comportamento antigo da R36S).
  if [ "${SPRUCE:-0}" = 1 ] && [ -z "${BENDER_SDL_VIDEODRIVER:-}" ] && [ -z "${SDL_VIDEODRIVER:-}" ]; then
    export SDL_VIDEODRIVER=mali
  fi
  export SDL_VIDEODRIVER="${BENDER_SDL_VIDEODRIVER:-${SDL_VIDEODRIVER:-kmsdrm}}"
  # Reforca o backend escolhido logo antes do exec (cuida de env setado por
  # outros hooks do PortMaster/ES entre a sessao 6c e aqui).
  if [ "${ULTRAKILL_GL_BACKEND:-mali}" = "mali" ] && [ -n "${MALI_BLOB:-}" ]; then
    # Alias dir volta ao topo do path (audio_bridge/PLIB foram prepended acima).
    [ -n "${ULTRAKILL_GPU_ALIAS_DIR:-}" ] && \
      export LD_LIBRARY_PATH="$ULTRAKILL_GPU_ALIAS_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export SDL_VIDEO_EGL_DRIVER="$MALI_BLOB"
    export SDL_VIDEO_GL_DRIVER="$MALI_BLOB"
    export ULTRAKILL_REAL_LIBGLESV2="$MALI_BLOB"
    export ULTRAKILL_REAL_LIBEGL="$MALI_BLOB"
    export BENDER_EGL_LIBRARY="$MALI_BLOB"
    export BENDER_GPU_WATCHDOG_SEC="${BENDER_GPU_WATCHDOG_SEC:-45}"
    unset EGL_PLATFORM ULTRAKILL_EGL_SURFACELESS __EGL_VENDOR_LIBRARY_FILENAMES
    echo ">> BENDER GL: backend=mali blob=$MALI_BLOB real_egl=$ULTRAKILL_REAL_LIBEGL SDL_VIDEODRIVER=$SDL_VIDEODRIVER gpu_watchdog=${BENDER_GPU_WATCHDOG_SEC}s"
  else
    echo ">> BENDER GL: backend=${ULTRAKILL_GL_BACKEND:-mali} (sem blob Mali)"
  fi
  # ---- 6c-VIDEO: acerto SO DE VIDEO/RENDER do BENDER (nao mexe na logica) ----
  # Esses envs sao lidos pelo probe (strings BENDER_RENDER_SCALE_75,
  # BENDER_FRAME_PACING_MODE, BENDER_VIDEO_INTRO_SKIP, BENDER_EGL_LIBRARY no
  # binario) e mudam apenas resolucao interna / pacing / intro / EGL.
  # Render 75% -> jogo desenha em 480x360 e sobe para 640x480. NO BENDER este
  # caminho usa glBlitFramebuffer + "single-present" (guest_swaps=0) e deixou a
  # tela PRETA nesta cartaо (o present nativo 640x480 do cuphead e o que funciona).
  # Entao o PADRAO e 0 (640x480 nativo, swap normal). ULTRAKILL_RENDER_SCALE_75=1
  # re-liga o 75% so depois de confirmar que da imagem.
  ULTRAKILL_RENDER_SCALE_75="${ULTRAKILL_RENDER_SCALE_75:-0}"
  export BENDER_RENDER_SCALE_75="$ULTRAKILL_RENDER_SCALE_75"
  # PRESENT (causa raiz da tela preta "que nunca acaba"): o ULTRAKILL (Unity
  # 2019 IL2CPP via BENDER) NUNCA chama eglSwapBuffers no guest - o log mostra
  # guest_swaps=0 em TODAS as 351 frames e reason=nativeRender-no-eglSwapBuffers.
  # Sem um present do host, nada vai para a tela => preto para sempre (voce teve
  # que reiniciar). O port MODELO (60 Parsecs) exporta BENDER_FRAME_HOST_PRESENT=1:
  # obriga o host a apresentar o backbuffer do window a cada frame, independente
  # do swap do guest. Combinado com render_scale=0 (surface nativo 640x480, sem o
  # blit offscreen), esse e o caminho de present que o cuphead/60 Parsecs usam.
  # ULTRAKILL_HOST_PRESENT=0 desliga (voltaria a depender do swap do guest).
  ULTRAKILL_HOST_PRESENT="${ULTRAKILL_HOST_PRESENT:-1}"
  export BENDER_FRAME_HOST_PRESENT="$ULTRAKILL_HOST_PRESENT"
  # Pacing: compat | auto60 | uncapped (mesma semantica do port 60 Parsecs).
  ULTRAKILL_FRAME_PACING="${ULTRAKILL_FRAME_PACING:-auto60}"
  export BENDER_FRAME_PACING_MODE="$ULTRAKILL_FRAME_PACING"
  case "$ULTRAKILL_FRAME_PACING" in
    auto60)
      # alvo do frame vem do ORCAMENTO DE FPS (ULTRAKILL_TARGET_FPS, rev -L):
      # 30 fps -> 33333 us. Antes era 16666 fixo (60) o que na GE8300 so
      # fazia o loader queimar CPU em spin de vsync sem chegar a 60.
      export BENDER_FRAME_TARGET_US="${BENDER_FRAME_TARGET_US:-${UK_FRAME_US:-16666}}"
      export BENDER_FRAME_LOOP_DELAY_US="${BENDER_FRAME_LOOP_DELAY_US:-${UK_FRAME_US:-16666}}" ;;
    uncapped) export BENDER_FRAME_LOOP_DELAY_US="0" ;;
    *)        export BENDER_FRAME_LOOP_DELAY_US="${BENDER_FRAME_COMPAT_DELAY_US:-${UK_FRAME_US:-16666}}" ;;
  esac
  echo ">> BENDER FPS: alvo=${ULTRAKILL_TARGET_FPS} modo=$BENDER_FRAME_PACING_MODE target_us=${BENDER_FRAME_TARGET_US:-n/a} delay_us=${BENDER_FRAME_LOOP_DELAY_US:-n/a}"
  # ---------------------------------------------------------------------------
  # WATCHDOGS DO RENDER (rev -L, novos no TSP)
  # O log do device de 04/11 mostrou o probe CHEGANDO ao 1o nativeRender e depois
  # parado em 'sync wait kind=cond' dentro de libunity.so+0x2e38ac com o watchdog
  # de IDLE em 20 s (action=observe-only-contin). Num primeiro boot em exFAT, 20 s
  # de 'nada acontece' pode ser so o catalog/Addressables carregando - entao
  # damos folga REAL (e ajustavel no conf) para distinguir 'devagar' de 'morto'.
  export BENDER_RENDER_IDLE_WATCHDOG_SEC="${BENDER_RENDER_IDLE_WATCHDOG_SEC:-90}"
  export BENDER_RENDER_ABSOLUTE_WATCHDOG_SEC="${BENDER_RENDER_ABSOLUTE_WATCHDOG_SEC:-900}"
  export BENDER_RENDER_EXTEND_SEC="${BENDER_RENDER_EXTEND_SEC:-120}"
  export BENDER_RENDER_INSPECT="${BENDER_RENDER_INSPECT:-0}"
  # Play Core: com BENDER_PLAYCORE_MISSING=1 o Unity entende que o Google Play
  # NAO esta no aparelho e para de criar o fluxo de "instalar pack" (Dialog +
  # Runnable na UI thread) - foi esse Runnable que travou o 1o frame em 04/11
  # (NoSuchMethodError em java.lang.Runnable.run + cond wait libunity+0x2e38ac).
  # O padrao fica 0 (comportamento historico); ligue para testar o caminho B.
  export BENDER_PLAYCORE_MISSING="${BENDER_PLAYCORE_MISSING:-0}"
  # Dialog auto-fechado: o repack mobile abre AlertDialog ('Error / Not enough
  # storage space to install required resources') e o libunity espera o toque.
  # Sem tela Android isso nunca chega -> cond wait eterno no 1o frame. O probe
  # agora dispara onClick/onCancel nos listeners capturados no show().
  export BENDER_DIALOG_AUTODISMIS="${BENDER_DIALOG_AUTODISMIS:-1}"
  echo ">> BENDER WATCHDOG: idle=${BENDER_RENDER_IDLE_WATCHDOG_SEC}s absolute=${BENDER_RENDER_ABSOLUTE_WATCHDOG_SEC}s extend=${BENDER_RENDER_EXTEND_SEC}s"
  # Pula os videos de abertura (FMV). ANTES era 1 por padrao, mas o boot do
  # ULTRAKILL esta morrendo com NullReferenceException em 'main start' e o
  # skip do intro e um suspeito plausivel (a coroutine do jogo espera no no da
  # intro). Voltei para 0 (VANILLA, mantem os videos) para ISOLAR se eram as
  # minhas otimizacoes de video que quebravam o boot. ULTRAKILL_SKIP_INTRO=1 re-liga.
  ULTRAKILL_SKIP_INTRO="${ULTRAKILL_SKIP_INTRO:-0}"
  export BENDER_VIDEO_INTRO_SKIP="$ULTRAKILL_SKIP_INTRO"
  # AUDIO (motivo do corte do log dentro do 1o nativeRender): com o NRE
  # resolvido, o boot agora avanca ate o primeiro frame e AI fica preso na
  # enumeracao de dispositivos de audio (AudioManager.getDevices -> builtin-
  # speaker) que o FMOD embutido faz no 1o frame. BENDER_FMOD_FORCE_NOSOUND=1
  # abre o FMOD com saida NOSOUND (string no proprio probe: FMOD_STUDIO_OUTPUT_
  # NOSOUND), pulando o init de hardware de audio - costuma destravar o 1o frame
  # em cartas onde o OpenSL/android audio e lento/complicado. So AV, nao mexe em
  # logica. ULTRAKILL_NO_SOUND=0 devolve o audio real (com som) depois.
  # AUDIO (foi aqui que o boot parou por muito tempo). Antes o log cortava
  # dentro do 1o nativeRender na enumeracao de dispositivos (FMOD embutido ->
  # AudioManager.getDevices), e o remendo era BENDER_FMOD_FORCE_NOSOUND=1.
  # AGORA o padrao e 0 (SOM LIGADO): o bridge OpenSL ES -> SDL2
  # (bender/bender_opensles.c, compilado para audio_bridge/libOpenSLES.so) e o
  # mesmo que outros Unity desta firmware usam, e sem ele o FMOD chamava
  # (*engine)->Realize() num objeto sem vtable -> SIGBUS. Se o som causar
  # travamento de novo, ULTRAKILL_NO_SOUND=1 volta ao modo mudo para isolar.
  # Ajustes finos do mixer: ULTRAKILL_AUDIO_RATE/CHANS/BUF_MS viram
  # BENDER_AUDIO_SAMPLE_RATE/CHANNELS/FRAMES_PER_BUFFER abaixo.
  ULTRAKILL_NO_SOUND="${ULTRAKILL_NO_SOUND:-0}"
  export BENDER_FMOD_FORCE_NOSOUND="$ULTRAKILL_NO_SOUND"
  [ -n "${ULTRAKILL_AUDIO_RATE:-}" ]   && export BENDER_AUDIO_SAMPLE_RATE="$ULTRAKILL_AUDIO_RATE"
  [ -n "${ULTRAKILL_AUDIO_CHANS:-}" ]  && export BENDER_AUDIO_CHANNELS="$ULTRAKILL_AUDIO_CHANS"
  [ -n "${ULTRAKILL_AUDIO_BUF_MS:-}" ] && export BENDER_AUDIO_FRAMES_PER_BUFFER="$ULTRAKILL_AUDIO_BUF_MS"
  # MOUSE VIRTUAL no BENDER: START+R1 liga/desliga e B e o clique (patch rev -L
  # em bender/unity_probe.c; FN+DOWN / L1+R1+DOWN continuam como alias).
  export BENDER_MOUSE_TOGGLE_START_R1="${BENDER_MOUSE_TOGGLE_START_R1:-1}"
  export BENDER_MOUSE_CLICK_B="${BENDER_MOUSE_CLICK_B:-1}"
  echo ">> BENDER INPUT: mouse START+R1=$BENDER_MOUSE_TOGGLE_START_R1 clique-B=$BENDER_MOUSE_CLICK_B | AUDIO nosound=$BENDER_FMOD_FORCE_NOSOUND rate=${BENDER_AUDIO_SAMPLE_RATE:-auto}"
  # PRESENT POR FRAMEBUFFER (a CAUSA RAIZ da tela preta 'sem imagem'). No log
  # -H o loop roda e host_presents sobe, mas o SDL kmsdrm falha com
  # 'The specified window has not been made current' (~680x) inclusive no
  # SWAP_INTERVAL => o jogo renderiza mas NADA chega ao painel (cursor). O probe
  # traz um backend feito p/ ULTRAKILL (strings BENDER_UK_PRESENT): glReadPixels
  # do backbuffer e escreve DIRETO em /dev/fb0 ('CRTC intacto'), sem depender do
  # flip do SDL. Esta carta tem /dev/fb0 escrevivel. So AV (present), nao mexe em
  # logica. ULTRAKILL_FB_PRESENT=off desliga (volta ao SDL-only).
  ULTRAKILL_FB_PRESENT="${ULTRAKILL_FB_PRESENT:-1}"
  export BENDER_UK_PRESENT="$ULTRAKILL_FB_PRESENT"
  # FALTA do -I: so BENDER_UK_PRESENT=1 NAO bastou (zero linhas [BENDER_UK_PRESENT]
  # no log). Quem ROTEIA a saida pro framebuffer e o segundo knob BENDER_UK_KMS,
  # que aceita FB / GL / AUTO / NONE (strings g_uk_gl/g_uk_bl no probe). Default e
  # GL (usa o GBM/KMS que nao estava aparecendo). FB = desenha e da flip via
  # /dev/fb0 ('CRTC intacto'), ignorando o page-flip do SDL que dava 'window not
  # current'. ULTRAKILL_FB_KMS=GL volta ao modo antigo.
  ULTRAKILL_FB_KMS="${ULTRAKILL_FB_KMS:-FB}"
  export BENDER_UK_KMS="$ULTRAKILL_FB_KMS"
  # SDK Android reportado ao Unity (25 = mesmo do port modelo; nao afeta logica).
  export BENDER_ANDROID_SDK_INT="${BENDER_ANDROID_SDK_INT:-25}"
  # Se ha blob Mali, aponta o EGL proprio do probe para o libEGL do alias dir.
  if [ "${ULTRAKILL_GL_BACKEND:-mali}" = "mali" ] && [ -n "${ULTRAKILL_GPU_ALIAS_DIR:-}" ] && [ -f "$ULTRAKILL_GPU_ALIAS_DIR/libEGL.so" ]; then
    export BENDER_EGL_LIBRARY="$ULTRAKILL_GPU_ALIAS_DIR/libEGL.so"
  fi
  # Forca o nivel de qualidade Unity mais BAIXO via PlayerPrefs (SO config - nao
  # edita shader compilado nem logica). Com o pref ausente o Unity ate cai em 0,
  # mas escrever 0 explicito garante QualitySettings.SetQualityLevel(0,true) no
  # boot, que aplica as variantes de shader mais baratas (resolucao/cascade de
  # sombra menores, post mais leve). ULTRAKILL_FORCE_LOW_QUALITY=0 desliga.
  ULTRAKILL_FORCE_LOW_QUALITY="${ULTRAKILL_FORCE_LOW_QUALITY:-0}"
  UK_PREFS="$PORTDIR/data/files/bender_shared_prefs.tsv"
  QKHEX="556e69747947726170686963735175616c697479"   # hex(UnityGraphicsQuality)
  PKHEX="61726b626f7867616d65732e756c7472616b696c6c2e76322e706c617965727072656673"
  if [ "$ULTRAKILL_FORCE_LOW_QUALITY" = "1" ] && [ -f "$UK_PREFS" ]; then
    grep -q "$QKHEX" "$UK_PREFS" 2>/dev/null || \
      printf 'I|%s|%s|30\n' "$PKHEX" "$QKHEX" >> "$UK_PREFS" 2>/dev/null || true
    echo "prefs: UnityGraphicsQuality fixado em 0 (qualidade minima / shaders baratos)"
  fi
  echo ">> BENDER VIDEO: render_scale75=$BENDER_RENDER_SCALE_75 host_present=$BENDER_FRAME_HOST_PRESENT pacing=$BENDER_FRAME_PACING_MODE intro_skip=$BENDER_VIDEO_INTRO_SKIP lowq=$ULTRAKILL_FORCE_LOW_QUALITY nosound=$BENDER_FMOD_FORCE_NOSOUND uk_present=$BENDER_UK_PRESENT uk_kms=$BENDER_UK_KMS egl=${BENDER_EGL_LIBRARY:-auto} sdk=$BENDER_ANDROID_SDK_INT"
  # ---------------------------------------------------------------------------
  echo ">> exec BENDER: $PROBE --dir $PLIB --app $PORTDIR --phase $BENDER_PHASE"
  # kill switch START+SELECT pelo mesmo caminho do port modelo (gptokeyb do
  # PortMaster). Sem GPTOKEYB no env (port rodado fora do ES) so ignora.
  GPTK=""
  if [ -n "${GPTOKEYB:-}" ] && [ -f "$PORTDIR/unity_probe.gptk" ]; then
    ( "$GPTOKEYB" unity_probe.aarch64 -c "$PORTDIR/unity_probe.gptk" >/dev/null 2>&1 ) &
    GPTK=$!
    echo ">> gptokeyb pid $GPTK (kill switch START+SELECT)"
  fi
  # O cartao e exFAT sem journal e o device vem sendo puxado sem ejectar: dois
  # unity_probe.log SUMIRAM do cartao (ficaram no page cache). Um sync periodico
  # custa dois centavos e garante que a evidencia sobrevive ate desligamento bruto.
  SYNPID=""
  if command -v sync >/dev/null 2>&1; then
    ( while :; do sync 2>/dev/null; sleep 3; done ) & SYNPID=$!
  fi
  # ---- TELA DE PROGRESSO (loading screen) -------------------------------
  # O launcher redireciona stdout p/ LOG (exec > LOG), entao enquanto o BENDER
  # faz o warmup lento do 1o frame a tela fica SO com o cursor piscando. Este
  # watcher mantem um status AO VIVO via /tmp/status_message.txt (que a ES do
  # ArkOS mostra por cima) e /dev/console, DERIVADO do proprio log: tempo
  # decorrido, fase do boot e n. de frames. Some sozinho quando o jogo assume a
  # tela. So feedback de launcher, nao mexe no jogo nem na logica. Desliga com
  # ULTRAKILL_PROGRESS=0 no ultrakill.conf.
  ULTRAKILL_PROGRESS="${ULTRAKILL_PROGRESS:-1}"
  PROGPID=""
  if [ "$ULTRAKILL_PROGRESS" = "1" ]; then
    (
      _t0=$(date +%s 2>/dev/null || echo 0)
      while :; do
        _now=$(date +%s 2>/dev/null || echo 0)
        _el=$(( _now - _t0 ))
        _fr=$(grep -c 'eglMakeCurrent' "$LOG" 2>/dev/null); [ -z "$_fr" ] && _fr=0
        _st="preparando GPU (Mali)"
        if   grep -q 'RENDER_STAGE\]\[BEGIN' "$LOG" 2>/dev/null; then _st="1o frame: carregando/compilando shaders"
        elif grep -q 'CREATE_CONTEXT\|GL_VERSION' "$LOG" 2>/dev/null; then _st="criando contexto GL"
        elif grep -q 'exec BENDER' "$LOG" 2>/dev/null; then _st="iniciando loader BENDER"
        fi
        [ "${_fr:-0}" -gt 2 ] 2>/dev/null && _st="renderizando - imagem deve aparecer"
        _msg="ULTRAKILL carregando ${_el}s | ${_st} | gl=${_fr}"
        printf '%s\n' "$_msg" > /tmp/status_message.txt 2>/dev/null || true
        ( printf '%s\n' "$_msg" > /dev/console ) 2>/dev/null || true
        ( printf '%s\r\n' "$_msg" >&3 ) 2>/dev/null || true
        sleep 1
      done
    ) & PROGPID=$!
  fi
  (
    cd "$PORTDIR" || exit 91
    # BENDER_MAX_SEC: teto do modo contínuo. SEM teto, se o jogo travar de tela
    # preta o port nunca devolve o ES (foi o "nao voltou para a lista").
    # 143 (SIGTERM) e tratado adiante como saida limpa.
    # 600s: o primeiro boot do ULTRAKILL neste RK3326 compila shaders do 1o
    # frame + carrega Addressables e pode passar de 300s antes de dar imagem.
    BMS=${BENDER_MAX_SEC:-0}
    if [ "$BMS" = 0 ]; then
      # sem teto proprio: deixa o watchdog do probe decidir (idle/absolute), e o
      # timeout externo so corta em BENDER_HARD_TIMEOUT_SEC para nunca virar tela
      # eterna. No TSP 1200 s da para carregar catalog + compilar shader do 1o
      # boot num SD frio sem o launcher matar o processo no meio.
      BMS=$(( ${BENDER_RENDER_ABSOLUTE_WATCHDOG_SEC:-900} + 300 ))
    fi
    if command -v timeout >/dev/null 2>&1 && [ "${BENDER_PHASE:-}" = "unity-run" ]; then
      timeout -s TERM "$BMS" "$PROBE" --dir "$PLIB" --app "$PORTDIR" --phase "$BENDER_PHASE" \
        --log "$PORTDIR/unity_probe.log"
    else
      timeout -s INT 300 "$PROBE" --dir "$PLIB" --app "$PORTDIR" --phase "$BENDER_PHASE" \
        --log "$PORTDIR/unity_probe.log" 2>/dev/null ||
      "$PROBE" --dir "$PLIB" --app "$PORTDIR" --phase "$BENDER_PHASE" \
        --log "$PORTDIR/unity_probe.log"
    fi
  )
  BRC=$?
  if [ -n "$PROGPID" ]; then kill "$PROGPID" 2>/dev/null || true; fi
  printf 'ULTRAKILL - finalizado (exit %s)\n' "$BRC" > /tmp/status_message.txt 2>/dev/null || true
  if [ -n "$SYNPID" ]; then kill "$SYNPID" 2>/dev/null || true; fi
  sync 2>/dev/null || true
  if [ -n "$GPTK" ]; then kill "$GPTK" 2>/dev/null || true; fi
  echo ">> BENDER exit $BRC"
  # Limpa o dir de aliases Mali em tmpfs (nao bagunca o SD; e so /dev/shm).
  case "${ULTRAKILL_GPU_ALIAS_DIR:-}" in
    /dev/shm/ultrakill-gpu-*|"$STATEDIR"/.gpu-alias-*) rm -rf "$ULTRAKILL_GPU_ALIAS_DIR" 2>/dev/null || true ;;
  esac
  if [ -f "$PORTDIR/unity_probe.log" ]; then
    echo "===== UNITY_PROBE.LOG (ultimas 80 linhas) ====="
    tail -80 "$PORTDIR/unity_probe.log"
    echo "===== FIM UNITY_PROBE.LOG ====="
  fi
  case "$BRC" in
    0|124|125|143)
      echo ">> BENDER concluiu sem crash de sinal (exit $BRC) - NAO roda o bridge legado"
      cp -f "$PORTDIR/unity_probe.log" "$PORTDIR/unity_probe.ultimo.log" 2>/dev/null || true
      sync 2>/dev/null || true
      [ -w /proc/sys/vm/swappiness ] && echo 60 > /proc/sys/vm/swappiness 2>/dev/null
      exit $BRC ;;
    *)
      echo ">> BENDER falhou (exit $BRC); tentando o bridge legado do port" ;;
  esac
else
  echo "(loader=$ULTRAKILL_LOADER=bridge, ou sem unity_probe/unity_payload - usando o bridge legado do port)"
fi

phase "iniciando bridge"

# ---- ENV ESPECIFICA DO BRIDGE (loader nativo, codigo aberto) ----
# 1) LD_LIBRARY_PATH: shim dir PRIMEIRO (liblog/libandroid/libEGL/libOpenSLES
#    fake), depois as libs do APK (libunity/libil2cpp), depois o resto. Se o
#    libOpenSLES do SD for o stub velho (sem vtable), o bridge embarcado em
#    audio_bridge/ entra ANTES dele - senao o SIGBUS no audio volta.
if [ -f "$SHIMDIR/libOpenSLES.so" ] && ! grep -aq SL_IID_ENGINE "$SHIMDIR/libOpenSLES.so" 2>/dev/null \
   && [ -f "$PORTDIR/audio_bridge/libOpenSLES.so" ]; then
  export LD_LIBRARY_PATH="$PORTDIR/audio_bridge:$SHIMDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  echo ">> bridge: audio_bridge/ na frente do shim (stub velho detectado)"
fi
# 2) Persistencia: bridge_main le ULTRAKILL_PERSIST p/ externalDataPath.
export ULTRAKILL_PERSIST="${ULTRAKILL_BRIDGE_PERSIST:-$STATEDIR}"
# 3) O bridge pega resolucao por ULTRAKILL_INT_W/H (ja exportados acima).
# 4) DDK PowerVR: nada de surfaceless; o SDL cria a janela e o DDK apresenta.
if [ "$GL_BACKEND" = "pvr" ]; then
  export SDL_VIDEODRIVER="${SDL_VIDEODRIVER_BRIDGE:-${SDL_VIDEODRIVER:-kmsdrm}}"
  # nativeFocusChanged antes do resume ja derrubou este build em firmware sem
  # Choreographer; so pulamos se o usuario pedir (ULTRAKILL_SKIP_FOCUS=1).
  echo ">> bridge pvr: SDL_VIDEODRIVER=$SDL_VIDEODRIVER, presente por EGL nativo"
  # ULTRAKILL_NO_SDL_GL deixado DESLIGado de proposito: e o remendo do Mesa
  # software (readback glReadPixels->CPU). Com PowerVR o bridge_gl reaproveita
  # a window surface do SDL (ver wrapper/gl/bridge_gl.c) e o swap do proprio
  # Unity apresenta. Para forcar o modo software antigo: ULTRAKILL_NO_SDL_GL=1.
fi
echo ">> bridge: persist=$ULTRAKILL_PERSIST asset_root=${ULTRAKILL_ASSET_ROOT:-$GD} libunity=${ULTRAKILL_LIBUNITY:-?}"

if [ -z "$BIN" ]; then
  cat <<HELP
NAO HAH binario nativo em $GD/bin/.

O pre-flight extract/build rodou mas o bridge ELF ainda nao esta la.
Causas possiveis:
  * wrapper/build.sh nao linkou o alvo 'bridge' (cheque log de build)
  * ARCH_TAG ($LIBSUB) nao bate com o target buildado
  * o APK foi copiado mas a build nativa falhou (SDL2 dev nao instalado)

Reesecute manualmente:
  cd $PORTDIR/wrapper && bash build.txt $LIBSUB
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
# Diagnostico EGL puro (sem SDL): separa culpa mesa x culpa SDL-kmsdrm. So faz
# sentido na pilha software/Mesa; no PowerVR do TSP o egltest do port usa os
# paths Mesa e so rouba 25 s do boot - pular.
if [ -f "$PORTDIR/tools/egltest" ] && [ "$GL_BACKEND" != "pvr" ]; then
  chmod +x "$PORTDIR/tools/egltest" 2>/dev/null
  echo "== EGLTEST (mesa sem SDL) =="
  if command -v timeout >/dev/null 2>&1; then
    timeout 25 "$PORTDIR/tools/egltest"
  else
    "$PORTDIR/tools/egltest"
  fi
  echo "== EGLTEST exit $? =="
fi
# Guarda anti-trava: se o bridge hangar (tela preta eterna), timeout mata
# e devolve o controle ao ES. Busybox do ArkOS tem timeout.
RUNNER=""
command -v timeout >/dev/null 2>&1 && RUNNER="timeout -s INT ${BRIDGE_TIMEOUT_SEC:-900}"
# TELA DE PROGRESSO do bridge (o BENDER tem a sua la em cima). Sem isto o
# primeiro boot - que compila shader do PowerVR - parece tela morta.
PROGPID=""
if [ "${ULTRAKILL_PROGRESS:-1}" = "1" ]; then
  (
    _t0=$(date +%s 2>/dev/null || echo 0)
    while :; do
      _now=$(date +%s 2>/dev/null || echo 0); _el=$(( _now - _t0 ))
      _st="preparando GPU (PowerVR) + contexto EGL"
      grep -qa 'JNI_OnLoad retornou' "$LOG" 2>/dev/null && _st="engine Unity iniciando (JNI_OnLoad ok)"
      grep -qa 'chamando initJni' "$LOG" 2>/dev/null && _st="initJni (contexto, assets, audio)"
      grep -qa 'chamando nativeResume' "$LOG" 2>/dev/null && _st="nativeResume (1o frame: shaders)"
      grep -qa 'loop nativeRender' "$LOG" 2>/dev/null && _st="renderizando - imagem deve aparecer"
      _msg="ULTRAKILL ${_el}s | ${_st}"
      printf '%s\n' "$_msg" > /tmp/status_message.txt 2>/dev/null || true
      ( printf '%s\n' "$_msg" > /dev/console ) 2>/dev/null || true
      sleep 2
    done
  ) & PROGPID=$!
fi
for drv in $CANDIDATES; do
  echo ">> tentando SDL_VIDEODRIVER=$drv"
  export SDL_VIDEODRIVER="$drv"
  if command -v taskset >/dev/null 2>&1; then
    $RUNNER taskset -c 0-3 "$RUN_BIN" \
      -screen-width "$INT_W" -screen-height "$INT_H" -screen-fullscreen 1 \
      -logFile "$PORTDIR/player.log"
  else
    $RUNNER "$RUN_BIN" \
      -screen-width "$INT_W" -screen-height "$INT_H" -screen-fullscreen 1 \
      -logFile "$PORTDIR/player.log"
  fi
  RC=$?
  if [ $RC -ne 6 ] && [ $RC -ne 1 ]; then
    echo ">> driver $drv deu exit $RC (nao e mais problema de video, para loop)"
    break
  fi
  echo ">> driver $drv falhou (exit $RC), proximo..."
done
if [ -n "$PROGPID" ]; then kill "$PROGPID" 2>/dev/null || true; fi
printf 'ULTRAKILL - finalizado (exit %s)\n' "$RC" > /tmp/status_message.txt 2>/dev/null || true
RC1=$RC
DRV1="$SDL_VIDEODRIVER"

# -------------------------------------- 8b. BISECT AUTOMATICO (headless)
# Se morreu por sinal (128+7=SIGBUS, 128+11=SIGSEGV, 128+6=SIGABRT), roda UMA
# vez mais sem nenhum backend de present (sem KMS, sem fb0, sem renderer SDL):
#   - sobrevive headless  -> o sinal vem do NOSSO caminho de apresentacao
#                            (mmap do dumb buffer / blit), nao do Unity
#   - morre igual         -> o sinal vem do Unity a ler o payload (mmap
#                            past-EOF de bundle/metadata) ou do il2cpp
# Separar estes dois casos e o que falta para parar de chutar.
if [ $RC1 -ge 128 ] && [ -n "$RUNNER" ]; then
  echo ">> BISECT headless (sem KMS, sem fb0, sem renderer) - exit anterior $RC1 em $DRV1"
  echo "===== BISECT HEADLESS $(date -Is) =====" >> "$ULTRAKILL_CRASHFILE" 2>/dev/null
  unset ULTRAKILL_KMS_DIRECT
  export ULTRAKILL_NO_FB=1
  export SDL_VIDEODRIVER=offscreen
  if command -v taskset >/dev/null 2>&1; then
    $RUNNER taskset -c 0-3 "$RUN_BIN" \
      -screen-width "$INT_W" -screen-height "$INT_H" -screen-fullscreen 1 \
      -logFile "$PORTDIR/player.log"
  else
    $RUNNER "$RUN_BIN" \
      -screen-width "$INT_W" -screen-height "$INT_H" -screen-fullscreen 1 \
      -logFile "$PORTDIR/player.log"
  fi
  RC2=$?
  echo ">> BISECT headless exit $RC2"
  if [ $RC2 -eq 0 ] || [ $RC2 -eq 124 ] || [ $RC2 -lt 128 ]; then
    echo ">> VEREDITO: headless NAO morreu por sinal -> o crash esta no caminho de present (KMS/fb0), NAO no Unity"
  else
    echo ">> VEREDITO: headless morreu igual (exit $RC2) -> o crash e dentro do Unity/il2cpp (leitura do payload), nao do present"
  fi
fi

echo "== final exit $RC1, driver usado: $DRV1 $(date -Is) =="
# Unity escreveu o proprio log no SD? despeja aqui p/ lermos erros de
# boot/graphics SEM depender do rootfs.
if [ -f "$PORTDIR/player.log" ]; then
  echo "===== PLAYER.LOG (primeiras 100 linhas) ====="
  head -100 "$PORTDIR/player.log"
  echo "===== FIM PLAYER.LOG ====="
fi

# ---------------------------------------------------- 9. FORENSE DE CRASH (p/ ler no PC)
# 139=SIGSEGV 134=SIGABRT 132=SIGILL 135=SIGTRAP 136=SIGBUS 133=SIGFPE.
# Se o bridge caiu por sinal, ajuntamos EVIDENCIA NUM ARQUIVO SO no SD - o
# usuario so traz o cartao de volta e a gente abre sem precisar de rede.
CRASHF="$PORTDIR/ULTRAKILL-CRASH.txt"
rm -f "$CRASHF" 2>/dev/null || true
if [ $RC -ge 128 ] || [ $RC -eq 6 ]; then
  {
    echo "== ULTRAKILL forense $(date -Is) exit(pass1)=${RC1:-?} driver=${DRV1:-?} exit(bisect-headless)=${RC2:-nao rodou} =="
    echo "-- id / mounts --"
    id 2>&1
    grep -E ' /roms | /dev/dri| overlay' /proc/mounts 2>/dev/null
    echo "-- devices --"
    ls -l /dev/dri /dev/fb0 2>&1
    echo "-- grupos --"
    groups 2>&1
    echo "-- kernel (linhas do fault: unhandled signal / alignment / PC-LR) --"
    dmesg 2>/dev/null | tail -30 || echo "(dmesg negado)"
    dmesg 2>/dev/null | grep -iE 'ultrakill|unhandled signal|bus error|alignment|pc :|lr :' | tail -20
    echo "-- memoria --"
    head -3 /proc/meminfo 2>/dev/null
    echo "-- ultrakill-crash.log (guard: pc / si_addr / modulo+offset) --"
    cat "$PORTDIR/ultrakill-crash.log" 2>/dev/null || echo "  (vazio: ou nao houve crash, ou o guard nao ganhou a corrida)"
    echo "-- linhas-chave do log (guard/shim/bridge/audio/input) --"
    grep -aE '\[guard\]|shim/egl|shim/libEGL|bridge_gl|bridge_input|\[input\]|OpenSLES|kms:|fb0:|chamando |voltou|nativeResume|nativeInjectEvent|SIGSEGV|SIGBUS|eglGetProcAddress|present =|CONSOLE detectado|GPU backend|LOADER|VEREDITO|escrevivel' "$LOG" 2>/dev/null | tail -80
    echo "-- ultimas 60 linhas do ultrakill.log --"
    tail -60 "$LOG" 2>/dev/null
    echo "-- player.log (se existir) --"
    [ -f "$PORTDIR/player.log" ] && tail -40 "$PORTDIR/player.log"
    echo "-- FIM --"
  } > "$CRASHF" 2>/dev/null
  echo ">> forense escrito em $CRASHF"
fi

# Devolve a tela ao ES se nos quedamos com o CRTC (KMS direto).
# O ES repoe o proprio fb ao recuperar o foco, mas um modo torto pode
# deixar preto ate reiniciar: nao tocamos mais aqui, so avisamos.
[ "$RUN_BIN" != "$BIN" ] && rm -f /tmp/ultrakill

# ---------------------------------------------------- 10. RESUMO DO BOOT (p/ o usuario)
# Um bloco curto no FIM do ultrakill.log dizendo em 8 linhas o que aconteceu e
# o que fazer a seguir. Evita ter que caçar no log de 1000 linhas.
{
  echo ""
  echo "================= RESUMO DO BOOT ================="
  echo " box detectado : $BOX (gpu=$GPU, painel=$PANEL_W x $PANEL_H)"
  echo " loader        : $ULTRAKILL_LOADER   backend GL: $GL_BACKEND"
  echo " resolucao     : ${INT_W}x${INT_H}  driver SDL tentado: ${DRV1:-?}"
  echo " audio         : $AUDIO_LIB_DESC"
  echo " mouse virtual : ${ULTRAKILL_MOUSE} (${ULTRAKILL_MOUSE_TOGGLE} liga, ${ULTRAKILL_MOUSE_CLICK} clica)"
  echo " fps alvo      : ${ULTRAKILL_TARGET_FPS} | interno ${INT_W}x${INT_H} | spruce=${SPRUCE:-0} sdl_drv=${SDL_VIDEODRIVER:-?}"
  echo " exit final    : ${RC1:-?}  (0=ok, 124/125/143=tempo esgotado, >=128=sinal)"
  _frames=$(grep -ac 'loop nativeRender' "$LOG" 2>/dev/null | tr -d ' ')
  _crash=$(grep -aoE 'SIG(SEGV|BUS|ABRT)[^ ]*' "$LOG" 2>/dev/null | head -1)
  echo " chegou a renderizar? : ${_frames:-0} vez(es) do loop de render"
  echo " sinal?               : ${_crash:-nao}"
  if [ "${RC1:-0}" -ge 128 ]; then
    echo " >>> VEREDITO: caiu por SINAL. Manda ULTRAKILL-CRASH.txt + ultrakill.log"
    echo "     (o ultrakill-crash.log tem pc/si_addr/modulo+OFFSET do guard)."
  elif [ "${_frames:-0}" != "0" ] && [ "${RC1:-0}" = "0" ]; then
    echo " >>> VEREDITO: bootou e rodou. Se a tela ficou preta, o problema e"
    echo "     apresentacao (SDL_VIDEODRIVER / DDK), nao o jogo."
  fi
  echo "================================================"
} >> "$LOG" 2>&1
sync 2>/dev/null || true

# ---------------------------------------------------------------- 8. RESTAURA
# devfreq: repoe os clocks que a gente travou no topo (receita do Cuphead,
# que restaura tudo no finish/EXIT para nao deixar a ES com o SoC no teto).
if [ -n "${UK_DEVFREQ_RESTORE:-}" ] && [ -f "$UK_DEVFREQ_RESTORE" ]; then
  while read -r path old; do
    [ -n "$path" ] && [ -n "$old" ] && echo "$old" > "$path" 2>/dev/null
  done < "$UK_DEVFREQ_RESTORE"
  rm -f "$UK_DEVFREQ_RESTORE" 2>/dev/null || true
  echo ">> devfreq restaurado"
fi
[ -w /proc/sys/vm/swappiness ] && echo 60 > /proc/sys/vm/swappiness
[ -w /proc/sys/vm/vfs_cache_pressure ] && echo 100 > /proc/sys/vm/vfs_cache_pressure
exit $RC
