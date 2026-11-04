{
  pkgs,
  inputs,
  perSystem,
  ...
}:

pkgs.writeShellApplication {
  name = "test";
  runtimeInputs = with pkgs; [
    coreutils
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

    if [[ $# -eq 0 ]]; then
      WORK="$work/regression" ${inputs.self}/scripts/run_tests.sh
      WORK="$work/negative" ${inputs.self}/scripts/run_negative_tests.sh
    else
      WORK="$work/regression" ${inputs.self}/scripts/run_tests.sh "$@"
    fi
  '';
  meta = {
    description = "Run Lama VM regression and malformed-input tests";
    mainProgram = "test";
    platforms = [ "x86_64-linux" ];
  };
}
