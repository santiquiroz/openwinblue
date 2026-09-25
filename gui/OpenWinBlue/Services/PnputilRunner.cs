using System.Diagnostics;

namespace OpenWinBlue.Services;

public sealed record PnputilResult(int ExitCode, string Output)
{
    private const int ErrorSuccessRebootRequired = 3010;

    public bool Succeeded => ExitCode is 0 or ErrorSuccessRebootRequired;
}

// The GUI runs requireAdministrator, so pnputil inherits the elevation: no runas, no helper scripts.
public static class PnputilRunner
{
    public static PnputilResult Run(params string[] arguments)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName               = "pnputil.exe",
            RedirectStandardOutput = true,
            UseShellExecute        = false,
            CreateNoWindow         = true,
        };
        foreach (var argument in arguments)
            startInfo.ArgumentList.Add(argument);

        using var proc = Process.Start(startInfo)
            ?? throw new InvalidOperationException("pnputil.exe could not be started");
        var output = proc.StandardOutput.ReadToEnd();
        proc.WaitForExit();
        return new PnputilResult(proc.ExitCode, output);
    }
}
