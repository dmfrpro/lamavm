{ pkgs, perSystem, ... }:

pkgs.stdenv.mkDerivation (finalAttrs: {
  pname = "lama-custom-vm";
  version = "0.1.0";
  src = ./.;

  makeFlags = [ "LAMA_DIR=${perSystem.self.lama}/share/Lama" ];

  installPhase = ''
    runHook preInstall
    install -Dm755 build/lama-custom-vm "$out/bin/lama-custom-vm"
    runHook postInstall
  '';

  passthru.tests = import ../../nix/checks.nix {
    inherit pkgs;
    lama = perSystem.self.lama;
    lama-vm = finalAttrs.finalPackage;
  };

  meta = {
    description = "Iterative interpreter for the Lama stack machine";
    mainProgram = "lama-custom-vm";
    platforms = [ "x86_64-linux" ];
  };
})
