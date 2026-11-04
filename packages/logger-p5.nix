{ pkgs, ... }:

let
  inherit (pkgs) lib;
  ocamlPackages = pkgs.ocamlPackages;
in

pkgs.stdenv.mkDerivation {
  pname = "logger-p5";
  version = "0.4.5";

  src = pkgs.fetchFromGitHub {
    owner = "dboulytchev";
    repo = "logger";
    rev = "0.4.5";
    hash = "sha256-uspwU4J/jRhEM7Iy/jce83VITpYqNX9ZY3BePH3QzEE=";
  };

  nativeBuildInputs = with ocamlPackages; [
    ocaml
    findlib
    camlp5
  ];

  buildPhase = ''
    runHook preBuild
    make -f Makefile.ob -j "$NIX_BUILD_CORES"
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p "$OCAMLFIND_DESTDIR"
    make -f Makefile.ob install
    runHook postInstall
  '';

  meta = {
    description = "Camlp5 syntax extension for logging";
    homepage = "https://github.com/dboulytchev/logger";
    license = lib.licenses.lgpl21Plus;
  };
}
