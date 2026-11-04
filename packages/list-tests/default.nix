{
  pkgs,
  perSystem,
  ...
}:

let
  inherit (pkgs) lib;
  regressionDir = "${perSystem.self.lama.src}/regression";
  negativeGenerator = ../../scripts/make_bad_bc.py;

  regressionTests = lib.mapAttrsToList (filename: _: lib.removeSuffix ".lama" filename) (
    lib.filterAttrs (
      name: type: type == "regular" && lib.hasPrefix "test" name && lib.hasSuffix ".lama" name
    ) (builtins.readDir regressionDir)
  );

  negativeTests = lib.concatMap (part: if builtins.isList part then part else [ ]) (
    builtins.split ''@case\("([^"]+)"\)'' (builtins.readFile negativeGenerator)
  );

  printNames = lib.concatMapStrings (name: ''
    printf '%s\n' ${lib.escapeShellArg name}
  '');
in

pkgs.writeShellApplication {
  name = "list-tests";
  text = ''
    if [[ $# -ne 0 ]]; then
      echo "usage: list-tests" >&2
      exit 2
    fi

    printf 'Regression tests:\n'
    ${printNames regressionTests}

    printf '\nMalformed-input tests:\n'
    ${printNames negativeTests}
  '';
  meta = {
    description = "List Lama VM regression and malformed-input tests";
    mainProgram = "list-tests";
    platforms = [ "x86_64-linux" ];
  };
}
