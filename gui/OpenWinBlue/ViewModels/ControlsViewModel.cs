using CommunityToolkit.Mvvm.ComponentModel;
using OpenWinBlue.Services;

namespace OpenWinBlue.ViewModels;

public partial class ControlsViewModel : ObservableObject
{
    private readonly IIpcSender _ipc;

    [ObservableProperty] private bool _hfpGuardEnabled  = true;
    [ObservableProperty] private bool _noiseReduction   = false;
    [ObservableProperty] private bool _adaptiveBitrate  = true;

    public string HfpGuardNote =>
        HfpGuardEnabled
        ? "HFP guard is active — headphones stay in A2DP stereo mode."
        : "HFP guard disabled — headphones may switch to mono when mic is used.";

    // Design-time constructor
    public ControlsViewModel() : this(new NullIpcSender()) { }

    public ControlsViewModel(IIpcSender ipc)
    {
        _ipc = ipc;
    }

    partial void OnHfpGuardEnabledChanged(bool value)
        => OnPropertyChanged(nameof(HfpGuardNote));

    partial void OnNoiseReductionChanged(bool value)
        => _ipc.SendSetCodec("AI", "noise_reduction", value ? 1L : 0L);

    partial void OnAdaptiveBitrateChanged(bool value)
        => _ipc.SendSetCodec("AI", "adaptive_bitrate", value ? 1L : 0L);

    private sealed class NullIpcSender : IIpcSender
    {
        public bool IsConnected => false;
        public bool SendSetCodec(string c, string k, long v) => false;
    }
}
