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
    [ObservableProperty] private string _applyStatus    = "";

    public const int MinBitpool = 2;
    public const int MaxBitpool = 53;

    public string[] AvailableCodecs { get; } = { "SBC", "aptX", "aptX-HD", "LDAC", "AAC", "LC3", "aptX-Adaptive" };
    // Index == SBC_MODE_* value in libsbc's sbc.h, sent as the "mode" parameter.
    public string[] ChannelModes    { get; } = { "Mono", "Dual Channel", "Stereo", "Joint Stereo" };
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
        var codec    = AvailableCodecs[SelectedCodecIndex];
        var rejected = SendRequests(codec);
        ApplyStatus = rejected.Count == 0
            ? $"{codec} applied."
            : $"Service rejected: {string.Join(", ", rejected)}.";
    }

    private List<string> SendRequests(string codec)
    {
        var rejected = new List<string>();
        foreach (var (key, value) in BuildRequests(codec))
            if (!_ipc.SendSetCodec(codec, key, value)) rejected.Add(key);
        return rejected;
    }

    private IEnumerable<KeyValuePair<string, long>> BuildRequests(string codec)
    {
        yield return new("switch", 1);
        if (codec == "SBC") {
            yield return new("bitpool", Bitpool);
            yield return new("mode",    ChannelModeIndex);
        }
        yield return new("freq", SampleRateHz[SampleRateIndex]);
    }

    private bool CanApply() =>
        _ipc.IsConnected && Bitpool >= MinBitpool && Bitpool <= MaxBitpool;

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
