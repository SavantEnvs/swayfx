#!/usr/bin/env bash
#
# mayhem/build.sh — run upstream's own meson build twice (instrumented, and with the project's
# normal flags), then link the fuzz harnesses, their standalone reproducers and the selftest
# against the objects it produced.
#
# swayfx needs wlroots 0.20 and scenefx 0.5, which Debian lacks; mayhem/deps.sh (run by the
# Dockerfile) built them, and what they need, into static prefixes under /opt/toolchains/deps.
# Runs offline: nothing is fetched here. Upstream is not edited.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
B="$SRC/build-mayhem"
rm -rf "$B"
mkdir -p "$B/fuzz" "$B/test"

DEPS=/opt/toolchains/deps
export PKG_CONFIG_PATH=""
for d in scenefx-0.5 wlroots-0.20.2 pixman-0.46.4 xkbcommon-1.8.1 libdrm-2.4.129 \
         wayland-protocols-1.47 wayland-1.25.0; do
  PKG_CONFIG_PATH="$DEPS/$d/lib/pkgconfig:$DEPS/$d/share/pkgconfig:$PKG_CONFIG_PATH"
done
# The *-enum.h headers of wayland-protocols are found through -I, not pkg-config.
DEP_CFLAGS="-I$DEPS/wayland-protocols-1.47/include"

# meson runs the compiler's own configure-time test programs; ASan would fail them on leaks.
MESON_OPTS=(--buildtype=plain -Dwerror=false -Dtray=disabled -Dgdk-pixbuf=disabled -Dman-pages=disabled
            -Ddefault-wallpaper=false -Dzsh-completions=false -Dbash-completions=false
            -Dfish-completions=false)
# scenefx's static library carries its own copies of a few wlroots functions; the shared libraries
# upstream links against hide that, a static link needs it said.
LINK_FIX="-Wl,--allow-multiple-definition"

# 1) Instrumented build: ASan+UBSan (halting), DWARF 3, SanCov on all of upstream's code.
FUZZ_CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1 $DEP_CFLAGS"
ASAN_OPTIONS=detect_leaks=0 CFLAGS="$FUZZ_CFLAGS" LDFLAGS="$SANITIZER_FLAGS $LINK_FIX" \
  meson setup "$B/fuzz-meson" "${MESON_OPTS[@]}"
ASAN_OPTIONS=detect_leaks=0 meson compile -C "$B/fuzz-meson" -j "$MAYHEM_JOBS"

# 2) The project's normal build, for the selftest: no sanitizers, no -gdwarf-3.
CFLAGS="-O1 $COVERAGE_FLAGS $DEP_CFLAGS" LDFLAGS="$COVERAGE_FLAGS $LINK_FIX" \
  meson setup "$B/test-meson" "${MESON_OPTS[@]}"
meson compile -C "$B/test-meson" -j "$MAYHEM_JOBS"

# --- harness compile/link flags ------------------------------------------------------------------
DEFS=(-std=c11 -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L
      '-DSWAY_VERSION="0.6"' '-DSWAY_ORIGINAL_VERSION="1.12.0"' '-DSYSCONFDIR="/etc"'
      -Wno-unused-parameter -Wno-unused-result -Wno-missing-braces -Wno-format-zero-length)
PKGS=(json-c pango pangocairo cairo wayland-client wayland-cursor)
read -r -a PKG_CFLAGS <<< "$(pkg-config --cflags "${PKGS[@]}")"
read -r -a PKG_LIBS <<< "$(pkg-config --static --libs "${PKGS[@]}")"
PKG_LIBS+=(-lm -lrt)

# objs <meson build dir> <target>: the compiled upstream code a harness links, as upstream's
# meson built it. swaybar: everything but main(). swaynag: the option/config/type code the
# harnesses call (its renderer needs a compositor).
bar_objs()   { find "$1/swaybar/swaybar.p" -name '*.o' ! -name 'main.c.o' | sort; }
nag_objs()   { printf '%s\n' "$1/swaynag/swaynag.p/config.c.o" "$1/swaynag/swaynag.p/types.c.o"; }
# Upstream's static libraries, pulled as needed.
libs()       { echo "$1/client/libsway-client.a"; echo "$1/common/libsway-common.a"; }

# per-target upstream objects: fuzz_i3bar -> swaybar; swaynag_* -> swaynag; the rest: common only.
target_objs() {
  case "$1" in
    fuzz_i3bar) bar_objs "$2" ;;
    fuzz_swaynag_args|fuzz_swaynag_config) nag_objs "$2" ;;
  esac
}
TARGETS=(fuzz_i3bar fuzz_swaymsg_pretty fuzz_swaynag_args fuzz_swaynag_config fuzz_stringop)
INCS=(-I"$SRC/include" -I"$SRC/mayhem/harnesses")

# LeakSanitizer off for every ASan binary, at build time (leaks are not what is fuzzed here).
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/fuzz/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -x c -c "$STANDALONE_FUZZ_MAIN" -o "$B/fuzz/standalone_main.o"

# 3) Harnesses: the fuzzer binary, and a run-once standalone reproducer.
FM="$B/fuzz-meson"
for t in "${TARGETS[@]}"; do
  mapfile -t OBJS < <(target_objs "$t" "$FM")
  $CC $FUZZ_CFLAGS "${DEFS[@]}" "${INCS[@]}" -I"$FM/include" -I"$FM/protocols" "${PKG_CFLAGS[@]}" \
      -c "$SRC/mayhem/harnesses/$t.c" -o "$B/fuzz/$t.o"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE \
      "$B/fuzz/$t.o" "${OBJS[@]}" $(libs "$FM") "$B/fuzz/lsan_off.o" "${PKG_LIBS[@]}" -o "$SRC/$t"
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS \
      "$B/fuzz/$t.o" "$B/fuzz/standalone_main.o" "${OBJS[@]}" $(libs "$FM") "$B/fuzz/lsan_off.o" \
      "${PKG_LIBS[@]}" -o "$SRC/$t-standalone"
done

# 4) The selftest: swaybar and swaynag code from the normal build, no sanitizers.
TM="$B/test-meson"
mapfile -t OBJS < <(bar_objs "$TM"; nag_objs "$TM")
$CC -O1 $COVERAGE_FLAGS $DEP_CFLAGS "${DEFS[@]}" "${INCS[@]}" -I"$TM/include" -I"$TM/protocols" \
    "${PKG_CFLAGS[@]}" -c "$SRC/mayhem/harnesses/selftest.c" -o "$B/test/selftest.o"
$CC $COVERAGE_FLAGS "$B/test/selftest.o" "${OBJS[@]}" $(libs "$TM") "${PKG_LIBS[@]}" \
    -o "$SRC/swayfx_selftest"
