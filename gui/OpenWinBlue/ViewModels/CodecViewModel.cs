// gui/OpenWinBlue/ViewModels/CodecViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using OpenWinBlue.Services;

namespace OpenWinBlue.ViewModels;

public partial class CodecViewModel : ObservableObject
{
    private readonly IIpcSender _ipc;

    [ObservableProperty] private int _bitpool           = 53;
    [ObservableProperty] private int _channelModeIndex  = 3; // Joint Stereo
    [ObservableProperty] private int _sampleRateIndex   = 0; // 44100 Hz
    [ObservableProperty] private int _selectedCodecIndex = 0; // SBC

    public string[] AvailableCodecs { get; } = { "SBC", "aptX", "aptX-HD", "LDAC" };
    public string[] ChannelModes    { get; } = { "Mono", "Stereo", "Dual Channel", "Joint Stereo" };
    public string[] SampleRates     { get; } = { "44 100 Hz", "48 000 Hz" };
    private static readonly int[] SampleRateHz = { 44100, 48000 };

    // Design-time constructor
    public CodecViewModel() : this(new NullIpcSender()) { }

    public CodecViewModel(IIpcSender ipc)
    {
        _ipc = ipc;
    }

    [RelayCommand(CanExecute = nameof(CanApply))]
    private void ApplyCodec()
    {
        var codec = AvailableCodecs[SelectedCodecIndex];
        _ipc.SendSetCodec(codec, "switch", 1);
        _ipc.SendSetCodec(codec, "bitpool", Bitpool);
        _ipc.SendSetCodec(codec, "mode",    ChannelModeIndex);
        _ipc.SendSetCodec(codec, "freq",    SampleRateHz[SampleRateIndex]);
    }

    private bool CanApply() =>
        _ipc.IsConnected && Bitpool >= 2 && Bitpool <= 64;

    partial void OnBitpoolChanged(int value)
        => ApplyCodecCommand.NotifyCanExecuteChanged();

    partial void OnSelectedCodecIndexChanged(int value)
        => ApplyCodecCommand.NotifyCanExecuteChanged();

    /// <summary>Null-object sender for design-time and disconnected state.</summary>
    private sealed class NullIpcSender : IIpcSender
    {
        public bool IsConnected => false;
        public bool SendSetCodec(string c, string k, long v) => false;
    }
}
