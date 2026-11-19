{
  description = "Linux development environment for the Godot libmpv Vulkan player";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/dc5d91f840324650bac8c379428c7037a416959a";
    # Newer nixpkgs, used ONLY for its mpv 0.42 and libplacebo 7.387 package
    # recipes; they are built with the pinned nixpkgs toolchain above so the
    # libmpv matches the glibc of the patched Godot.
    nixpkgs-mpv = {
      url = "github:NixOS/nixpkgs/04906337b6d53e8bb0eaf0470ae9f070a4d4eb55";
      flake = false;
    };
    mpv-src = {
      url = "github:hiinaspace/mpv/5f4d6211187375568c273262eacb44304ea618b1";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, nixpkgs-mpv, mpv-src }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      godot = import ./nix/godot-official.nix { inherit pkgs; };
      steam-audio = import ./nix/steam-audio.nix { inherit pkgs; };
      godot-interop = pkgs.godot_4.overrideAttrs (old: {
        patches = (old.patches or [ ]) ++ [
          ./patches/godot/0001-enable-libplacebo-device-features.patch
          ./patches/godot/0002-retire-audio-before-extension-unload.patch
          ./patches/godot/0003-track-external-texture-writes.patch
        ];
        sconsFlags = old.sconsFlags ++ [ "use_static_cpp=false" ];
      });
      libplacebo-base = pkgs.callPackage
        (nixpkgs-mpv + "/pkgs/by-name/li/libplacebo/package.nix") { };
      libplacebo = libplacebo-base.overrideAttrs (old: {
        patches = (old.patches or [ ]) ++ [
          ./patches/libplacebo/0001-lower-empty-synchronization-scopes.patch
        ];
      });
      mpv-base = pkgs.callPackage
        (nixpkgs-mpv + "/pkgs/by-name/mp/mpv-unwrapped/package.nix") { inherit libplacebo; };
      mpv = mpv-base.overrideAttrs (old: {
        pname = "libmpv-godot";
        version = "0.42.0-godot-5f4d6211";
        src = mpv-src;
        patches = (old.patches or [ ]) ++ [
          ./patches/mpv/0001-finish-vulkan-handoff-before-return.patch
          ./patches/mpv/0002-drive-audio-from-consumer-queue.patch
          ./patches/mpv/0003-return-images-in-the-handoff-layout.patch
          ./patches/mpv/0004-require-the-device-owners-enabled-features.patch
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
