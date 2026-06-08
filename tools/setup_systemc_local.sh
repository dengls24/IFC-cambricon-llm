#!/usr/bin/env bash
set -eu

PROJECT_ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
DEST_ROOT="${1:-"$PROJECT_ROOT/../.ifc_systemc"}"
PKG_DIR="$DEST_ROOT/systemc_pkgs"
SYSROOT="$DEST_ROOT/systemc_sysroot"

mkdir -p "$PKG_DIR" "$SYSROOT"
cd "$PKG_DIR"

apt-get download libsystemc libsystemc-dev

for pkg in "$PKG_DIR"/*.deb; do
    dpkg-deb -x "$pkg" "$SYSROOT"
done

test -f "$SYSROOT/usr/include/systemc.h"
test -f "$SYSROOT/usr/include/tlm.h"
test -f "$SYSROOT/usr/lib/x86_64-linux-gnu/libsystemc.so"

printf 'SYSTEMC_HOME=%s/usr\n' "$SYSROOT"
