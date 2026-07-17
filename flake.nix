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
        in {
          default = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [
              cmake
              ninja
              pkg-config
              clang
              llvmPackages.clang-tools
              wayland-scanner
              python3
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
              lunasvg
              plutovg
              libev
              pulseaudio
              sndio
              mesa
            ];

            # The project is also developed under stal/ix, whose static-link
            # flags may be present in the parent shell.  They must not leak
            # into this glibc-based development environment.
            shellHook = ''
              unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
              export CC=clang
              export CXX=clang++
              export VK_DRIVER_FILES="${pkgs.mesa}/share/vulkan/icd.d/lvp_icd.${icdArch}.json"
            '';
          };
        });
    };
}
