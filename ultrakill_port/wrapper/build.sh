#!/bin/bash
# build.sh - wrapper em volta do cmake. Escolhe toolchain conforme alvo.
#   ./build.sh arm64           # nativo (roda no proprio TSP se gcc existe)
#   ./build.sh arm64 --cross   # usa aarch64-linux-gnu-
#   ./build.sh armv7 --cross   # usa arm-linux-gnueabihf-
set -eu
TARGET="${1:-arm64}"
CROSS="${2:-}"

case "$TARGET" in
  arm64) TRIPLET="aarch64-linux-gnu" ; CC="aarch64-linux-gnu-gcc" ; SYSROOT="" ;;
  armv7) TRIPLET="arm-linux-gnueabihf" ; CC="arm-linux-gnueabihf-gcc" ; SYSROOT="" ;;
  *) echo "alvo invalido: $TARGET (use arm64 ou armv7)"; exit 2 ;;
esac

BUILD="build-$TARGET"
mkdir -p "$BUILD"

CMAKE_ARGS=(-S . -B "$BUILD" -DTARGET_ARCH="$TARGET" -DCMAKE_BUILD_TYPE=Release)

if [ "$CROSS" = "--cross" ]; then
  if ! command -v "$CC" >/dev/null; then
    echo "faltando toolchain: $CC (instale gcc-${TRIPLET} ou apont --toolchain)"
    exit 3
  fi
  CMAKE_ARGS+=( -DCMAKE_SYSTEM_NAME=Linux
                -DCMAKE_SYSTEM_PROCESSOR="$( [ "$TARGET" = arm64 ] && echo aarch64 || echo armv7 )"
                -DCMAKE_C_COMPILER="$CC" )
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD" -j"$(nproc)"
cmake --install "$BUILD"

echo
echo "== prontos =="
echo "stubs em: $BUILD/../../libs/$TARGET/"
ls -1 "../libs/$TARGET/" 2>/dev/null || echo "(ainda vazio - cmake --install nao rodou?)"
echo "binario em: ../gamedata/bin/"
ls -1 "../gamedata/bin/" 2>/dev/null || echo "(vazio)"
