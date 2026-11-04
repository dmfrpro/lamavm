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
    gawk
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

    WORK="$work/bench" ${inputs.self}/scripts/bench.sh "$@"
  '';
  meta = {
    description = "Run Lama VM performance benchmarks";
    mainProgram = "bench";
    platforms = [ "x86_64-linux" ];
  };
}
