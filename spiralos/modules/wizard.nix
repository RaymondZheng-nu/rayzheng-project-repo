{ config, lib, pkgs, ... }:

with lib;

{
  options.spiralos.wizard.enable = mkEnableOption "first-boot setup wizard" // {
    default = true;
  };

  config = mkIf config.spiralos.wizard.enable {
    environment.systemPackages = [ pkgs.spiralos-wizard ];

    # runs once on the first console before login, on a freshly installed
    # system. re-runnable by hand afterwards via `spiralos-wizard --force`.
    systemd.services.spiralos-wizard = {
      description = "SpiralOS first-boot setup wizard";
      conflicts = [ "getty@tty1.service" ];
      after = [ "getty@tty1.service" "systemd-user-sessions.service" ];
      before = [ "multi-user.target" ];
      unitConfig.ConditionPathExists = "!/etc/spiralos/wizard-done";
      serviceConfig = {
        Type = "oneshot";
        RemainAfterExit = true;
        ExecStart = "${pkgs.spiralos-wizard}/bin/spiralos-wizard";
        StandardInput = "tty";
        StandardOutput = "tty";
        TTYPath = "/dev/tty1";
        TTYReset = true;
        TTYVHangup = true;
        TTYVTDisallocate = true;
      };
      wantedBy = [ "multi-user.target" ];
    };
  };
}
