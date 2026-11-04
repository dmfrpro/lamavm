{ pkgs, ... }:

let
  inherit (pkgs) lib;
  ocamlPackages = pkgs.ocamlPackages;
in

ocamlPackages.buildDunePackage {
  pname = "posix-uname";
  inherit (ocamlPackages.posix-base) version src;

  propagatedBuildInputs = with ocamlPackages; [
    ctypes
    posix-base
    posix-errno
  ];

  meta = {
    description = "OCaml bindings for POSIX uname";
    homepage = "https://github.com/savonet/ocaml-posix";
    license = lib.licenses.mit;
  };
}
