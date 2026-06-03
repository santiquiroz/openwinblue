# Phase 7 — Polish & Release: LC3 Codec + WiX Installer + HFP Level 1

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the project with LC3 (LE Audio) codec support, a functional WiX installer that packages the service + GUI + driver, HFP Guard Level 1 (registry-based), and final CLAUDE.md updates reflecting the complete build environment.

**Architecture:** `CodecLc3` wraps liblc3 (Apache 2.0, already submoduled) implementing `ICodec`. It joins the `CodecFactory` alongside SBC/LDAC/aptX/aptX-HD. The WiX installer bundles `owb_service.exe`, `OpenWinBlue.exe`, `owb_a2dp.sys`, and the rollback script. HFP Level 1 is a `DriverInstallerService` enhancement that disables the `Handsfree Telephony` service on a specific BT device via `ServiceController`. CLAUDE.md is updated with the real environment discovered during development.

**Tech Stack:** C++20, liblc3 (Apache 2.0, `third-party/liblc3/`), WiX v5 (MSI), C# `ServiceController`, GoogleTest.

---

## Environment Notes

- **WDK installed**: `C:/Program Files (x86)/Windows Kits/10/` (10.0.26100)
- **cmake**: `C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe`
- **dotnet**: .NET 10 SDK
- **Solution**: `gui/OpenWinBlue.slnx`

---

## File Map

```
service/
  codecs/
    codec_lc3.h/.cpp        # NEW: LC3 codec wrapper via liblc3
  CMakeLists.txt            # MODIFIED: add owb_lc3 + owb_codec_lc3

gui/
  OpenWinBlue/Services/
    DriverInstallerService.cs  # MODIFIED: add DisableHfpProfile(deviceAddr) method
  ViewModels/
    ControlsViewModel.cs    # MODIFIED: add DisableHfpProfileCommand
  Views/
    ControlsView.xaml        # MODIFIED: add "Disable HFP Profile (Level 1)" button

installer/
  Product.wxs               # REPLACED: package owb_service + GUI + driver
  OpenWinBlue.wixproj       # MODIFIED: reference actual build outputs

CLAUDE.md                   # UPDATED: real build env, cmake location, .slnx format

tests/service/
  codec_lc3_test.cpp        # NEW: 5 tests for LC3 encoder
```

---

## Task 1: liblc3 CMake + CodecLc3 wrapper

**Files:**
- Modify: `service/CMakeLists.txt`
- Create: `service/codecs/codec_lc3.h`
- Create: `service/codecs/codec_lc3.cpp`
- Create: `tests/service/codec_lc3_test.cpp`

**liblc3 API:**
```c
// Frame duration: 10000 us (10ms)
// Frame samples: lc3_frame_samples(10000, 48000) = 480
// Frame bytes at 80kbps: lc3_frame_bytes(10000, 80000) = 100
lc3_encoder_t enc = lc3_setup_encoder(10000, 48000, 0,  // dt_us, sr_hz, sr_pcm_hz
                                       malloc(lc3_encoder_size(10000, 48000)));
int err = lc3_encode(enc, LC3_PCM_FORMAT_S16, pcm_ptr, 2 /*stride=channels*/,
                     frame_bytes, output_buf);
```

- [ ] **Step 1.1: Write failing tests**

```cpp
// tests/service/codec_lc3_test.cpp
#include <gtest/gtest.h>
#include <vector>
#include "codec_lc3.h"

namespace {
std::vector<int16_t> make_pcm_lc3(int stereo_frames) {
    std::vector<int16_t> buf(static_cast<size_t>(stereo_frames * 2));
    for (int i = 0; i < stereo_frames * 2; ++i)
        buf[i] = static_cast<int16_t>((i % 64) * 512 - 16384);
    return buf;
}
} // namespace

TEST(CodecLc3, NameIsLC3) {
    owb::CodecLc3 codec;
    EXPECT_EQ(codec.name(), "LC3");
}

TEST(CodecLc3, DefaultFreqIs48000) {
    owb::CodecLc3 codec;
    EXPECT_EQ(codec.get_param("freq"), 48000);
}

TEST(CodecLc3, DefaultBitrateIs80000) {
    owb::CodecLc3 codec;
    EXPECT_EQ(codec.get_param("bitrate"), 80000);
}

TEST(CodecLc3, UnknownParamReturnsNullopt) {
    owb::CodecLc3 codec;
    EXPECT_EQ(codec.get_param("nonexistent"), std::nullopt);
}

TEST(CodecLc3, EncodeProducesOutput) {
    owb::CodecLc3 codec;
    // LC3 frame = 480 stereo samples at 48kHz
    auto pcm = make_pcm_lc3(480);
    std::vector<uint8_t> out(512);
    auto n = codec.encode(pcm, out);
    EXPECT_GT(n, 0) << "LC3 encode should produce output";
}
```

- [ ] **Step 1.2: Add liblc3 to `service/CMakeLists.txt`**

Read the current file. Find the liblc3 source files:
```powershell
ls "c:/suru/open winblue/third-party/liblc3/" -Recurse -Filter "*.c" | Select-Object Name
```

Add after owb_codec_ldac section:

```cmake
# ── liblc3 static library (LC3 / LE Audio, Google, Apache 2.0) ────────────────
file(GLOB OWB_LC3_SOURCES
    ${CMAKE_SOURCE_DIR}/third-party/liblc3/src/*.c
)
add_library(owb_lc3 STATIC ${OWB_LC3_SOURCES})
target_include_directories(owb_lc3 PUBLIC
    ${CMAKE_SOURCE_DIR}/third-party/liblc3/include
)
target_compile_options(owb_lc3 PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W0 /utf-8>
)

# ── LC3 codec C++ wrapper ─────────────────────────────────────────────────────
add_library(owb_codec_lc3 STATIC
    codecs/codec_lc3.cpp
)
target_include_directories(owb_codec_lc3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/codecs
)
target_link_libraries(owb_codec_lc3 PUBLIC owb_lc3)
target_compile_options(owb_codec_lc3 PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)
```

Add `owb_codec_lc3` to `target_link_libraries(owb_service ...)` and `target_link_libraries(owb_codec_factory ...)`.

- [ ] **Step 1.3: Create `service/codecs/codec_lc3.h`**

```cpp
// service/codecs/codec_lc3.h
#pragma once
#include "codec_interface.h"
#include <memory>
#include <cstdlib>

// Opaque forward declaration
struct lc3_encoder;

namespace owb {

/// LC3 codec wrapper (LE Audio, liblc3 Apache 2.0).
/// Default: 48kHz, 80kbps, 10ms frames (480 PCM samples).
class CodecLc3 final : public ICodec {
public:
    CodecLc3();
    ~CodecLc3() override;

    std::string_view        name()     const noexcept override;
    std::ptrdiff_t          encode(std::span<const int16_t> input,
                                   std::span<uint8_t>       output) override;
    bool                    set_param(CodecParam param)           override;
    std::optional<int64_t>  get_param(std::string_view key) const override;

private:
    void reinit();

    struct lc3_encoder* enc_  = nullptr;
    void*               mem_  = nullptr;
    int                 freq_    = 48000;
    int                 bitrate_ = 80000;
    static constexpr int kDtUs = 10000;  // 10ms frame duration
};

} // namespace owb
```

- [ ] **Step 1.4: Create `service/codecs/codec_lc3.cpp`**

```cpp
// service/codecs/codec_lc3.cpp
#include "codec_lc3.h"
#include <lc3.h>
#include <cstdlib>
#include <cstring>

namespace owb {

CodecLc3::CodecLc3() { reinit(); }

CodecLc3::~CodecLc3() {
    std::free(mem_);
}

void CodecLc3::reinit() {
    std::free(mem_);
    unsigned sz = lc3_encoder_size(kDtUs, freq_);
    mem_ = std::malloc(sz);
    enc_ = lc3_setup_encoder(kDtUs, freq_, 0, mem_);
}

std::string_view CodecLc3::name() const noexcept { return "LC3"; }

std::ptrdiff_t CodecLc3::encode(std::span<const int16_t> input,
                                 std::span<uint8_t>       output) {
    if (!enc_ || output.empty() || input.empty()) return -1;

    const int frame_samples = lc3_frame_samples(kDtUs, freq_);
    const int frame_bytes   = lc3_frame_bytes(kDtUs, bitrate_);

    if (frame_bytes <= 0 || static_cast<int>(output.size()) < frame_bytes) return -1;
    if (static_cast<int>(input.size()) < frame_samples * 2) return -1;

    // lc3_encode expects stride = number of channels (2 for stereo interleaved)
    int err = lc3_encode(enc_, LC3_PCM_FORMAT_S16,
                          input.data(), 2 /*stride*/,
                          frame_bytes, output.data());
    if (err != 0) return -1;
    return static_cast<std::ptrdiff_t>(frame_bytes);
}

bool CodecLc3::set_param(CodecParam p) {
    if (p.key == "freq") {
        freq_ = static_cast<int>(p.value);
        reinit();
        return true;
    }
    if (p.key == "bitrate") {
        bitrate_ = static_cast<int>(p.value);
        return true;
    }
    return false;
}

std::optional<int64_t> CodecLc3::get_param(std::string_view key) const {
    if (key == "freq")    return freq_;
    if (key == "bitrate") return bitrate_;
    return std::nullopt;
}

} // namespace owb
```

- [ ] **Step 1.5: Add codec_lc3_test.cpp to tests + update codec_factory**

Add `codec_lc3_test.cpp` to `tests/service/CMakeLists.txt` sources.
Add `owb_codec_lc3` to test link libraries.

Update `service/codecs/codec_factory.cpp`: add `#include "codec_lc3.h"` and add case:
```cpp
case OWB_CODEC_LC3: return std::make_unique<CodecLc3>();
```

Add test to `tests/service/codec_factory_test.cpp`:
```cpp
TEST(CodecFactory, CreateLc3_NamedLC3) {
    auto codec = owb::CodecFactory::create(OWB_CODEC_LC3);
    ASSERT_NE(codec, nullptr);
    EXPECT_EQ(codec->name(), "LC3");
}
```

Add `"LC3"` to `gui/OpenWinBlue/ViewModels/CodecViewModel.cs` `AvailableCodecs` array.

- [ ] **Step 1.6: Build and run LC3 tests**

```powershell
Set-Location "c:\suru\open winblue"
& cmake --preset nmake-debug
& cmake --build build/nmake-debug --target owb_service_tests
Set-Location build/nmake-debug
& ctest --output-on-failure -R "CodecLc3"
```

Expected: 5 LC3 tests pass + 1 factory test.

- [ ] **Step 1.7: Commit**

```powershell
cd "c:/suru/open winblue"
git add service/codecs/codec_lc3.h service/codecs/codec_lc3.cpp
git add service/codecs/codec_factory.cpp service/CMakeLists.txt
git add tests/service/codec_lc3_test.cpp tests/service/CMakeLists.txt
git add gui/OpenWinBlue/ViewModels/CodecViewModel.cs
git commit -m "feat(codec): add LC3 codec wrapper via liblc3 (Apache 2.0) — LE Audio support"
```

---

## Task 2: HFP Guard Level 1 — registry-based profile disable

**Files:**
- Modify: `gui/OpenWinBlue/Services/DriverInstallerService.cs`
- Modify: `gui/OpenWinBlue/Services/IDriverInstaller.cs`
- Modify: `gui/OpenWinBlue/ViewModels/ControlsViewModel.cs`
- Modify: `gui/OpenWinBlue/Views/ControlsView.xaml`

HFP Level 1 disables the `Handsfree Telephony` Windows service for a Bluetooth device. When disabled, the device won't offer the HFP profile, preventing the A2DP→HFP quality downgrade. The user can re-enable it normally via Device Manager.

- [ ] **Step 2.1: Add `DisableHfpProfile` to `IDriverInstaller.cs`**

Read the current file. Add to the interface:

```csharp
/// <summary>
/// Disable the Hands-Free Telephony profile for all paired BT headsets.
/// Prevents automatic A2DP→HFP quality downgrade.
/// </summary>
void DisableHfpProfile();

/// <summary>Re-enable Hands-Free Telephony (undo DisableHfpProfile).</summary>
void EnableHfpProfile();
```

- [ ] **Step 2.2: Implement in `DriverInstallerService.cs`**

Read the current file. Add implementations:

```csharp
public void DisableHfpProfile()
{
    // Stop and disable the Bluetooth Hands-Free service.
    // This prevents Windows from switching headphones to HFP mono mode.
    try {
        using var sc = new ServiceController("BthHFSrv");
        if (sc.Status == ServiceControllerStatus.Running) {
            sc.Stop();
            sc.WaitForStatus(ServiceControllerStatus.Stopped, TimeSpan.FromSeconds(5));
        }
        // Disable via sc.exe (requires elevation handled by caller via runas)
        Process.Start(new ProcessStartInfo {
            FileName        = "sc.exe",
            Arguments       = "config BthHFSrv start= disabled",
            Verb            = "runas",
            UseShellExecute = true,
        });
    }
    catch (InvalidOperationException) { /* service not present */ }
}

public void EnableHfpProfile()
{
    try {
        Process.Start(new ProcessStartInfo {
            FileName        = "sc.exe",
            Arguments       = "config BthHFSrv start= demand",
            Verb            = "runas",
            UseShellExecute = true,
        });
        using var sc = new ServiceController("BthHFSrv");
        sc.Start();
    }
    catch (InvalidOperationException) { /* service not present */ }
}
```

- [ ] **Step 2.3: Add HFP Level 1 controls to `ControlsViewModel.cs`**

Read the current file. Add these properties and commands:

```csharp
    private readonly IDriverInstaller? _installer;

    // Add second constructor overload (design-time keeps no-arg):
    public ControlsViewModel(IIpcSender ipc, IDriverInstaller? installer = null)
    {
        _ipc       = ipc;
        _installer = installer;
    }

    [RelayCommand]
    private void DisableHfpLevel1()
    {
        _installer?.DisableHfpProfile();
    }

    [RelayCommand]
    private void EnableHfpLevel1()
    {
        _installer?.EnableHfpProfile();
    }
```

Update `MainViewModel.cs` to pass `DriverInstallerService` to ControlsViewModel:
```csharp
Controls = new ControlsViewModel(ipc, new DriverInstallerService());
```

- [ ] **Step 2.4: Add Level 1 buttons to `ControlsView.xaml`**

Read current file. Add after the HfpGuardNote TextBlock:

```xml
        <!-- HFP Level 1 — registry-based profile disable -->
        <StackPanel Orientation="Horizontal" Margin="0,8,0,0">
            <Button Content="Disable HFP Profile (Level 1)"
                    Command="{Binding DisableHfpLevel1Command}"
                    ToolTip="Stops BthHFSrv — prevents automatic HFP switching. Requires admin."
                    Padding="10,4" Margin="0,0,8,0"/>
            <Button Content="Re-enable HFP Profile"
                    Command="{Binding EnableHfpLevel1Command}"
                    ToolTip="Restores BthHFSrv to normal operation."
                    Padding="10,4"/>
        </StackPanel>
```

- [ ] **Step 2.5: Build and verify GUI tests pass**

```powershell
cd "c:/suru/open winblue/gui"
dotnet build OpenWinBlue/OpenWinBlue.csproj -c Debug
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: Build succeeded, all GUI tests pass.

- [ ] **Step 2.6: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/OpenWinBlue/Services/IDriverInstaller.cs gui/OpenWinBlue/Services/DriverInstallerService.cs
git add gui/OpenWinBlue/ViewModels/ControlsViewModel.cs gui/OpenWinBlue/ViewModels/MainViewModel.cs
git add gui/OpenWinBlue/Views/ControlsView.xaml
git commit -m "feat(gui): HFP Guard Level 1 — disable/enable BthHFSrv via ControlsView"
```

---

## Task 3: WiX installer with actual binaries

**Files:**
- Modify: `installer/Product.wxs`
- Modify: `installer/OpenWinBlue.wixproj`
- Modify: `.github/workflows/release.yml`

- [ ] **Step 3.1: Replace `installer/Product.wxs`**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">
  <Package Name="OpenWinBlue"
           Manufacturer="OpenWinBlue Project"
           Version="0.3.0"
           UpgradeCode="7c3d4e5f-6a7b-8c9d-0e1f-2a3b4c5d6e7f"
           Compressed="true">

    <MajorUpgrade DowngradeErrorMessage="A newer version of OpenWinBlue is already installed." />
    <MediaTemplate EmbedCab="true" />

    <!-- Installation directory -->
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="ProgramFiles64Folder">
        <Directory Id="INSTALLDIR" Name="OpenWinBlue">

          <!-- Main executables -->
          <Component Id="ServiceExe" Guid="A1B2C3D4-E5F6-7890-ABCD-EF1234567890">
            <File Id="owb_service" Name="owb-service.exe"
                  Source="$(var.ServiceBuildDir)\owb_service.exe"
                  KeyPath="yes"/>
          </Component>

          <Component Id="GuiExe" Guid="B2C3D4E5-F6A7-8901-BCDE-F12345678901">
            <File Id="OpenWinBlue" Name="OpenWinBlue.exe"
                  Source="$(var.GuiBuildDir)\OpenWinBlue.exe"
                  KeyPath="yes"/>
          </Component>

          <!-- Emergency rollback script -->
          <Component Id="RollbackScript" Guid="C3D4E5F6-A7B8-9012-CDEF-123456789012">
            <File Id="owb_rollback" Name="owb-rollback.bat"
                  Source="$(sys.SOURCEFILEDIR)..\tools\owb-rollback.bat"
                  KeyPath="yes"/>
          </Component>

        </Directory>
      </Directory>
    </Directory>

    <!-- Driver directory (System32\drivers) -->
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="SystemFolder">
        <Directory Id="DriversFolder" Name="drivers">
          <Component Id="DriverSys" Guid="D4E5F6A7-B8C9-0123-DEF0-234567890123">
            <File Id="owb_a2dp_sys" Name="owb_a2dp.sys"
                  Source="$(var.DriverBuildDir)\owb_a2dp.sys"
                  KeyPath="yes"/>
          </Component>
        </Directory>
      </Directory>
    </Directory>

    <Feature Id="ProductFeature" Title="OpenWinBlue" Level="1">
      <ComponentRef Id="ServiceExe"/>
      <ComponentRef Id="GuiExe"/>
      <ComponentRef Id="RollbackScript"/>
      <ComponentRef Id="DriverSys"/>
    </Feature>

    <!-- Install the driver using pnputil on install/uninstall -->
    <CustomAction Id="InstallDriver"
                  Execute="deferred"
                  Impersonate="no"
                  ExeCommand="pnputil.exe /add-driver &quot;[INSTALLDIR]owb_a2dp.inf&quot; /install"
                  FileKey="owb_a2dp_sys"/>
    <InstallExecuteSequence>
      <Custom Action="InstallDriver" After="InstallFiles">NOT Installed</Custom>
    </InstallExecuteSequence>

  </Package>
</Wix>
```

- [ ] **Step 3.2: Update `installer/OpenWinBlue.wixproj`**

Read the current file. Replace with:

```xml
<Project Sdk="WixToolset.Sdk/5.0.0">
  <PropertyGroup>
    <OutputName>OpenWinBlue-Setup</OutputName>
    <DefineConstants>
      ProductVersion=0.3.0;
      ServiceBuildDir=$(MSBuildThisFileDirectory)..\build\release\service\Release;
      GuiBuildDir=$(MSBuildThisFileDirectory)..\gui\OpenWinBlue\bin\Release\net10.0-windows;
      DriverBuildDir=$(MSBuildThisFileDirectory)..\driver\x64\Release
    </DefineConstants>
  </PropertyGroup>
</Project>
```

- [ ] **Step 3.3: Update `.github/workflows/release.yml`**

Read the current file. Update the installer build step:

```yaml
      - name: Build installer
        run: |
          dotnet build installer/OpenWinBlue.wixproj -c Release
        shell: pwsh

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            build/release/service/Release/owb_service.exe
            gui/OpenWinBlue/bin/Release/net10.0-windows/OpenWinBlue.exe
            installer/bin/Release/OpenWinBlue-Setup.msi
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

- [ ] **Step 3.4: Commit**

```powershell
cd "c:/suru/open winblue"
git add installer/Product.wxs installer/OpenWinBlue.wixproj .github/workflows/release.yml
git commit -m "feat(installer): WiX installer packages service + GUI + driver with pnputil registration"
```

---

## Task 4: Update CLAUDE.md with final environment

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 4.1: Update CLAUDE.md**

Read the current CLAUDE.md. Update the "Build Commands" section to reflect the real environment discovered during development:

```markdown
## Build Commands

### Full solution (all components)
```powershell
# Prerequisites: VS 18 2026, WDK 11, .NET 10 SDK, CMake 3.28+
# CMake: use nmake-debug preset locally (VS18) or windows-debug preset on CI (VS17 runner)
cmake --preset nmake-debug         # local build (NMake, VS18 cl.exe)
# cmake --preset windows-debug     # CI/VS17 build
dotnet build gui/OpenWinBlue.slnx  # builds GUI (.slnx format — .NET 10 SDK)
```

### Service only (local — NMake)
```powershell
cmake --preset nmake-debug
cmake --build build/nmake-debug --target owb_service
```

### GUI only
```powershell
dotnet build gui/OpenWinBlue.slnx -c Release
```

### Run all tests
```powershell
# C++ tests — from build dir
cd build/nmake-debug && ctest --output-on-failure

# C# tests
dotnet test gui/tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

### Notes on environment
- **cmake**: not on PATH. Location: `C:/Users/santi/AppData/Local/Android/Sdk/cmake/4.1.2/bin/cmake.exe`
  Add to PATH or use full path. VS17 2022 generator unavailable locally (VS18 installed).
  Use `nmake-debug` preset locally.
- **Visual Studio**: VS 18 2026 Community at `C:/Program Files/Microsoft Visual Studio/18/`
  MSVC v145 (cl.exe 19.50.35729)
- **WDK**: 10.0.26100 installed at `C:/Program Files (x86)/Windows Kits/10/`
  Kernel mode driver headers available. Use `C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/km/`
- **dotnet**: .NET 10 SDK installed. Solution file uses `.slnx` format (new in .NET 10 SDK).
- **CI (GitHub Actions)**: uses `windows-2022` runner (VS17, .NET 10). CI workflow uses
  `windows-debug` preset and `net10.0-windows` TFM. WDK installed via chocolatey.
```

Also update the Phase Plan table to show completed phases.

- [ ] **Step 4.2: Commit**

```powershell
cd "c:/suru/open winblue"
git add CLAUDE.md
git commit -m "docs(claude): update build env notes — WDK path, VS18 real locations, .slnx format"
```

---

## Task 5: Final push + CI verification

- [ ] **Step 5.1: Run all local tests**

```powershell
Set-Location "c:\suru\open winblue"
$env:PATH = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64;C:\Users\santi\AppData\Local\Android\Sdk\cmake\4.1.2\bin;$env:PATH"
$env:INCLUDE = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\include;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\ucrt;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared"
$env:LIB = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.50.35717\lib\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\x64"
& cmake --preset nmake-debug
& cmake --build build/nmake-debug --target owb_service_tests
Set-Location build/nmake-debug
& ctest --output-on-failure
```

Expected: All C++ tests pass.

```powershell
cd "c:/suru/open winblue/gui"
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected: All GUI tests pass.

- [ ] **Step 5.2: Final push**

```bash
SANTI_TOKEN=$(gh auth token --user santiquiroz)
git push "https://santiquiroz:${SANTI_TOKEN}@github.com/santiquiroz/openwinblue.git" main
```

- [ ] **Step 5.3: Poll CI**

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

Expected: `completed success` — all 3 jobs green.

---

## Self-Review

**Spec coverage:**
- ✅ LC3 codec wrapper (liblc3 Apache 2.0) — Task 1
- ✅ LC3 in CodecFactory + "LC3" in GUI codec selector — Task 1
- ✅ HFP Guard Level 1 (BthHFSrv service stop/disable) — Task 2
- ✅ Level 1 buttons in ControlsView — Task 2
- ✅ WiX installer with service + GUI + driver — Task 3
- ✅ CLAUDE.md updated with real paths — Task 4
- ✅ CI 3/3 green — Task 5

**Placeholder scan:** None.

**Type consistency:**
- `CodecLc3::encode` uses `LC3_PCM_FORMAT_S16` from lc3.h ✅
- `IDriverInstaller` interface extended with `DisableHfpProfile()/EnableHfpProfile()` ✅
- `ControlsViewModel(IIpcSender, IDriverInstaller?)` constructor used in MainViewModel ✅
