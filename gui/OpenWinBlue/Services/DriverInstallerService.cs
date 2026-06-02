using System.Diagnostics;
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
                using var sc = new ServiceController("owb_a2dp");
                _ = sc.Status;
                return true;
            }
            catch (InvalidOperationException)
            {
                return false;
            }
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
            Arguments       = "/delete-driver owb_a2dp.inf /uninstall",
            Verb            = "runas",
            UseShellExecute = true,
        });
    }
}
