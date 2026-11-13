#!/usr/bin/env bash
#
# mayhem/deps.sh — run as root by mayhem/Dockerfile, before the source is copied in.
#
# swayfx 0.6 needs wlroots 0.20 and scenefx 0.5, and wlroots 0.20 in turn needs wayland >= 1.24,
# wayland-protocols >= 1.47, libdrm >= 2.4.129, libxkbcommon >= 1.8 and pixman >= 0.46, none of
# which Debian 13 ships at those versions. Each is built once from a pinned release tarball (sha256
# below) into a static prefix /opt/toolchains/deps/<name>-<version>/ (SPEC 6.2 item 19): source in
# src/, out-of-tree build with -g -gdwarf-3, no .so, uninstrumented. mayhem/build.sh then runs
# upstream's own meson against these prefixes.
set -euo pipefail

DEPS=/opt/toolchains/deps
WORK=$(mktemp -d)
export CC=clang
export CFLAGS="-O2 -g -gdwarf-3"
PC=""   # PKG_CONFIG_PATH, grown as each prefix is installed

# fetch <name> <version> <url> <sha256>  -> unpacks into $DEPS/<name>-<version>/src
fetch() {
  local d="$DEPS/$1-$2"
  mkdir -p "$d/src"
  curl -fsSL -o "$WORK/$1.tar" "$3"
  echo "$4  $WORK/$1.tar" | sha256sum -c -
  tar -xf "$WORK/$1.tar" --strip-components=1 -C "$d/src"
}

# build <name> <version> <meson options...>
build() {
  local d="$DEPS/$1-$2"; shift 2
  PKG_CONFIG_PATH="$PC" meson setup "$WORK/b-$(basename "$d")" "$d/src" --prefix="$d" --libdir=lib \
      --buildtype=plain --default-library=static "$@"
  PKG_CONFIG_PATH="$PC" meson install -C "$WORK/b-$(basename "$d")" >/dev/null
  PC="$d/lib/pkgconfig:$d/share/pkgconfig:$PC"
  chmod -R a+rX "$d"
}

fetch wayland 1.25.0 \
  https://gitlab.freedesktop.org/wayland/wayland/-/releases/1.25.0/downloads/wayland-1.25.0.tar.xz \
  c065f040afdff3177680600f249727e41a1afc22fccf27222f15f5306faa1f03
build wayland 1.25.0 -Ddocumentation=false -Dtests=false -Ddtd_validation=false

fetch wayland-protocols 1.47 \
  https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/1.47/downloads/wayland-protocols-1.47.tar.xz \
  5fd4349bcbc9bab9a46f8cf77d1f434296a7a052c87440a094f63fcf62a58e20
build wayland-protocols 1.47 -Dtests=false
# Its headers (the *-enum.h files the generated protocol headers include) are found through -I.
export CFLAGS="$CFLAGS -I$DEPS/wayland-protocols-1.47/include"

fetch libdrm 2.4.129 https://dri.freedesktop.org/libdrm/libdrm-2.4.129.tar.xz \
  5978183f978d697e26a50ba066174964efb0ab97caa1ecaa1b861fbe5ddf77dc
build libdrm 2.4.129 -Dintel=disabled -Dradeon=disabled -Damdgpu=disabled -Dnouveau=disabled \
  -Dvmwgfx=disabled -Dfreedreno=disabled -Dvc4=disabled -Detnaviv=disabled -Dexynos=disabled \
  -Domap=disabled -Dtegra=disabled -Dcairo-tests=disabled -Dman-pages=disabled -Dvalgrind=disabled \
  -Dtests=false

fetch xkbcommon 1.8.1 \
  https://github.com/xkbcommon/libxkbcommon/archive/refs/tags/xkbcommon-1.8.1.tar.gz \
  c65c668810db305c4454ba26a10b6d84a96b5469719fe3c729e1c6542b8d0d87
build xkbcommon 1.8.1 -Denable-x11=false -Denable-wayland=false -Denable-docs=false \
  -Denable-tools=false -Denable-xkbregistry=false

fetch pixman 0.46.4 https://www.cairographics.org/releases/pixman-0.46.4.tar.gz \
  d09c44ebc3bd5bee7021c79f922fe8fb2fb57f7320f55e97ff9914d2346a591c
build pixman 0.46.4 -Dtests=disabled -Ddemos=disabled -Dgtk=disabled -Dlibpng=disabled

# The libinput backend is what makes sway's input code link; no xwayland, no DRM backend (a
# compositor session is not something the targets reach).
fetch wlroots 0.20.2 \
  https://gitlab.freedesktop.org/wlroots/wlroots/-/releases/0.20.2/downloads/wlroots-0.20.2.tar.gz \
  80c567d2ed4efb2cfa6b077f22c7a710d9c78ba4e185a75e8694001114af0733
build wlroots 0.20.2 -Dexamples=false -Dxwayland=disabled -Dbackends=libinput -Drenderers=gles2 \
  -Dallocators=gbm -Dcolor-management=disabled -Dsession=enabled

fetch scenefx 0.5 https://github.com/wlrfx/scenefx/archive/refs/tags/0.5.tar.gz \
  0fa8ecca0e310f813efd052624c5ed7d9153d6a0fdead5cc957d34c07f9a86c6
build scenefx 0.5 -Dexamples=false

rm -rf "$WORK"
