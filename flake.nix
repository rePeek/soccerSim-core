{
  description = "Linux development environment for compiling Google Research Football";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in {
          # Single GCC toolchain. GCC 14+ supports CMake's C++20 module
          # scanning out of the box (no clang-scan-deps, no libc++ flags).
          default = pkgs.mkShell {
            packages = [
              pkgs.cmake
              pkgs.gcc
              pkgs.gnumake
              pkgs.ninja
              pkgs.ccache
              pkgs.python3
            ];
            shellHook = ''
              echo "Toolchain: $(g++ --version | head -1)"
              echo "Configure: cmake --preset debug   (or: release)"
              echo "Build:     cmake --build --preset debug"
              echo "Test:      ctest --preset debug"
            '';
          };
        });
    };
}
