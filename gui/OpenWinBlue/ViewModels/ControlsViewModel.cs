using CommunityToolkit.Mvvm.ComponentModel;

namespace OpenWinBlue.ViewModels;

public partial class ControlsViewModel : ObservableObject
{
    [ObservableProperty] private bool _hfpGuardEnabled  = true;
    [ObservableProperty] private bool _noiseReduction    = false;
    [ObservableProperty] private bool _adaptiveBitrate   = true;
}
