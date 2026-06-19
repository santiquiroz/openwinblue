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
                using var proc = Process.Start(new ProcessStartInfo
                {
                    FileName               = "pnputil.exe",
                    Arguments              = "/enum-drivers",
                    RedirectStandardOutput = true,
                    UseShellExecute        = false,
                    CreateNoWindow         = true,
                });
                var output = proc?.StandardOutput.ReadToEnd() ?? string.Empty;
                proc?.WaitForExit(5_000);
                var installed = output.Contains("owb_a2dp.inf", StringComparison.OrdinalIgnoreCase);
                OWBLogger.Info($"IsInstalled check → {installed}");
                return installed;
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
        Process.Start(new ProcessStartInfo
        {
            FileName        = "pnputil.exe",
            Arguments       = $"/add-driver \"{infPath}\" /install",
            Verb            = "runas",
            UseShellExecute = true,
        });
    }

    public void Rollback()
    {
        OWBLogger.Info("Rolling back owb_a2dp driver");
        Process.Start(new ProcessStartInfo
        {
            FileName        = "pnputil.exe",
            Arguments       = "/delete-driver owb_a2dp.inf /uninstall /force",
            Verb            = "runas",
            UseShellExecute = true,
        });
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
