using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using OpenWinBlue.Services;
using System.IO;

namespace OpenWinBlue.ViewModels;

public partial class DriverViewModel : ObservableObject
{
    private readonly IDriverInstaller _installer;

    [ObservableProperty] private string _driverStatus = "Checking…";
    [ObservableProperty] private bool   _canInstall   = false;
    [ObservableProperty] private bool   _canRollback  = false;

    // Design-time constructor
    public DriverViewModel() : this(new DriverInstallerService()) { }

    public DriverViewModel(IDriverInstaller installer)
    {
        _installer = installer;
        RefreshStatus();
    }

    public void RefreshStatus()
    {
        bool installed  = _installer.IsInstalled;
        DriverStatus    = installed ? "Installed ✓" : "Not installed";
        CanInstall      = !installed;
        CanRollback     =  installed;
    }

    [RelayCommand(CanExecute = nameof(CanInstall))]
    private void InstallDriver()
    {
        var infPath = Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory,
            "driver", "owb_a2dp.inf");
        _installer.Install(infPath);
        RefreshStatus();
    }

    [RelayCommand(CanExecute = nameof(CanRollback))]
    private void RollbackDriver()
    {
        _installer.Rollback();
        RefreshStatus();
    }
}
