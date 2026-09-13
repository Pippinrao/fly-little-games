#!/usr/bin/env bash
#
# WSL-side toolchain builder for the bundled homebrew content pipeline.
# Archives arrive already SHA-256-verified from the Windows side; this script
# only compiles/extracts them and installs a small, inspectable tree.
#
# Usage: bootstrap-toolchain-in-wsl.sh <archive-cache-dir> <install-dir>
#
set -eu

CACHE="${1:?archive cache dir required}"
INSTALL="${2:?install dir required}"

STAGE=/tmp/flynes-content-toolchain
rm -rf "$STAGE"
mkdir -p "$STAGE"

echo "--- millfork ---"
mkdir -p "$STAGE/millfork"
unzip -q -o "$CACHE/millfork-0.3.12.zip" -d "$STAGE/millfork"
JAR=$(find "$STAGE/millfork" -name 'millfork.jar' | head -1)
[ -n "$JAR" ] || { echo "BOOTSTRAP_FAIL no millfork.jar in the archive"; exit 1; }

echo "--- 6502-gcc (nested prefix.tar) ---"
mkdir -p "$STAGE/gcc-6502"
unzip -q -o "$CACHE/gcc-6502.zip" -d "$STAGE/gcc-6502"
if [ -f "$STAGE/gcc-6502/prefix.tar" ]; then
  tar -xf "$STAGE/gcc-6502/prefix.tar" -C "$STAGE/gcc-6502"
fi
CC=$(find "$STAGE/gcc-6502" -type f -name '6502-gcc' | head -1)
[ -n "$CC" ] || { echo "BOOTSTRAP_FAIL no 6502-gcc in the archive"; exit 1; }
chmod +x "$CC"

echo "--- xa65-stb (sgadrat fork, built from source) ---"
mkdir -p "$STAGE/xa65-stb"
tar -xzf "$CACHE/xa65-stb.tar.gz" -C "$STAGE/xa65-stb" --strip-components=1
( cd "$STAGE/xa65-stb/xa" && make -j"$(nproc)" ) >"$STAGE/xa65-stb.log" 2>&1 || {
  echo "BOOTSTRAP_FAIL xa65-stb build failed"; tail -5 "$STAGE/xa65-stb.log"; exit 1; }
XA="$STAGE/xa65-stb/xa/xa"
[ -x "$XA" ] || { echo "BOOTSTRAP_FAIL xa binary missing"; exit 1; }

echo "--- huffmunch (built from source) ---"
mkdir -p "$STAGE/huffmunch"
tar -xzf "$CACHE/huffmunch.tar.gz" -C "$STAGE/huffmunch" --strip-components=1
( cd "$STAGE/huffmunch" && make -j"$(nproc)" ) >"$STAGE/huffmunch.log" 2>&1 || {
  echo "BOOTSTRAP_FAIL huffmunch build failed"; tail -5 "$STAGE/huffmunch.log"; exit 1; }
HUFF="$STAGE/huffmunch/huffmunch"
[ -x "$HUFF" ] || { echo "BOOTSTRAP_FAIL huffmunch binary missing"; exit 1; }

echo "--- installing ---"
rm -rf "$INSTALL"
mkdir -p "$INSTALL/millfork" "$INSTALL/gcc-6502" "$INSTALL/xa/xa" "$INSTALL/huffmunch"
# Millfork resolves its standard library (stdlib, zp_reg, bcd_6502, ...) from an
# include/ directory next to the jar, so the whole distribution must be kept.
cp -r "$(dirname "$JAR")/." "$INSTALL/millfork/"
[ -d "$INSTALL/millfork/include" ] || { echo "BOOTSTRAP_FAIL millfork include/ directory was not installed"; exit 1; }
cp -r "$(dirname "$(dirname "$CC")")" "$INSTALL/gcc-6502/prefix"
cp "$XA" "$INSTALL/xa/xa/xa"
cp "$HUFF" "$INSTALL/huffmunch/huffmunch"
chmod +x "$INSTALL/xa/xa/xa" "$INSTALL/huffmunch/huffmunch" \
  "$INSTALL/gcc-6502/prefix/bin/6502-gcc"

echo "--- observed versions ---"
echo "cc65=$(dpkg-query -W -f='\${Version}' cc65 2>/dev/null || echo unknown)"
echo "ca65=$(ca65 --version 2>&1 | head -1)"
echo "pillow=$(python3 -c 'import PIL; print(PIL.__version__)' 2>/dev/null || echo unknown)"
echo "jre=$(java -version 2>&1 | head -1)"
echo "gcc6502=$("$INSTALL/gcc-6502/prefix/bin/6502-gcc" --version 2>&1 | head -1)"
echo "xa=$("$INSTALL/xa/xa/xa" 2>&1 | head -1)"
echo "millfork_jar_sha256=$(sha256sum "$INSTALL/millfork/millfork.jar" | cut -d' ' -f1)"
echo "BOOTSTRAP_OK"
