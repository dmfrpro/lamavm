{ pkgs, ... }:

let
  inherit (pkgs) lib;
  ocamlPackages = pkgs.ocamlPackages;
in

ocamlPackages.buildDunePackage {
  pname = "bisect_ppx_ng";
  version = "3.0.0";

  src = pkgs.fetchurl {
    url = "https://github.com/Kakadu/bisect_ppx_ng/releases/download/3.0.0/bisect_ppx_ng-3.0.0.tbz";
    hash = "sha256-315EUd7F/McetrtCNRW7pakRDDHNUfi7lwqxarMzS+0=";
  };

  propagatedBuildInputs = with ocamlPackages; [
    cmdliner
    ppxlib
  ];

  meta = {
    description = "Code coverage instrumentation for OCaml";
    homepage = "https://github.com/Kakadu/bisect_ppx_ng";
    license = lib.licenses.mit;
  };
}
