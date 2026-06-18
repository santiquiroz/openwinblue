using System.Diagnostics;
using System.IO;
using System.ServiceProcess;

namespace OpenWinBlue.Services;

public sealed class DriverInstallerService : IDriverInstaller
{
    // True when owb_a2dp.inf is registered in the Windows driver store.
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
                return output.Contains("owb_a2dp.inf", StringComparison.OrdinalIgnoreCase);
            }
            catch { return false; }
        }
    }

    public void Install(string infPath)
    {
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
        try
        {
            using var sc = new ServiceController("BthHFSrv");
            if (sc.Status == ServiceControllerStatus.Running)
            {
                sc.Stop();
                sc.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(5));
            }
        }
        catch (InvalidOperationException) { }

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
        }
        catch (InvalidOperationException) { }
    }
}
