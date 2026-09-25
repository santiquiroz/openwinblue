using System.Diagnostics;
using System.IO;
using System.ServiceProcess;

namespace OpenWinBlue.Services;

public sealed class DriverInstallerService : IDriverInstaller
{
    public bool IsInstalled
    {
        get
        {
            try
            {
                var publishedNames = OwbDriverStore.FindPublishedNames();
                OWBLogger.Info($"IsInstalled check → {publishedNames.Count > 0} ({string.Join(", ", publishedNames)})");
                return publishedNames.Count > 0;
            }
            catch (Exception ex)
            {
                OWBLogger.Error(ex, "IsInstalled check failed");
                return false;
            }
        }
    }

    public void Install(string infPath)
    {
        OWBLogger.Info($"Installing driver from: {infPath}");
        var result = OwbDriverStore.Add(infPath);
        OWBLogger.Info($"pnputil /add-driver → exit={result.ExitCode}\n{result.Output.Trim()}");
    }

    public void Rollback()
    {
        OWBLogger.Info("Rolling back owb_a2dp driver");
        if (OwbDriverStore.Remove().Count == 0)
            OWBLogger.Warn($"Rollback: no {OwbDriverStore.InfName} package in the driver store");
    }

    public void DisableHfpProfile()
    {
        OWBLogger.Info("Disabling HFP profile (BthHFSrv)");
        try
        {
            using var sc = new ServiceController("BthHFSrv");
            if (sc.Status == ServiceControllerStatus.Running)
            {
                sc.Stop();
                sc.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(5));
                OWBLogger.Info("BthHFSrv stopped");
            }
        }
        catch (InvalidOperationException ex) { OWBLogger.Warn($"BthHFSrv stop: {ex.Message}"); }

        Process.Start(new ProcessStartInfo
        {
            FileName        = "sc.exe",
            Arguments       = "config BthHFSrv start= disabled",
            Verb            = "runas",
            UseShellExecute = true,
        });
    }

    public void EnableHfpProfile()
    {
        OWBLogger.Info("Enabling HFP profile (BthHFSrv)");
        Process.Start(new ProcessStartInfo
        {
            FileName        = "sc.exe",
            Arguments       = "config BthHFSrv start= demand",
            Verb            = "runas",
            UseShellExecute = true,
        });

        try
        {
            using var sc = new ServiceController("BthHFSrv");
            sc.Start();
            OWBLogger.Info("BthHFSrv started");
        }
        catch (InvalidOperationException ex) { OWBLogger.Warn($"BthHFSrv start: {ex.Message}"); }
    }
}
