using System.IO;
using System.Runtime.InteropServices;

namespace OpenWinBlue.Services;

public sealed record DriverActivationResult(bool Succeeded, bool RebootRequired, int Win32Error);

// Overrides WHQL driver ranking so every A2DP sink picks up the OWB package.
public static class PnpDriverActivator
{
    private const string A2dpSinkCompatibleId = @"BTHENUM\{0000110b-0000-1000-8000-00805f9b34fb}";
    private const uint   InstallFlagForce     = 0x00000001;

    [DllImport("newdev.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UpdateDriverForPlugAndPlayDevicesW(
        IntPtr hwndParent,
        string hardwareId,
        string fullInfPath,
        uint installFlags,
        [MarshalAs(UnmanagedType.Bool)] out bool rebootRequired);

    public static DriverActivationResult ForceOnA2dpSinks(string infPath)
    {
        var ok = UpdateDriverForPlugAndPlayDevicesW(
            IntPtr.Zero, A2dpSinkCompatibleId, Path.GetFullPath(infPath), InstallFlagForce, out var reboot);
        return new DriverActivationResult(ok, reboot, ok ? 0 : Marshal.GetLastWin32Error());
    }
}
