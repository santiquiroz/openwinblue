// gui/OpenWinBlue/ViewModels/StatusViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;
using OpenWinBlue.Models;

namespace OpenWinBlue.ViewModels;

public partial class StatusViewModel : ObservableObject
{
    [ObservableProperty] private string _connectionState = "Disconnected";
    [ObservableProperty] private string _activeCodec     = "—";
    [ObservableProperty] private string _bitrateText     = "—";
    [ObservableProperty] private bool   _isStreaming      = false;
    [ObservableProperty] private bool   _hfpGuardActive   = false;

    public void Update(StatusPayload status)
    {
        ActiveCodec    = string.IsNullOrEmpty(status.CodecName) ? "—" : status.CodecName;
        BitrateText    = status.Bitrate > 0
                         ? $"{status.Bitrate / 1000} kbps"
                         : "—";
        IsStreaming    = status.IsCapturing == 1;
        HfpGuardActive = status.HfpGuardOn  == 1;
    }

    public void SetConnected(bool connected)
        => ConnectionState = connected ? "Connected" : "Disconnected";
}
