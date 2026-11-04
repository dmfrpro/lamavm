{ pkgs, perSystem, ... }:

let
  inherit (pkgs) lib;
  ocamlPackages = pkgs.ocamlPackages;
in

ocamlPackages.buildDunePackage {
  pname = "GT";
  version = "0.5.5";

  src = pkgs.fetchurl {
    url = "https://github.com/PLTools/GT/releases/download/0.5.5/GT-0.5.5.tbz";
    hash = "sha256-xBPLQ4kOb/RulvIrAoVfYuAt+abq6Sy8ku5v4PweMuo=";
  };

  nativeBuildInputs =
    with ocamlPackages;
    [
      camlp5
      mdx
    ]
    ++ [ pkgs.m4 ];
  buildInputs = [ ocamlPackages.dune-configurator ];
  propagatedBuildInputs = [
    perSystem.self.bisect_ppx_ng
    perSystem.self.logger-p5
  ]
  ++ (with ocamlPackages; [
    camlp5
    ocamlgraph
    ppx_inline_test_nobase
    ppxlib
  ]);

  meta = {
    description = "Generic programming with extensible transformations";
    homepage = "https://github.com/PLTools/GT";
    license = lib.licenses.lgpl21Plus;
  };
}
