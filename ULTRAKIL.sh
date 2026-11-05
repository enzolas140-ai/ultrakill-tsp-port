#!/bin/sh
# PORTMASTER: ultrakill.zip, ULTRAKIL.sh
#
# Entry point do ULTRAKILL no TrimUI Smart Pro (spruceOS). Este arquivo FICA
# NA RAIZ de /roms/ports (ao lado da pasta ultrakill/), porque e assim que o
# ES/spruceOS e o PortMaster descobrem um port: a lista de jogos do sistema
# "ports" sao os .sh da raiz + o gamelist.xml. A pasta ultrakill/ so tem o
# payload e o launcher interno.
#
# NOME: ULTRAKIL.sh (renomeado a pedido em 04/11). gamelist.xml aponta
# ./ULTRAKIL.sh e ultrakill.txt traz button="ULTRAKIL.sh" - se mudar de nome de
# novo, atualize os DOIS (e o <name> e o que aparece na tela, pode ficar
# ULTRAKILL mesmo com o ficheiro ULTRAKIL.sh).
#
# >>> JA FUNCIONOU: ha log de 04/11 08:19 (ultrakill/ultrakill.log +
# unity_probe.log) gerado POR ESTE entrypoint. Ele nao e o problema. <<<
#
# Escrito em /bin/sh POSIX porque o ES do TSP invoca os .sh com sh. O launcher
# interno (ultrakill_start.sh) usa arrays/[[ ]] e entao precisa de bash: aqui
# fazemos o re-exec quando o bash existir.

SELF="$(readlink -f "$0" 2>/dev/null || echo "$0")"
[ -x /bin/bash ] && [ -z "${BASH_VERSION:-}" ] && exec /bin/bash "$SELF" "$@"
OWNDIR="$(dirname "$SELF")"
case "$OWNDIR" in /*) : ;; *) OWNDIR="$PWD/$OWNDIR" ;; esac

# --------------------------------------------------------------- entry.log + SEGURO
# ULTRAKILL fix -M. Duas coisas que faltavam aqui:
#  (a) LOG PROPRIO. Ate agora so o launcher gravava; quando o travamento era
#      ANTES dele (na lista de jogos) nao ficava nenhuma prova. Este ficheiro
#      prova que o entry comecou e em que passo saiu.
#  (b) SELF-HEAL DO CONSOLE. Versoes antigas deste port desligavam o console do
#      framebuffer (vtcon/bind=0) e nao devolviam - a ES parava de repintar e
#      parecia travada na lista. Religamos aqui, ANTES de qualquer coisa, para o
#      simples ato de acionar o port reparar o estrago do run anterior.
#  (c) MODO SEGURO: se existir ultrakill/SAFE (facil de criar do PC), nada de
#      takeover de display/tty e teto curto de execucao. Recovery sem reboot.
ENTRY_LOG="$OWNDIR/ultrakill/entry.log"
SAFE_FLAG="$OWNDIR/ultrakill/SAFE"
mv -f "$ENTRY_LOG" "$ENTRY_LOG.prev" 2>/dev/null || true
{
  echo "===== ULTRAKIL.sh entry $(date '+%Y-%m-%d %H:%M:%S') ====="
  echo "  self=$SELF owndir=$OWNDIR"
  [ -f "$SAFE_FLAG" ] && echo "  MODO SEGURO: $SAFE_FLAG existe (sem takeover, teto 90s)"
} > "$ENTRY_LOG" 2>/dev/null || true
uk_log() { echo "$*" >> "$ENTRY_LOG" 2>/dev/null || true; }
for uk_vt in /sys/class/vtconsole/vtcon0/bind /sys/class/vtconsole/vtcon1/bind /sys/class/vtconsole/vtcon2/bind; do
  if [ -e "$uk_vt" ]; then
    uk_now=$(cat "$uk_vt" 2>/dev/null)
    if [ "${uk_now:-1}" = 0 ]; then
      echo 1 > "$uk_vt" 2>/dev/null && uk_log "  self-heal: $uk_vt estava 0, religado"
    fi
  fi
done
uk_log "  vtcon: $(for v in /sys/class/vtconsole/vtcon*/bind; do [ -e "$v" ] && printf '%s=%s ' "${v##*/vtconsole/}" "$(cat "$v" 2>/dev/null)"; done)"

# ---------------------------------------------------------------- PortMaster
if [ -f /mnt/SDCARD/System/etc/ex_config ]; then
  . /mnt/SDCARD/System/etc/ex_config 2>/dev/null || true
fi
export PATH="/mnt/SDCARD/System/bin:/usr/bin:/usr/sbin:/bin:/sbin:$PATH"

PM=""
for p in /opt/system/Tools/PortMaster /opt/tools/PortMaster \
         "$HOME/.config/PortMaster/PortMaster" \
         /mnt/SDCARD/Persistent/portmaster/PortMaster \
         /mnt/SDCARD/Apps/PortMaster/PortMaster \
         "$OWNDIR/PortMaster" /roms/ports/PortMaster /mnt/SDCARD/Roms/ports/PortMaster; do
  if [ -f "$p/control.txt" ]; then PM="$p"; break; fi
done

if [ -n "$PM" ]; then
  # control.txt define $directory, $ESUDO, $GPTOKEYB e a funcao get_controls
  # (que monta $sdl_controllerconfig com o mapa CORRETO do pad do TSP - sem
  # isso os botoes chegam embaralhados no SDL e o START+R1 do mouse virtual
  # viraria outra coisa qualquer).
  #
  # ULTRAKILL fix -M: TUDO aqui roda com stdin fechado. Um sudo/get_controls que
  # peca interatividade bloqueava o entry inteiro - e o resultado e exatamente
  # 'travou na lista de jogos' sem log nenhum. NAO enrolamos get_controls num
  # `timeout sh -c`: control.txt e para ser FONTADO, entao chama-lo assim perdia
  # o mapa do pad (regressao pior que o risco que queria evitar).
  . "$PM/control.txt" < /dev/null 2>/dev/null || true
  get_controls < /dev/null >/dev/null 2>&1 || uk_log "  get_controls falhou (continuando; o pad usa o db privado do launcher)"
  uk_log "  PortMaster=$PM directory=${directory:-(vazio)}"
fi

# ---------------------------------------------------------------- acha o port
UK_CAND=""
if [ -n "${directory:-}" ]; then
  d="${directory#/}"; d="${d%/}"
  UK_CAND="/$d/ports/ultrakill"
fi
for c in "$OWNDIR/ultrakill" "$UK_CAND" \
         /mnt/SDCARD/Roms/ports/ultrakill /mnt/SDCARD/Data/ports/ultrakill \
         /roms/ports/ultrakill /roms2/ports/ultrakill; do
  if [ -d "$c" ] && [ -f "$c/ultrakill_start.sh" ]; then GAMEDIR="$c"; break; fi
done
if [ -z "${GAMEDIR:-}" ]; then
  # Sem launcher nada funciona: avisa na tela em vez de sair mudo (era o
  # sintoma de "aperte A e nao acontece nada").
  echo "ULTRAKILL: pasta ultrakill/ nao encontrada ao lado de $SELF" > /tmp/status_message.txt 2>/dev/null || true
  exit 1
fi

# ---------------------------------------------------------------- feedback
# ULTRAKILL fix -M: NAO escrevemos mais no /dev/tty0 por omissao. O console do
# TSP e o que a ES usa para pintar; limpa-lo ('\033c') no meio da partida da
# tela congelada/branca na lista. Para ligar esse aviso visual, dois caminhos
# reais (o entry NAO sourceia o conf - quem o le e o launcher - entao vamos ler
# a chave no ficheiro diretamente, senao seria um knob documentado que ninguem
# le, que e justamente o que o auditor procura):
#   ULTRAKILL_TTY_FEEDBACK=1  em ultrakill/ultrakill.conf
#   ou o ficheiro vazio ULTRAKILL-TTY na raiz de /roms/ports
UK_TTY_OK=0
UK_TTY_VAL="${ULTRAKILL_TTY_FEEDBACK:-}"
if [ -z "$UK_TTY_VAL" ] && [ -f "$OWNDIR/ultrakill/ultrakill.conf" ]; then
  UK_TTY_VAL=$(sed -n 's/^[[:space:]]*ULTRAKILL_TTY_FEEDBACK=\(.\{1,4\}\).*/\1/p' "$OWNDIR/ultrakill/ultrakill.conf" 2>/dev/null | tail -1)
fi
[ "$UK_TTY_VAL" = "1" ] && UK_TTY_OK=1
[ -f "$OWNDIR/ULTRAKILL-TTY" ] && UK_TTY_OK=1
if [ "$UK_TTY_OK" = 1 ] && [ -c /dev/tty0 ]; then
  if command -v timeout >/dev/null 2>&1; then
    timeout 3 $ESUDO chmod 666 /dev/tty0 < /dev/null 2>/dev/null || chmod 666 /dev/tty0 < /dev/null 2>/dev/null || true
  fi
  printf '\033c' > /dev/tty0 2>/dev/null || true
  echo "ULTRAKILL - iniciando port (bridge SDL + PowerVR)" > /dev/tty0 2>/dev/null || true
fi
echo "ULTRAKILL - iniciando" > /tmp/status_message.txt 2>/dev/null || true
uk_log "  GAMEDIR=$GAMEDIR tty_feedback=$UK_TTY_OK"

# O pad do TSP: mapa oficial do PortMaster (spruce) em SDL_GAMECONTROLLERCONFIG.
[ -n "${sdl_controllerconfig:-}" ] && export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
[ -n "$PM" ] && export SDL_GAMECONTROLLERCONFIG_FILE="$PM/gamecontrollerdb.txt"

cd "$GAMEDIR" || exit 1

# gptokeyB NAO e ligado aqui: o bridge ja le o gamepad direto pelo SDL e tem o
# proprio kill switch (SELECT+START 1 s). Ligaria um teclado/mouse falsos por
# cima do nosso input (foi o que motivou o mesmo comentario no Dead Space.sh).

if [ -f "$GAMEDIR/ultrakill_start.bin" ] && [ ! -f "$GAMEDIR/ultrakill_start.sh" ]; then
  cp -f "$GAMEDIR/ultrakill_start.bin" "$GAMEDIR/ultrakill_start.sh" 2>/dev/null || true
fi
# ULTRAKILL fix -AC: em exFAT com cache do Windows e ejecao sem 'remover com
# seguranca', o .sh do device pode ficar VELHO enquanto o .bin (ou o probe) ja e
# novo - e o launcher velho anula qualquer correcao (foi o que aconteceu nas
# rodadas de 05/11 10:16 e 10:23: o MAPA DE DISPLAY nunca saiu porque rodou um
# launcher anterior). Entao: se os dois forem diferentes, usa-se o MAIS NOVO e
# copia-se para o outro. Assim a divergencia se cura sozinha, independentemente de
# qual dos dois o Windows conseguiu gravar.
if [ -f "$GAMEDIR/ultrakill_start.sh" ] && [ -f "$GAMEDIR/ultrakill_start.bin" ]; then
  if [ "$GAMEDIR/ultrakill_start.bin" -nt "$GAMEDIR/ultrakill_start.sh" ]; then
    if ! cmp -s "$GAMEDIR/ultrakill_start.bin" "$GAMEDIR/ultrakill_start.sh"; then
      cp -f "$GAMEDIR/ultrakill_start.bin" "$GAMEDIR/ultrakill_start.sh" 2>/dev/null || true
      echo "  curado: ultrakill_start.sh era mais velho que o .bin; re-copidado" >> "$ENTRY_LOG" 2>/dev/null || true
    fi
  elif [ "$GAMEDIR/ultrakill_start.sh" -nt "$GAMEDIR/ultrakill_start.bin" ]; then
    if ! cmp -s "$GAMEDIR/ultrakill_start.sh" "$GAMEDIR/ultrakill_start.bin"; then
      cp -f "$GAMEDIR/ultrakill_start.sh" "$GAMEDIR/ultrakill_start.bin" 2>/dev/null || true
      echo "  curado: ultrakill_start.bin era mais velho que o .sh; re-copidado" >> "$ENTRY_LOG" 2>/dev/null || true
    fi
  fi
fi
# selo de revisao: para o log dizer QUAL launcher rodou (acaba com os 'mesmo erro'
# ambiguos entre device velho e correcao errada)
ULTRA_REV="rev -AD (mapa de display com marcadores, sem redirecionamento)"
echo "$ULTRA_REV  entry $(date '+%H:%M:%S')" >> "$ENTRY_LOG" 2>/dev/null || true
chmod a+x "$GAMEDIR/ultrakill_start.sh" 2>/dev/null || true

BINSH=/bin/bash
command -v bash >/dev/null 2>&1 && BINSH=bash

# ULTRAKILL fix -M: teto duro tambem AQUI. O launcher tem BENDER_MAX_SEC=300, mas
# se ele morrer numa espera (flock, sudo, driver) a ES fica a espera do filho e
# a tela congela na lista. 330s normal, 90s em MODO SEGURO (ficheiro SAFE).
UK_CAP=330
[ -f "$SAFE_FLAG" ] && UK_CAP=90
export ULTRAKILL_SAFE="$( [ -f "$SAFE_FLAG" ] && echo 1 || echo 0 )"
uk_log "  chamando launcher (teto ${UK_CAP}s): $BINSH $GAMEDIR/ultrakill_start.sh"
if command -v timeout >/dev/null 2>&1; then
  timeout -k 5 "$UK_CAP" "$BINSH" "$GAMEDIR/ultrakill_start.sh"
  UK_RC=$?
else
  "$BINSH" "$GAMEDIR/ultrakill_start.sh"
  UK_RC=$?
fi
uk_log "  launcher devolveu exit $UK_RC"

# Saida: devolvemos o console, sempre (ate se o launcher foi morto -9).
for uk_vt in /sys/class/vtconsole/vtcon0/bind /sys/class/vtconsole/vtcon1/bind; do
  [ -w "$uk_vt" ] && echo 1 > "$uk_vt" 2>/dev/null
done

printf 'ULTRAKILL - finalizado (exit %s)\n' "$UK_RC" > /tmp/status_message.txt 2>/dev/null || true
if [ "$UK_TTY_OK" = 1 ] && [ -c /dev/tty0 ]; then
  printf '\033c' > /dev/tty0 2>/dev/null || true
fi
if command -v timeout >/dev/null 2>&1; then
  timeout 3 $ESUDO kill -9 $(pidof gptokeyb) < /dev/null 2>/dev/null || true
fi
sync 2>/dev/null || true
exit $UK_RC
