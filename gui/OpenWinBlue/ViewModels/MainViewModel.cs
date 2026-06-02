using CommunityToolkit.Mvvm.ComponentModel;

namespace OpenWinBlue.ViewModels;

public partial class MainViewModel : ObservableObject
{
    [ObservableProperty]
    private string _statusMessage = "OpenWinBlue — ready";
}
