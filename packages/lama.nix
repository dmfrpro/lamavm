{ pkgs, perSystem, ... }:

let
  inherit (pkgs) lib;
  ocamlPackages = pkgs.ocamlPackages;
  rev = "235828c57c0bf24b965c96a41bf3348ac3334a46";
in

pkgs.stdenv.mkDerivation {
  pname = "lama";
  version = "1.30";

  src = pkgs.fetchFromGitHub {
    owner = "PLTools";
    repo = "Lama";
    inherit rev;
    hash = "sha256-w1iTCXNp4wIMPAGzHHTYVM2x+zTUe/Kw/ivQgAtn2dc=";
  };

  nativeBuildInputs = with ocamlPackages; [
    ocaml
    findlib
    pkgs.dune_3
  ];
  buildInputs = [
    perSystem.self.GT
    perSystem.self.ostap
    perSystem.self.posix-uname
    pkgs.libffi
  ];

  postPatch = ''
    substituteInPlace src/dune \
      --replace-fail '(run git rev-parse --abbrev-ref HEAD)' '(run echo "1.30")' \
      --replace-fail '(run git rev-parse --short HEAD)' '(run echo "${rev}")' \
      --replace-fail '(run git rev-parse --verify HEAD)' '(run echo "${rev}")' \
      --replace-fail "(run git show --no-patch --no-notes --pretty='%cd')" '(run echo "pinned by Nix")' \
      --replace-fail '(run opam var share)' "(run echo \"$out/share\")"
    substituteInPlace stdlib/Makefile \
      --replace-fail 'SHELL := /bin/bash' 'SHELL := ${pkgs.bash}/bin/bash'
  '';

  buildPhase = ''
    runHook preBuild
    dune build src runtime stdlib/x64 virtual_machine -j "$NIX_BUILD_CORES"
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 _build/default/src/Driver.exe "$out/bin/lamac"
    install -Dm755 _build/default/virtual_machine/lama.exe "$out/bin/lamavm"

    mkdir -p "$out/share/Lama/"{runtime,stdlib/x64,x64,regression}
    cp runtime/*.{c,h,S,i} "$out/share/Lama/runtime/"
    cp -r regression/. "$out/share/Lama/regression/"
    install -m644 _build/default/stdlib/x64/*.{i,bc} "$out/share/Lama/stdlib/x64/"
    install -m644 _build/default/stdlib/x64/*.{i,bc,o} "$out/share/Lama/x64/"
    install -m644 runtime/Std.i _build/default/runtime/runtime.a "$out/share/Lama/x64/"
    runHook postInstall
  '';

  meta = {
    description = "Lama compiler, runtime, standard library, and regression sources";
    homepage = "https://github.com/PLTools/Lama";
    license = lib.licenses.gpl3Only;
    mainProgram = "lamac";
    platforms = [ "x86_64-linux" ];
  };
}
