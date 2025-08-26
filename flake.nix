{
  outputs = {
    self,
    nixpkgs,
  }: let
    forAllSystems = nixpkgs.lib.genAttrs ["x86_64-linux" "aarch64-linux"];
  in {
    packages = forAllSystems (system: let
      pkgs = nixpkgs.legacyPackages.${system};
      mkAstalPkg = import ./nix/mkAstalPkg.nix pkgs;
      mkPkg = src:
        import src {
          inherit self pkgs;
          mkAstalPkg = mkAstalPkg;
        };

      mkTarball = pkg: pkgs.runCommand "${pkg.pname}-tarball" {
        nativeBuildInputs = [ pkgs.xz ];
      } ''
        mkdir -p $out
        tar -cJf $out/${pkg.pname}-${pkg.version}.tar.xz -C ${pkg} .
        sha256sum $out/${pkg.pname}-${pkg.version}.tar.xz > $out/${pkg.pname}-${pkg.version}.tar.xz.sha256
      '';

      packages = {
        io = mkPkg ./lib/astal/io;
        astal3 = mkPkg ./lib/astal/gtk3;
        astal4 = mkPkg ./lib/astal/gtk4;
        apps = mkPkg ./lib/apps;
        auth = mkPkg ./lib/auth;
        battery = mkPkg ./lib/battery;
        bluetooth = mkPkg ./lib/bluetooth;
        cava = mkPkg ./lib/cava;
        greet = mkPkg ./lib/greet;
        hyprland = mkPkg ./lib/hyprland;
        mpris = mkPkg ./lib/mpris;
        network = mkPkg ./lib/network;
        notifd = mkPkg ./lib/notifd;
        powerprofiles = mkPkg ./lib/powerprofiles;
        river = mkPkg ./lib/river;
        tray = mkPkg ./lib/tray;
        wireplumber = mkPkg ./lib/wireplumber;
      };
    in packages // {
      default = packages.io;
      docs = import ./docs {inherit self pkgs;};

      io-tarball = mkTarball packages.io;
      astal3-tarball = mkTarball packages.astal3;
      astal4-tarball = mkTarball packages.astal4;
      apps-tarball = mkTarball packages.apps;
      auth-tarball = mkTarball packages.auth;
      battery-tarball = mkTarball packages.battery;
      bluetooth-tarball = mkTarball packages.bluetooth;
      cava-tarball = mkTarball packages.cava;
      greet-tarball = mkTarball packages.greet;
      hyprland-tarball = mkTarball packages.hyprland;
      mpris-tarball = mkTarball packages.mpris;
      network-tarball = mkTarball packages.network;
      notifd-tarball = mkTarball packages.notifd;
      powerprofiles-tarball = mkTarball packages.powerprofiles;
      river-tarball = mkTarball packages.river;
      tray-tarball = mkTarball packages.tray;
      wireplumber-tarball = mkTarball packages.wireplumber;

      all-tarballs = pkgs.runCommand "all-astal-tarballs" {
        nativeBuildInputs = [ pkgs.xz ];
      } ''
        mkdir -p $out
        ${pkgs.lib.concatMapStringsSep "\n" (pkg: ''
          tar -cJf $out/${pkg.pname}-${pkg.version}.tar.xz -C ${pkg} .
          sha256sum $out/${pkg.pname}-${pkg.version}.tar.xz > $out/${pkg.pname}-${pkg.version}.tar.xz.sha256
        '') (builtins.attrValues packages)}
      '';
    });

    devShells = forAllSystems (system:
      import ./nix/devshell.nix {
        inherit self;
        pkgs = nixpkgs.legacyPackages.${system};
      });
  };

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };
}
