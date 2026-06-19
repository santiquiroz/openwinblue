using System.IO;

namespace OpenWinBlue;

public static class Program
{
    private static string _earlyLog = "";

    [STAThread]
    public static void Main(string[] args)
    {
        _earlyLog = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "OpenWinBlue", "startup.log");

        Directory.CreateDirectory(Path.GetDirectoryName(_earlyLog)!);

        Log($"=== STARTUP OS:{Environment.OSVersion} .NET:{Environment.Version} ===");
        try
        {
            Log("Step 1: new App()...");
            var app = new App();
            Log("Step 2: InitializeComponent()...");
            app.InitializeComponent();
            Log("Step 3: app.Run()...");
            app.Run();
            Log("Step 4: app.Run() returned normally");
        }
        catch (Exception ex)
        {
            var msg = $"FATAL {ex.GetType().Name}: {ex.Message}\n{ex.StackTrace}";
            Log(msg);
            System.Windows.MessageBox.Show(
                $"OpenWinBlue no pudo iniciar:\n\n{ex.Message}\n\nLog:\n{_earlyLog}",
                "OpenWinBlue — Error fatal",
                System.Windows.MessageBoxButton.OK,
                System.Windows.MessageBoxImage.Error);
        }
        Log("=== PROCESS EXIT ===");
    }

    private static void Log(string msg)
    {
        var line = $"[{DateTime.Now:HH:mm:ss.fff}] {msg}";
        try { File.AppendAllText(_earlyLog, line + "\n"); } catch { }
    }
}
