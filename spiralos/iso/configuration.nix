{ modulesPath, ... }:

{
  imports = [
    "${modulesPath}/installer/cd-dvd/installation-cd-minimal.nix"
    ../modules/installer.nix
  ];

  # the live media's own job is to run spiralos-installer on tty1, not to
  # run the post-install first-boot wizard (that belongs to the target
  # system spiralos-installer writes).
  spiralos.wizard.enable = false;

  networking.hostName = "spiralos-installer";
  time.timeZone = "America/Los_Angeles";

  image.fileName = "spiralos.iso";
}
