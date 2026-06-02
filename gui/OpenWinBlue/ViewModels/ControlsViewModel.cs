// gui/OpenWinBlue/ViewModels/ControlsViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;

namespace OpenWinBlue.ViewModels;

public partial class ControlsViewModel : ObservableObject
{
    [ObservableProperty] private bool _hfpGuardEnabled = true;
    [ObservableProperty] private bool _noiseReduction  = false;
    [ObservableProperty] private bool _adaptiveBitrate = true;

    public string HfpGuardNote =>
        HfpGuardEnabled
        ? "HFP guard is active — headphones stay in A2DP stereo mode."
        : "HFP guard disabled — headphones may switch to mono when mic is used.";

    partial void OnHfpGuardEnabledChanged(bool value)
        => OnPropertyChanged(nameof(HfpGuardNote));
}
