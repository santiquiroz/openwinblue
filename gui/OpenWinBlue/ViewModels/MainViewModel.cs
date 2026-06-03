// gui/OpenWinBlue/ViewModels/MainViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;
using OpenWinBlue.Models;
using OpenWinBlue.Services;
using System.Windows;

namespace OpenWinBlue.ViewModels;

public partial class MainViewModel : ObservableObject
{
    public StatusViewModel   Status   { get; } = new();
    public CodecViewModel    Codec    { get; }
    public DriverViewModel   Driver   { get; } = new();
    public ControlsViewModel Controls { get; }

    [ObservableProperty] private int    _selectedTab = 0;
    [ObservableProperty] private string _titleSuffix = "— not connected";

    private readonly IpcClientService _ipc;

    // Design-time constructor (no-arg) used by XAML designer.
    public MainViewModel() : this(new IpcClientService()) { }

    public MainViewModel(IpcClientService ipc)
    {
        _ipc = ipc;
        Codec    = new CodecViewModel(ipc);
        Controls = new ControlsViewModel(ipc, new DriverInstallerService());
        _ipc.StatusReceived += OnStatusReceived;
    }

    public void StartService() => _ipc.Start();
    public void StopService()  => _ipc.Stop();

    private void OnStatusReceived(StatusPayload payload)
    {
        var dispatcher = System.Windows.Application.Current?.Dispatcher;
        if (dispatcher != null)
        {
            dispatcher.Invoke(() =>
            {
                Status.SetConnected(true);
                Status.Update(payload);
                TitleSuffix = $"— {payload.CodecName}, {payload.Bitrate / 1000} kbps";
            });
        }
        else
        {
            Status.SetConnected(true);
            Status.Update(payload);
            TitleSuffix = $"— {payload.CodecName}, {payload.Bitrate / 1000} kbps";
        }
    }
}
