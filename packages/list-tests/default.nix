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

  testRows =
    map (name: {
      suite = "regression";
      test = name;
    }) regressionTests
    ++ map (name: {
      suite = "malformed-input";
      test = name;
    }) negativeTests;
  report = builtins.toJSON testRows;
in

pkgs.writeShellApplication {
  name = "list-tests";
  text = ''
    if [[ $# -ne 0 ]]; then
      echo "usage: list-tests" >&2
      exit 2
    fi

    ${pkgs.nushell}/bin/nu --stdin -c "\$in | from json | table --width 120" \
      <<< ${lib.escapeShellArg report}
  '';
  meta = {
    description = "List Lama VM regression and malformed-input tests";
    mainProgram = "list-tests";
    platforms = [ "x86_64-linux" ];
  };
}
