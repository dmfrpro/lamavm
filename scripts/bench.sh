#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
LAMA_DIR=${LAMA_DIR:?LAMA_DIR is not set; run 'nix develop' first}
LAMAC=${LAMAC:-$(command -v lamac || true)}
ITER=${ITER:-$ROOT/packages/lama-vm/build/lama-custom-vm}
REF=${REF:-$(command -v lamavm || true)}
WORK=${WORK:-$ROOT/build/bench}
BENCH_TIMEOUT=${BENCH_TIMEOUT:-30}
mkdir -p "$WORK"

measure() {
  local start end rc
  start=${EPOCHREALTIME/,/.}
  timeout "$BENCH_TIMEOUT" "$@" </dev/null >/dev/null
  rc=$?
  if [[ $rc -ne 0 ]]; then
    if [[ $rc -eq 124 ]]; then
      printf 'benchmark timed out after %ss: %s\n' "$BENCH_TIMEOUT" "$*" >&2
    else
      printf 'benchmark command failed with status %d: %s\n' "$rc" "$*" >&2
    fi
    return "$rc"
  fi
  end=${EPOCHREALTIME/,/.}
  awk -v a="$start" -v b="$end" 'BEGIN { printf "%.3f", b - a }'
}

[[ -x $LAMAC ]] || { echo "lamac not found; run 'nix develop' first" >&2; exit 2; }
[[ -x $REF ]] || { echo "lamavm not found; run the benchmark through 'nix develop' first" >&2; exit 2; }

printf '%-12s %14s %14s %14s %11s %12s\n' \
  benchmark 'custom-vm(s)' 'lamavm(s)' 'ocaml-s(s)' 'up/custom' 'ocaml/custom'
for src in bench/*.lama; do
  name=$(basename "$src" .lama)
  cp "$src" "$WORK/$name.lama"
  (cd "$WORK" && timeout "$BENCH_TIMEOUT" "$LAMAC" -I "$LAMA_DIR/runtime" -I "$LAMA_DIR/stdlib/x64" -b "$name.lama") || {
    echo "cannot compile $name" >&2
    continue
  }

  ti=$(measure "$ITER" -I "$LAMA_DIR/stdlib/x64" "$WORK/$name.bc") || exit $?
  tr=$(measure "$REF" -I "$LAMA_DIR/stdlib/x64" "$WORK/$name.bc") || exit $?
  to=$(measure "$LAMAC" -I "$LAMA_DIR/runtime" -I "$LAMA_DIR/stdlib/x64" -s "$src") || exit $?
  printf '%-12s %14s %14s %14s %10sx %11sx\n' "$name" "$ti" "$tr" "$to" \
    "$(awk -v a="$ti" -v b="$tr" 'BEGIN { if (a+0 == 0) a = 0.001; printf "%.1f", b / a }')" \
    "$(awk -v a="$ti" -v b="$to" 'BEGIN { if (a+0 == 0) a = 0.001; printf "%.1f", b / a }')"
done
