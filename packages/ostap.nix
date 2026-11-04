{ pkgs, perSystem, ... }:

let
  inherit (pkgs) lib;
  ocamlPackages = pkgs.ocamlPackages;
in

ocamlPackages.buildDunePackage {
  pname = "ostap";
  version = "0.6.1";

  src = pkgs.fetchFromGitHub {
    owner = "PLTools";
    repo = "ostap";
    rev = "v0.6.1";
    hash = "sha256-Re/Eficmin2zYA5ioSPVs6Ll45LhPFQqZNBOS+KDGf8=";
  };

  nativeBuildInputs = [ ocamlPackages.camlp5 ];
  buildInputs = [ ocamlPackages.dune-configurator ];
  propagatedBuildInputs = with ocamlPackages; [
    camlp5
    perSystem.self.GT
    re
  ];

  meta = {
    description = "Parser-combinator library";
    homepage = "https://github.com/PLTools/ostap";
    license = lib.licenses.lgpl21Plus;
  };
}
