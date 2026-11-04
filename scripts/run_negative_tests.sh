#!/usr/bin/env bash

set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
ITER=${ITER:-$ROOT/packages/lama-vm/build/lama-custom-vm}
WORK=${WORK:-$ROOT/build/negative}

[[ -x $ITER ]] || { echo "lama-custom-vm not built; run 'make -C packages/lama-vm' first" >&2; exit 2; }
python3 scripts/make_bad_bc.py "$WORK" >/dev/null

pass=0
fail=0
for bc in "$WORK"/*.bc; do
  name=$(basename "$bc" .bc)
  out=$("$ITER" "$bc" 2>&1 >/dev/null)
  rc=$?
  if [[ $rc -eq 0 ]]; then
    echo "FAIL $name: accepted malformed input"
    ((fail++))
  elif [[ $rc -ge 128 || -z $out ]]; then
    echo "FAIL $name: crashed or produced no diagnostic (exit $rc)"
    ((fail++))
  else
    ((pass++))
  fi
done

echo "negative tests: passed $pass, failed $fail"
((fail == 0))
