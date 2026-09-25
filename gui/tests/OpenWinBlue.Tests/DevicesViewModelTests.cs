using OpenWinBlue.ViewModels;

namespace OpenWinBlue.Tests;

// DevicesViewModel's constructor scans Bluetooth devices, so only its pure helpers are tested here.
public class DevicesViewModelTests
{
    [Fact]
    public void DescribeApplyResult_WhenAcked_ReportsAppliedCodec()
    {
        var message = DevicesViewModel.DescribeApplyResult(true, "LDAC", 990, "WH-1000XM4");
        Assert.Equal("LDAC a 990 kbps aplicado a WH-1000XM4.", message);
    }

    [Fact]
    public void DescribeApplyResult_WhenRejected_DoesNotClaimSuccess()
    {
        var message = DevicesViewModel.DescribeApplyResult(false, "LDAC", 990, "WH-1000XM4");
        Assert.DoesNotContain("aplicado", message);
        Assert.Contains("rechazó", message);
    }
}
