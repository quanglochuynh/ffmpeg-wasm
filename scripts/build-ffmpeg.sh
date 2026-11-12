#!/usr/bin/env bash
# Builds a minimal static FFmpeg (MOV/MP4 + Matroska demuxers, H.264/HEVC/MPEG-4/VP8/VP9
# decoders, MJPEG encoder)
# for WebAssembly into build/ffmpeg. Run once; scripts/build.sh links against it.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EMSDK_ENV="$ROOT_DIR/../emsdk/emsdk_env.sh"
FFMPEG_TAG="n7.1"
SRC_DIR="$ROOT_DIR/third_party/ffmpeg"
PREFIX="$ROOT_DIR/build/ffmpeg"

if [[ ! -f "$EMSDK_ENV" ]]; then
  echo "Emscripten SDK not found at $EMSDK_ENV" >&2
  exit 1
fi
source "$EMSDK_ENV"

if [[ ! -d "$SRC_DIR" ]]; then
  git clone --depth 1 --branch "$FFMPEG_TAG" https://github.com/FFmpeg/FFmpeg.git "$SRC_DIR"
fi

cd "$SRC_DIR"
make distclean >/dev/null 2>&1 || true

emconfigure ./configure \
  --prefix="$PREFIX" \
  --target-os=none --arch=x86_32 --enable-cross-compile \
  --cc=emcc --cxx=em++ --ar=emar --ranlib=emranlib --nm=llvm-nm \
  --disable-everything --disable-programs --disable-doc \
  --disable-debug --disable-runtime-cpudetect --disable-autodetect \
  --disable-asm --disable-inline-asm --disable-stripping --disable-network \
  --disable-avdevice --disable-avfilter --disable-postproc \
  --enable-avcodec --enable-avformat --enable-avutil --enable-swscale \
  --enable-protocol=file \
  --enable-demuxer=mov,matroska \
  --enable-parser=h264,hevc,mpeg4video,vp8,vp9 \
  --enable-decoder=h264,hevc,mpeg4,vp8,vp9 \
  --enable-encoder=mjpeg \
  --extra-cflags="-O3"

emmake make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
emmake make install
