using CommunityToolkit.Mvvm.ComponentModel;

namespace OpenWinBlue.ViewModels;

public partial class CodecViewModel : ObservableObject
{
    [ObservableProperty] private int    _bitpool     = 53;
    [ObservableProperty] private string _channelMode = "Joint Stereo";
    [ObservableProperty] private int    _sampleRate  = 44100;
}
