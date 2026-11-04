{
  inputs,
  perSystem,
  pkgs,
  ...
}:
let
  inherit (pkgs) lib;
  sourceRoot = toString inputs.self.outPath;

  sourceFiles =
    dir:
    lib.concatLists (
      lib.mapAttrsToList (
        name: type:
        let
          path = dir + "/${name}";
        in
        if type == "directory" then
          sourceFiles path
        else if type == "regular" && (lib.hasSuffix ".c" name || lib.hasSuffix ".h" name) then
          [ path ]
        else
          [ ]
      ) (builtins.readDir dir)
    );

  files = sourceFiles sourceRoot;
  relativeFiles = map (file: lib.removePrefix "${sourceRoot}/" file) files;
in
assert files != [ ];
pkgs.runCommand "checkpatch" { nativeBuildInputs = [ perSystem.self.checkpatch ]; } ''
  set -eu
  cd ${sourceRoot}
  checkpatch.pl --file ${lib.escapeShellArgs relativeFiles}
  mkdir -p "$out"
''
