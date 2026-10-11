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
          common = [
            pkgs.cmake
            pkgs.gnumake
            pkgs.ninja
            pkgs.ccache
          ];
          banner = ''
            echo "Configure: cmake --preset release   (or: debug)"
            echo "Build:     cmake --build --preset release"
            echo "Test:      ctest --preset release"
          '';
        in {
          # Default toolchain stays on GCC so existing Debug/Release artifacts
          # and golden fingerprints remain valid during the module migration.
          default = pkgs.mkShell {
            packages = common ++ [ pkgs.gcc13 ];
            shellHook = banner;
          };

          # C++20 named-module toolchain (CMake CXX_MODULES needs Clang 16+ or
          # GCC 14+). Use a separate build directory; never mix with build/debug.
          modules = (pkgs.mkShell.override { stdenv = pkgs.llvmPackages_19.libcxxStdenv; }) {
            packages = common ++ [
              pkgs.llvmPackages_19.clang
              pkgs.llvmPackages_19.clang-tools
            ];
            shellHook = banner + ''
              echo "Modules shell: clang++ (Clang 19), use -B build/modules"
            '';
          };
        });
    };
}
