{
  description = "Linux development environment for compiling Google Research Football";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";

  outputs = { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          # Boost.Python and the interpreter found by CMake must use the same
          # Python ABI.
          python = pkgs.python311.withPackages (ps: [
            ps.pip
            ps.psutil
            ps.setuptools
            ps.wheel
          ]);
        in {
          default = pkgs.mkShell {
            packages = [
              pkgs.cmake
              pkgs.gcc
              pkgs.gnumake
              pkgs.pkg-config
              pkgs.ninja
              python

              # Engine C++ dependencies.
              pkgs.libGL.dev
              pkgs.SDL2
              pkgs.SDL2_image
              pkgs.SDL2_ttf
              pkgs.SDL2_gfx
              pkgs.python311Packages.boost
            ];

            shellHook = ''
              export PYTHONNOUSERSITE=1
              echo "Football engine build environment loaded."
              echo "Build with: ./gfootball/build_game_engine.sh"
            '';
          };
        });
    };
}
