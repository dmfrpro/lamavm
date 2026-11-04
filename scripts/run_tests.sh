#!/usr/bin/env bash
# Correctness: run every regression test through the recursive SM interpreter
# (lamac -s) and through our iterative bytecode interpreter, and compare the
# produced outputs (ignoring the interactive " > " prompts and whitespace).
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
LAMA_DIR=${LAMA_DIR:?LAMA_DIR is not set; run 'nix develop' first}
LAMAC=${LAMAC:-$(command -v lamac || true)}
ITER=${ITER:-$ROOT/packages/lama-vm/build/lama-custom-vm}
WORK=${WORK:-$ROOT/build/tests}
mkdir -p "$WORK"

[[ -x $LAMAC ]] || { echo "lamac not found; run 'nix develop' first" >&2; exit 2; }
[[ -x $ITER ]] || { echo "lama-custom-vm not built; run 'make -C packages/lama-vm' first" >&2; exit 2; }

only=${1:-}
if [[ $# -gt 1 ]]; then
  echo "usage: $0 [test-name]" >&2
  exit 2
fi
if [[ -n $only && ! -f "$LAMA_DIR/regression/$only.lama" ]]; then
  echo "unknown regression test: $only" >&2
  exit 2
fi

norm() { tr -d ' >\n\r\t'; }

pass=0
fail=0
skip=0
failed=()

for src in "$LAMA_DIR"/regression/test*.lama; do
  name=$(basename "$src" .lama)
  if [[ -n $only && $name != "$only" ]]; then
    continue
  fi

  input="$LAMA_DIR/regression/$name.input"
  [[ -f $input ]] || input=/dev/null

  cp -f "$src" "$WORK/$name.lama" || exit 1
  if ! (cd "$WORK" && "$LAMAC" -I "$LAMA_DIR/runtime" -I "$LAMA_DIR/stdlib/x64" -b "$name.lama") >"$WORK/$name.compile.log" 2>&1; then
    echo "SKIP $name: bytecode compilation failed (see $WORK/$name.compile.log)"
    ((skip++))
    continue
  fi

  ref=$(timeout 120 "$LAMAC" -I "$LAMA_DIR/runtime" -I "$LAMA_DIR/stdlib/x64" -s "$src" <"$input" 2>/dev/null | norm)
  ref_rc=${PIPESTATUS[0]}
  got=$(timeout 60 "$ITER" -I "$WORK" -I "$LAMA_DIR/stdlib/x64" "$WORK/$name.bc" <"$input" 2>/dev/null | norm)
  got_rc=${PIPESTATUS[0]}

  if [[ $ref_rc -ne 0 ]]; then
    echo "SKIP $name: reference recursive interpreter exited with $ref_rc"
    ((skip++))
    continue
  fi
  if [[ $got_rc -ne 0 ]]; then
    echo "FAIL $name: lama-custom-vm exited with $got_rc"
    "$ITER" -I "$WORK" -I "$LAMA_DIR/stdlib/x64" "$WORK/$name.bc" <"$input" 2>&1 | tail -3
    ((fail++))
    failed+=("$name")
    continue
  fi

  if [[ $ref == "$got" ]]; then
    ((pass++))
  else
    ((fail++))
    failed+=("$name")
    printf '%s' "$ref" >"$WORK/$name.expected"
    printf '%s' "$got" >"$WORK/$name.actual"
  fi
done

echo
echo "passed: $pass, failed: $fail, skipped: $skip"
if ((fail > 0)); then
  printf 'failed tests: %s\n' "${failed[*]}"
  exit 1
fi
