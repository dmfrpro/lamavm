{
  pkgs,
  lama,
  lama-vm,
}:

let
  inherit (pkgs) lib;
  lamaData = "${lama}/share/Lama";
  lamacFlags = "-I ${lamaData}/runtime -I ${lamaData}/stdlib/x64";
  regressionDir = "${lama.src}/regression";
  negativeGenerator = ../scripts/make_bad_bc.py;

  compileSkips = [
    "test054"
    "test110"
  ];
  referenceSkips = [
    "test111"
    "test803"
  ];

  inputs = [
    lama
    lama-vm
    pkgs.coreutils
    pkgs.diffutils
    pkgs.gnugrep
  ];

  inputFor =
    name:
    let
      path = regressionDir + "/${name}.input";
    in
    if builtins.pathExists path then path else "/dev/null";

  mkCompileSkip =
    name:
    pkgs.runCommand "lama-vm-${name}-upstream-skip" { nativeBuildInputs = inputs; } ''
      set -eu
      cp ${regressionDir}/${name}.lama ${name}.lama
      if lamac ${lamacFlags} -b ${name}.lama >compile.log 2>&1; then
        echo "expected the upstream indirect-assignment limitation" >&2
        exit 1
      fi
      cat compile.log
      grep -F "Indirect assignment is not supported yet" compile.log
      mkdir -p "$out"
      echo "SKIP: upstream compiler does not support indirect assignment" >"$out/status"
    '';

  mkReferenceSkip =
    name:
    pkgs.runCommand "lama-vm-${name}-upstream-skip" { nativeBuildInputs = inputs; } ''
      set -eu
      cp ${regressionDir}/${name}.lama ${name}.lama
      lamac ${lamacFlags} -b ${name}.lama
      set +e
      timeout 120 lamac ${lamacFlags} -s ${regressionDir}/${name}.lama \
        <${inputFor name} >reference.out 2>reference.err
      status=$?
      set -e
      if [ "$status" -eq 0 ]; then
        echo "expected the reference recursive interpreter to fail" >&2
        exit 1
      fi
      cat reference.err
      mkdir -p "$out"
      echo "SKIP: reference recursive interpreter exited with $status" >"$out/status"
    '';

  mkRegression =
    name:
    pkgs.runCommand "lama-vm-${name}" { nativeBuildInputs = inputs; } ''
      set -eu
      cp ${regressionDir}/${name}.lama ${name}.lama
      if ! lamac ${lamacFlags} -b ${name}.lama >compile.log 2>&1; then
        cat compile.log
        exit 1
      fi

      timeout 120 lamac ${lamacFlags} -s ${regressionDir}/${name}.lama \
        <${inputFor name} >reference.out 2>reference.err || {
        status=$?
        cat reference.err
        exit "$status"
      }
      timeout 60 lama-custom-vm -I . -I ${lamaData}/stdlib/x64 ${name}.bc \
        <${inputFor name} >actual.out 2>actual.err || {
        status=$?
        cat actual.err
        exit "$status"
      }

      tr -d ' >\n\r\t' <reference.out >reference.norm
      tr -d ' >\n\r\t' <actual.out >actual.norm
      diff -u --label reference --label iterative reference.norm actual.norm

      mkdir -p "$out"
      cp actual.out "$out/output"
      echo "PASS" >"$out/status"
    '';

  regressionFiles = lib.filterAttrs (
    name: type: type == "regular" && lib.hasPrefix "test" name && lib.hasSuffix ".lama" name
  ) (builtins.readDir regressionDir);

  regressionChecks = lib.mapAttrs' (
    filename: _:
    let
      name = lib.removeSuffix ".lama" filename;
    in
    {
      name = "regression-${name}";
      value =
        if builtins.elem name compileSkips then
          mkCompileSkip name
        else if builtins.elem name referenceSkips then
          mkReferenceSkip name
        else
          mkRegression name;
    }
  ) regressionFiles;

  negativeNames = lib.concatMap (part: if builtins.isList part then part else [ ]) (
    builtins.split ''@case\("([^"]+)"\)'' (builtins.readFile negativeGenerator)
  );

  negativeChecks = lib.listToAttrs (
    map (name: {
      name = "negative-${name}";
      value =
        pkgs.runCommand "lama-vm-negative-${name}"
          {
            nativeBuildInputs = [
              lama-vm
              pkgs.coreutils
              pkgs.python3
            ];
          }
          ''
            set -eu
            mkdir cases
            python3 ${negativeGenerator} cases ${name} >/dev/null
            set +e
            lama-custom-vm cases/${name}.bc >actual.out 2>actual.err
            status=$?
            set -e
            if [ "$status" -eq 0 ]; then
              echo "accepted malformed bytecode ${name}" >&2
              exit 1
            fi
            if [ "$status" -ge 128 ]; then
              echo "crashed on malformed bytecode ${name} with status $status" >&2
              exit 1
            fi
            if [ ! -s actual.err ]; then
              echo "no diagnostic for malformed bytecode ${name}" >&2
              exit 1
            fi
            mkdir -p "$out"
            cp actual.err "$out/diagnostic"
            echo "PASS: rejected with status $status" >"$out/status"
          '';
    }) negativeNames
  );
in
regressionChecks // negativeChecks
