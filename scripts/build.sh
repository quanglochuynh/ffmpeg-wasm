#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EMSDK_ENV="$ROOT_DIR/../emsdk/emsdk_env.sh"

if [[ ! -f "$EMSDK_ENV" ]]; then
  echo "Emscripten SDK not found at $EMSDK_ENV" >&2
  echo "Install or move emsdk to ../emsdk, relative to the repository root." >&2
  exit 1
fi

source "$EMSDK_ENV"

mkdir -p "$ROOT_DIR/dist"
FFMPEG_DIR="$ROOT_DIR/build/ffmpeg"
if [[ ! -f "$FFMPEG_DIR/lib/libavcodec.a" ]]; then
  echo "FFmpeg not built yet. Run: bash scripts/build-ffmpeg.sh" >&2
  exit 1
fi

em++ "$ROOT_DIR/src/index.cpp" -o "$ROOT_DIR/dist/index.js" \
  --bind \
  -I"$FFMPEG_DIR/include" \
  -L"$FFMPEG_DIR/lib" \
  -lavformat -lavcodec -lswscale -lswresample -lavutil \
  -sALLOW_MEMORY_GROWTH=1 \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -O3