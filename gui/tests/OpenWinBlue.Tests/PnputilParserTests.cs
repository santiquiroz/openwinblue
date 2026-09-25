using OpenWinBlue.Services;

namespace OpenWinBlue.Tests;

public class PnputilParserTests
{
    private const string OwbInf = "owb_a2dp.inf";

    [Fact]
    public void PnputilParser_FindsPublishedNameForOwbInf()
    {
        var names = PnputilParser.ParsePublishedNames(Fixture.Read("pnputil-enum-drivers-en.txt"), OwbInf);
        Assert.Equal(["oem42.inf"], names);
    }

    [Fact]
    public void PnputilParser_SpanishLocale()
    {
        var names = PnputilParser.ParsePublishedNames(Fixture.Read("pnputil-enum-drivers-es.txt"), OwbInf);
        Assert.Equal(["oem108.inf"], names);
    }

    [Fact]
    public void PnputilParser_NoMatchReturnsEmpty()
    {
        var names = PnputilParser.ParsePublishedNames(Fixture.Read("pnputil-enum-drivers-en.txt"), "missing.inf");
        Assert.Empty(names);
    }

    [Fact]
    public void PnputilParser_ReadsCrLfOutputAndEveryInstalledCopy()
    {
        var output = Fixture.Read("pnputil-enum-drivers-en.txt") +
                     "Published Name:     oem77.inf\nOriginal Name:      owb_a2dp.inf\n";
        var names = PnputilParser.ParsePublishedNames(output.Replace("\n", "\r\n"), OwbInf);
        Assert.Equal(["oem42.inf", "oem77.inf"], names);
    }

    [Fact]
    public void PnputilParser_DeviceDriversUseInstanceIdAndDriverName()
    {
        var devices = PnputilParser.ParseDeviceDrivers(Fixture.Read("pnputil-enum-devices-ids-en.txt"));

        Assert.Equal(4, devices.Count);
        Assert.EndsWith("AC800A1B2C3D_C00000000", devices[0].InstanceId);
        Assert.Equal("oem42.inf", devices[0].DriverInf);
        Assert.Equal(string.Empty, devices[3].DriverInf);
    }

    [Fact]
    public void DeviceDriverMapping_MarksOwb()
    {
        var devices = PnputilParser.ParseDeviceDrivers(Fixture.Read("pnputil-enum-devices-ids-en.txt"));

        var map = A2dpDriverMap.Build(devices, ["oem42.inf"]);

        Assert.Equal(3, map.Count);
        Assert.True(map["AC800A1B2C3D"].UsesOwbDriver);
        Assert.False(map["F0B3EC112233"].UsesOwbDriver);
        Assert.Equal("bthavctpsnk.inf", map["F0B3EC112233"].DriverInf);
        Assert.False(map["001122334455"].UsesOwbDriver);
    }

    [Fact]
    public void DeviceDriverMapping_WithoutOwbPackageMarksNothing()
    {
        var devices = PnputilParser.ParseDeviceDrivers(Fixture.Read("pnputil-enum-devices-ids-en.txt"));

        var map = A2dpDriverMap.Build(devices, []);

        Assert.All(map.Values, binding => Assert.False(binding.UsesOwbDriver));
    }
}
