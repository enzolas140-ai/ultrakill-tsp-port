#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PORTROOT="$(cd "$HERE/.." && pwd)"
BOOTLIN_NAME="aarch64--glibc--stable-2020.02-2"
BOOTLIN_SHA256="55eb1a29c01c4cc92943acdecf87e7cfa7d6c33b025faf0867f98c6c9cbc0bca"
BOOTLIN_URL="https://toolchains.bootlin.com/downloads/releases/toolchains/aarch64/tarballs/${BOOTLIN_NAME}.tar.bz2"
CACHE="${BENDER_TOOLCHAIN_CACHE:-$HOME/.cache/bender-port-builder/toolchains}"
TC_DIR="$CACHE/$BOOTLIN_NAME"
TARBALL="$CACHE/${BOOTLIN_NAME}.tar.bz2"
for tool in curl tar sha256sum find file; do
  command -v "$tool" >/dev/null 2>&1 || { echo "[ERRO] Ferramenta WSL ausente: $tool"; exit 10; }
done
mkdir -p "$CACHE"
if [ ! -f "$TC_DIR/.bender_ready" ]; then
  echo "[INFO] Baixando toolchain AArch64 (primeira vez apenas)..."
  if [ ! -f "$TARBALL" ]; then
    curl -fL --retry 4 --retry-delay 2 --connect-timeout 20 -o "$TARBALL.part" "$BOOTLIN_URL"
    mv -f "$TARBALL.part" "$TARBALL"
  fi
  ACTUAL="$(sha256sum "$TARBALL" | cut -d' ' -f1)"
  if [ "$ACTUAL" != "$BOOTLIN_SHA256" ]; then
    echo "[ERRO] SHA-256 do toolchain invalido."; rm -f "$TARBALL"; exit 11
  fi
  rm -rf "$TC_DIR"; mkdir -p "$TC_DIR"
  tar -xjf "$TARBALL" -C "$TC_DIR" --strip-components=1
  touch "$TC_DIR/.bender_ready"
fi
CC="$(find -L "$TC_DIR" -type f -path '*/bin/*-gcc' -perm -u+x 2>/dev/null | head -n 1 || true)"
[ -n "$CC" ] || { echo "[ERRO] gcc AArch64 nao encontrado em $TC_DIR"; exit 12; }
READELF="${CC%-gcc}-readelf"
[ -x "$READELF" ] || READELF="$(command -v readelf || true)"
echo "[OK] Compiler Unity probe: $CC"
"$CC" -dumpmachine
"$CC" -O2 -fPIE -pie -Wall -Wextra -Werror -Wformat-truncation=2 -Werror=misleading-indentation -Werror=multichar \
  "$HERE/unity_probe.c" -o "$PORTROOT/unity_probe.aarch64" -ldl -pthread
chmod +x "$PORTROOT/unity_probe.aarch64"
mkdir -p "$PORTROOT/audio_bridge"
"$CC" -O2 -fPIC -shared -Wall -Wextra -Werror -Wformat-truncation=2 -Werror=misleading-indentation -Werror=multichar "$HERE/bender_opensles.c" -o "$PORTROOT/audio_bridge/libOpenSLES.so" -ldl -pthread
echo "[OK] OpenSL ES -> SDL2 bridge: $PORTROOT/audio_bridge/libOpenSLES.so"
echo "============================================================"
echo "[OK] UNITY IL2CPP AARCH64 DISPLAY/JNI-ABI PROBE CRIADO"
file "$PORTROOT/unity_probe.aarch64" 2>/dev/null || true
if [ -n "$READELF" ]; then
  "$READELF" -h "$PORTROOT/unity_probe.aarch64" 2>/dev/null | grep -E 'Class:|Machine:|Type:' || true
  echo "[INFO] GLIBC requeridas:"
  "$READELF" --version-info "$PORTROOT/unity_probe.aarch64" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -Vu || true
fi
echo "Arquivo: $PORTROOT/unity_probe.aarch64"
echo "============================================================"
