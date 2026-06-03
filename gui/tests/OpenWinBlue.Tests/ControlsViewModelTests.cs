using NSubstitute;
using OpenWinBlue.Services;
using OpenWinBlue.ViewModels;
using System.Collections.Generic;

namespace OpenWinBlue.Tests;

public class ControlsViewModelTests
{
    [Fact]
    public void ControlsViewModel_HfpGuardEnabled_DefaultsToTrue()
    {
        var vm = new ControlsViewModel();
        Assert.True(vm.HfpGuardEnabled);
    }

    [Fact]
    public void ControlsViewModel_PropertyChanged_FiresOnToggle()
    {
        var vm = new ControlsViewModel();
        var changed = new List<string?>();
        vm.PropertyChanged += (_, e) => changed.Add(e.PropertyName);
        vm.HfpGuardEnabled = false;
        Assert.Contains(nameof(vm.HfpGuardEnabled), changed);
    }

    [Fact]
    public void ControlsViewModel_NoiseReductionToggle_SendsAiCommand()
    {
        var ipc = Substitute.For<IIpcSender>();
        ipc.IsConnected.Returns(true);
        var vm = new ControlsViewModel(ipc);
        vm.NoiseReduction = true;
        ipc.Received().SendSetCodec("AI", "noise_reduction", 1L);
    }

    [Fact]
    public void DisableHfpLevel1Command_CallsDisableHfpProfile()
    {
        var ipc       = Substitute.For<IIpcSender>();
        var installer = Substitute.For<IDriverInstaller>();
        var vm = new ControlsViewModel(ipc, installer);
        vm.DisableHfpLevel1Command.Execute(null);
        installer.Received(1).DisableHfpProfile();
    }

    [Fact]
    public void EnableHfpLevel1Command_CallsEnableHfpProfile()
    {
        var ipc       = Substitute.For<IIpcSender>();
        var installer = Substitute.For<IDriverInstaller>();
        var vm = new ControlsViewModel(ipc, installer);
        vm.EnableHfpLevel1Command.Execute(null);
        installer.Received(1).EnableHfpProfile();
    }

    [Fact]
    public void DisableHfpLevel1Command_NullInstaller_DoesNotThrow()
    {
        var vm = new ControlsViewModel();
        var ex = Record.Exception(() => vm.DisableHfpLevel1Command.Execute(null));
        Assert.Null(ex);
    }
}
