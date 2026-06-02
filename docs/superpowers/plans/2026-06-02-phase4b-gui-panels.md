# Phase 4b — GUI: Codec Config + Driver Management + HFP/AI Controls Panels

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the three placeholder tabs (Codec / Driver / Controls) with real functional panels — SBC codec sliders with Apply via IPC, driver install/rollback via elevated pnputil, and HFP/AI toggle controls.

**Architecture:** Each panel expands its existing stub ViewModel with commands (RelayCommand from CommunityToolkit.Mvvm) and a corresponding XAML View with real controls. `CodecViewModel` receives `IpcClientService` via `MainViewModel` constructor injection. `DriverViewModel` receives `IDriverInstaller` (interface) so it can be tested with a mock. `ControlsViewModel` manages toggle state locally (AI features are Phase 6 stubs, HFP state is display-only in Phase 4b). Phase 4b does not add new IPC message types — it uses `SetCodec` already in the protocol.

**Tech Stack:** C# .NET 10, WPF, CommunityToolkit.Mvvm (RelayCommand, ObservableProperty), `System.ServiceProcess.ServiceController`, `System.Diagnostics.Process` (elevated pnputil), xUnit.

---

## Environment Notes

- Solution: `gui/OpenWinBlue.slnx`
- Build: `dotnet build gui/OpenWinBlue.slnx -c Debug`
- Test: `dotnet test gui/tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj`
- Current tests: 12 passing — must stay passing, plan adds 7 more (total 19)

---

## File Map

### Modified files

```
gui/OpenWinBlue/
  ViewModels/
    MainViewModel.cs        # inject IpcClientService into CodecViewModel ctor
    CodecViewModel.cs       # REPLACED: add IpcClientService, RelayCommand, validation
    DriverViewModel.cs      # REPLACED: add IDriverInstaller, install/rollback commands
    ControlsViewModel.cs    # REPLACED: add PropChanged notifications, Phase 6 stubs

  Views/
    CodecView.xaml          # REPLACED: Slider + ComboBox + Apply button
    CodecView.xaml.cs       # unchanged
    DriverView.xaml         # REPLACED: status label + Install + Rollback buttons
    DriverView.xaml.cs      # unchanged
    ControlsView.xaml       # REPLACED: CheckBoxes for HFP/AI/bitrate toggles
    ControlsView.xaml.cs    # unchanged

  Services/
    IDriverInstaller.cs     # NEW: interface for install/rollback (enables mocking)
    DriverInstallerService.cs  # NEW: pnputil implementation via Process.Start

gui/tests/OpenWinBlue.Tests/
  CodecViewModelTests.cs    # NEW: 3 tests
  DriverViewModelTests.cs   # NEW: 2 tests
  ControlsViewModelTests.cs # NEW: 2 tests
```

---

## Task 1: IDriverInstaller interface + service + DriverViewModel

**Files:**
- Create: `gui/OpenWinBlue/Services/IDriverInstaller.cs`
- Create: `gui/OpenWinBlue/Services/DriverInstallerService.cs`
- Modify: `gui/OpenWinBlue/ViewModels/DriverViewModel.cs`
- Create: `gui/tests/OpenWinBlue.Tests/DriverViewModelTests.cs`

- [ ] **Step 1.1: Write failing tests first**

```csharp
// gui/tests/OpenWinBlue.Tests/DriverViewModelTests.cs
using NSubstitute;
using OpenWinBlue.Services;
using OpenWinBlue.ViewModels;

namespace OpenWinBlue.Tests;

public class DriverViewModelTests
{
    [Fact]
    public void DriverViewModel_ConstructsWithUnknownStatus()
    {
        var installer = Substitute.For<IDriverInstaller>();
        installer.IsInstalled.Returns(false);
        var vm = new DriverViewModel(installer);
        Assert.Equal("Not installed", vm.DriverStatus);
    }

    [Fact]
    public void DriverViewModel_RefreshStatus_UpdatesDriverStatus()
    {
        var installer = Substitute.For<IDriverInstaller>();
        installer.IsInstalled.Returns(true);
        var vm = new DriverViewModel(installer);
        vm.RefreshStatus();
        Assert.Equal("Installed ✓", vm.DriverStatus);
    }
}
```

Save to: `gui/tests/OpenWinBlue.Tests/DriverViewModelTests.cs`

- [ ] **Step 1.2: Add NSubstitute to test project**

```powershell
cd "c:/suru/open winblue/gui/tests/OpenWinBlue.Tests"
dotnet add package NSubstitute --version 5.3.0
```

- [ ] **Step 1.3: Run failing tests**

```powershell
cd "c:/suru/open winblue/gui"
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --filter "DriverViewModel" --verbosity normal
```

Expected: FAIL with "type not found"

- [ ] **Step 1.4: Create `gui/OpenWinBlue/Services/IDriverInstaller.cs`**

```csharp
// gui/OpenWinBlue/Services/IDriverInstaller.cs
namespace OpenWinBlue.Services;

public interface IDriverInstaller
{
    /// <summary>True if owb_a2dp.sys is currently installed as a service.</summary>
    bool IsInstalled { get; }

    /// <summary>Run pnputil /add-driver elevated to install the driver.</summary>
    /// <param name="infPath">Absolute path to owb_a2dp.inf</param>
    void Install(string infPath);

    /// <summary>Run pnputil /delete-driver elevated to remove the driver.</summary>
    void Rollback();
}
```

- [ ] **Step 1.5: Create `gui/OpenWinBlue/Services/DriverInstallerService.cs`**

```csharp
// gui/OpenWinBlue/Services/DriverInstallerService.cs
using System.Diagnostics;
using System.ServiceProcess;

namespace OpenWinBlue.Services;

/// <summary>
/// Real implementation that invokes pnputil via an elevated Process.
/// Requires the user to accept a UAC prompt for install/rollback.
/// </summary>
public sealed class DriverInstallerService : IDriverInstaller
{
    public bool IsInstalled
    {
        get
        {
            // Check if the owb_a2dp service (kernel driver) is registered.
            try {
                using var sc = new ServiceController("owb_a2dp");
                // Accessing Status throws if service does not exist.
                _ = sc.Status;
                return true;
            }
            catch (InvalidOperationException) { return false; }
        }
    }

    public void Install(string infPath)
    {
        Process.Start(new ProcessStartInfo {
            FileName        = "pnputil.exe",
            Arguments       = $"/add-driver \"{infPath}\" /install",
            Verb            = "runas",          // triggers UAC prompt
            UseShellExecute = true,
        });
    }

    public void Rollback()
    {
        Process.Start(new ProcessStartInfo {
            FileName        = "pnputil.exe",
            Arguments       = "/delete-driver owb_a2dp.inf /uninstall",
            Verb            = "runas",
            UseShellExecute = true,
        });
    }
}
```

- [ ] **Step 1.6: Replace `gui/OpenWinBlue/ViewModels/DriverViewModel.cs`**

```csharp
// gui/OpenWinBlue/ViewModels/DriverViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using OpenWinBlue.Services;
using System.IO;

namespace OpenWinBlue.ViewModels;

public partial class DriverViewModel : ObservableObject
{
    private readonly IDriverInstaller _installer;

    [ObservableProperty] private string _driverStatus  = "Checking…";
    [ObservableProperty] private bool   _canInstall    = false;
    [ObservableProperty] private bool   _canRollback   = false;

    // Design-time constructor
    public DriverViewModel() : this(new DriverInstallerService()) { }

    public DriverViewModel(IDriverInstaller installer)
    {
        _installer = installer;
        RefreshStatus();
    }

    public void RefreshStatus()
    {
        bool installed = _installer.IsInstalled;
        DriverStatus = installed ? "Installed ✓" : "Not installed";
        CanInstall   = !installed;
        CanRollback  =  installed;
    }

    [RelayCommand(CanExecute = nameof(CanInstall))]
    private void InstallDriver()
    {
        // Look for the INF relative to the executable directory
        var infPath = Path.Combine(
            AppDomain.CurrentDomain.BaseDirectory,
            "driver", "owb_a2dp.inf");
        _installer.Install(infPath);
        RefreshStatus();
    }

    [RelayCommand(CanExecute = nameof(CanRollback))]
    private void RollbackDriver()
    {
        _installer.Rollback();
        RefreshStatus();
    }
}
```

- [ ] **Step 1.7: Run tests**

```powershell
cd "c:/suru/open winblue/gui"
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: `Passed! - Failed: 0, Passed: 14, Total: 14`

- [ ] **Step 1.8: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/Services/IDriverInstaller.cs gui/OpenWinBlue/Services/DriverInstallerService.cs
git add gui/OpenWinBlue/ViewModels/DriverViewModel.cs
git add gui/tests/OpenWinBlue.Tests/DriverViewModelTests.cs
git commit -m "feat(gui): DriverViewModel with install/rollback commands and IDriverInstaller interface"
```

---

## Task 2: CodecViewModel with Apply command

**Files:**
- Modify: `gui/OpenWinBlue/ViewModels/CodecViewModel.cs`
- Modify: `gui/OpenWinBlue/ViewModels/MainViewModel.cs`
- Create: `gui/tests/OpenWinBlue.Tests/CodecViewModelTests.cs`

- [ ] **Step 2.1: Write failing tests first**

```csharp
// gui/tests/OpenWinBlue.Tests/CodecViewModelTests.cs
using NSubstitute;
using OpenWinBlue.Services;
using OpenWinBlue.ViewModels;

namespace OpenWinBlue.Tests;

public class CodecViewModelTests
{
    [Fact]
    public void CodecViewModel_DefaultBitpool_Is53()
    {
        var ipc = Substitute.For<IIpcSender>();
        var vm = new CodecViewModel(ipc);
        Assert.Equal(53, vm.Bitpool);
    }

    [Fact]
    public void CodecViewModel_ApplyCommand_SendsSetCodecBitpool()
    {
        var ipc = Substitute.For<IIpcSender>();
        ipc.IsConnected.Returns(true);
        var vm = new CodecViewModel(ipc) { Bitpool = 40 };
        vm.ApplyCodecCommand.Execute(null);
        ipc.Received().SendSetCodec("SBC", "bitpool", 40);
    }

    [Fact]
    public void CodecViewModel_ApplyCommand_DisabledWhenNotConnected()
    {
        var ipc = Substitute.For<IIpcSender>();
        ipc.IsConnected.Returns(false);
        var vm = new CodecViewModel(ipc);
        Assert.False(vm.ApplyCodecCommand.CanExecute(null));
    }
}
```

Save to: `gui/tests/OpenWinBlue.Tests/CodecViewModelTests.cs`

Note: We introduce `IIpcSender` as a minimal interface on `IpcClientService` so `CodecViewModel` can be tested without a real pipe.

- [ ] **Step 2.2: Create `gui/OpenWinBlue/Services/IIpcSender.cs`**

```csharp
// gui/OpenWinBlue/Services/IIpcSender.cs
namespace OpenWinBlue.Services;

/// <summary>
/// Minimal interface for sending commands to the OpenWinBlue service.
/// Implemented by IpcClientService; mockable in tests.
/// </summary>
public interface IIpcSender
{
    bool IsConnected { get; }
    bool SendSetCodec(string codec, string paramKey, long paramValue);
}
```

- [ ] **Step 2.3: Make `IpcClientService` implement `IIpcSender`**

Read `gui/OpenWinBlue/Services/IpcClientService.cs`. Add `: IIpcSender` to the class declaration:

```csharp
public sealed class IpcClientService : IIpcSender, IDisposable
```

No other changes needed — `IsConnected` and `SendSetCodec` already match the interface.

- [ ] **Step 2.4: Replace `gui/OpenWinBlue/ViewModels/CodecViewModel.cs`**

```csharp
// gui/OpenWinBlue/ViewModels/CodecViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using OpenWinBlue.Services;

namespace OpenWinBlue.ViewModels;

public partial class CodecViewModel : ObservableObject
{
    private readonly IIpcSender _ipc;

    // SBC parameters
    [ObservableProperty] private int    _bitpool     = 53;   // 2–64
    [ObservableProperty] private int    _channelModeIndex = 3; // 0=Mono 1=Stereo 2=DualCh 3=JointStereo
    [ObservableProperty] private int    _sampleRateIndex  = 0; // 0=44100 1=48000

    public string[] ChannelModes  { get; } = { "Mono", "Stereo", "Dual Channel", "Joint Stereo" };
    public string[] SampleRates   { get; } = { "44 100 Hz", "48 000 Hz" };
    private static readonly int[] SampleRateHz = { 44100, 48000 };

    // Design-time constructor
    public CodecViewModel() : this(new NullIpcSender()) { }

    public CodecViewModel(IIpcSender ipc)
    {
        _ipc = ipc;
    }

    [RelayCommand(CanExecute = nameof(CanApply))]
    private void ApplyCodec()
    {
        _ipc.SendSetCodec("SBC", "bitpool",  Bitpool);
        _ipc.SendSetCodec("SBC", "mode",     ChannelModeIndex);
        _ipc.SendSetCodec("SBC", "freq",     SampleRateHz[SampleRateIndex]);
    }

    private bool CanApply() =>
        _ipc.IsConnected && Bitpool >= 2 && Bitpool <= 64;

    partial void OnBitpoolChanged(int value)        => ApplyCodecCommand.NotifyCanExecuteChanged();
    partial void OnChannelModeIndexChanged(int value) { }
    partial void OnSampleRateIndexChanged(int value)  { }

    /// <summary>Null-object sender for design-time and disconnected state.</summary>
    private sealed class NullIpcSender : IIpcSender {
        public bool IsConnected => false;
        public bool SendSetCodec(string c, string k, long v) => false;
    }
}
```

- [ ] **Step 2.5: Update `MainViewModel` to inject IIpcSender into CodecViewModel**

Read `gui/OpenWinBlue/ViewModels/MainViewModel.cs`. Change the `Codec` property initialization in the constructor:

```csharp
// In MainViewModel:
public CodecViewModel    Codec    { get; }  // remove "= new()"

public MainViewModel(IpcClientService ipc)
{
    _ipc  = ipc;
    Codec = new CodecViewModel(ipc);  // inject IPC
    _ipc.StatusReceived += OnStatusReceived;
}
```

The no-arg constructor should also be updated:
```csharp
public MainViewModel() : this(new IpcClientService()) { }
```

The other child VMs (Status, Driver, Controls) keep their current initialization.

- [ ] **Step 2.6: Run tests**

```powershell
cd "c:/suru/open winblue/gui"
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: `Passed! - Failed: 0, Passed: 17, Total: 17`

- [ ] **Step 2.7: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/Services/IIpcSender.cs gui/OpenWinBlue/Services/IpcClientService.cs
git add gui/OpenWinBlue/ViewModels/CodecViewModel.cs gui/OpenWinBlue/ViewModels/MainViewModel.cs
git add gui/tests/OpenWinBlue.Tests/CodecViewModelTests.cs
git commit -m "feat(gui): CodecViewModel with ApplyCodec command via IIpcSender + tests"
```

---

## Task 3: ControlsViewModel with toggle state + tests

**Files:**
- Modify: `gui/OpenWinBlue/ViewModels/ControlsViewModel.cs`
- Create: `gui/tests/OpenWinBlue.Tests/ControlsViewModelTests.cs`

- [ ] **Step 3.1: Write failing tests**

```csharp
// gui/tests/OpenWinBlue.Tests/ControlsViewModelTests.cs
using OpenWinBlue.ViewModels;

namespace OpenWinBlue.Tests;

public class ControlsViewModelTests
{
    [Fact]
    public void ControlsViewModel_HfpGuardEnabled_DefaultsToTrue()
    {
        var vm = new ControlsViewModel();
        Assert.True(vm.HfpGuardEnabled);
    }

    [Fact]
    public void ControlsViewModel_PropertyChanged_FiresOnToggle()
    {
        var vm = new ControlsViewModel();
        var changed = new List<string?>();
        vm.PropertyChanged += (_, e) => changed.Add(e.PropertyName);
        vm.HfpGuardEnabled = false;
        Assert.Contains(nameof(vm.HfpGuardEnabled), changed);
    }
}
```

- [ ] **Step 3.2: Replace `gui/OpenWinBlue/ViewModels/ControlsViewModel.cs`**

```csharp
// gui/OpenWinBlue/ViewModels/ControlsViewModel.cs
using CommunityToolkit.Mvvm.ComponentModel;

namespace OpenWinBlue.ViewModels;

public partial class ControlsViewModel : ObservableObject
{
    // HFP Guard: reflects the current guard level (Level 2 is always on in the service).
    // Toggling will send a future IPC command in Phase 5.
    [ObservableProperty] private bool _hfpGuardEnabled = true;

    // AI features — implemented in Phase 6 (ONNX/DirectML).
    // Disabled controls in the UI; state preserved for when backend is ready.
    [ObservableProperty] private bool _noiseReduction  = false;
    [ObservableProperty] private bool _adaptiveBitrate = true;

    public string HfpGuardNote =>
        HfpGuardEnabled
        ? "HFP guard is active — headphones stay in A2DP stereo mode."
        : "HFP guard disabled — headphones may switch to mono when mic is used.";

    partial void OnHfpGuardEnabledChanged(bool value)
        => OnPropertyChanged(nameof(HfpGuardNote));
}
```

- [ ] **Step 3.3: Run tests**

```powershell
cd "c:/suru/open winblue/gui"
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: `Passed! - Failed: 0, Passed: 19, Total: 19`

- [ ] **Step 3.4: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/ViewModels/ControlsViewModel.cs
git add gui/tests/OpenWinBlue.Tests/ControlsViewModelTests.cs
git commit -m "feat(gui): ControlsViewModel with HFP/AI toggles and HfpGuardNote"
```

---

## Task 4: CodecView.xaml — real controls

**Files:**
- Modify: `gui/OpenWinBlue/Views/CodecView.xaml`

- [ ] **Step 4.1: Replace `gui/OpenWinBlue/Views/CodecView.xaml`**

```xml
<UserControl x:Class="OpenWinBlue.Views.CodecView"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <StackPanel Margin="16">
        <TextBlock Text="Codec Configuration" FontWeight="Bold" FontSize="13" Margin="0,0,0,12"/>

        <!-- SBC Bitpool -->
        <Grid Margin="0,0,0,12">
            <Grid.ColumnDefinitions>
                <ColumnDefinition Width="140"/>
                <ColumnDefinition Width="180"/>
                <ColumnDefinition Width="50"/>
            </Grid.ColumnDefinitions>
            <TextBlock Grid.Column="0" Text="Bitpool (quality):" VerticalAlignment="Center" Foreground="#888"/>
            <Slider   Grid.Column="1" Minimum="2" Maximum="64" SmallChange="1" LargeChange="5"
                      Value="{Binding Bitpool, Mode=TwoWay}"
                      VerticalAlignment="Center"/>
            <TextBlock Grid.Column="2" Text="{Binding Bitpool}" VerticalAlignment="Center"
                       FontFamily="Consolas" Margin="6,0,0,0"/>
        </Grid>

        <!-- Channel Mode -->
        <Grid Margin="0,0,0,12">
            <Grid.ColumnDefinitions>
                <ColumnDefinition Width="140"/>
                <ColumnDefinition Width="180"/>
            </Grid.ColumnDefinitions>
            <TextBlock Grid.Column="0" Text="Channel mode:" VerticalAlignment="Center" Foreground="#888"/>
            <ComboBox  Grid.Column="1"
                       ItemsSource="{Binding ChannelModes}"
                       SelectedIndex="{Binding ChannelModeIndex, Mode=TwoWay}"
                       VerticalAlignment="Center"/>
        </Grid>

        <!-- Sample Rate -->
        <Grid Margin="0,0,0,16">
            <Grid.ColumnDefinitions>
                <ColumnDefinition Width="140"/>
                <ColumnDefinition Width="180"/>
            </Grid.ColumnDefinitions>
            <TextBlock Grid.Column="0" Text="Sample rate:" VerticalAlignment="Center" Foreground="#888"/>
            <ComboBox  Grid.Column="1"
                       ItemsSource="{Binding SampleRates}"
                       SelectedIndex="{Binding SampleRateIndex, Mode=TwoWay}"
                       VerticalAlignment="Center"/>
        </Grid>

        <!-- Apply button -->
        <StackPanel Orientation="Horizontal">
            <Button Content="Apply to device"
                    Command="{Binding ApplyCodecCommand}"
                    Padding="12,4"
                    MinWidth="120"/>
            <TextBlock Margin="12,0,0,0" VerticalAlignment="Center"
                       Foreground="#666" FontSize="11"
                       Text="Requires service running and device connected."/>
        </StackPanel>

        <TextBlock Margin="0,12,0,0" Foreground="#888" FontSize="11" TextWrapping="Wrap"
                   Text="Bitpool 53 = recommended high quality (A2DP spec). Higher = better but may stutter on weak signal. Dual Channel doubles bitrate vs Joint Stereo at same bitpool."/>
    </StackPanel>
</UserControl>
```

- [ ] **Step 4.2: Build to verify XAML compiles**

```powershell
cd "c:/suru/open winblue/gui"
dotnet build OpenWinBlue/OpenWinBlue.csproj -c Debug
```

Expected: Build succeeded, 0 errors.

- [ ] **Step 4.3: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/Views/CodecView.xaml
git commit -m "feat(gui): CodecView with bitpool slider, channel mode + sample rate selectors"
```

---

## Task 5: DriverView.xaml + ControlsView.xaml

**Files:**
- Modify: `gui/OpenWinBlue/Views/DriverView.xaml`
- Modify: `gui/OpenWinBlue/Views/ControlsView.xaml`

- [ ] **Step 5.1: Replace `gui/OpenWinBlue/Views/DriverView.xaml`**

```xml
<UserControl x:Class="OpenWinBlue.Views.DriverView"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <StackPanel Margin="16">
        <TextBlock Text="Driver Management" FontWeight="Bold" FontSize="13" Margin="0,0,0,12"/>

        <!-- Status row -->
        <Grid Margin="0,0,0,16">
            <Grid.ColumnDefinitions>
                <ColumnDefinition Width="120"/>
                <ColumnDefinition Width="*"/>
            </Grid.ColumnDefinitions>
            <TextBlock Grid.Column="0" Text="Driver status:" VerticalAlignment="Center" Foreground="#888"/>
            <TextBlock Grid.Column="1" VerticalAlignment="Center" FontWeight="SemiBold">
                <TextBlock.Style>
                    <Style TargetType="TextBlock">
                        <Setter Property="Text"       Value="{Binding DriverStatus}"/>
                        <Setter Property="Foreground"  Value="#888"/>
                        <Style.Triggers>
                            <DataTrigger Binding="{Binding CanRollback}" Value="True">
                                <Setter Property="Foreground" Value="#22C55E"/>
                            </DataTrigger>
                            <DataTrigger Binding="{Binding CanInstall}" Value="True">
                                <Setter Property="Foreground" Value="#F59E0B"/>
                            </DataTrigger>
                        </Style.Triggers>
                    </Style>
                </TextBlock.Style>
            </TextBlock>
        </Grid>

        <!-- Action buttons -->
        <StackPanel Orientation="Horizontal" Margin="0,0,0,12">
            <Button Content="Install Driver"
                    Command="{Binding InstallDriverCommand}"
                    Padding="12,4" MinWidth="120" Margin="0,0,8,0"/>
            <Button Content="Rollback to Windows Default"
                    Command="{Binding RollbackDriverCommand}"
                    Padding="12,4" MinWidth="200"/>
        </StackPanel>

        <TextBlock Foreground="#888" FontSize="11" TextWrapping="Wrap"
                   Text="Install requires Administrator (UAC prompt). Rollback restores btavchdt.sys and requires a reboot to complete."/>

        <TextBlock Margin="0,12,0,0" Foreground="#888" FontSize="11" TextWrapping="Wrap"
                   Text="Emergency recovery: run tools\owb-rollback.bat as Administrator from the installation directory."/>
    </StackPanel>
</UserControl>
```

- [ ] **Step 5.2: Replace `gui/OpenWinBlue/Views/ControlsView.xaml`**

```xml
<UserControl x:Class="OpenWinBlue.Views.ControlsView"
             xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
             xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
    <StackPanel Margin="16">
        <TextBlock Text="HFP &amp; AI Controls" FontWeight="Bold" FontSize="13" Margin="0,0,0,12"/>

        <!-- HFP Guard -->
        <TextBlock Text="HFP Guard" FontWeight="SemiBold" Margin="0,0,0,6"/>
        <CheckBox Content="Prevent automatic HFP switching (keep A2DP stereo active)"
                  IsChecked="{Binding HfpGuardEnabled, Mode=TwoWay}"
                  Margin="0,0,0,4"/>
        <TextBlock Text="{Binding HfpGuardNote}"
                   Foreground="#666" FontSize="11" TextWrapping="Wrap"
                   Margin="0,0,0,16"/>

        <!-- AI Enhancement -->
        <TextBlock Text="AI Audio Enhancement" FontWeight="SemiBold" Margin="0,0,0,6"/>
        <TextBlock Text="Requires ONNX Runtime + DirectML (any DX12 GPU). Coming in Phase 6."
                   Foreground="#888" FontSize="11" Margin="0,0,0,8"/>

        <CheckBox Content="AI Noise Reduction (DeepFilterNet3 — ~12ms GPU latency)"
                  IsChecked="{Binding NoiseReduction, Mode=TwoWay}"
                  IsEnabled="False"
                  Margin="0,0,0,4"/>
        <CheckBox Content="Psychoacoustic Pre-Emphasis (DSP, codec-aware EQ)"
                  IsEnabled="False"
                  Margin="0,0,0,4"/>
        <CheckBox Content="Smart Adaptive Bitrate (RF-quality driven)"
                  IsChecked="{Binding AdaptiveBitrate, Mode=TwoWay}"
                  IsEnabled="False"
                  Margin="0,0,0,4"/>
    </StackPanel>
</UserControl>
```

- [ ] **Step 5.3: Build and run all tests**

```powershell
cd "c:/suru/open winblue/gui"
dotnet build OpenWinBlue/OpenWinBlue.csproj -c Debug
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: Build succeeded, `Passed! - Failed: 0, Passed: 19, Total: 19`

- [ ] **Step 5.4: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/Views/DriverView.xaml gui/OpenWinBlue/Views/ControlsView.xaml
git commit -m "feat(gui): DriverView with install/rollback buttons, ControlsView with HFP/AI toggles"
```

---

## Task 6: Push + CI verification

- [ ] **Step 6.1: Push**

```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
git push "https://santiquiroz:${SANTI_TOKEN}@github.com/santiquiroz/openwinblue.git" main
```

- [ ] **Step 6.2: Poll CI**

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
- ✅ SBC bitpool slider (2–64, default 53) — Task 4
- ✅ Channel mode ComboBox (Mono/Stereo/DualCh/JointStereo) — Task 4
- ✅ Sample rate ComboBox (44.1 / 48 kHz) — Task 4
- ✅ Apply button sends SetCodec via IPC when connected — Task 2
- ✅ Apply disabled when not connected — Task 2 (CanApply)
- ✅ Driver status (Installed/Not installed) — Task 1
- ✅ Install driver button (pnputil elevated) — Task 1
- ✅ Rollback button — Task 1
- ✅ Emergency recovery note (owb-rollback.bat) — Task 5 (DriverView.xaml)
- ✅ HFP Guard toggle with status note — Task 3 + Task 5
- ✅ AI toggles disabled with Phase 6 note — Task 5
- ✅ 19 tests passing — Tasks 1-3

**Placeholder scan:** No TBDs. "Phase 6" labels on disabled controls are user-facing feature notes, not code placeholders.

**Type consistency:**
- `IIpcSender.SendSetCodec(string, string, long)` defined in Task 2, used by `CodecViewModel.ApplyCodec()` ✅
- `IDriverInstaller.Install(string), Rollback(), IsInstalled` defined in Task 1, used by `DriverViewModel` ✅
- `ControlsViewModel.HfpGuardNote` (computed property) defined in Task 3, bound in `ControlsView.xaml` Task 5 ✅
- `DriverViewModel.CanInstall` / `CanRollback` booleans defined in Task 1, used as `DataTrigger` in Task 5 ✅
