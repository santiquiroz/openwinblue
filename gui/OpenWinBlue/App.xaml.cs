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
        OWBLogger.Info($"App starting — OS: {Environment.OSVersion}, .NET: {Environment.Version}");

        try
        {
            var dict = new System.Windows.ResourceDictionary
            {
                Source = new Uri("pack://application:,,,/Themes/Dark.xaml", UriKind.Absolute)
            };
            Resources.MergedDictionaries.Add(dict);
            OWBLogger.Info("Theme loaded OK");
        }
        catch (Exception ex)
        {
            OWBLogger.Error(ex, "Theme load failed — continuing without theme");
        }

        // Captura excepciones en el dispatcher (después de que inicia el loop)
        DispatcherUnhandledException += (_, ex) => {
            OWBLogger.Error(ex.Exception, "DispatcherUnhandledException");
            System.Windows.MessageBox.Show(
                $"Error inesperado:\n\n{ex.Exception.Message}\n\nVer log completo en:\n{OWBLogger.LogFilePath}",
                "OpenWinBlue — Error",
                System.Windows.MessageBoxButton.OK,
                System.Windows.MessageBoxImage.Error);
            ex.Handled = true;
        };

        // Captura excepciones en hilos de background
        AppDomain.CurrentDomain.UnhandledException += (_, ex) => {
            OWBLogger.Error(ex.ExceptionObject as Exception ?? new Exception(ex.ExceptionObject?.ToString()),
                "UnhandledException");
        };

        try
        {
            SetupTrayIcon();
            ShowMainWindow();
            _ipc.Start();
            OWBLogger.Info("App startup complete");
        }
        catch (Exception ex)
        {
            OWBLogger.Error(ex, "Fatal error during startup");
            System.Windows.MessageBox.Show(
                $"OpenWinBlue no pudo iniciar:\n\n{ex.Message}\n\nLog en:\n{OWBLogger.LogFilePath}",
                "OpenWinBlue — Error fatal",
                System.Windows.MessageBoxButton.OK,
                System.Windows.MessageBoxImage.Error);
            Shutdown(1);
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        OWBLogger.Info("App exiting");
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
