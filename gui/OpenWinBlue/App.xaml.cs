using System.Windows;
using System.Windows.Forms;
using OpenWinBlue.Services;
using OpenWinBlue.ViewModels;
using Application = System.Windows.Application;

namespace OpenWinBlue;

public partial class App : Application
{
    private NotifyIcon?    _trayIcon;
    private MainWindow?    _mainWindow;
    private readonly IpcClientService _ipc = new();

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        SetupTrayIcon();
        ShowMainWindow();
        _ipc.Start();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _ipc.Stop();
        _trayIcon?.Dispose();
        base.OnExit(e);
    }

    private void SetupTrayIcon()
    {
        _trayIcon = new NotifyIcon {
            Icon    = SystemIcons.Application,
            Text    = "OpenWinBlue",
            Visible = true,
        };

        var menu = new ContextMenuStrip();
        menu.Items.Add("Open",  null, (_, _) => ShowMainWindow());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit",  null, (_, _) => Shutdown());
        _trayIcon.ContextMenuStrip = menu;
        _trayIcon.DoubleClick     += (_, _) => ShowMainWindow();
    }

    private void ShowMainWindow()
    {
        if (_mainWindow == null || !_mainWindow.IsLoaded) {
            var vm = new MainViewModel(_ipc);
            _mainWindow = new MainWindow { DataContext = vm };
            _mainWindow.Show();
        } else {
            _mainWindow.Activate();
            _mainWindow.WindowState = WindowState.Normal;
        }
    }
}
