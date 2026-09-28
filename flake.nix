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
        in {
          default = pkgs.mkShell {
            packages = [
              pkgs.cmake
              pkgs.gcc
              pkgs.gnumake
              pkgs.pkg-config
              pkgs.ninja

            ];

            shellHook = ''
              echo "Football engine build environment loaded."
              echo "Build with: cmake -S . -B build -G Ninja && cmake --build build"
            '';
          };
        });
    };
}
