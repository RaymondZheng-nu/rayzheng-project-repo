final: prev: {
  spiralos-wizard = final.callPackage ../pkgs/spiralos-wizard { };
  spiralos-installer = final.callPackage ../pkgs/spiralos-installer { };
  spiralos-rebuild = final.callPackage ../pkgs/spiralos-rebuild { };
}
