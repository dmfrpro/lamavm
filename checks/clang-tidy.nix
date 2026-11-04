{
  inputs,
  perSystem,
  pkgs,
  ...
}:
let
  sourceRoot = toString inputs.self.outPath;
in
pkgs.runCommand "clang-tidy"
  {
    nativeBuildInputs = with pkgs; [
      bear
      clang-tools
      gcc
      gnumake
    ];
  }
  ''
    set -eu
    mkdir -p "$TMPDIR/src"
    cp -r ${sourceRoot}/. "$TMPDIR/src/"
    chmod -R u+w "$TMPDIR/src"

    make -C "$TMPDIR/src/packages/lama-vm" \
      LAMA_DIR=${perSystem.self.lama}/share/Lama compile-commands

    cd "$TMPDIR/src"
    clang-tidy -p compile_commands.json packages/lama-vm/src/*.c
    mkdir -p "$out"
  ''
