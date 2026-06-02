using CommunityToolkit.Mvvm.ComponentModel;

namespace OpenWinBlue.ViewModels;

public partial class DriverViewModel : ObservableObject
{
    [ObservableProperty] private string _driverStatus = "Unknown";
}
