{
  description = "imway — a Wayland compositor and desktop shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    {
      self,
      nixpkgs,
    }:
    let
      inherit (nixpkgs) lib;

      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forAllSystems = lib.genAttrs systems;

      nixpkgsFor = system: nixpkgs.legacyPackages.${system};

      # YYYY.MM.DD from the flake revision date.
      versionFromFlake =
        let
          d = self.lastModifiedDate or "19700101";
        in
        "${builtins.substring 0 4 d}.${builtins.substring 4 2 d}.${builtins.substring 6 2 d}";

      sanitizerConfigs = {
        asan = {
          flag = "-fsanitize=address";
          environment = "export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1";
        };
        ubsan = {
          flag = "-fsanitize=undefined";
          environment = "export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1";
        };
      };

      configureBuildEnvironment =
        pkgs:
        {
          sanitizer ? null,
          coverage ? false,
        }:
        assert !(coverage && sanitizer != null);
        let
          config = if sanitizer == null then null else sanitizerConfigs.${sanitizer};
          icdArch = if pkgs.stdenv.hostPlatform.isx86_64 then "x86_64" else "aarch64";
          sanitizerFlags =
            if config == null then
              ""
            else
              lib.concatStringsSep " " [
                config.flag
                "-fno-sanitize-recover=all"
                "-fno-omit-frame-pointer"
                "-g"
              ];
        in
        ''
          unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS ASAN_OPTIONS UBSAN_OPTIONS LLVM_PROFILE_FILE
          # The build runner deliberately passes its canonical target triple.
          # Nix's wrapper spells the equivalent native vendor field differently.
          export NIX_CC_WRAPPER_SUPPRESS_TARGET_WARNING=1
          export VK_DRIVER_FILES=${pkgs.mesa}/share/vulkan/icd.d/lvp_icd.${icdArch}.json
          ${lib.optionalString (config != null) ''
            export CFLAGS=${lib.escapeShellArg sanitizerFlags}
            export CXXFLAGS="$CFLAGS"
            export LDFLAGS=${lib.escapeShellArg config.flag}
            ${config.environment}
          ''}
          ${lib.optionalString coverage ''
            export CFLAGS="-fprofile-instr-generate -fcoverage-mapping -fcoverage-compilation-dir=. -fcoverage-prefix-map=$PWD=."
            export CXXFLAGS="$CFLAGS"
            export LDFLAGS="-fprofile-instr-generate -Wl,--build-id=sha1"
          ''}
        '';

      mkImway =
        pkgs:
        {
          sanitizer ? null,
        }:
        let
          stdenv = pkgs.llvmPackages.stdenv;
          sanitizerSuffix = lib.optionalString (sanitizer != null) "-${sanitizer}";
          buildDirectory = ".build${sanitizerSuffix}";
        in
        stdenv.mkDerivation {
          pname = "imway${sanitizerSuffix}";
          version = versionFromFlake;

          src = self;

          dontConfigure = true;

          nativeBuildInputs = with pkgs; [
            addDriverRunpath
            glslang
            pkg-config
            python3
            wayland-protocols
            wayland-scanner
          ];

          buildInputs = with pkgs; [
            dbus
            glfw
            lcms2
            libdisplay-info
            libdrm
            libev
            libinput
            libjxl
            libpng
            libxcrypt
            libxkbcommon
            linux-pam
            lunasvg
            plutovg
            pulseaudio
            seatd
            sndio
            systemd
            vulkan-headers
            vulkan-loader
            wayland
          ];

          buildPhase = ''
            runHook preBuild
            ${configureBuildEnvironment pkgs { inherit sanitizer; }}
            python3 ./build \
              -B ${buildDirectory} \
              -j "$NIX_BUILD_CORES" \
              imway
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            install -Dm755 ${buildDirectory}/imway "$out/bin/imway"
            runHook postInstall
          '';

          # Vulkan ICDs live under /run/opengl-driver on NixOS.
          postFixup = ''
            addDriverRunpath "$out/bin/imway"
          '';

          meta = {
            description = "Wayland compositor and desktop shell";
            homepage = "https://github.com/pg83/imway";
            license = lib.licenses.mit;
            mainProgram = "imway";
            platforms = lib.platforms.linux;
            badPlatforms = lib.platforms.darwin;
          };
        };

      mkTestCheck =
        pkgs:
        {
          sanitizer ? null,
          coverage ? false,
        }:
        assert !(coverage && sanitizer != null);
        let
          base = mkImway pkgs { inherit sanitizer; };
          sanitizerSuffix = lib.optionalString (sanitizer != null) "-${sanitizer}";
          checkSuffix = if coverage then "-coverage" else sanitizerSuffix;
          buildDirectory = ".build-tests${checkSuffix}";
        in
        base.overrideAttrs (old: {
          pname = "imway-tests${checkSuffix}";

          nativeBuildInputs =
            old.nativeBuildInputs
            ++ (with pkgs; [
              binutils
              dbus
              foot
              gdb
              sndio
              util-linux
              wl-clipboard
            ])
            ++ lib.optionals (sanitizer != null || coverage) [ pkgs.llvmPackages.llvm ];

          FONTCONFIG_FILE = pkgs.makeFontsConf {
            fontDirectories = [ pkgs.dejavu_fonts ];
          };

          buildPhase = ''
            runHook preBuild
            ${configureBuildEnvironment pkgs { inherit sanitizer coverage; }}
            ${lib.optionalString (sanitizer == "asan") ''
              export ASAN_SYMBOLIZER_PATH=${lib.getExe' pkgs.llvmPackages.llvm "llvm-symbolizer"}
            ''}
            ${lib.optionalString coverage ''
              profileDirectory="$TMPDIR/imway-coverage-profiles"
              mkdir -p "$profileDirectory"
              export LLVM_PROFILE_FILE="$profileDirectory/%b-%16m.profraw"
            ''}
            python3 ./build \
              -B ${buildDirectory} \
              -j "$NIX_BUILD_CORES" \
              -k \
              -Druns=1 \
              test
            ${lib.optionalString coverage ''
              coverageDirectory="$PWD/.coverage"
              coverageBinary="${buildDirectory}/imway_test"
              coverageIgnore='(^|/)(tst|ext/libstd|\.build[^/]*)/|^/nix/store/'
              mkdir -p "$coverageDirectory/html"
              buildId="$(llvm-readelf -n "$coverageBinary" |
                sed -n 's/.*Build ID: //p' |
                head -1)"
              if [[ -z "$buildId" ]]; then
                echo "coverage binary has no build ID: $coverageBinary" >&2
                exit 1
              fi
              coverageProfiles=("$profileDirectory/$buildId"-*.profraw)
              if [[ ! -e "''${coverageProfiles[0]}" ]]; then
                echo "coverage binary produced no profiles: $coverageBinary" >&2
                exit 1
              fi
              llvm-profdata merge \
                -sparse \
                "''${coverageProfiles[@]}" \
                -o "$coverageDirectory/coverage.profdata"
              llvm-cov export \
                "$coverageBinary" \
                -instr-profile="$coverageDirectory/coverage.profdata" \
                -format=lcov \
                -ignore-filename-regex="$coverageIgnore" \
                > "$coverageDirectory/coverage.info"
              llvm-cov report \
                "$coverageBinary" \
                -instr-profile="$coverageDirectory/coverage.profdata" \
                -ignore-filename-regex="$coverageIgnore" \
                > "$coverageDirectory/summary.txt"
              llvm-cov show \
                "$coverageBinary" \
                -instr-profile="$coverageDirectory/coverage.profdata" \
                -format=html \
                -output-dir="$coverageDirectory/html" \
                -show-branches=percent \
                -coverage-watermark=80,50 \
                -ignore-filename-regex="$coverageIgnore"
              substituteInPlace "$coverageDirectory/coverage.info" \
                --replace-quiet "SF:$PWD/" "SF:"
              if grep -q '^SF:/' "$coverageDirectory/coverage.info"; then
                echo "coverage report contains absolute source paths" >&2
                grep '^SF:/' "$coverageDirectory/coverage.info" | head -10 >&2
                exit 1
              fi
              if ! grep -q '^SF:' "$coverageDirectory/coverage.info"; then
                echo "coverage report does not contain source files" >&2
                exit 1
              fi
              cat "$coverageDirectory/summary.txt"
            ''}
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p "$out"
            ${
              if coverage then
                ''
                  install -Dm644 .coverage/coverage.info "$out/coverage.info"
                  install -Dm644 .coverage/summary.txt "$out/summary.txt"
                  cp -R .coverage/html "$out/html"
                ''
              else
                ''
                  touch "$out/passed"
                ''
            }
            runHook postInstall
          '';

          postFixup = "";
        });

      mkDevShell =
        pkgs:
        let
          stdenv = pkgs.llvmPackages.stdenv;
          imway = mkImway pkgs { };
        in
        pkgs.mkShell.override { inherit stdenv; } {
          inputsFrom = [ imway ];
          NIX_CC_WRAPPER_SUPPRESS_TARGET_WARNING = "1";

          packages = with pkgs; [
            clang-tools
            foot
            gdb
            wl-clipboard
          ];

          shellHook = ''
            unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS
            echo "imway dev shell — run: ./build imway"
          '';
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor system;
          imway = mkImway pkgs { };
        in
        {
          default = imway;
          imway = imway;
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor system;
        in
        {
          default = mkDevShell pkgs;
        }
      );

      checks = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor system;
        in
        {
          build = mkImway pkgs { };
          tests = mkTestCheck pkgs { };
          coverage = mkTestCheck pkgs { coverage = true; };
          build-asan = mkImway pkgs { sanitizer = "asan"; };
          tests-asan = mkTestCheck pkgs { sanitizer = "asan"; };
          build-ubsan = mkImway pkgs { sanitizer = "ubsan"; };
          tests-ubsan = mkTestCheck pkgs { sanitizer = "ubsan"; };
        }
      );

      formatter = forAllSystems (system: (nixpkgsFor system).nixfmt);

      overlays.default = final: prev: {
        imway = mkImway final { };
      };
    };
}
