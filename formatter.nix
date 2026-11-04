{ pkgs, inputs, ... }:

inputs.treefmt-nix.lib.mkWrapper pkgs {
  projectRootFile = "flake.nix";

  programs = {
    clang-format.enable = true;
    deadnix = {
      enable = true;
      priority = 10;
    };
    nixf-diagnose = {
      enable = true;
      priority = 20;
    };
    nixfmt = {
      enable = true;
      priority = 30;
    };
  };

  settings.formatter.make-format = {
    command = pkgs.lib.getExe pkgs.mbake;
    options = [ "format" ];
    includes = [
      "**/Makefile"
      "**/makefile"
      "**/*.mk"
    ];
  };
}
