#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
export PATH="$root/.tools/emsdk/upstream/emscripten:$root/.tools/msys-make/usr/bin:$PATH"
export EM_CONFIG="$(cygpath -m "$root/.tools/emsdk/.emscripten")"
mkdir -p "$root/build/ffmpeg-bink"
cd "$root/build/ffmpeg-bink"
if ! test -f kisak-configured.stamp || test "$root/tools/build_cinematic_codec.sh" -nt kisak-configured.stamp ||
    test "$root/tools/cinematic_codec.json" -nt kisak-configured.stamp ||
    test "$EM_CONFIG" -nt kisak-configured.stamp; then
    bash "$root/.tools/ffmpeg-8.0.3/configure" \
        --enable-cross-compile --target-os=none --arch=wasm32 \
        --cc=emcc --host-cc=emcc --cxx=em++ --ar=emar --ranlib=emranlib --nm=emnm \
        --disable-everything --disable-autodetect --disable-programs --disable-doc \
        --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
        --disable-network --disable-pthreads --disable-w32threads --disable-os2threads \
        --disable-asm --disable-inline-asm --disable-x86asm --disable-debug --enable-small \
        --enable-demuxer=bink --enable-decoder=bink,binkaudio_rdft,binkaudio_dct \
        --extra-cflags='-Oz -flto' --extra-ldflags='-Oz -flto'
    touch kisak-configured.stamp
fi
make -j4
make install-headers prefix="$root/build/ffmpeg-bink/install"
