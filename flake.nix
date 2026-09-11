{
  description = "Linux development environment for the Godot libmpv Vulkan player";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/dc5d91f840324650bac8c379428c7037a416959a";
    mpv-src = {
      url = "github:hiinaspace/mpv/970250ad1480e78ca03999e1733d212278f07dbb";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, mpv-src }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      godot = import ./nix/godot-official.nix { inherit pkgs; };
      steam-audio = import ./nix/steam-audio.nix { inherit pkgs; };
      godot-interop = pkgs.godot_4.overrideAttrs (old: {
        patches = (old.patches or [ ]) ++ [
          ./patches/godot/0001-enable-libplacebo-device-features.patch
          ./patches/godot/0002-retire-audio-before-extension-unload.patch
        ];
        sconsFlags = old.sconsFlags ++ [ "use_static_cpp=false" ];
      });
      libplacebo = pkgs.libplacebo.overrideAttrs (old: {
        patches = (old.patches or [ ]) ++ [
          ./patches/libplacebo/0001-lower-empty-synchronization-scopes.patch
        ];
      });
      mpv = (pkgs.mpv-unwrapped.override { inherit libplacebo; }).overrideAttrs (old: {
        pname = "libmpv-godot";
        version = "0.41.0-godot-970250ad";
        src = mpv-src;
        patches = (old.patches or [ ]) ++ [
          ./patches/mpv/0001-finish-vulkan-handoff-before-return.patch
        ];
        doInstallCheck = false; # The fork reports its upstream version.
        postInstall = old.postInstall + ''
          cp ../include/mpv/godot_audio.h ../include/mpv/render_vk.h "$dev/include/mpv/"
        '';
      });
    in {
      packages.${system} = { inherit mpv godot godot-interop steam-audio; };
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          cmake ninja pkg-config python3 scons patchelf binutils gdb
          vulkan-headers vulkan-loader vulkan-tools vulkan-validation-layers
          ffmpeg yt-dlp deno libpulseaudio
        ] ++ [ godot-interop ];
        buildInputs = [ mpv pkgs.stdenv.cc.cc.lib ];
        MPV_DIR = "${mpv.dev}";
        LIBMPV_ZERO_MPV_LIBRARY = "${mpv}/lib/libmpv.so.2";
        LIBMPV_ZERO_VULKAN_LIBRARY = "${pkgs.vulkan-loader}/lib/libvulkan.so.1";
        LIBMPV_ZERO_YTDLP = "${pkgs.yt-dlp}/bin/yt-dlp";
        # For prebuilt Steam Audio libraries; native Nix-built dependencies use RPATH.
        LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [ pkgs.stdenv.cc.cc.lib ];
      };
    };
}
