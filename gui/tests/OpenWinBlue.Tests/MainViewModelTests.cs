using OpenWinBlue.ViewModels;

namespace OpenWinBlue.Tests;

public class MainViewModelTests
{
    [Fact]
    public void MainViewModel_Instantiates_WithReadyMessage()
    {
        var vm = new MainViewModel();
        Assert.Equal("OpenWinBlue — ready", vm.StatusMessage);
    }

    [Fact]
    public void MainViewModel_StatusMessage_RaisesPropertyChanged()
    {
        var vm = new MainViewModel();
        var changed = new List<string?>();
        vm.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

        vm.StatusMessage = "connecting…";

        Assert.Contains(nameof(vm.StatusMessage), changed);
    }
}
