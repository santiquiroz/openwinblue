using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Win32;
using OpenWinBlue.Services;
using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace OpenWinBlue.ViewModels;

public sealed record BluetoothDeviceInfo(
    string   Name,
    string   Address,
    string   TypeLabel,
    string   TypeIcon,
    bool     IsAudio,
    string[] EstimatedCodecs,
    string   DriverStatus);

public partial class DevicesViewModel : ObservableObject
{
    private readonly IIpcSender _ipc;

    public ObservableCollection<BluetoothDeviceInfo> Devices { get; } = new();

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasSelection))]
    [NotifyCanExecuteChangedFor(nameof(ApplyCodecCommand))]
    [NotifyCanExecuteChangedFor(nameof(InstallDriverCommand))]
    [NotifyCanExecuteChangedFor(nameof(ResetDriverCommand))]
    private BluetoothDeviceInfo? _selectedDevice;

    [ObservableProperty] private string _statusMessage = "Escaneando dispositivos…";

    public bool HasSelection => SelectedDevice is not null;

    // ── Driver state ──────────────────────────────────────────────────────────
    public bool OwbDriverInstalled  => CheckOwbInstalled();
    public bool TestSigningEnabled  => CheckTestSigning();

    private static bool CheckOwbInstalled()
    {
        using var key = Registry.LocalMachine.OpenSubKey(
            @"SYSTEM\CurrentControlSet\Services\owb_a2dp");
        return key is not null;
    }

    // NtQuerySystemInformation works without elevation, unlike bcdedit /enum.
    [StructLayout(LayoutKind.Sequential)]
    private struct SYSTEM_CODEINTEGRITY_INFORMATION
    {
        public uint Length;
        public uint CodeIntegrityOptions;
    }

    [DllImport("ntdll.dll")]
    private static extern int NtQuerySystemInformation(
        int SystemInformationClass,
        ref SYSTEM_CODEINTEGRITY_INFORMATION SystemInformation,
        int SystemInformationLength,
        out int ReturnLength);

    private static bool CheckTestSigning()
    {
        const int SystemCodeIntegrityInformation = 103;
        const uint CODEINTEGRITY_OPTION_TESTSIGN = 0x00000002;
        try
        {
            var info = new SYSTEM_CODEINTEGRITY_INFORMATION
            {
                Length = (uint)Marshal.SizeOf<SYSTEM_CODEINTEGRITY_INFORMATION>()
            };
            int status = NtQuerySystemInformation(
                SystemCodeIntegrityInformation, ref info, Marshal.SizeOf(info), out _);
            return status == 0 && (info.CodeIntegrityOptions & CODEINTEGRITY_OPTION_TESTSIGN) != 0;
        }
        catch { return false; }
    }

    // ── Constructors ──────────────────────────────────────────────────────────
    public DevicesViewModel() : this(new NullIpcSender()) { }

    public DevicesViewModel(IIpcSender ipc)
    {
        _ipc = ipc;
        RefreshCommand.Execute(null);
    }

    // ── Commands ──────────────────────────────────────────────────────────────
    [RelayCommand]
    private void Refresh()
    {
        try
        {
            Devices.Clear();
            SelectedDevice = null;

            const string btKey = @"SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Devices";
            using var key = Registry.LocalMachine.OpenSubKey(btKey);

            if (key is null)
            {
                StatusMessage = "No hay adaptador Bluetooth o no hay dispositivos emparejados.";
                return;
            }

            var all = new System.Collections.Generic.List<BluetoothDeviceInfo>();

            foreach (var addrKey in key.GetSubKeyNames())
            {
                try
                {
                    using var devKey = key.OpenSubKey(addrKey);
                    if (devKey is null) continue;

                    var nameRaw  = devKey.GetValue("Name") as byte[];
                    var name     = nameRaw is not null ? DecodeBtName(nameRaw) : addrKey;
                    if (string.IsNullOrWhiteSpace(name)) name = addrKey;

                    var addr     = FormatMac(addrKey);
                    var classVal = devKey.GetValue("ClassOfDevice");
                    var cod      = classVal is int ci ? ci : classVal is uint u ? (int)u : 0;
                    var (typeLabel, typeIcon, isAudio) = ClassifyDevice(cod, name);
                    var codecs   = EstimateCodecs(name, isAudio);
                    var drvStatus = GetDriverStatus(addrKey);

                    all.Add(new BluetoothDeviceInfo(name, addr, typeLabel, typeIcon, isAudio, codecs, drvStatus));
                }
                catch { /* skip malformed entry */ }
            }

            // Audio devices first, then others
            foreach (var d in all.OrderByDescending(d => d.IsAudio).ThenBy(d => d.Name))
                Devices.Add(d);

            var audioCount = all.Count(d => d.IsAudio);
            StatusMessage = all.Count > 0
                ? $"{all.Count} dispositivo(s) — {audioCount} de audio, {all.Count - audioCount} otros."
                : "No se encontraron dispositivos emparejados.";
        }
        catch (Exception ex)
        {
            StatusMessage = $"Error: {ex.Message}";
        }
    }

    [RelayCommand(CanExecute = nameof(CanApply))]
    private void ApplyCodec()
    {
        if (SelectedDevice is null) return;
        _ipc.SendSetCodec("SBC", "switch", 1);
        StatusMessage = $"Codec aplicado a {SelectedDevice.Name}. Configura parámetros en la pestaña Codec.";
    }
    private bool CanApply() => SelectedDevice?.IsAudio == true && _ipc.IsConnected;

    [RelayCommand(CanExecute = nameof(CanInstall))]
    private void InstallDriver()
    {
        if (SelectedDevice is null) return;

        if (!TestSigningEnabled)
        {
            var result = System.Windows.MessageBox.Show(
                "El driver de OpenWinBlue no está firmado para producción.\n\n" +
                "Para instalarlo en desarrollo necesitas activar Test Signing Mode:\n\n" +
                "  bcdedit /set testsigning on\n\n" +
                "¿Deseas activarlo ahora? (Requiere reiniciar Windows después)",
                "Test Signing requerido",
                System.Windows.MessageBoxButton.YesNo,
                System.Windows.MessageBoxImage.Warning);

            if (result == System.Windows.MessageBoxResult.Yes)
                EnableTestSigningCommand.Execute(null);
            return;
        }

        try
        {
            // Find INF: try repo path relative to exe, then current dir
            var infPath = FindInfPath();
            if (infPath is null)
            {
                StatusMessage = "No se encontró owb_a2dp.inf. Compila el driver primero.";
                return;
            }

            // Run pnputil via temp bat to capture output
            var logFile = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "owb_install.log");
            var bat = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "owb_install.bat");
            System.IO.File.WriteAllText(bat,
                $"@echo off\r\npnputil.exe /add-driver \"{infPath}\" /install > \"{logFile}\" 2>&1\r\n");

            var proc = Process.Start(new ProcessStartInfo {
                FileName        = "cmd.exe",
                Arguments       = $"/c \"{bat}\"",
                Verb            = "runas",
                UseShellExecute = true,
            });
            StatusMessage = $"Instalando driver para {SelectedDevice.Name}… (espera UAC)";

            // Poll for completion in background
            System.Threading.Tasks.Task.Run(() => {
                proc?.WaitForExit(30_000);
                var log = System.IO.File.Exists(logFile)
                    ? System.IO.File.ReadAllText(logFile).Trim()
                    : "(sin output)";
                System.Windows.Application.Current.Dispatcher.Invoke(() => {
                    StatusMessage = log.Length > 0 ? log : "Instalación completada.";
                    OnPropertyChanged(nameof(OwbDriverInstalled));
                    RefreshCommand.Execute(null);
                });
            });
        }
        catch (Exception ex) { StatusMessage = $"Error: {ex.Message}"; }
    }
    private bool CanInstall() => SelectedDevice?.IsAudio == true;

    [RelayCommand]
    private void EnableTestSigning()
    {
        try
        {
            Process.Start(new ProcessStartInfo {
                FileName        = "cmd.exe",
                Arguments       = "/c bcdedit /set testsigning on",
                Verb            = "runas",
                UseShellExecute = true,
            });
            System.Windows.MessageBox.Show(
                "Test Signing activado.\n\nReinicia Windows para que surta efecto.\n" +
                "Después podrás instalar el driver OpenWinBlue.",
                "Test Signing activado",
                System.Windows.MessageBoxButton.OK,
                System.Windows.MessageBoxImage.Information);
            OnPropertyChanged(nameof(TestSigningEnabled));
        }
        catch (Exception ex) { StatusMessage = $"Error: {ex.Message}"; }
    }

    private static string? FindInfPath()
    {
        var candidates = new[] {
            // From repo root (dev build)
            System.IO.Path.GetFullPath(System.IO.Path.Combine(
                AppDomain.CurrentDomain.BaseDirectory, "..", "..", "..", "..", "driver", "owb_a2dp.inf")),
            // Same dir as exe (installed build)
            System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "owb_a2dp.inf"),
            // Hardcoded repo path
            @"c:\suru\open winblue\driver\owb_a2dp.inf",
        };
        return candidates.FirstOrDefault(System.IO.File.Exists);
    }

    [RelayCommand(CanExecute = nameof(CanReset))]
    private void ResetDriver()
    {
        if (SelectedDevice is null) return;
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName        = "pnputil.exe",
                Arguments       = "/delete-driver owb_a2dp.inf /uninstall /force",
                Verb            = "runas",
                UseShellExecute = true,
            });
            StatusMessage = $"Restaurando driver de Windows para {SelectedDevice.Name}…";
            OnPropertyChanged(nameof(OwbDriverInstalled));
        }
        catch (Exception ex) { StatusMessage = $"Error al restaurar: {ex.Message}"; }
    }
    private bool CanReset() => SelectedDevice?.IsAudio == true;

    // ── Helpers ───────────────────────────────────────────────────────────────
    private static string DecodeBtName(byte[] raw)
    {
        var nullIdx = Array.IndexOf(raw, (byte)0);
        var len = nullIdx >= 0 ? nullIdx : raw.Length;
        if (len == 0) return string.Empty;
        var utf8 = Encoding.UTF8.GetString(raw, 0, len).Trim();
        return !utf8.Contains('�') && utf8.Length > 0
            ? utf8
            : Encoding.Unicode.GetString(raw).TrimEnd('\0', ' ').Trim();
    }

    private static string FormatMac(string addrKey) =>
        addrKey.Length == 12
            ? string.Join(":", Enumerable.Range(0, 6).Select(i => addrKey.Substring(i * 2, 2).ToUpper()))
            : addrKey.ToUpper();

    private static (string label, string icon, bool isAudio) ClassifyDevice(int cod, string name)
    {
        // Windows BTHPORT often doesn't store ClassOfDevice — use name heuristics first.
        var n = name.ToLowerInvariant();

        // Clearly NOT audio
        if (n.Contains("controller") || n.Contains("keyboard") || n.Contains("mouse") ||
            n.Contains("teclado") || n.Contains("raton"))
            return ("Periférico", "🖱️", false);

        if (n.Contains("phone") || n.Contains("iphone") || n.Contains("android") ||
            n.Contains("galaxy s") || n.Contains("pixel ") || n.Contains("móvil") ||
            n.Contains("ultra de "))
            return ("Teléfono / Móvil", "📱", false);

        // Clearly audio by name
        if (n.Contains("headset") || n.Contains("headphone") || n.Contains("auricular") ||
            n.Contains("earbud") || n.Contains("earphone") || n.Contains("buds") ||
            n.Contains("cloud ") || n.Contains("recon ") || n.Contains("arctis") ||
            n.Contains("kraken") || n.Contains("razer") || n.Contains("corsair") ||
            n.Contains("hyperx") || n.Contains("jabra") || n.Contains("sennheiser") ||
            n.Contains("bose") || n.Contains("jbl") || n.Contains("airpods") ||
            n.Contains("beats") || n.Contains("wh-") || n.Contains("wf-") ||
            n.Contains("linkbuds") || n.Contains("sony wh") || n.Contains("soundcore") ||
            n.Contains("anker") || n.Contains("az09") || n.Contains("oficina") ||
            n.Contains("office") || n.Contains("par de") || n.Contains("pro ") ||
            n.Contains(" pro") || n.Contains("speaker") || n.Contains("altavoz"))
            return ("Auriculares / Audio", "🎧", true);

        // Fallback: COD if available
        int major = (cod >> 8) & 0x1F;
        return major switch {
            4 => ("Dispositivo de audio",  "🎵", true),
            2 => ("Teléfono",              "📱", false),
            1 => ("Computadora",           "💻", false),
            5 => ("Periférico",            "🖱️", false),
            _ => ("Desconocido",           "🔷", false),
        };
    }

    private static string[] EstimateCodecs(string name, bool isAudio)
    {
        if (!isAudio) return Array.Empty<string>();

        var n   = name.ToLowerInvariant();
        var res = new System.Collections.Generic.List<string> { "SBC" }; // mandatory

        // LDAC — Sony
        if (n.Contains("sony") || n.Contains("wh-") || n.Contains("wf-") ||
            n.Contains("linkbuds") || n.Contains("xm"))
            res.Add("LDAC");

        // AAC — Apple, modern headsets
        if (n.Contains("airpods") || n.Contains("beats") || n.Contains("jbl") ||
            n.Contains("jabra") || n.Contains("plantronics") || n.Contains("poly") ||
            n.Contains("bose") || n.Contains("samsung") || n.Contains("buds") ||
            n.Contains("galaxy"))
            res.Add("AAC");

        // aptX — Qualcomm-powered
        if (n.Contains("jabra") || n.Contains("sennheiser") || n.Contains("audio-technica") ||
            n.Contains("anker") || n.Contains("soundcore") || n.Contains("oneplus") ||
            n.Contains("lg ") || n.Contains("pixel") || n.Contains("az09") ||
            n.Contains("recon"))
            res.Add("aptX");

        // aptX-HD — premium tier
        if (n.Contains("jabra") || n.Contains("sennheiser") || n.Contains("b&w") ||
            n.Contains("bowers") || n.Contains("recon"))
            res.Add("aptX-HD");

        // LC3 — LE Audio generation
        if (n.Contains("galaxy buds3") || n.Contains("airpods pro 2") ||
            n.Contains("le audio") || n.Contains("lc3"))
            res.Add("LC3");

        return res.ToArray();
    }

    private static string GetDriverStatus(string addrKey)
    {
        // Check if owb_a2dp service is globally registered
        using var svcKey = Registry.LocalMachine.OpenSubKey(
            @"SYSTEM\CurrentControlSet\Services\owb_a2dp");
        return svcKey is not null
            ? "OpenWinBlue instalado"
            : "Driver Windows (btavchdt.sys)";
    }

    private sealed class NullIpcSender : IIpcSender
    {
        public bool IsConnected => false;
        public bool SendSetCodec(string c, string k, long v) => false;
    }
}
