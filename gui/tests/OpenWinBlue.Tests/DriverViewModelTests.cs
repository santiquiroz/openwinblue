using NSubstitute;
using OpenWinBlue.Services;
using OpenWinBlue.ViewModels;

namespace OpenWinBlue.Tests;

public class DriverViewModelTests
{
    [Fact]
    public void DriverViewModel_ConstructsWithNotInstalledStatus()
    {
        var installer = Substitute.For<IDriverInstaller>();
        installer.IsInstalled.Returns(false);
        var vm = new DriverViewModel(installer);
        Assert.Equal("Not installed", vm.DriverStatus);
    }

    [Fact]
    public void DriverViewModel_RefreshStatus_UpdatesDriverStatus()
    {
        var installer = Substitute.For<IDriverInstaller>();
        installer.IsInstalled.Returns(true);
        var vm = new DriverViewModel(installer);
        vm.RefreshStatus();
        Assert.Equal("Installed ✓", vm.DriverStatus);
    }
}
