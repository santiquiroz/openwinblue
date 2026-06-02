using OpenWinBlue.ViewModels;

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
}
