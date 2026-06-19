using System.IO;
using System.Runtime.CompilerServices;

namespace OpenWinBlue.Services;

public static class OWBLogger
{
    private static readonly string LogPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "OpenWinBlue", "owb.log");

    private static readonly object _lock = new();
    private const long MaxLogBytes = 2 * 1024 * 1024; // 2 MB — rota a .bak

    static OWBLogger()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
        RotateIfNeeded();
        Write("INFO", "OWBLogger", $"=== OpenWinBlue session started — log: {LogPath} ===");
    }

    public static string LogFilePath => LogPath;

    public static void Info(string message, [CallerMemberName] string caller = "")
        => Write("INFO ", caller, message);

    public static void Warn(string message, [CallerMemberName] string caller = "")
        => Write("WARN ", caller, message);

    public static void Error(string message, [CallerMemberName] string caller = "")
        => Write("ERROR", caller, message);

    public static void Error(Exception ex, string context = "", [CallerMemberName] string caller = "")
        => Write("ERROR", caller, $"{context}: {ex.GetType().Name}: {ex.Message}{Environment.NewLine}{ex.StackTrace}");

    private static void Write(string level, string caller, string message)
    {
        var line = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [{level}] [{caller}] {message}";
        System.Diagnostics.Debug.WriteLine(line);
        lock (_lock)
        {
            try { File.AppendAllText(LogPath, line + Environment.NewLine); }
            catch { /* never crash the app because of logging */ }
        }
    }

    private static void RotateIfNeeded()
    {
        try
        {
            if (!File.Exists(LogPath)) return;
            if (new FileInfo(LogPath).Length <= MaxLogBytes) return;
            var backup = LogPath + ".bak";
            if (File.Exists(backup)) File.Delete(backup);
            File.Move(LogPath, backup);
        }
        catch { }
    }
}
