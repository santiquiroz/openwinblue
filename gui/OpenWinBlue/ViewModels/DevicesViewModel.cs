using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using OpenWinBlue.Services;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using WpfApp = System.Windows.Application;
using WpfMsg = System.Windows.MessageBox;
using WpfMsgButton = System.Windows.MessageBoxButton;
using WpfMsgImage  = System.Windows.MessageBoxImage;
using WpfMsgResult = System.Windows.MessageBoxResult;

namespace OpenWinBlue.ViewModels;

public sealed record BluetoothDeviceInfo(
    string   Name,
    string   Address,
    string   AddrKey,
    string   TypeLabel,
    string   TypeIcon,
    bool     IsAudio,
    string[] EstimatedCodecs,
    string   DriverStatus,
    bool     UsesOwbDriver,
    bool     IsConnected);

public partial class DevicesViewModel : ObservableObject
{
    private readonly IIpcSender _ipc;

    // A2DP profile devices keyed by MAC without colons (AABBCCDDEEFF), uppercase.
    private Dictionary<string, A2dpInfo> _a2dpCache = [];

    public ObservableCollection<BluetoothDeviceInfo> Devices { get; } = new();

    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(HasSelection))]
    [NotifyPropertyChangedFor(nameof(OwbDriverInstalled))]
    [NotifyCanExecuteChangedFor(nameof(ApplyCodecCommand))]
    [NotifyCanExecuteChangedFor(nameof(InstallDriverCommand))]
    [NotifyCanExecuteChangedFor(nameof(ResetDriverCommand))]
    private BluetoothDeviceInfo? _selectedDevice;

    [ObservableProperty] private string _statusMessage = "Escaneando dispositivos…";
    [ObservableProperty] private string _selectedCodec  = "SBC";
    [ObservableProperty] private int    _selectedBitrate = 320;
    [ObservableProperty] private bool   _isNoiseCancellationEnabled;
    [ObservableProperty] private bool   _isVoiceEnhancementEnabled;

    public bool HasSelection      => SelectedDevice is not null;
    public bool OwbDriverInstalled => SelectedDevice?.UsesOwbDriver == true;
    public bool TestSigningEnabled => CheckTestSigning();

    // ── Test-signing check ────────────────────────────────────────────────────
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
        const int  SystemCodeIntegrityInformation   = 103;
        const uint CODEINTEGRITY_OPTION_TESTSIGN    = 0x00000002;
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

    // ── Win32 Bluetooth device enumeration API ────────────────────────────────
    [StructLayout(LayoutKind.Sequential)]
    private struct SYSTEMTIME
    {
        public ushort Year, Month, DayOfWeek, Day, Hour, Minute, Second, Milliseconds;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct BLUETOOTH_DEVICE_INFO
    {
        public uint  dwSize;
        public ulong Address;
        public uint  ulClassofDevice;
        [MarshalAs(UnmanagedType.Bool)] public bool fConnected;
        [MarshalAs(UnmanagedType.Bool)] public bool fRemembered;
        [MarshalAs(UnmanagedType.Bool)] public bool fAuthenticated;
        public SYSTEMTIME stLastSeen;
        public SYSTEMTIME stLastUsed;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 248)]
        public string szName;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct BLUETOOTH_DEVICE_SEARCH_PARAMS
    {
        public uint  dwSize;
        [MarshalAs(UnmanagedType.Bool)] public bool fReturnAuthenticated;
        [MarshalAs(UnmanagedType.Bool)] public bool fReturnRemembered;
        [MarshalAs(UnmanagedType.Bool)] public bool fReturnUnknown;
        [MarshalAs(UnmanagedType.Bool)] public bool fReturnConnected;
        [MarshalAs(UnmanagedType.Bool)] public bool fIssueInquiry;
        public byte  cTimeoutMultiplier;
        public IntPtr hRadio;
    }

    [DllImport("bthprops.cpl", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr BluetoothFindFirstDevice(
        ref BLUETOOTH_DEVICE_SEARCH_PARAMS pSearchParams,
        ref BLUETOOTH_DEVICE_INFO pbtdi);

    [DllImport("bthprops.cpl", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool BluetoothFindNextDevice(
        IntPtr hFind,
        ref BLUETOOTH_DEVICE_INFO pbtdi);

    [DllImport("bthprops.cpl", SetLastError = true)]
    private static extern bool BluetoothFindDeviceClose(IntPtr hFind);

    private static Dictionary<string, (string name, int cod, bool isConnected)> QueryBtDevices()
    {
        var result = new Dictionary<string, (string, int, bool)>(StringComparer.OrdinalIgnoreCase);

        var info = new BLUETOOTH_DEVICE_INFO
        {
            dwSize = (uint)Marshal.SizeOf<BLUETOOTH_DEVICE_INFO>(),
            szName = string.Empty,
        };
        var searchParams = new BLUETOOTH_DEVICE_SEARCH_PARAMS
        {
            dwSize               = (uint)Marshal.SizeOf<BLUETOOTH_DEVICE_SEARCH_PARAMS>(),
            fReturnAuthenticated = false,
            fReturnRemembered    = false,
            fReturnUnknown       = false,
            fReturnConnected     = true,
            fIssueInquiry        = false,
            cTimeoutMultiplier   = 0,
            hRadio               = IntPtr.Zero,
        };

        var hFind = BluetoothFindFirstDevice(ref searchParams, ref info);
        if (hFind == IntPtr.Zero) return result;

        try
        {
            do
            {
                var addrKey = BtAddressToKey(info.Address);
                result[addrKey] = (info.szName ?? string.Empty, (int)info.ulClassofDevice, info.fConnected);
                info = new BLUETOOTH_DEVICE_INFO
                {
                    dwSize = (uint)Marshal.SizeOf<BLUETOOTH_DEVICE_INFO>(),
                    szName = string.Empty,
                };
            }
            while (BluetoothFindNextDevice(hFind, ref info));
        }
        finally { BluetoothFindDeviceClose(hFind); }

        return result;
    }

    // BT address is a ulong; bytes[5..0] map to the 6 display octets (little-endian).
    private static string BtAddressToKey(ulong address)
    {
        var b = BitConverter.GetBytes(address);
        return $"{b[5]:X2}{b[4]:X2}{b[3]:X2}{b[2]:X2}{b[1]:X2}{b[0]:X2}";
    }

    // ── Constructors ──────────────────────────────────────────────────────────
    public DevicesViewModel() : this(new NullIpcSender()) { }

    public DevicesViewModel(IIpcSender ipc)
    {
        _ipc = ipc;
        RefreshCommand.Execute(null);
    }

    partial void OnSelectedDeviceChanged(BluetoothDeviceInfo? value)
    {
        SelectedCodec  = "SBC";
        SelectedBitrate = 320;
        IsNoiseCancellationEnabled = false;
        IsVoiceEnhancementEnabled  = false;
    }

    // ── Commands ──────────────────────────────────────────────────────────────
    [RelayCommand]
    private void Refresh()
    {
        try
        {
            Devices.Clear();
            SelectedDevice = null;

            _a2dpCache = QueryA2dpDevices();
            var btDevices = QueryBtDevices();

            if (btDevices.Count == 0)
            {
                StatusMessage = "No hay dispositivos Bluetooth conectados.";
                return;
            }

            var all = new List<BluetoothDeviceInfo>();

            foreach (var (addrKey, (name, cod, isConnected)) in btDevices)
            {
                var displayName = string.IsNullOrWhiteSpace(name) ? addrKey : name;
                var addr        = FormatMac(addrKey);
                var (typeLabel, typeIcon, isAudio) = ClassifyDevice(cod, displayName);
                var codecs      = EstimateCodecs(displayName, isAudio);

                _a2dpCache.TryGetValue(addrKey, out var a2dp);
                var drvStatus = BuildDriverStatus(a2dp);
                var usesOwb   = a2dp?.DriverInf.Contains("owb_a2dp",
                                    StringComparison.OrdinalIgnoreCase) == true;

                all.Add(new BluetoothDeviceInfo(
                    displayName, addr, addrKey,
                    typeLabel, typeIcon, isAudio, codecs, drvStatus, usesOwb, isConnected));
            }

            foreach (var d in all.OrderByDescending(d => d.IsAudio).ThenBy(d => d.Name))
                Devices.Add(d);

            var audioCount = all.Count(d => d.IsAudio);
            StatusMessage = $"{all.Count} dispositivo(s) conectado(s) — {audioCount} de audio.";
        }
        catch (Exception ex)
        {
            StatusMessage = $"Error al escanear: {ex.Message}";
        }
    }

    [RelayCommand(CanExecute = nameof(CanApply))]
    private void ApplyCodec()
    {
        if (SelectedDevice is null) return;
        if (!_ipc.IsConnected)
        {
            StatusMessage = "El servicio OpenWinBlue no está activo. Inicia owb-service.exe para aplicar cambios en tiempo real.";
            return;
        }
        _ipc.SendSetCodec(SelectedCodec, "switch", (long)SelectedBitrate * 1000);
        StatusMessage = $"{SelectedCodec} a {SelectedBitrate} kbps aplicado a {SelectedDevice.Name}.";
    }
    private bool CanApply() => SelectedDevice?.IsAudio == true && OwbDriverInstalled;

    [RelayCommand(CanExecute = nameof(CanInstall))]
    private void InstallDriver()
    {
        if (SelectedDevice is null) return;

        if (!TestSigningEnabled)
        {
            var result = WpfMsg.Show(
                "El driver de OpenWinBlue no está firmado para producción.\n\n" +
                "Para instalarlo en desarrollo necesitas activar Test Signing Mode:\n\n" +
                "  bcdedit /set testsigning on\n\n" +
                "¿Deseas activarlo ahora? (Requiere reiniciar Windows después)",
                "Test Signing requerido",
                WpfMsgButton.YesNo,
                WpfMsgImage.Warning);

            if (result == WpfMsgResult.Yes)
                EnableTestSigningCommand.Execute(null);
            return;
        }

        try
        {
            var infPath = FindInfPath();
            if (infPath is null)
            {
                StatusMessage = "No se encontró owb_a2dp.inf. Compila el driver primero.";
                return;
            }

            var logFile = Path.Combine(Path.GetTempPath(), "owb_install.log");
            var batFile = Path.Combine(Path.GetTempPath(), "owb_install.bat");
            File.WriteAllText(batFile,
                $"@echo off\r\npnputil.exe /add-driver \"{infPath}\" /install > \"{logFile}\" 2>&1\r\n");

            var proc = Process.Start(new ProcessStartInfo
            {
                FileName        = "cmd.exe",
                Arguments       = $"/c \"{batFile}\"",
                Verb            = "runas",
                UseShellExecute = true,
            });

            StatusMessage = $"Instalando driver… (espera UAC)";

            Task.Run(() =>
            {
                proc?.WaitForExit(30_000);
                var exitCode = proc?.ExitCode ?? -1;

                ForceActivateA2dp(infPath);

                var log = File.Exists(logFile) ? File.ReadAllText(logFile).Trim() : string.Empty;
                var registered = exitCode == 0
                    || log.Contains("correctamente", StringComparison.OrdinalIgnoreCase)
                    || log.Contains("ya existe",     StringComparison.OrdinalIgnoreCase)
                    || log.Contains("successfully",  StringComparison.OrdinalIgnoreCase);

                WpfApp.Current.Dispatcher.Invoke(() =>
                {
                    StatusMessage = registered
                        ? "Driver registrado. Reconecta los auriculares para que Windows lo aplique."
                        : $"Error al registrar driver (código {exitCode}).";
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
            Process.Start(new ProcessStartInfo
            {
                FileName        = "cmd.exe",
                Arguments       = "/c bcdedit /set testsigning on",
                Verb            = "runas",
                UseShellExecute = true,
            });
            WpfMsg.Show(
                "Test Signing activado.\n\nReinicia Windows para que surta efecto.\n" +
                "Después podrás instalar el driver OpenWinBlue.",
                "Test Signing activado",
                WpfMsgButton.OK,
                WpfMsgImage.Information);
            OnPropertyChanged(nameof(TestSigningEnabled));
        }
        catch (Exception ex) { StatusMessage = $"Error: {ex.Message}"; }
    }

    [RelayCommand(CanExecute = nameof(CanReset))]
    private void ResetDriver()
    {
        if (SelectedDevice is null) return;
        try
        {
            var proc = Process.Start(new ProcessStartInfo
            {
                FileName        = "pnputil.exe",
                Arguments       = "/delete-driver owb_a2dp.inf /uninstall /force",
                Verb            = "runas",
                UseShellExecute = true,
            });
            StatusMessage = $"Restaurando driver de Windows para {SelectedDevice.Name}…";
            Task.Run(() =>
            {
                proc?.WaitForExit(15_000);
                WpfApp.Current.Dispatcher.Invoke(() => RefreshCommand.Execute(null));
            });
        }
        catch (Exception ex) { StatusMessage = $"Error al restaurar: {ex.Message}"; }
    }
    private bool CanReset() => SelectedDevice?.IsAudio == true;

    // ── Force-activate via Win32 UpdateDriverForPlugAndPlayDevicesW ───────────
    // Runs elevated to override WHQL driver ranking for all A2DP Sink devices.
    private static void ForceActivateA2dp(string infPath)
    {
        const string script = """
            param([string]$InfPath)
            Add-Type -TypeDefinition @'
            using System; using System.Runtime.InteropServices;
            public class PnpForcer {
                [DllImport("newdev.dll", CharSet=CharSet.Unicode, SetLastError=true)]
                public static extern bool UpdateDriverForPlugAndPlayDevicesW(
                    IntPtr h, string hwId, string inf, uint flags, out bool reboot);
            }
            '@
            $r = $false
            $ok = [PnpForcer]::UpdateDriverForPlugAndPlayDevicesW(
                [IntPtr]::Zero,
                'BTHENUM\{0000110b-0000-1000-8000-00805f9b34fb}',
                $InfPath, 1, [ref]$r)
            Write-Host "ForceActivate: ok=$ok reboot=$r"
            if ($r) { Write-Host "Reinicio requerido para activar el driver." }
            """;

        var psPath = Path.Combine(Path.GetTempPath(), "owb_force_a2dp.ps1");
        File.WriteAllText(psPath, script);

        Process.Start(new ProcessStartInfo
        {
            FileName        = "powershell.exe",
            Arguments       = $"-NoProfile -ExecutionPolicy Bypass -File \"{psPath}\" -InfPath \"{infPath}\"",
            Verb            = "runas",
            UseShellExecute = true,
        })?.WaitForExit(20_000);
    }

    // ── A2DP device enumeration via pnputil ───────────────────────────────────
    private static Dictionary<string, A2dpInfo> QueryA2dpDevices()
    {
        const string a2dpUuid = "0000110b-0000-1000-8000-00805f9b34fb";
        var result = new Dictionary<string, A2dpInfo>(StringComparer.OrdinalIgnoreCase);

        string output;
        try
        {
            using var proc = Process.Start(new ProcessStartInfo
            {
                FileName               = "pnputil.exe",
                Arguments              = "/enum-devices /ids",
                RedirectStandardOutput = true,
                UseShellExecute        = false,
                CreateNoWindow         = true,
            });
            output = proc?.StandardOutput.ReadToEnd() ?? string.Empty;
            proc?.WaitForExit(5_000);
        }
        catch { return result; }

        // Split into per-device blocks separated by blank lines.
        var blocks = output.Split(
            ["\r\n\r\n", "\n\n"],
            StringSplitOptions.RemoveEmptyEntries);

        foreach (var block in blocks)
        {
            if (!block.Contains(a2dpUuid, StringComparison.OrdinalIgnoreCase))
                continue;

            string? instanceId = null;
            string? driverInf  = null;

            foreach (var raw in block.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries))
            {
                var colon = raw.IndexOf(':');
                if (colon < 0) continue;
                var value = raw[(colon + 1)..].Trim();

                if (value.Contains(a2dpUuid, StringComparison.OrdinalIgnoreCase))
                    instanceId = value;
                else if (value.EndsWith(".inf", StringComparison.OrdinalIgnoreCase))
                    driverInf = value;
            }

            if (instanceId is null) continue;

            // Extract 12-char MAC segment from instance ID: ...LOCALADDR REMOTEADDR _C0xxxxxx
            var m = Regex.Match(instanceId, @"([0-9A-Fa-f]{12})_C0", RegexOptions.None);
            if (!m.Success) continue;

            var mac = m.Groups[1].Value.ToUpperInvariant();
            result[mac] = new A2dpInfo(instanceId, driverInf ?? string.Empty);
        }

        return result;
    }

    private static string BuildDriverStatus(A2dpInfo? a2dp)
    {
        if (a2dp is null)
            return "Sin perfil A2DP activo";
        if (a2dp.DriverInf.Contains("owb_a2dp", StringComparison.OrdinalIgnoreCase))
            return "OpenWinBlue instalado ✓";
        return string.IsNullOrEmpty(a2dp.DriverInf)
            ? "Driver desconocido"
            : $"Driver: {a2dp.DriverInf}";
    }

    // ── Helpers ───────────────────────────────────────────────────────────────
    private static string? FindInfPath()
    {
        var candidates = new[]
        {
            Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "owb_a2dp.inf"),
            Path.GetFullPath(Path.Combine(
                AppDomain.CurrentDomain.BaseDirectory,
                "..", "..", "..", "..", "driver", "owb_a2dp.inf")),
            @"c:\suru\open winblue\driver\owb_a2dp.inf",
        };
        return candidates.FirstOrDefault(File.Exists);
    }

    private static string FormatMac(string addrKey) =>
        addrKey.Length == 12
            ? string.Join(":", Enumerable.Range(0, 6).Select(i => addrKey.Substring(i * 2, 2).ToUpper()))
            : addrKey.ToUpper();

    private static (string label, string icon, bool isAudio) ClassifyDevice(int cod, string name)
    {
        var n = name.ToLowerInvariant();

        if (n.Contains("controller") || n.Contains("keyboard") || n.Contains("mouse") ||
            n.Contains("teclado") || n.Contains("raton"))
            return ("Periférico", "🖱️", false);

        if (n.Contains("phone") || n.Contains("iphone") || n.Contains("android") ||
            n.Contains("galaxy s") || n.Contains("pixel ") || n.Contains("móvil") ||
            n.Contains("ultra de "))
            return ("Teléfono / Móvil", "📱", false);

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

        int major = (cod >> 8) & 0x1F;
        return major switch
        {
            4 => ("Dispositivo de audio", "🎵", true),
            2 => ("Teléfono",             "📱", false),
            1 => ("Computadora",          "💻", false),
            5 => ("Periférico",           "🖱️", false),
            _ => ("Desconocido",          "🔷", false),
        };
    }

    private static string[] EstimateCodecs(string name, bool isAudio)
    {
        if (!isAudio) return [];

        var n   = name.ToLowerInvariant();
        var res = new List<string> { "SBC" };

        if (n.Contains("sony") || n.Contains("wh-") || n.Contains("wf-") ||
            n.Contains("linkbuds") || n.Contains("xm"))
            res.Add("LDAC");

        if (n.Contains("airpods") || n.Contains("beats") || n.Contains("jbl") ||
            n.Contains("jabra") || n.Contains("plantronics") || n.Contains("poly") ||
            n.Contains("bose") || n.Contains("samsung") || n.Contains("buds") ||
            n.Contains("galaxy"))
            res.Add("AAC");

        if (n.Contains("jabra") || n.Contains("sennheiser") || n.Contains("audio-technica") ||
            n.Contains("anker") || n.Contains("soundcore") || n.Contains("oneplus") ||
            n.Contains("lg ") || n.Contains("pixel") || n.Contains("az09") ||
            n.Contains("recon"))
            res.Add("aptX");

        if (n.Contains("jabra") || n.Contains("sennheiser") || n.Contains("b&w") ||
            n.Contains("bowers") || n.Contains("recon"))
            res.Add("aptX-HD");

        if (n.Contains("galaxy buds3") || n.Contains("airpods pro 2") ||
            n.Contains("le audio") || n.Contains("lc3"))
            res.Add("LC3");

        return [.. res];
    }

    // Pnputil-derived A2DP profile device info (internal only).
    private sealed record A2dpInfo(string InstanceId, string DriverInf);

    private sealed class NullIpcSender : IIpcSender
    {
        public bool IsConnected => false;
        public bool SendSetCodec(string c, string k, long v) => false;
    }
}
