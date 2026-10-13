{
  inputs = {
    flake-parts = {
      url = "github:hercules-ci/flake-parts";
      inputs.nixpkgs-lib.follows = "nixpkgs";
    };
    nixpkgs = {
      url = "github:nixos/nixpkgs/nixos-unstable";
    };
  };

  outputs =
    inputs:
    inputs.flake-parts.lib.mkFlake
      {
        inherit inputs;
      }
      {
        systems = [
          "aarch64-darwin"
          "x86_64-linux"
        ];

        perSystem = { pkgs, ... }: {
          devShells.default = pkgs.mkShell {
            # To build toolchain:
            # CFLAGS="-g -O2 -Wno-format-security" CXXFLAGS="-g -O2 -Wno-format-security" ./build_toolchain.sh
            packages = with pkgs; [
              gnumake
              cdrtools # mkisofs
              gnutar
              limine-full # Limine with all pre-built artifacts
              # QEMU is expected to be provided from host

              # toolchain's dependencies (gcc & binutils)
              automake
              autoconf
              libmpc
              mpfr
              gmp
              m4
              diffutils
            ];
            shellHook = ''
                set -a;
                export LIMINE_BIOS_SYS=${pkgs.limine-full}/share/limine/limine-bios.sys;
                export LIMINE_BIOS_CD_BIN=${pkgs.limine-full}/share/limine/limine-bios-cd.bin;
                set +a;
            '';
          };
        };
      };
}
