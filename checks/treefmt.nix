{
  inputs,
  flake,
  pkgs,
  ...
}:
let
  system = pkgs.stdenv.hostPlatform.system;
  sourceRoot = toString inputs.self.outPath;
in
pkgs.runCommand "treefmt"
  {
    nativeBuildInputs = [ flake.formatter.${system} ];
  }
  ''
    set -eu
    mkdir -p "$TMPDIR/src"
    cp -r ${sourceRoot}/. "$TMPDIR/src/"
    chmod -R u+w "$TMPDIR/src"

    cd "$TMPDIR/src"
    treefmt --ci
    mkdir -p "$out"
  ''
