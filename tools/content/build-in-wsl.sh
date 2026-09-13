#!/usr/bin/env bash
#
# WSL-side worker for the bundled homebrew content pipeline.
#
# The Windows side fetches every source tarball (WSL's access to GitHub is
# unreliable here) and verifies its SHA-256; this script only extracts, builds
# and reports. It never invents a version pin: everything it needs comes from
# content/sources.lock.json and the bootstrapped toolchain directory.
#
# Usage:
#   build-in-wsl.sh <lock.json> <source-cache-dir> <toolchain-dir> <output-dir> [gameId]
#
# Output: one machine-readable line per game on stdout
#   RESULT|<canonicalId>|OK|<sha256>|<size>|<assetFilename>
#   RESULT|<canonicalId>|FAIL|<reason>
#
set -u

LOCK="${1:?lock file required}"
SRC_CACHE="${2:?source cache dir required}"
TOOLCHAIN="${3:?toolchain dir required}"
OUT_DIR="${4:?output dir required}"
ONLY_GAME="${5:-}"

BUILD_ROOT="${FLYNES_CONTENT_BUILD_ROOT:-/tmp/flynes-content-build}"
rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT" "$OUT_DIR"

# Toolchain paths (built by bootstrap-content-toolchain.ps1).
export MILLFORK_JAR="$TOOLCHAIN/millfork/millfork.jar"
export XA_BIN="$TOOLCHAIN/xa/xa/xa"
export CC_BIN="$TOOLCHAIN/gcc-6502/prefix/bin/6502-gcc"
export HUFFMUNCH_BIN="$TOOLCHAIN/huffmunch/huffmunch"
# The rescue image targets the Rainbow cartridge's recovery partition; it is not
# part of any emulator-usable ROM and costs ~15 minutes of single-threaded
# huffmunch, so the pipeline skips it.
export SKIP_RESCUE_IMG=2

emit_fail() { echo "RESULT|$1|FAIL|$2"; }

records=$(python3 - "$LOCK" <<'PY'
import json, sys
lock = json.load(open(sys.argv[1], encoding="utf-8"))
for source in lock.get("sources", []):
    steps = source.get("buildSteps") or []
    env = source.get("env") or {}
    fields = [
        source.get("canonicalId", ""),
        source.get("upstreamRepo", ""),
        source.get("sourceRevision", ""),
        source.get("sourceTarballSha256", ""),
        source.get("assetFilename", ""),
        source.get("buildWorkDir", "."),
        "1" if source.get("usePty") else "0",
        source.get("outputRomPath", ""),
        source.get("mapper", ""),
        "\x1f".join(steps),
        "\x1f".join("%s=%s" % (k, v) for k, v in env.items()),
        source.get("romSha256", ""),
        source.get("hashPolicy", "pinned"),
    ]
    print("\x1e".join(str(field) for field in fields))
PY
) || { echo "RESULT|*|FAIL|lock file could not be read"; exit 1; }

while IFS= read -r record; do
  [ -z "$record" ] && continue
  IFS=$'\x1e' read -r id repo revision tarball_sha asset workdir use_pty out_rom mapper steps_blob env_blob want_sha hash_policy <<< "$record"
  if [ -n "$ONLY_GAME" ] && [ "$ONLY_GAME" != "$id" ]; then continue; fi

  short="${revision:0:7}"
  # Same filesystem-safe naming rule as the Windows-side fetcher.
  slug="${id#builtin:}"
  tarball="$SRC_CACHE/$slug-$short.tar.gz"
  if [ ! -f "$tarball" ]; then
    emit_fail "$id" "missing cached source tarball $tarball"
    continue
  fi
  if [ -n "$tarball_sha" ]; then
    actual=$(sha256sum "$tarball" | cut -d' ' -f1)
    if [ "$actual" != "$tarball_sha" ]; then
      emit_fail "$id" "source tarball SHA-256 mismatch (want $tarball_sha got $actual)"
      continue
    fi
  fi

  src="$BUILD_ROOT/$id"
  mkdir -p "$src"
  if ! tar -xzf "$tarball" -C "$src" --strip-components=1; then
    emit_fail "$id" "source tarball could not be extracted"
    continue
  fi

  log="$BUILD_ROOT/$id.build.log"
  : >"$log"
  build_dir="$src/$workdir"
  if [ ! -d "$build_dir" ]; then
    emit_fail "$id" "build directory $workdir is missing in the source tree"
    continue
  fi

  # Declared per-game environment overrides.
  if [ -n "$env_blob" ]; then
    while IFS= read -r pair; do
      [ -z "$pair" ] && continue
      export "${pair?}"
    done <<< "$(printf '%s' "$env_blob" | tr '\037' '\n')"
  fi

  failed=""
  if [ -n "$steps_blob" ]; then
    while IFS= read -r step; do
      [ -z "$step" ] && continue
      echo "--- $id: $step" >>"$log"
      ok=1
      if [ "$use_pty" = "1" ]; then
        script -qec "cd '$build_dir' && $step" /dev/null >>"$log" 2>&1 || ok=0
      else
        ( cd "$build_dir" && eval "$step" ) >>"$log" 2>&1 || ok=0
      fi
      if [ "$ok" != "1" ]; then
        failed="step failed: $step"
        break
      fi
    done <<<"$(printf '%s' "$steps_blob" | tr '\037' '\n')"
  else
    failed="no build steps declared"
  fi

  if [ -n "$failed" ]; then
    emit_fail "$id" "$failed (see $log)"
    continue
  fi

  rom="$src/$out_rom"
  if [ ! -f "$rom" ]; then
    emit_fail "$id" "build produced no ROM at $out_rom"
    continue
  fi

  size=$(stat -c%s "$rom")
  sha=$(sha256sum "$rom" | cut -d' ' -f1)
  header=$(xxd -p -l 4 "$rom")
  if [ "$header" != "4e45531a" ]; then
    emit_fail "$id" "output is not an iNES payload (header $header)"
    continue
  fi
  b6=$(xxd -p -s 6 -l 1 "$rom")
  b7=$(xxd -p -s 7 -l 1 "$rom")
  built_mapper=$(( (0x$b6 >> 4) | (0x$b7 & 0xF0) ))
  if [ -n "$mapper" ] && [ "$built_mapper" != "$mapper" ]; then
    emit_fail "$id" "mapper drift (lock $mapper, built $built_mapper)"
    continue
  fi
  # A drift is only ever accepted through an explicit, reasoned lock update.
  # Records marked hashPolicy=artifact belong to upstreams whose own build is
  # not byte-reproducible (they randomise internal layout); for those the
  # shipped asset stays pinned and verified, and a rebuild only has to prove
  # that the pinned source still builds a valid ROM with the expected mapper.
  drifted=""
  if [ -n "$want_sha" ] && [ "$sha" != "$want_sha" ]; then
    if [ "$hash_policy" = "artifact" ]; then
      drifted="drift"
    elif [ "${FLYNES_ACCEPT_NEW_HASH:-0}" != "1" ]; then
      emit_fail "$id" "ROM hash drift (lock $want_sha built $sha; size $size, mapper $built_mapper)"
      continue
    fi
  fi

  cp "$rom" "$OUT_DIR/$asset"
  echo "RESULT|$id|OK|$sha|$size|$asset|$hash_policy|$drifted"
done < <(printf '%s\n' "$records")
