using OpenWinBlue.Models;
using OpenWinBlue.Services;

namespace OpenWinBlue.Tests;

public class IpcClientServiceTests
{
    [Fact]
    public void IpcClientService_ConstructsWithoutCrash()
    {
        var svc = new IpcClientService();
        Assert.False(svc.IsConnected);
    }

    [Fact]
    public void IpcClientService_StopBeforeStartDoesNotThrow()
    {
        var svc = new IpcClientService();
        svc.Stop();  // must not throw
    }

    [Fact]
    public void IpcClientService_StatusReceived_EventIsWireable()
    {
        var svc = new IpcClientService();
        int eventCount = 0;
        svc.StatusReceived += _ => eventCount++;
        // No service running — just verify event subscription doesn't throw
        Assert.Equal(0, eventCount);
        svc.Stop();
    }
}
