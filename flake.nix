{
  description = "imway development environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          icdArch = if system == "x86_64-linux" then "x86_64" else "aarch64";
          pgstd = pkgs.stdenv.mkDerivation {
            pname = "pgstd";
            version = "git";
            src = ./third_party/libstd;
            dontConfigure = true;
            nativeBuildInputs = [ pkgs.clang pkgs.llvmPackages.llvm ];

            buildPhase = ''
              runHook preBuild
              make -j$NIX_BUILD_CORES CXX=${pkgs.clang}/bin/clang++ std/libstd.a
              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall
              make CXX=${pkgs.clang}/bin/clang++ DESTDIR=$out install
              runHook postInstall
            '';
          };
        in {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              clang
              llvmPackages.clang-tools
              wayland-scanner
              glslang
              python3
              gdb
              foot
              wl-clipboard
            ];

            buildInputs = with pkgs; [
              wayland
              wayland-protocols
              libffi
              vulkan-loader
              vulkan-headers
              libdrm
              libinput
              systemd
              libxkbcommon
              seatd
              dbus
              glfw
              libpng
              libxcrypt
              lunasvg
              plutovg
              libev
              pulseaudio
              sndio
              mesa
              pgstd
            ];

            # The project is also developed under stal/ix, whose static-link
            # flags may be present in the parent shell.  They must not leak
            # into this glibc-based development environment.
            shellHook = ''
              unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
              export CC=clang
              export CXX=clang++
              export GLFW_LIB=-lglfw
              export CPPFLAGS="-I${pkgs.libdrm.dev}/include/libdrm -I${pkgs.dbus.dev}/include/dbus-1.0 -I${pkgs.dbus.lib}/lib/dbus-1.0/include -I${pkgs.lunasvg}/include/lunasvg"
              export WL_PROTOCOL_DIR="${pkgs.wayland-protocols}/share/wayland-protocols"
              export VK_DRIVER_FILES="${pkgs.mesa}/share/vulkan/icd.d/lvp_icd.${icdArch}.json"
            '';
          };
        });
    };
}
