using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Win32;
using OpenWinBlue.Services;
using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;

namespace OpenWinBlue.ViewModels;

public sealed record BluetoothDeviceInfo(string Name, string Address, string Class);

public partial class DevicesViewModel : ObservableObject
{
    private readonly IIpcSender _ipc;

    public ObservableCollection<BluetoothDeviceInfo> Devices { get; } = new();

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ApplyCodecCommand))]
    private BluetoothDeviceInfo? _selectedDevice;

    [ObservableProperty] private string _statusMessage = "Click Refresh to scan paired devices.";

    public DevicesViewModel() : this(new NullIpcSender()) { }

    public DevicesViewModel(IIpcSender ipc)
    {
        _ipc = ipc;
        RefreshCommand.Execute(null);
    }

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
                StatusMessage = "No paired Bluetooth devices found (no BT radio or not paired yet).";
                return;
            }

            foreach (var addrKey in key.GetSubKeyNames())
            {
                try
                {
                    using var devKey = key.OpenSubKey(addrKey);
                    if (devKey is null) continue;

                    var nameRaw = devKey.GetValue("Name") as byte[];
                    var name = nameRaw is not null ? DecodeBtName(nameRaw) : addrKey;

                    if (string.IsNullOrWhiteSpace(name)) name = addrKey;

                    var addr = addrKey.Length == 12
                        ? string.Join(":", Enumerable.Range(0, 6)
                            .Select(i => addrKey.Substring(i * 2, 2).ToUpper()))
                        : addrKey.ToUpper();

                    var classVal = devKey.GetValue("ClassOfDevice");
                    var cls = classVal is int c ? DescribeClass(c) : "Unknown";

                    Devices.Add(new BluetoothDeviceInfo(name, addr, cls));
                }
                catch { /* skip malformed entry */ }
            }

            StatusMessage = Devices.Count > 0
                ? $"{Devices.Count} device(s) found. Select one and click Apply Codec."
                : "No paired Bluetooth devices found.";
        }
        catch (Exception ex)
        {
            StatusMessage = $"Error scanning devices: {ex.Message}";
        }
    }

    [RelayCommand(CanExecute = nameof(CanApply))]
    private void ApplyCodec()
    {
        if (SelectedDevice is null) return;
        // Send codec switch to the service — it applies to the active A2DP connection.
        // The selected device name is informational; A2DP has one active stream at a time.
        _ipc.SendSetCodec("SBC", "switch", 1);
        StatusMessage = $"Codec applied to {SelectedDevice.Name}. Use the Codec tab to configure parameters.";
    }

    private bool CanApply() => SelectedDevice is not null && _ipc.IsConnected;

    private static string DecodeBtName(byte[] raw)
    {
        // Windows BTHPORT stores names as raw BT GAP bytes (UTF-8, null-terminated).
        // Some older entries may be UTF-16 LE. Try UTF-8 first.
        var nullIdx = Array.IndexOf(raw, (byte)0);
        var len = nullIdx >= 0 ? nullIdx : raw.Length;
        if (len == 0) return string.Empty;

        var utf8 = Encoding.UTF8.GetString(raw, 0, len).Trim();
        // Heuristic: if result has only printable ASCII/Unicode (not replacement chars), use it
        if (!utf8.Contains('�') && utf8.Length > 0)
            return utf8;

        // Fallback: try UTF-16 LE
        return Encoding.Unicode.GetString(raw).TrimEnd('\0', ' ').Trim();
    }

    private static string DescribeClass(int cod)
    {
        int major = (cod >> 8) & 0x1F;
        return major switch {
            1  => "Computer",
            2  => "Phone",
            4  => "Audio / Headset",
            5  => "Peripheral",
            6  => "Imaging",
            _ => $"Class 0x{cod:X}"
        };
    }

    private sealed class NullIpcSender : IIpcSender
    {
        public bool IsConnected => false;
        public bool SendSetCodec(string c, string k, long v) => false;
    }
}
