#!/bin/sh
# probe.sh - levantamento de capacidades reais do TSP (SpruceOS) antes de escrever qualquer port.
# Uso: copiar para o SD, rodar no terminal do SpruceOS (ou via ssh) e mandar o arquivo gerado.
# Nada aqui instala ou altera nada: so leitura.

OUT="/tmp/tsp_probe_$(date +%Y%m%d_%H%M).txt"
{
echo "===== ARQUITETURA / KERNEL ====="
uname -a
arch
cat /proc/cpuinfo | grep -m1 -E 'Hardware|model name|CPU part|Features'
grep -m1 -E 'Flags' /proc/cpuinfo | grep -o -E 'armv8|asimd|evtstrm|aes|crc32' | sort -u
echo "-- binarios ELF aceitos (arquivo que existe aqui):"
for f in /bin/sh /usr/bin/env; do [ -e "$f" ] && od -An -tx1 -N5 -j18 "$f"; done
readelf -h /bin/sh 2>/dev/null | grep -i machine

echo
echo "===== MEMORIA / SWAP / CGROUPS ====="
free -m 2>/dev/null || cat /proc/meminfo | head -8
cat /proc/meminfo | grep -E 'MemTotal|MemAvailable|SwapTotal|CmaTotal|Dirty'
swapon --show 2>/dev/null
ls -l /dev/zram* 2>/dev/null
for d in /sys/block/zram*; do [ -e "$d" ] && { echo "-- $d"; cat "$d"/disksize 2>/dev/null; cat "$d"/comp_algorithm 2>/dev/null; }; done
echo "-- cgroups disponiveis (limite duro de RAM por processo):"
ls /sys/fs/cgroup 2>/dev/null | head -20
cat /proc/self/cgroup

echo
echo "===== GPU / DRM / DRIVER ====="
ls -l /dev/dri /dev/pvr 2>/dev/null
cat /sys/class/drm/card*/device/uevent 2>/dev/null
dmesg 2>/dev/null | grep -i -E 'pvr|rgx|ge8300|mali|powervr|viv' | tail -30
echo "-- EGL/GL: bibliotecas presentes"
for p in /usr/lib /usr/lib64 /lib /usr/lib/arm-linux-gnueabihf /usr/lib/aarch64-linux-gnu; do
  [ -d "$p" ] && ls "$p" 2>/dev/null | grep -E '^(libEGL|libGLESv|libOpenGL|libGLX|libvulkan|libpvr|libsunxi|libmali)' | sed "s|^|$p/|"
done
echo "-- extensoes GL (rode com um contexto offscreen se disponivel):"
which glxinfo eglinfo vulkaninfo 2>/dev/null
eglinfo -l 2>/dev/null | head -40
vulkaninfo --summary 2>/dev/null | head -30

echo
echo "===== SAIDAS DE VIDEO / DISPLAY ====="
echo "DISPLAY=$DISPLAY  WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
ls /run/ 2>/dev/null | grep -iE 'x11|wayland'
cat /sys/class/drm/card*-*/modes 2>/dev/null | head -20
fbset -s 2>/dev/null
ls /dev/fb* 2>/dev/null

echo
echo "===== AUDIO ====="
aplay -l 2>/dev/null | head -20
ls /dev/snd 2>/dev/null
pactl info 2>/dev/null | head -12

echo
echo "===== RUNITMES DE PORT ====="
for b in box86 box64 wine wine64 mono glport python3 godot retroarch scummvm; do
  printf "%-10s " "$b"; command -v "$b" || echo "(ausente)"
done
ls -d /opt/portmaster* ~/portmaster* /roms/ports 2>/dev/null

echo
echo "===== CPU GOVERNOR / LIMITES TERMICOS ====="
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
cat /sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies 2>/dev/null
sensors 2>/dev/null | head -20
cat /sys/class/thermal/thermal_zone*/type 2>/dev/null

echo
echo "===== PORTMASTER ====="
find / -maxdepth 4 -iname "*portmaster*" -not -path "/proc/*" 2>/dev/null | head -20
cat /roms/ports/*.sh 2>/dev/null | head -5

echo "===== FIM ====="
} 2>&1 | tee "$OUT"
echo "relatorio salvo em: $OUT"
