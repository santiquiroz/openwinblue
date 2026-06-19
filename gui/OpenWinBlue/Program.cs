using System.IO;

namespace OpenWinBlue;

public static class Program
{
    [STAThread]
    public static void Main(string[] args)
    {
        var earlyLog = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "OpenWinBlue", "startup.log");

        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(earlyLog)!);
            File.AppendAllText(earlyLog,
                $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] Process started — OS: {Environment.OSVersion}, .NET: {Environment.Version}\n");

            var app = new App();
            app.InitializeComponent();
            app.Run();
        }
        catch (Exception ex)
        {
            var msg = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] FATAL: {ex.GetType().Name}: {ex.Message}\n{ex.StackTrace}\n";
            try { File.AppendAllText(earlyLog, msg); } catch { }

            System.Windows.MessageBox.Show(
                $"OpenWinBlue no pudo iniciar:\n\n{ex.Message}\n\nLog de diagnóstico:\n{earlyLog}",
                "OpenWinBlue — Error fatal",
                System.Windows.MessageBoxButton.OK,
                System.Windows.MessageBoxImage.Error);
        }
    }
}
