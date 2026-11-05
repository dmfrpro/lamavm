{
  pkgs,
  inputs,
  perSystem,
  ...
}:

let
  python = pkgs.python3;
in

pkgs.writeShellApplication {
  name = "test";
  runtimeInputs = with pkgs; [
    coreutils
    nushell
    python
    perSystem.self.lama
    perSystem.self.lama-vm
  ];
  text = ''
    export LAMA_DIR=${perSystem.self.lama}/share/Lama
    export LAMAC=${perSystem.self.lama}/bin/lamac
    export REF=${perSystem.self.lama}/bin/lamavm
    export ITER=${perSystem.self.lama-vm}/bin/lama-custom-vm
    export NEGATIVE_GENERATOR=${inputs.self}/scripts/make_bad_bc.py
    exec ${python}/bin/python3 ${inputs.self}/scripts/test.py "$@"
  '';
  meta = {
    description = "Run Lama VM regression and malformed-input tests";
    mainProgram = "test";
    platforms = [ "x86_64-linux" ];
  };
}
