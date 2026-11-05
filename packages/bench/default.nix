{
  pkgs,
  inputs,
  perSystem,
  ...
}:

pkgs.writeShellApplication {
  name = "bench";
  runtimeInputs = with pkgs; [
    coreutils
    nushell
    python3
    perSystem.self.lama
    perSystem.self.lama-vm
  ];
  text = ''
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT

    export LAMA_DIR=${perSystem.self.lama}/share/Lama
    export LAMAC=${perSystem.self.lama}/bin/lamac
    export ITER=${perSystem.self.lama-vm}/bin/lama-custom-vm
    export REF=${perSystem.self.lama}/bin/lamavm

    WORK="$work/bench" ${pkgs.python3}/bin/python3 ${inputs.self}/scripts/bench.py "$@"
  '';
  meta = {
    description = "Render three-VM benchmark tables with Nushell and rusage";
    mainProgram = "bench";
    platforms = [ "x86_64-linux" ];
  };
}
