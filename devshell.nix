{
  pkgs,
  inputs,
  perSystem,
  flake,
  system,
  ...
}:

(import inputs.devshell { nixpkgs = pkgs; }).mkShell {
  packages = with pkgs; [
    gcc
    gnumake
    binutils
    bear
    coreutils
    gawk
    time

    git
    python3
    clang-tools
    mbake
    nixf-diagnose
    deadnix
    nixfmt
    flake.formatter.${system}
    perSystem.self.checkpatch
  ];

  commands = [
    {
      name = "bench";
      help = "run the Lama VM benchmarks";
      command = ''
        exec ${perSystem.self.bench}/bin/bench "$@"
      '';
    }
    {
      name = "lamac";
      help = "compile or interpret Lama source with standard include paths";
      command = ''
        exec ${perSystem.self.lama}/bin/lamac \
          -I "$LAMA_DIR/runtime" -I "$LAMA_DIR/stdlib/x64" "$@"
      '';
    }
    {
      name = "lama-custom-vm";
      help = "run bytecode on the custom iterative interpreter";
      command = ''
        exec ${perSystem.self.lama-vm}/bin/lama-custom-vm \
          -I "$LAMA_DIR/stdlib/x64" "$@"
      '';
    }
    {
      name = "lamavm";
      help = "run bytecode on the upstream interpreter";
      command = ''
        exec ${perSystem.self.lama}/bin/lamavm \
          -I "$LAMA_DIR/stdlib/x64" "$@"
      '';
    }
    {
      name = "list-tests";
      help = "list regression and malformed-input test names";
      command = ''
        exec ${perSystem.self.list-tests}/bin/list-tests "$@"
      '';
    }
    {
      name = "ocaml-s";
      help = "compile and run source on the recursive OCaml interpreter";
      command = ''
        exec ${perSystem.self.lama}/bin/lamac \
          -I "$LAMA_DIR/runtime" -I "$LAMA_DIR/stdlib/x64" -s "$@"
      '';
    }
    {
      name = "test-all";
      help = "run regression and malformed-input tests";
      command = ''
        exec ${perSystem.self.test}/bin/test "$@"
      '';
    }
    {
      name = "test-one";
      help = "run one upstream regression: test-one test001";
      command = ''
        exec ${perSystem.self.test}/bin/test "$@"
      '';
    }
  ];

  env = [
    {
      name = "LAMA_DIR";
      value = "${perSystem.self.lama}/share/Lama";
    }
    {
      name = "TMPDIR";
      eval = ''"$([ -w /tmp ] && echo /tmp || echo /build)"'';
    }
  ];
}
