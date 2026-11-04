{ pkgs, flake, ... }:

let
  system = pkgs.stdenv.hostPlatform.system;
in

pkgs.dockerTools.buildNixShellImage {
  name = "dmfrpro/lama-vm";
  tag = "latest";
  drv = flake.devShells.${system}.default // {
    outputs = [ "out" ];
  };
}
