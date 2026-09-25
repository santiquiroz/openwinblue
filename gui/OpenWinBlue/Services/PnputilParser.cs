using System.Text.RegularExpressions;

namespace OpenWinBlue.Services;

public sealed record PnpDeviceDriver(string InstanceId, string DriverInf);

// pnputil labels are localized ("Published Name" / "Nombre publicado"), so blocks are read by value only.
public static partial class PnputilParser
{
    public static IReadOnlyList<string> ParsePublishedNames(string enumDriversOutput, string originalName) =>
        SplitBlocks(enumDriversOutput)
            .Where(values => values.Contains(originalName, StringComparer.OrdinalIgnoreCase))
            .Select(values => values.FirstOrDefault(IsPublishedName))
            .OfType<string>()
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .ToList();

    // Each /enum-devices block starts with the instance ID.
    public static IReadOnlyList<PnpDeviceDriver> ParseDeviceDrivers(string enumDevicesOutput) =>
        SplitBlocks(enumDevicesOutput)
            .Select(values => new PnpDeviceDriver(values[0], values.FirstOrDefault(IsInfFileName) ?? string.Empty))
            .ToList();

    private static IEnumerable<IReadOnlyList<string>> SplitBlocks(string output)
    {
        var block = new List<string>();
        foreach (var line in output.Split('\n'))
        {
            if (string.IsNullOrWhiteSpace(line))
            {
                if (block.Count > 0) yield return block;
                block = [];
                continue;
            }
            if (ValueOf(line) is { } value) block.Add(value);
        }
        if (block.Count > 0) yield return block;
    }

    private static string? ValueOf(string line)
    {
        var colon = line.IndexOf(':');
        if (colon < 0) return null;
        var value = line[(colon + 1)..].Trim();
        return value.Length == 0 ? null : value;
    }

    private static bool IsPublishedName(string value) => PublishedNamePattern().IsMatch(value);

    private static bool IsInfFileName(string value) => InfFileNamePattern().IsMatch(value);

    [GeneratedRegex(@"^oem\d+\.inf$", RegexOptions.IgnoreCase)]
    private static partial Regex PublishedNamePattern();

    [GeneratedRegex(@"^[^\\/\s]+\.inf$", RegexOptions.IgnoreCase)]
    private static partial Regex InfFileNamePattern();
}
