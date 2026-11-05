{
  pkgs,
  lama,
  lama-vm,
}:

let
  inherit (pkgs) lib;
  lamaData = "${lama}/share/Lama";
  regressionDir = "${lama.src}/regression";
  negativeGenerator = ../scripts/make_bad_bc.py;
  testRunner = ../scripts;
  testPython = pkgs.python3;

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
    testPython
    pkgs.coreutils
    pkgs.nushell
  ];

  mkTest =
    {
      name,
      suite,
      expectSkip ? null,
    }:
    pkgs.runCommand "lama-vm-${name}"
      {
        nativeBuildInputs = inputs;
      }
      ''
        set -eu
        mkdir -p "$out"
        export LAMA_DIR=${lamaData}
        export LAMAC=${lama}/bin/lamac
        export REF=${lama}/bin/lamavm
        export ITER=${lama-vm}/bin/lama-custom-vm
        export NEGATIVE_GENERATOR=${negativeGenerator}
        export TABLE_RENDERER=${testRunner}/render_tables.nu
        ${testPython}/bin/python3 ${testRunner}/test.py ${suite} ${name} \
          --work "$TMPDIR/work" \
          --status-file "$out/status" \
          ${lib.optionalString (expectSkip != null) "--expect-skip ${expectSkip}"}
      '';

  regressionFiles = lib.filterAttrs (
    name: type: type == "regular" && lib.hasPrefix "test" name && lib.hasSuffix ".lama" name
  ) (builtins.readDir regressionDir);

  regressionChecks = lib.mapAttrs' (
    filename: _:
    let
      name = lib.removeSuffix ".lama" filename;
      expectSkip =
        if builtins.elem name compileSkips then
          "compile"
        else if builtins.elem name referenceSkips then
          "reference"
        else
          null;
    in
    {
      name = "regression-${name}";
      value = mkTest {
        inherit name expectSkip;
        suite = "regression";
      };
    }
  ) regressionFiles;

  negativeNames = lib.concatMap (part: if builtins.isList part then part else [ ]) (
    builtins.split ''@case\("([^"]+)"\)'' (builtins.readFile negativeGenerator)
  );

  negativeChecks = lib.listToAttrs (
    map (name: {
      name = "negative-${name}";
      value = mkTest {
        inherit name;
        suite = "negative";
      };
    }) negativeNames
  );
in
regressionChecks // negativeChecks
