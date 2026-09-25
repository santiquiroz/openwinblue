using System.Text.RegularExpressions;

namespace OpenWinBlue.Services;

public sealed record A2dpDriverBinding(string InstanceId, string DriverInf, bool UsesOwbDriver);

public static partial class A2dpDriverMap
{
    private const string A2dpSinkUuid = "0000110b-0000-1000-8000-00805f9b34fb";

    // Keyed by the remote MAC without colons (AABBCCDDEEFF), uppercase.
    public static IReadOnlyDictionary<string, A2dpDriverBinding> Build(
        IEnumerable<PnpDeviceDriver> devices, IReadOnlyCollection<string> owbPublishedNames)
    {
        var result = new Dictionary<string, A2dpDriverBinding>(StringComparer.OrdinalIgnoreCase);
        foreach (var device in devices.Where(IsA2dpSink))
        {
            if (ExtractMac(device.InstanceId) is not { } mac) continue;
            result[mac] = new A2dpDriverBinding(
                device.InstanceId, device.DriverInf, IsOwbDriver(device.DriverInf, owbPublishedNames));
        }
        return result;
    }

    // pnputil reports third-party drivers by published name (oemNN.inf), never as owb_a2dp.inf.
    public static bool IsOwbDriver(string driverInf, IReadOnlyCollection<string> owbPublishedNames) =>
        driverInf.Length > 0 && owbPublishedNames.Contains(driverInf, StringComparer.OrdinalIgnoreCase);

    private static bool IsA2dpSink(PnpDeviceDriver device) =>
        device.InstanceId.Contains(A2dpSinkUuid, StringComparison.OrdinalIgnoreCase);

    // Instance ID ends in ...&<12-hex MAC>_C00000000.
    private static string? ExtractMac(string instanceId)
    {
        var match = MacPattern().Match(instanceId);
        return match.Success ? match.Groups[1].Value.ToUpperInvariant() : null;
    }

    [GeneratedRegex("([0-9A-Fa-f]{12})_C0")]
    private static partial Regex MacPattern();
}
