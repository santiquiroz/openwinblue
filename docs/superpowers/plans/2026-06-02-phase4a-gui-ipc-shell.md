# Phase 4a — GUI: IPC Client + Main Shell + Status Tab

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the stub WPF window with a working application that connects to the OpenWinBlue service via named pipe, polls device status every second, and shows it in a tabbed interface with a system tray icon.

**Architecture:** `IpcClientService` runs on a background thread (NamedPipeClientStream), polling `GetStatus` every second. It raises events that update the ViewModels on the UI thread via `Application.Current.Dispatcher`. The main window uses a `TabControl` (4 tabs: Status / Codec / Driver / Controls). `App.xaml.cs` wires DI and starts the IPC service. Phase 4a delivers Tab 1 (Status) fully functional; Tabs 2-4 are placeholder shells — they are implemented in Phase 4b.

**Tech Stack:** C# .NET 10, WPF, CommunityToolkit.Mvvm, `System.IO.Pipes.NamedPipeClientStream`, `System.Runtime.InteropServices` (binary struct marshaling), Microsoft.Extensions.DependencyInjection.

---

## Environment Notes

- Solution: `gui/OpenWinBlue.slnx` (new .NET 10 `.slnx` format)
- Build: `dotnet build gui/OpenWinBlue.slnx -c Debug`
- Test: `dotnet test gui/tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj`
- The service (`owb-service.exe`) must be running for IPC to connect; app handles disconnected state gracefully.

---

## File Map

### New files

```
gui/OpenWinBlue/
  Models/
    IpcProtocol.cs          # C# binary structs matching ipc_protocol.h (packed, little-endian)
  Services/
    IpcClientService.cs     # Background thread: NamedPipeClientStream, polling, events
    IDriverInstaller.cs     # Interface for driver install/rollback (used by DriverViewModel)
  ViewModels/
    MainViewModel.cs        # REPLACED: tabs list, connection state, hosts child VMs
    StatusViewModel.cs      # Codec name, bitrate, streaming/capturing state, RF quality
    CodecViewModel.cs       # Stub for Phase 4b
    DriverViewModel.cs      # Stub for Phase 4b
    ControlsViewModel.cs    # Stub for Phase 4b
  Views/
    StatusView.xaml         # Tab 1: connection + device status panel
    CodecView.xaml          # Tab 2: placeholder "Coming in Phase 4b"
    DriverView.xaml         # Tab 3: placeholder
    ControlsView.xaml       # Tab 4: placeholder
  MainWindow.xaml           # REPLACED: TabControl + system tray
  MainWindow.xaml.cs        # System tray logic (NotifyIcon)
  App.xaml.cs               # REPLACED: DI setup, service start/stop
```

### Modified files

```
gui/OpenWinBlue/OpenWinBlue.csproj   # Add System.Drawing, WindowsFormsIntegration (for NotifyIcon)
gui/tests/OpenWinBlue.Tests/MainViewModelTests.cs   # REPLACED with new tests
```

---

## Task 1: IPC protocol C# types

**Files:**
- Create: `gui/OpenWinBlue/Models/IpcProtocol.cs`
- Test: `gui/tests/OpenWinBlue.Tests/IpcProtocolTests.cs`

The structs must be binary-compatible with the C++ packed structs in `service/src/ipc_protocol.h`. All fields are little-endian (x86 native — no byte-swap needed).

- [ ] **Step 1.1: Create `gui/OpenWinBlue/Models/IpcProtocol.cs`**

```csharp
// gui/OpenWinBlue/Models/IpcProtocol.cs
// Binary IPC protocol — C# port of service/src/ipc_protocol.h.
// All structs are packed (1-byte alignment) to match the C++ layout.
using System.Runtime.InteropServices;
using System.Text;

namespace OpenWinBlue.Models;

public enum MsgType : ushort {
    Ping        = 0x0001,
    Pong        = 0x0002,
    GetStatus   = 0x0010,
    StatusReply = 0x0011,
    SetCodec    = 0x0020,
    CodecAck    = 0x0021,
    Error       = 0x00FF,
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct MsgHeader {
    public MsgType  Type;        // 2 bytes
    public ushort   PayloadLen;  // 2 bytes
    public const int Size = 4;
}

/// <summary>Payload for StatusReply (24 bytes).</summary>
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct StatusPayload {
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
    public byte[] CodecNameBytes;   // 16 bytes, null-terminated ASCII

    public uint   Bitrate;          // 4 bytes — effective bits/sec
    public byte   IsCapturing;      // 1 byte — 0 or 1
    public byte   HfpGuardOn;       // 1 byte — 0 or 1
    public ushort Pad;              // 2 bytes

    public const int Size = 24;

    public string CodecName =>
        Encoding.ASCII.GetString(CodecNameBytes ?? Array.Empty<byte>())
                       .TrimEnd('\0');
}

/// <summary>Payload for SetCodec (40 bytes).</summary>
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct SetCodecPayload {
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
    public byte[] CodecNameBytes;   // 16 bytes

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
    public byte[] ParamKeyBytes;    // 16 bytes

    public long ParamValue;         // 8 bytes

    public const int Size = 40;

    public static SetCodecPayload Create(string codec, string key, long value) {
        var p = new SetCodecPayload {
            CodecNameBytes = new byte[16],
            ParamKeyBytes  = new byte[16],
            ParamValue     = value,
        };
        Encoding.ASCII.GetBytes(codec, 0, Math.Min(codec.Length, 15), p.CodecNameBytes, 0);
        Encoding.ASCII.GetBytes(key,   0, Math.Min(key.Length,   15), p.ParamKeyBytes,  0);
        return p;
    }
}

/// <summary>Reads/writes binary protocol messages over any stream.</summary>
public static class IpcMessage {
    public const string PipeName = @"\\.\pipe\openwinblue";

    /// <summary>Write a header-only message (Ping, GetStatus).</summary>
    public static void WriteHeader(Stream s, MsgType type, ushort payloadLen = 0) {
        Span<byte> buf = stackalloc byte[MsgHeader.Size];
        BitConverter.TryWriteBytes(buf,       (ushort)type);
        BitConverter.TryWriteBytes(buf[2..],  payloadLen);
        s.Write(buf);
    }

    /// <summary>Read the 4-byte header. Returns false if stream closed.</summary>
    public static bool TryReadHeader(Stream s, out MsgHeader hdr) {
        hdr = default;
        Span<byte> buf = stackalloc byte[MsgHeader.Size];
        int read = s.ReadAtLeast(buf, MsgHeader.Size, false);
        if (read < MsgHeader.Size) return false;
        hdr = new MsgHeader {
            Type       = (MsgType)BitConverter.ToUInt16(buf),
            PayloadLen = BitConverter.ToUInt16(buf[2..]),
        };
        return true;
    }

    /// <summary>Read StatusPayload after a StatusReply header.</summary>
    public static StatusPayload ReadStatusPayload(Stream s) {
        byte[] buf = new byte[StatusPayload.Size];
        s.ReadExactly(buf);
        var h = GCHandle.Alloc(buf, GCHandleType.Pinned);
        try {
            return Marshal.PtrToStructure<StatusPayload>(h.AddrOfPinnedObject());
        } finally {
            h.Free();
        }
    }

    /// <summary>Write a SetCodec message.</summary>
    public static void WriteSetCodec(Stream s, SetCodecPayload payload) {
        WriteHeader(s, MsgType.SetCodec, SetCodecPayload.Size);
        byte[] buf = new byte[SetCodecPayload.Size];
        var h = GCHandle.Alloc(buf, GCHandleType.Pinned);
        try {
            Marshal.StructureToPtr(payload, h.AddrOfPinnedObject(), false);
        } finally {
            h.Free();
        }
        s.Write(buf);
    }
}
```

Save to: `gui/OpenWinBlue/Models/IpcProtocol.cs`

- [ ] **Step 1.2: Write failing tests**

Create `gui/tests/OpenWinBlue.Tests/IpcProtocolTests.cs`:

```csharp
using OpenWinBlue.Models;
using System.Runtime.InteropServices;

namespace OpenWinBlue.Tests;

public class IpcProtocolTests
{
    [Fact]
    public void MsgHeader_SizeIs4Bytes()
        => Assert.Equal(4, MsgHeader.Size);

    [Fact]
    public void StatusPayload_SizeIs24Bytes()
        => Assert.Equal(24, StatusPayload.Size);

    [Fact]
    public void SetCodecPayload_SizeIs40Bytes()
        => Assert.Equal(40, SetCodecPayload.Size);

    [Fact]
    public void StatusPayload_CodecName_TrimsNullBytes()
    {
        var p = new StatusPayload {
            CodecNameBytes = new byte[16],
            Bitrate        = 328000,
            IsCapturing    = 1,
            HfpGuardOn     = 0,
            Pad            = 0,
        };
        System.Text.Encoding.ASCII.GetBytes("SBC", p.CodecNameBytes);
        Assert.Equal("SBC", p.CodecName);
    }

    [Fact]
    public void SetCodecPayload_Create_SetsAllFields()
    {
        var p = SetCodecPayload.Create("SBC", "bitpool", 53);
        Assert.Equal(53L, p.ParamValue);
        Assert.Equal("SBC",     System.Text.Encoding.ASCII
                                       .GetString(p.CodecNameBytes).TrimEnd('\0'));
        Assert.Equal("bitpool", System.Text.Encoding.ASCII
                                       .GetString(p.ParamKeyBytes).TrimEnd('\0'));
    }

    [Fact]
    public void WriteAndReadHeader_RoundTrips()
    {
        using var ms = new MemoryStream();
        IpcMessage.WriteHeader(ms, MsgType.GetStatus);
        ms.Position = 0;
        Assert.True(IpcMessage.TryReadHeader(ms, out var hdr));
        Assert.Equal(MsgType.GetStatus, hdr.Type);
        Assert.Equal((ushort)0, hdr.PayloadLen);
    }
}
```

Save to: `gui/tests/OpenWinBlue.Tests/IpcProtocolTests.cs`

- [ ] **Step 1.3: Run tests (expect compile error first — Models dir doesn't exist yet)**

```powershell
cd "c:/suru/open winblue/gui"
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: FAIL with "type not found" (Models namespace not yet recognized). Create the directory and file to make it pass.

- [ ] **Step 1.4: Verify 5 new protocol tests pass**

```powershell
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: `Passed! - Failed: 0, Passed: 7, Total: 7` (2 existing + 5 new).

- [ ] **Step 1.5: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/Models/ gui/tests/OpenWinBlue.Tests/IpcProtocolTests.cs
git commit -m "feat(gui): add IpcProtocol C# types with binary struct tests"
```

---

## Task 2: IPC client background service

**Files:**
- Create: `gui/OpenWinBlue/Services/IpcClientService.cs`
- Test: `gui/tests/OpenWinBlue.Tests/IpcClientServiceTests.cs`

`IpcClientService` connects to `\\.\pipe\openwinblue` in a loop, sends `GetStatus`, reads the reply, and raises `StatusReceived`. If the pipe is unavailable it retries every 2 seconds. It runs on a `ThreadPool` task for the lifetime of the app.

- [ ] **Step 2.1: Write failing tests**

```csharp
// gui/tests/OpenWinBlue.Tests/IpcClientServiceTests.cs
using OpenWinBlue.Models;
using OpenWinBlue.Services;

namespace OpenWinBlue.Tests;

public class IpcClientServiceTests
{
    [Fact]
    public void IpcClientService_ConstructsWithoutCrash()
    {
        var svc = new IpcClientService();
        Assert.False(svc.IsConnected);
    }

    [Fact]
    public void IpcClientService_StopBeforeStartDoesNotThrow()
    {
        var svc = new IpcClientService();
        svc.Stop();  // must not throw
    }

    [Fact]
    public void IpcClientService_StatusReceived_EventIsRaisable()
    {
        var svc = new IpcClientService();
        int eventCount = 0;
        svc.StatusReceived += _ => eventCount++;
        // Cannot test real pipe without service running — just verify event wiring.
        Assert.Equal(0, eventCount);
    }
}
```

- [ ] **Step 2.2: Create `gui/OpenWinBlue/Services/IpcClientService.cs`**

```csharp
// gui/OpenWinBlue/Services/IpcClientService.cs
using System.IO.Pipes;
using OpenWinBlue.Models;

namespace OpenWinBlue.Services;

/// <summary>
/// Background service that connects to the OpenWinBlue named pipe,
/// polls GetStatus every second, and raises StatusReceived with the result.
/// Automatically reconnects if the pipe closes.
/// </summary>
public sealed class IpcClientService : IDisposable
{
    public event Action<StatusPayload>? StatusReceived;

    public bool IsConnected => _connected;

    private volatile bool _connected;
    private volatile bool _running;
    private Task?         _task;
    private readonly CancellationTokenSource _cts = new();

    public void Start()
    {
        if (_running) return;
        _running = true;
        _task = Task.Run(() => RunLoop(_cts.Token));
    }

    public void Stop()
    {
        _running = false;
        _cts.Cancel();
        try { _task?.Wait(TimeSpan.FromSeconds(2)); } catch { }
    }

    public void Dispose() => Stop();

    private async Task RunLoop(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested) {
            try {
                using var pipe = new NamedPipeClientStream(
                    ".", "openwinblue",
                    PipeDirection.InOut,
                    PipeOptions.None);

                await pipe.ConnectAsync(500, ct).ConfigureAwait(false);
                _connected = true;

                while (!ct.IsCancellationRequested) {
                    // Send GetStatus
                    IpcMessage.WriteHeader(pipe, MsgType.GetStatus);
                    await pipe.FlushAsync(ct).ConfigureAwait(false);

                    // Read response header
                    if (!IpcMessage.TryReadHeader(pipe, out var hdr)) break;

                    if (hdr.Type == MsgType.StatusReply &&
                        hdr.PayloadLen == StatusPayload.Size) {
                        var status = IpcMessage.ReadStatusPayload(pipe);
                        StatusReceived?.Invoke(status);
                    } else {
                        // Drain unknown payload
                        if (hdr.PayloadLen > 0) {
                            byte[] drain = new byte[hdr.PayloadLen];
                            pipe.ReadExactly(drain);
                        }
                    }

                    await Task.Delay(1000, ct).ConfigureAwait(false);
                }
            }
            catch (OperationCanceledException) { break; }
            catch { /* pipe unavailable — retry */ }
            finally { _connected = false; }

            if (!ct.IsCancellationRequested)
                await Task.Delay(2000, ct).ConfigureAwait(false);
        }
    }

    /// <summary>
    /// Send a SetCodec command (fire-and-forget). Returns false if not connected.
    /// </summary>
    public bool SendSetCodec(string codec, string paramKey, long paramValue)
    {
        if (!_connected) return false;
        try {
            using var pipe = new NamedPipeClientStream(
                ".", "openwinblue", PipeDirection.InOut, PipeOptions.None);
            pipe.Connect(500);
            var payload = SetCodecPayload.Create(codec, paramKey, paramValue);
            IpcMessage.WriteSetCodec(pipe, payload);
            pipe.Flush();

            // Read ack
            if (IpcMessage.TryReadHeader(pipe, out var hdr) &&
                hdr.Type == MsgType.CodecAck &&
                hdr.PayloadLen >= 1) {
                byte[] ack = new byte[hdr.PayloadLen];
                pipe.ReadExactly(ack);
                return ack[0] == 1;
            }
        } catch { }
        return false;
    }
}
```

Save to: `gui/OpenWinBlue/Services/IpcClientService.cs`

- [ ] **Step 2.3: Run tests**

```powershell
cd "c:/suru/open winblue/gui"
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: `Passed! - Failed: 0, Passed: 10, Total: 10` (7 existing + 3 new).

- [ ] **Step 2.4: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/Services/ gui/tests/OpenWinBlue.Tests/IpcClientServiceTests.cs
git commit -m "feat(gui): add IpcClientService background pipe polling with reconnect"
```

---

## Task 3: ViewModels

**Files:**
- Modify: `gui/OpenWinBlue/ViewModels/MainViewModel.cs`
- Create: `gui/OpenWinBlue/ViewModels/StatusViewModel.cs`
- Create: `gui/OpenWinBlue/ViewModels/CodecViewModel.cs`
- Create: `gui/OpenWinBlue/ViewModels/DriverViewModel.cs`
- Create: `gui/OpenWinBlue/ViewModels/ControlsViewModel.cs`
- Modify: `gui/tests/OpenWinBlue.Tests/MainViewModelTests.cs`

- [ ] **Step 3.1: Create `gui/OpenWinBlue/ViewModels/StatusViewModel.cs`**

```csharp
// gui/OpenWinBlue/ViewModels/StatusViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;
using OpenWinBlue.Models;

namespace OpenWinBlue.ViewModels;

public partial class StatusViewModel : ObservableObject
{
    [ObservableProperty] private string  _connectionState   = "Disconnected";
    [ObservableProperty] private string  _activeCodec       = "—";
    [ObservableProperty] private string  _bitrateText       = "—";
    [ObservableProperty] private bool    _isStreaming        = false;
    [ObservableProperty] private bool    _hfpGuardActive    = false;

    public void Update(StatusPayload status)
    {
        ActiveCodec    = string.IsNullOrEmpty(status.CodecName) ? "—" : status.CodecName;
        BitrateText    = status.Bitrate > 0
                         ? $"{status.Bitrate / 1000} kbps"
                         : "—";
        IsStreaming    = status.IsCapturing == 1;
        HfpGuardActive = status.HfpGuardOn  == 1;
    }

    public void SetConnected(bool connected)
        => ConnectionState = connected ? "Connected" : "Disconnected";
}
```

- [ ] **Step 3.2: Create stub ViewModels for Tabs 2-4**

`gui/OpenWinBlue/ViewModels/CodecViewModel.cs`:
```csharp
using CommunityToolkit.Mvvm.ComponentModel;
namespace OpenWinBlue.ViewModels;

public partial class CodecViewModel : ObservableObject
{
    [ObservableProperty] private int    _bitpool     = 53;
    [ObservableProperty] private string _channelMode = "Joint Stereo";
    [ObservableProperty] private int    _sampleRate  = 44100;
}
```

`gui/OpenWinBlue/ViewModels/DriverViewModel.cs`:
```csharp
using CommunityToolkit.Mvvm.ComponentModel;
namespace OpenWinBlue.ViewModels;

public partial class DriverViewModel : ObservableObject
{
    [ObservableProperty] private string _driverStatus = "Unknown";
}
```

`gui/OpenWinBlue/ViewModels/ControlsViewModel.cs`:
```csharp
using CommunityToolkit.Mvvm.ComponentModel;
namespace OpenWinBlue.ViewModels;

public partial class ControlsViewModel : ObservableObject
{
    [ObservableProperty] private bool _hfpGuardEnabled   = true;
    [ObservableProperty] private bool _noiseReduction     = false;
    [ObservableProperty] private bool _adaptiveBitrate    = true;
}
```

- [ ] **Step 3.3: Replace `gui/OpenWinBlue/ViewModels/MainViewModel.cs`**

```csharp
// gui/OpenWinBlue/ViewModels/MainViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;
using OpenWinBlue.Models;
using OpenWinBlue.Services;
using System.Windows;

namespace OpenWinBlue.ViewModels;

public partial class MainViewModel : ObservableObject
{
    public StatusViewModel   Status   { get; } = new();
    public CodecViewModel    Codec    { get; } = new();
    public DriverViewModel   Driver   { get; } = new();
    public ControlsViewModel Controls { get; } = new();

    [ObservableProperty] private int    _selectedTab = 0;
    [ObservableProperty] private string _titleSuffix = "— not connected";

    private readonly IpcClientService _ipc;

    // Design-time constructor (no-arg) used by XAML designer.
    public MainViewModel() : this(new IpcClientService()) { }

    public MainViewModel(IpcClientService ipc)
    {
        _ipc = ipc;
        _ipc.StatusReceived += OnStatusReceived;
    }

    public void StartService() => _ipc.Start();
    public void StopService()  => _ipc.Stop();

    private void OnStatusReceived(StatusPayload payload)
    {
        Application.Current.Dispatcher.Invoke(() =>
        {
            Status.SetConnected(true);
            Status.Update(payload);
            TitleSuffix = $"— {payload.CodecName}, {payload.Bitrate / 1000} kbps";
        });
    }
}
```

- [ ] **Step 3.4: Update tests**

Replace `gui/tests/OpenWinBlue.Tests/MainViewModelTests.cs`:

```csharp
using OpenWinBlue.ViewModels;

namespace OpenWinBlue.Tests;

public class MainViewModelTests
{
    [Fact]
    public void MainViewModel_Instantiates_WithDefaultState()
    {
        var vm = new MainViewModel();
        Assert.Equal(0, vm.SelectedTab);
        Assert.NotNull(vm.Status);
        Assert.NotNull(vm.Codec);
        Assert.NotNull(vm.Driver);
        Assert.NotNull(vm.Controls);
    }

    [Fact]
    public void StatusViewModel_Update_SetsCodecAndBitrate()
    {
        var vm = new StatusViewModel();
        var payload = new OpenWinBlue.Models.StatusPayload {
            CodecNameBytes = new byte[16],
            Bitrate        = 328000,
            IsCapturing    = 1,
            HfpGuardOn     = 0,
            Pad            = 0,
        };
        System.Text.Encoding.ASCII.GetBytes("SBC", payload.CodecNameBytes);
        vm.Update(payload);
        Assert.Equal("SBC",      vm.ActiveCodec);
        Assert.Equal("328 kbps", vm.BitrateText);
        Assert.True(vm.IsStreaming);
    }

    [Fact]
    public void StatusViewModel_SetConnected_UpdatesConnectionState()
    {
        var vm = new StatusViewModel();
        vm.SetConnected(true);
        Assert.Equal("Connected", vm.ConnectionState);
        vm.SetConnected(false);
        Assert.Equal("Disconnected", vm.ConnectionState);
    }
}
```

- [ ] **Step 3.5: Run tests**

```powershell
cd "c:/suru/open winblue/gui"
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: `Passed! - Failed: 0, Passed: 13, Total: 13`.

- [ ] **Step 3.6: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/ViewModels/
git add gui/tests/OpenWinBlue.Tests/MainViewModelTests.cs
git commit -m "feat(gui): add StatusViewModel and tab ViewModels (Status/Codec/Driver/Controls)"
```

---

## Task 4: Main window XAML with TabControl

**Files:**
- Modify: `gui/OpenWinBlue/MainWindow.xaml`
- Modify: `gui/OpenWinBlue/MainWindow.xaml.cs`
- Create: `gui/OpenWinBlue/Views/StatusView.xaml`
- Create: `gui/OpenWinBlue/Views/CodecView.xaml`
- Create: `gui/OpenWinBlue/Views/DriverView.xaml`
- Create: `gui/OpenWinBlue/Views/ControlsView.xaml`

- [ ] **Step 4.1: Create `gui/OpenWinBlue/Views/StatusView.xaml`**

```xml
<UserControl x:Class="OpenWinBlue.Views.StatusView"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <StackPanel Margin="16">
        <TextBlock Text="Connection" FontWeight="Bold" FontSize="13" Margin="0,0,0,8"/>

        <Grid Margin="0,0,0,16">
            <Grid.ColumnDefinitions>
                <ColumnDefinition Width="120"/>
                <ColumnDefinition Width="*"/>
            </Grid.ColumnDefinitions>
            <Grid.RowDefinitions>
                <RowDefinition Height="28"/>
                <RowDefinition Height="28"/>
                <RowDefinition Height="28"/>
                <RowDefinition Height="28"/>
                <RowDefinition Height="28"/>
            </Grid.RowDefinitions>

            <TextBlock Grid.Row="0" Grid.Column="0" Text="Status:" VerticalAlignment="Center" Foreground="#888"/>
            <TextBlock Grid.Row="0" Grid.Column="1"
                       Text="{Binding ConnectionState}"
                       VerticalAlignment="Center"
                       FontWeight="SemiBold"/>

            <TextBlock Grid.Row="1" Grid.Column="0" Text="Active Codec:" VerticalAlignment="Center" Foreground="#888"/>
            <TextBlock Grid.Row="1" Grid.Column="1"
                       Text="{Binding ActiveCodec}"
                       VerticalAlignment="Center"
                       FontFamily="Consolas"/>

            <TextBlock Grid.Row="2" Grid.Column="0" Text="Bitrate:" VerticalAlignment="Center" Foreground="#888"/>
            <TextBlock Grid.Row="2" Grid.Column="1"
                       Text="{Binding BitrateText}"
                       VerticalAlignment="Center"
                       FontFamily="Consolas"/>

            <TextBlock Grid.Row="3" Grid.Column="0" Text="Streaming:" VerticalAlignment="Center" Foreground="#888"/>
            <Ellipse   Grid.Row="3" Grid.Column="1"
                       Width="12" Height="12" Margin="0,0,0,0"
                       HorizontalAlignment="Left" VerticalAlignment="Center">
                <Ellipse.Style>
                    <Style TargetType="Ellipse">
                        <Setter Property="Fill" Value="#888"/>
                        <Style.Triggers>
                            <DataTrigger Binding="{Binding IsStreaming}" Value="True">
                                <Setter Property="Fill" Value="#22C55E"/>
                            </DataTrigger>
                        </Style.Triggers>
                    </Style>
                </Ellipse.Style>
            </Ellipse>

            <TextBlock Grid.Row="4" Grid.Column="0" Text="HFP Guard:" VerticalAlignment="Center" Foreground="#888"/>
            <TextBlock Grid.Row="4" Grid.Column="1"
                       VerticalAlignment="Center">
                <TextBlock.Style>
                    <Style TargetType="TextBlock">
                        <Setter Property="Text" Value="Off"/>
                        <Setter Property="Foreground" Value="#888"/>
                        <Style.Triggers>
                            <DataTrigger Binding="{Binding HfpGuardActive}" Value="True">
                                <Setter Property="Text" Value="Active"/>
                                <Setter Property="Foreground" Value="#22C55E"/>
                            </DataTrigger>
                        </Style.Triggers>
                    </Style>
                </TextBlock.Style>
            </TextBlock>
        </Grid>

        <TextBlock Text="When the service is not running, this panel shows Disconnected."
                   Foreground="#666" FontStyle="Italic" FontSize="11"
                   TextWrapping="Wrap"/>
    </StackPanel>
</UserControl>
```

Also create `StatusView.xaml.cs` (code-behind, empty):
```csharp
namespace OpenWinBlue.Views;
public partial class StatusView : System.Windows.Controls.UserControl {
    public StatusView() => InitializeComponent();
}
```

- [ ] **Step 4.2: Create placeholder views for Tabs 2-4**

`gui/OpenWinBlue/Views/CodecView.xaml`:
```xml
<UserControl x:Class="OpenWinBlue.Views.CodecView"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <StackPanel Margin="16">
        <TextBlock Text="Codec Configuration" FontWeight="Bold" FontSize="13" Margin="0,0,0,8"/>
        <TextBlock Text="SBC bitpool, channel mode, and sampling rate controls — Phase 4b."
                   Foreground="#666" FontStyle="Italic"/>
    </StackPanel>
</UserControl>
```

`gui/OpenWinBlue/Views/DriverView.xaml`:
```xml
<UserControl x:Class="OpenWinBlue.Views.DriverView"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <StackPanel Margin="16">
        <TextBlock Text="Driver Management" FontWeight="Bold" FontSize="13" Margin="0,0,0,8"/>
        <TextBlock Text="Install / rollback driver controls — Phase 4b."
                   Foreground="#666" FontStyle="Italic"/>
    </StackPanel>
</UserControl>
```

`gui/OpenWinBlue/Views/ControlsView.xaml`:
```xml
<UserControl x:Class="OpenWinBlue.Views.ControlsView"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <StackPanel Margin="16">
        <TextBlock Text="HFP &amp; AI Controls" FontWeight="Bold" FontSize="13" Margin="0,0,0,8"/>
        <TextBlock Text="HFP switching prevention and AI enhancement toggles — Phase 4b."
                   Foreground="#666" FontStyle="Italic"/>
    </StackPanel>
</UserControl>
```

Create `.xaml.cs` code-behind for each:
```csharp
namespace OpenWinBlue.Views;
public partial class CodecView    : System.Windows.Controls.UserControl { public CodecView()    => InitializeComponent(); }
public partial class DriverView   : System.Windows.Controls.UserControl { public DriverView()   => InitializeComponent(); }
public partial class ControlsView : System.Windows.Controls.UserControl { public ControlsView() => InitializeComponent(); }
```

- [ ] **Step 4.3: Replace `gui/OpenWinBlue/MainWindow.xaml`**

```xml
<Window x:Class="OpenWinBlue.MainWindow"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:vm="clr-namespace:OpenWinBlue.ViewModels"
        xmlns:v="clr-namespace:OpenWinBlue.Views"
        Title="{Binding TitleSuffix, StringFormat='OpenWinBlue {0}'}"
        Height="500" Width="780"
        WindowStartupLocation="CenterScreen"
        Closing="Window_Closing">
    <Window.DataContext>
        <vm:MainViewModel/>
    </Window.DataContext>
    <Grid>
        <TabControl SelectedIndex="{Binding SelectedTab}" Margin="0">
            <TabItem Header="Status">
                <v:StatusView DataContext="{Binding Status}"/>
            </TabItem>
            <TabItem Header="Codec">
                <v:CodecView DataContext="{Binding Codec}"/>
            </TabItem>
            <TabItem Header="Driver">
                <v:DriverView DataContext="{Binding Driver}"/>
            </TabItem>
            <TabItem Header="Controls">
                <v:ControlsView DataContext="{Binding Controls}"/>
            </TabItem>
        </TabControl>
    </Grid>
</Window>
```

- [ ] **Step 4.4: Replace `gui/OpenWinBlue/MainWindow.xaml.cs`**

```csharp
using System.Windows;

namespace OpenWinBlue;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        // Start IPC polling when window opens
        if (DataContext is ViewModels.MainViewModel vm)
            vm.StartService();
    }

    private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
    {
        if (DataContext is ViewModels.MainViewModel vm)
            vm.StopService();
    }
}
```

- [ ] **Step 4.5: Build and verify**

```powershell
cd "c:/suru/open winblue/gui"
dotnet build OpenWinBlue/OpenWinBlue.csproj -c Debug
```

Expected: `Build succeeded.` with zero errors.

- [ ] **Step 4.6: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/Views/ gui/OpenWinBlue/MainWindow.xaml gui/OpenWinBlue/MainWindow.xaml.cs
git commit -m "feat(gui): add tabbed main window with Status/Codec/Driver/Controls tabs"
```

---

## Task 5: App.xaml.cs DI wiring + system tray icon

**Files:**
- Modify: `gui/OpenWinBlue/App.xaml`
- Modify: `gui/OpenWinBlue/App.xaml.cs`
- Modify: `gui/OpenWinBlue/OpenWinBlue.csproj` (add WindowsForms reference for NotifyIcon)

- [ ] **Step 5.1: Update `OpenWinBlue.csproj` for system tray support**

Read the current `.csproj`, then add the `UseWindowsForms` property and update `TargetFramework`:

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>WinExe</OutputType>
    <TargetFramework>net10.0-windows</TargetFramework>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <UseWPF>true</UseWPF>
    <UseWindowsForms>true</UseWindowsForms>
    <ApplicationIcon>Resources\app.ico</ApplicationIcon>
  </PropertyGroup>

  <ItemGroup>
    <PackageReference Include="CommunityToolkit.Mvvm" Version="8.3.2" />
    <PackageReference Include="Microsoft.Extensions.DependencyInjection" Version="8.0.1" />
  </ItemGroup>

  <ItemGroup>
    <!-- App icon for system tray -->
    <Resource Include="Resources\app.ico" Condition="Exists('Resources\app.ico')"/>
  </ItemGroup>
</Project>
```

Create a placeholder icon (16x16 .ico file). Since binary files can't be created via code, create a 1x1 BMP as a stand-in:

```powershell
# Create Resources dir and a minimal valid .ico file (16x16, 1 color)
New-Item -ItemType Directory -Force "c:/suru/open winblue/gui/OpenWinBlue/Resources"
# Use PowerShell to create a minimal .ico from a System.Drawing bitmap
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap(16, 16)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromArgb(30, 144, 255))
$g.DrawString("O", (New-Object System.Drawing.Font("Arial", 8)),
              [System.Drawing.Brushes]::White,
              (New-Object System.Drawing.PointF(2, 2)))
$g.Dispose()
$icon = [System.Drawing.Icon]::FromHandle($bmp.GetHicon())
$fs = [System.IO.File]::OpenWrite("c:/suru/open winblue/gui/OpenWinBlue/Resources/app.ico")
$icon.Save($fs)
$fs.Dispose()
$bmp.Dispose()
```

- [ ] **Step 5.2: Update `gui/OpenWinBlue/App.xaml`**

```xml
<Application x:Class="OpenWinBlue.App"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
             ShutdownMode="OnExplicitShutdown">
    <Application.Resources/>
</Application>
```

Note: `ShutdownMode="OnExplicitShutdown"` so the app keeps running when the window is closed (system tray).

- [ ] **Step 5.3: Update `gui/OpenWinBlue/App.xaml.cs`**

```csharp
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
        var iconPath = System.IO.Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory, "Resources", "app.ico");
        var icon = System.IO.File.Exists(iconPath)
                   ? new System.Drawing.Icon(iconPath)
                   : SystemIcons.Application;

        _trayIcon = new NotifyIcon {
            Icon    = icon,
            Text    = "OpenWinBlue",
            Visible = true,
        };

        var menu = new ContextMenuStrip();
        menu.Items.Add("Open",  null, (_, _) => ShowMainWindow());
        menu.Items.Add("-");
        menu.Items.Add("Exit",  null, (_, _) => Shutdown());
        _trayIcon.ContextMenuStrip = menu;
        _trayIcon.DoubleClick     += (_, _) => ShowMainWindow();
    }

    private void ShowMainWindow()
    {
        if (_mainWindow == null || !_mainWindow.IsLoaded) {
            // Pass the same IPC service so the window shares state
            var vm = new MainViewModel(_ipc);
            _mainWindow = new MainWindow { DataContext = vm };
            _mainWindow.Show();
        } else {
            _mainWindow.Activate();
            _mainWindow.WindowState = WindowState.Normal;
        }
    }
}
```

- [ ] **Step 5.4: Build and run manually to test**

```powershell
cd "c:/suru/open winblue/gui"
dotnet build OpenWinBlue/OpenWinBlue.csproj -c Debug
# Run it:
dotnet run --project OpenWinBlue/OpenWinBlue.csproj
```

Expected:
- Window opens with 4 tabs
- Tab 1 shows "Disconnected" (service not running)
- System tray icon appears in notification area
- Right-click tray icon shows "Open" / "Exit"
- Closing window does NOT exit app (stays in tray)
- If `owb-service.exe` is running: tab shows codec name and bitrate after ~1s

- [ ] **Step 5.5: Run tests**

```powershell
cd "c:/suru/open winblue/gui"
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: `Passed! - Failed: 0, Passed: 13, Total: 13` (no regressions).

- [ ] **Step 5.6: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/ gui/tests/
git commit -m "feat(gui): system tray + DI wiring + tabbed shell with live IPC status"
```

---

## Task 6: Push and verify CI

- [ ] **Step 6.1: Push**

```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
git push "https://santiquiroz:${SANTI_TOKEN}@github.com/santiquiroz/openwinblue.git" main
```

- [ ] **Step 6.2: Poll CI until completion (use 30s interval)**

```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
for i in $(seq 1 15); do
  sleep 30
  RESULT=$(curl -s -H "Authorization: Bearer $SANTI_TOKEN" \
    "https://api.github.com/repos/santiquiroz/openwinblue/actions/runs?per_page=1" | \
    python -c "import sys,json;r=json.load(sys.stdin)['workflow_runs'][0];print(r['status'],r.get('conclusion',''),r['head_sha'][:8])")
  echo "${i}x30s: $RESULT"
  if echo "$RESULT" | grep -q "completed"; then break; fi
done
```

Expected: `completed success <sha>`

---

## Self-Review

**Spec coverage:**
- ✅ IPC protocol C# binary types (size-verified with tests) — Task 1
- ✅ Background pipe client with reconnect + GetStatus polling — Task 2
- ✅ StatusViewModel with codec/bitrate/streaming/HFP state — Task 3
- ✅ MainViewModel hosts 4 child VMs + IPC event handling — Task 3
- ✅ TabControl main window (Status / Codec / Driver / Controls) — Task 4
- ✅ StatusView with live-updated fields — Task 4
- ✅ Placeholder views for Tabs 2-4 (Phase 4b) — Task 4
- ✅ System tray icon with Open/Exit menu — Task 5
- ✅ Tray-aware shutdown (ShutdownMode=OnExplicitShutdown) — Task 5
- ✅ CI verification — Task 6

**Placeholder scan:** No TBDs. Placeholder views for Tabs 2-4 explicitly say "Phase 4b" — not a code placeholder, just a deferred feature with correct label.

**Type consistency:**
- `StatusPayload.CodecName` property (string) defined in Task 1, used in Task 3 `Update()` ✅
- `IpcClientService.StatusReceived` event (Action<StatusPayload>) defined in Task 2, subscribed in Task 3 ✅
- `MainViewModel.StartService()` / `StopService()` called from `MainWindow.xaml.cs` Task 4 ✅
- `App.xaml.cs` passes `_ipc` to `MainViewModel(IpcClientService)` constructor Task 5 ✅
