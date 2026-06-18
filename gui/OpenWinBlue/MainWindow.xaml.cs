using System;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;

namespace OpenWinBlue;

public partial class MainWindow : Window
{
    private const int WM_DEVICECHANGE = 0x0219;

    private DispatcherTimer? _refreshDebounce;

    public MainWindow()
    {
        InitializeComponent();
        if (DataContext is ViewModels.MainViewModel vm)
            vm.StartService();
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);

        var source = HwndSource.FromHwnd(new WindowInteropHelper(this).Handle);
        source?.AddHook(WndProc);

        _refreshDebounce = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(800) };
        _refreshDebounce.Tick += (_, _) =>
        {
            _refreshDebounce.Stop();
            if (DataContext is ViewModels.MainViewModel vm)
                vm.Devices.RefreshCommand.Execute(null);
        };
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WM_DEVICECHANGE)
        {
            _refreshDebounce?.Stop();
            _refreshDebounce?.Start();
        }
        return IntPtr.Zero;
    }

    private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
    {
        if (DataContext is ViewModels.MainViewModel vm)
            vm.StopService();
    }
}
