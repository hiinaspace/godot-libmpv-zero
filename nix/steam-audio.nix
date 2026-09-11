{ pkgs }:
let
  sdk = pkgs.fetchzip {
    url = "https://github.com/ValveSoftware/steam-audio/releases/download/v4.8.1/steamaudio_4.8.1.zip";
    sha256 = "1h34kngcxrzcbzrabfbnaa3qr0hrm1m72snj1pc7xc7lzsqd0nkd";
  };
  godot-cpp = pkgs.fetchzip {
    url = "https://github.com/godotengine/godot-cpp/archive/4862a9dcf1471c9ea19680b9faadb5b6a9432092.tar.gz";
    sha256 = "0mf1m39rkcyv7vkrsxb6nnz02d68m55g8ggycf7d1s2fb1zy4fa6";
  };
  profile = pkgs.writeText "steam-audio-build-profile.json" (builtins.toJSON {
    enabled_classes = [
      "AudioFrame" "AudioStream" "AudioStreamPlayback" "AudioStreamPlayer3D"
      "ArrayMesh" "BoxMesh" "BoxShape3D" "CapsuleMesh" "CapsuleShape3D"
      "CollisionShape3D" "ConcavePolygonShape3D" "CylinderMesh" "CylinderShape3D"
      "Engine" "Mesh" "MeshInstance3D" "Node3D" "OS" "ProjectSettings"
      "Resource" "SphereMesh" "SphereShape3D" "Thread"
    ];
  });
in pkgs.stdenv.mkDerivation {
  pname = "godot-steam-audio";
  version = "8f65c29-sdk-4.8.1";
  src = pkgs.fetchzip {
    url = "https://github.com/stechyo/godot-steam-audio/archive/8f65c29b21c1d8cdbf2d6dbfc53c92ef95dd2a93.tar.gz";
    sha256 = "02i3bh9r1p089k2vcdkysl67rwwvgcq2dr5zrwi5vzlzln4xaa2c";
  };
  patches = [
    ../patches/steam-audio/0001-release-server-before-scene-bindings.patch
    ../patches/steam-audio/0002-include-used-standard-headers.patch
  ];
  nativeBuildInputs = with pkgs; [ cmake ninja python3 autoPatchelfHook ];
  buildInputs = [ pkgs.stdenv.cc.cc.lib ];
  postPatch = ''
    cp ${./steam-audio-CMakeLists.txt} CMakeLists.txt
  '';
  cmakeFlags = [
    "-DGODOT_CPP_SOURCE=${godot-cpp}"
    "-DSTEAM_AUDIO_SDK=${sdk}"
    "-DGODOTCPP_TARGET=template_debug"
    "-DGODOTCPP_BUILD_PROFILE=${profile}"
  ];
  installPhase = ''
    mkdir -p "$out/bin"
    cp libgodot-steam-audio.linux.template_debug.x86_64.so "$out/bin/"
    cp ${sdk}/lib/linux-x64/libphonon.so "$out/bin/"
    cp ../project/addons/godot-steam-audio/bin/libgodot-steam-audio.gdextension "$out/bin/"
    cp -r ../project/addons/godot-steam-audio/icons "$out/icons"
  '';
}
