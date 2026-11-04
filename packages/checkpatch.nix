{ pkgs, ... }:
let
  inherit (pkgs) lib;
in
pkgs.stdenv.mkDerivation {
  pname = "checkpatch";
  version = "7.4-rc1";

  src = pkgs.fetchurl {
    url = "https://raw.githubusercontent.com/torvalds/linux/05441d697f7f9a927264f4058cbbc6a03dfcac9d/scripts/checkpatch.pl";
    hash = "sha256-sxnZ9YzWJfCT4IqqebPL75lJcGv2ZbeX3FBsPITzXlg=";
  };

  nativeBuildInputs = [ pkgs.makeWrapper ];

  dontUnpack = true;
  dontBuild = true;

  installPhase = ''
    mkdir -p $out/bin
    makeWrapper ${lib.getExe pkgs.perl} $out/bin/checkpatch.pl \
      --add-flags "$src"
  '';

  meta = {
    description = "Linux kernel checkpatch.pl";
    mainProgram = "checkpatch.pl";
    homepage = "https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git";
    license = lib.licenses.gpl2Only;
    platforms = lib.platforms.all;
  };
}
