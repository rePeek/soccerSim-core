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
          # One toolchain for the whole project. C++20 named modules need
          # Clang 16+ / GCC 14+ and the CMake CXX_MODULES scanner; GCC 13
          # cannot process the module entry, so the default build and the
          # module build must not use different compilers. Clang 19 + libc++
          # is the single supported toolchain.
          shell = (pkgs.mkShell.override { stdenv = pkgs.llvmPackages_19.libcxxStdenv; }) {
            packages = [
              pkgs.cmake
              pkgs.gnumake
              pkgs.ninja
              pkgs.ccache
              pkgs.llvmPackages_19.clang
              pkgs.llvmPackages_19.clang-tools
            ];
            shellHook = ''
              echo "Toolchain: clang++ $(clang++ --version | head -1 | sed 's/.*\((.*)\).*/\1/')"
              echo "Configure: cmake --preset debug   (or: release)"
              echo "Build:     cmake --build --preset debug"
              echo "Test:      ctest --preset debug"
            '';
          };
        in {
          default = shell;
          # Retained name for any existing scripts; identical toolchain.
          modules = shell;
        });
    };
}
