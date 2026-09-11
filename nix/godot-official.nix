{ pkgs }:

# The nixpkgs source build exports static libstdc++ symbols which can interpose
# on libphonon's shared runtime. Use the official build with hidden symbols.
pkgs.stdenvNoCC.mkDerivation {
  pname = "godot-official";
  version = "4.7.2";
  src = pkgs.fetchurl {
    url = "https://github.com/godotengine/godot-builds/releases/download/4.7.2-stable/Godot_v4.7.2-stable_linux.x86_64.zip";
    hash = "sha256-yt0yBOcoo10/E623/Q15AmNrefa5XEDCZetztsNTKeQ=";
  };
  nativeBuildInputs = with pkgs; [ unzip autoPatchelfHook ];
  buildInputs = [ pkgs.stdenv.cc.cc.lib ];
  runtimeDependencies = with pkgs; map lib.getLib [
    vulkan-loader libGL fontconfig alsa-lib libpulseaudio dbus
    speechd-minimal glib libxkbcommon udev libx11 libxcursor libxinerama
    libxext libxrandr libxrender libxi libdecor wayland openxr-loader
  ];
  dontUnpack = true;
  dontStrip = true;
  installPhase = ''
    mkdir -p "$out/bin"
    unzip "$src"
    install -m755 Godot_v4.7.2-stable_linux.x86_64 "$out/bin/godot"
  '';
}
