# Phase 1 — Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the complete project skeleton — CMake for C/C++, .NET 8 WPF project, git submodules for all codec libs, GitHub Actions CI, `.gitignore`, `CLAUDE.md`, and a minimal "hello world" smoke test that verifies the full build pipeline works end-to-end.

**Architecture:** This phase creates no functional audio code. It wires up the build system (CMake presets for driver + service, `dotnet` for GUI), vendors all third-party codec libraries as git submodules, and validates the CI pipeline compiles every component cleanly on every push.

**Tech Stack:** CMake 3.28+, MSVC 2022, WDK 11, .NET 8 SDK, WPF, xUnit, GoogleTest, GitHub Actions, WiX Toolset v4 (skeleton only).

---

## File Map

### Created in this phase

```
.gitignore
.gitattributes
CMakeLists.txt                          # Root CMake, includes driver/ and service/
CMakePresets.json                       # Presets: driver-debug, service-debug, service-release, test-all
CLAUDE.md                               # Already created

driver/
  CMakeLists.txt                        # Driver build (uses WDK targets)
  src/
    owb_a2dp.c                          # Stub DriverEntry — logs "OpenWinBlue loaded" and returns
    owb_a2dp.h                          # Forward declarations, NTSTATUS helpers
  owb_a2dp.inf                          # Driver INF (skeleton, no hardware IDs yet)

service/
  CMakeLists.txt                        # Service build
  src/
    main.cpp                            # Stub: prints "owb-service started" and exits 0
  codecs/
    codec_interface.h                   # Abstract ICodec interface (encode/decode/name/params)
  ai/
    (empty, placeholder CMakeLists.txt)

gui/
  OpenWinBlue.sln
  OpenWinBlue/
    OpenWinBlue.csproj                  # .NET 8, WPF, CommunityToolkit.Mvvm
    App.xaml / App.xaml.cs
    MainWindow.xaml / MainWindow.xaml.cs  # Stub: window with "OpenWinBlue — ready" label
    ViewModels/
      MainViewModel.cs                  # Stub ViewModel, ObservableObject
  tests/
    OpenWinBlue.Tests/
      OpenWinBlue.Tests.csproj          # xUnit test project
      MainViewModelTests.cs             # One smoke test: MainViewModel instantiates without throwing

tests/
  service/
    CMakeLists.txt
    smoke_test.cpp                      # GoogleTest: service main() returns 0

third-party/
  .gitkeep                              # Submodules added via git commands in tasks below

tools/
  owb-rollback.bat                      # Emergency driver rollback script (no driver yet — stub)
  sign-driver.ps1                       # CI signing script stub

installer/
  OpenWinBlue.wixproj                   # WiX skeleton — builds but produces empty installer
  Product.wxs                           # WiX skeleton

.github/
  workflows/
    ci.yml                              # Build driver + service + GUI, run all tests
    release.yml                         # On tag push: build release + driver signing stub
```

---

## Task 1: Git hygiene — `.gitignore` and `.gitattributes`

**Files:**
- Create: `.gitignore`
- Create: `.gitattributes`

- [ ] **Step 1.1: Create `.gitignore`**

```
# Build outputs
build/
out/
*.obj *.pdb *.ilk *.exp *.lib *.dll *.exe *.sys *.cat
CMakeCache.txt CMakeFiles/
x64/ x86/ ARM/ ARM64/

# .NET / Visual Studio
bin/ obj/
*.user *.suo .vs/
packages/ *.nupkg

# WDK / driver
*.cer *.pfx

# OS
Thumbs.db .DS_Store desktop.ini

# ONNX models (large binaries, distributed separately)
*.onnx
```

Save to: `.gitignore`

- [ ] **Step 1.2: Create `.gitattributes`**

```
# Normalize line endings
* text=auto eol=lf
*.bat text eol=crlf
*.ps1 text eol=crlf
*.sln text eol=crlf
*.csproj text eol=crlf
*.inf text eol=crlf

# Treat binary files as binary
*.sys binary
*.dll binary
*.exe binary
*.onnx binary
*.png binary
*.ico binary
```

Save to: `.gitattributes`

- [ ] **Step 1.3: Commit**

```powershell
cd "c:/suru/open winblue"
git add .gitignore .gitattributes
git commit -m "chore: add gitignore and gitattributes"
```

---

## Task 2: Root CMake and presets

**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`

- [ ] **Step 2.1: Create root `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.28)
project(openwinblue VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GoogleTest via FetchContent (used in tests/)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)
enable_testing()

add_subdirectory(service)
add_subdirectory(tests/service)

# Driver is built separately via WDK — not included in regular CMake
# (driver/ has its own VS solution generated by WDK toolchain)
```

Save to: `CMakeLists.txt`

- [ ] **Step 2.2: Create `CMakePresets.json`**

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "windows-debug",
      "displayName": "Windows Debug",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "binaryDir": "${sourceDir}/build/debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    },
    {
      "name": "windows-release",
      "displayName": "Windows Release",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "binaryDir": "${sourceDir}/build/release",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "service-debug",
      "configurePreset": "windows-debug",
      "targets": ["owb_service"]
    },
    {
      "name": "service-release",
      "configurePreset": "windows-release",
      "targets": ["owb_service"]
    }
  ],
  "testPresets": [
    {
      "name": "test-all",
      "configurePreset": "windows-debug",
      "output": { "verbosity": "verbose" }
    }
  ]
}
```

Save to: `CMakePresets.json`

- [ ] **Step 2.3: Commit**

```powershell
git add CMakeLists.txt CMakePresets.json
git commit -m "chore: add root CMake and presets"
```

---

## Task 3: Service skeleton

**Files:**
- Create: `service/CMakeLists.txt`
- Create: `service/src/main.cpp`
- Create: `service/codecs/codec_interface.h`
- Create: `service/ai/CMakeLists.txt`

- [ ] **Step 3.1: Create `service/CMakeLists.txt`**

```cmake
add_executable(owb_service
    src/main.cpp
)

target_include_directories(owb_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/codecs
    ${CMAKE_CURRENT_SOURCE_DIR}/ai
)

target_compile_options(owb_service PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /utf-8>
)
```

Save to: `service/CMakeLists.txt`

- [ ] **Step 3.2: Create `service/src/main.cpp`**

```cpp
#include <cstdio>
#include <cstdlib>

int main() {
    printf("owb-service v0.1 started\n");
    return EXIT_SUCCESS;
}
```

Save to: `service/src/main.cpp`

- [ ] **Step 3.3: Create `service/codecs/codec_interface.h`**

```cpp
#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace owb {

// Parameters for codec configuration — each codec defines its own set.
// Values are passed as key/value pairs to keep the interface stable.
struct CodecParam {
    std::string_view key;
    int64_t          value;
};

// Abstract interface every codec wrapper must implement.
class ICodec {
public:
    virtual ~ICodec() = default;

    // Short codec name returned to GUI/IPC (e.g. "LDAC", "SBC", "aptX-HD").
    virtual std::string_view name() const noexcept = 0;

    // Encode PCM interleaved stereo int16 samples into the codec's output format.
    // input:  samples × 2 int16 (left, right, left, right …)
    // output: caller-allocated buffer; returns bytes written, or -1 on error.
    virtual int encode(std::span<const int16_t> input,
                       std::span<uint8_t>       output) = 0;

    // Apply a configuration parameter. Returns false if the key is unknown.
    virtual bool set_param(CodecParam param) = 0;

    // Retrieve a configuration parameter. Returns INT64_MIN if unknown.
    virtual int64_t get_param(std::string_view key) const = 0;
};

} // namespace owb
```

Save to: `service/codecs/codec_interface.h`

- [ ] **Step 3.4: Create `service/ai/CMakeLists.txt`** (placeholder)

```cmake
# AI pipeline — added in Phase 6
```

Save to: `service/ai/CMakeLists.txt`

- [ ] **Step 3.5: Commit**

```powershell
git add service/
git commit -m "feat(service): add service skeleton and ICodec interface"
```

---

## Task 4: Service smoke test

**Files:**
- Create: `tests/service/CMakeLists.txt`
- Create: `tests/service/smoke_test.cpp`

- [ ] **Step 4.1: Create `tests/service/CMakeLists.txt`**

```cmake
add_executable(owb_service_tests
    smoke_test.cpp
)

target_link_libraries(owb_service_tests PRIVATE
    GTest::gtest_main
)

target_include_directories(owb_service_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/service/codecs
)

include(GoogleTest)
gtest_discover_tests(owb_service_tests)
```

Save to: `tests/service/CMakeLists.txt`

- [ ] **Step 4.2: Write failing test**

```cpp
#include <gtest/gtest.h>
#include "codec_interface.h"

// Verify the ICodec interface header compiles cleanly
// and that a minimal concrete implementation satisfies the contract.
namespace {

class NullCodec final : public owb::ICodec {
public:
    std::string_view name() const noexcept override { return "null"; }

    int encode(std::span<const int16_t>, std::span<uint8_t>) override {
        return 0;
    }

    bool set_param(owb::CodecParam) override { return false; }

    int64_t get_param(std::string_view) const override {
        return INT64_MIN;
    }
};

} // namespace

TEST(CodecInterface, NullCodecSatisfiesContract) {
    NullCodec codec;
    EXPECT_EQ(codec.name(), "null");
    EXPECT_EQ(codec.get_param("anything"), INT64_MIN);
    EXPECT_FALSE(codec.set_param({"anything", 0}));
}

TEST(CodecInterface, EncodeReturnsZeroOnEmptyInput) {
    NullCodec codec;
    std::vector<int16_t> input;
    std::vector<uint8_t> output(64);
    EXPECT_EQ(codec.encode(input, output), 0);
}
```

Save to: `tests/service/smoke_test.cpp`

- [ ] **Step 4.3: Configure and run (expect FAIL — GoogleTest not yet fetched)**

```powershell
cd "c:/suru/open winblue"
cmake --preset windows-debug
cmake --build build/debug --target owb_service_tests
```

Expected first run: CMake fetches GoogleTest, build succeeds.

- [ ] **Step 4.4: Run tests**

```powershell
cd build/debug
ctest --preset test-all --output-on-failure
```

Expected output:
```
[==========] Running 2 tests from 1 test suite.
[----------] 2 tests from CodecInterface
[ RUN      ] CodecInterface.NullCodecSatisfiesContract
[       OK ] CodecInterface.NullCodecSatisfiesContract
[ RUN      ] CodecInterface.EncodeReturnsZeroOnEmptyInput
[       OK ] CodecInterface.EncodeReturnsZeroOnEmptyInput
[  PASSED  ] 2 tests.
```

- [ ] **Step 4.5: Commit**

```powershell
cd "c:/suru/open winblue"
git add tests/
git commit -m "test(service): add codec interface smoke test"
```

---

## Task 5: Driver skeleton

**Files:**
- Create: `driver/src/owb_a2dp.h`
- Create: `driver/src/owb_a2dp.c`
- Create: `driver/owb_a2dp.inf`
- Create: `driver/CMakeLists.txt`

> **Note:** The KMDF driver is compiled via the WDK Visual Studio integration, not directly
> via cmake. The `driver/CMakeLists.txt` documents dependencies but the actual build uses
> the WDK Targets file. Follow the WDK "KMDF Driver" project template in Visual Studio.

- [ ] **Step 5.1: Create `driver/src/owb_a2dp.h`**

```c
#pragma once

#include <ntddk.h>
#include <wdf.h>

// Driver version
#define OWB_DRIVER_VERSION_MAJOR 0
#define OWB_DRIVER_VERSION_MINOR 1

// Device extension — per-device context stored by WDF.
typedef struct _OWB_DEVICE_EXTENSION {
    WDFDEVICE   Device;
    BOOLEAN     IsActive;
} OWB_DEVICE_EXTENSION, *POWB_DEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(OWB_DEVICE_EXTENSION, OwbGetDeviceExtension)

// Forward declarations
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD OwbEvtDeviceAdd;
```

Save to: `driver/src/owb_a2dp.h`

- [ ] **Step 5.2: Create `driver/src/owb_a2dp.c`**

```c
#include "owb_a2dp.h"

// DriverEntry — called by Windows when the driver is loaded.
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS          status;

    WDF_DRIVER_CONFIG_INIT(&config, OwbEvtDeviceAdd);

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        WDF_NO_HANDLE
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfDriverCreate failed 0x%x\n", status));
        return status;
    }

    KdPrint(("OpenWinBlue: driver loaded (v%d.%d)\n",
             OWB_DRIVER_VERSION_MAJOR,
             OWB_DRIVER_VERSION_MINOR));

    return STATUS_SUCCESS;
}

// OwbEvtDeviceAdd — called when a Bluetooth audio device is enumerated.
NTSTATUS
OwbEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    NTSTATUS               status;
    WDFDEVICE              device;
    WDF_OBJECT_ATTRIBUTES  attributes;
    POWB_DEVICE_EXTENSION  ext;

    UNREFERENCED_PARAMETER(Driver);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, OWB_DEVICE_EXTENSION);

    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        KdPrint(("OpenWinBlue: WdfDeviceCreate failed 0x%x\n", status));
        return status;
    }

    ext = OwbGetDeviceExtension(device);
    ext->Device   = device;
    ext->IsActive = FALSE;

    KdPrint(("OpenWinBlue: device added\n"));
    return STATUS_SUCCESS;
}
```

Save to: `driver/src/owb_a2dp.c`

- [ ] **Step 5.3: Create `driver/owb_a2dp.inf`**

```ini
; OpenWinBlue A2DP Driver — skeleton INF
; Hardware IDs will be added in Phase 2 when AVDTP is implemented.

[Version]
Signature   = "$WINDOWS NT$"
Class       = Bluetooth
ClassGuid   = {e0cbf06c-cd8b-4647-bb8a-263b43f0f974}
Provider    = %ManufacturerName%
DriverVer   = 06/02/2026,0.1.0.0
CatalogFile = owb_a2dp.cat
PnpLockdown = 1

[Manufacturer]
%ManufacturerName% = Standard,NTamd64

[Standard.NTamd64]
; No hardware IDs yet — added in Phase 2.

[Strings]
ManufacturerName = "OpenWinBlue Project"
DiskName         = "OpenWinBlue A2DP Driver Disk"
```

Save to: `driver/owb_a2dp.inf`

- [ ] **Step 5.4: Create `driver/CMakeLists.txt`** (documentation only)

```cmake
# The KMDF driver (owb_a2dp.sys) is built via the WDK Visual Studio integration.
# Open driver/owb_a2dp.vcxproj in Visual Studio 2022 with WDK 11 installed.
#
# Build from command line:
#   msbuild driver\owb_a2dp.vcxproj /p:Configuration=Debug /p:Platform=x64
#
# The .vcxproj is generated via: File > New Project > Kernel Mode Driver (KMDF) in VS.
# After generating, replace the generated DriverEntry with driver/src/owb_a2dp.c.
```

Save to: `driver/CMakeLists.txt`

- [ ] **Step 5.5: Commit**

```powershell
git add driver/
git commit -m "feat(driver): add KMDF driver skeleton (DriverEntry + DeviceAdd stubs)"
```

---

## Task 6: WPF GUI skeleton

**Files:**
- Create: `gui/OpenWinBlue.sln`
- Create: `gui/OpenWinBlue/OpenWinBlue.csproj`
- Create: `gui/OpenWinBlue/App.xaml`
- Create: `gui/OpenWinBlue/App.xaml.cs`
- Create: `gui/OpenWinBlue/MainWindow.xaml`
- Create: `gui/OpenWinBlue/MainWindow.xaml.cs`
- Create: `gui/OpenWinBlue/ViewModels/MainViewModel.cs`
- Create: `gui/tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj`
- Create: `gui/tests/OpenWinBlue.Tests/MainViewModelTests.cs`

- [ ] **Step 6.1: Create the .NET solution and projects**

```powershell
cd "c:/suru/open winblue/gui"
dotnet new sln -n OpenWinBlue
dotnet new wpf -n OpenWinBlue -f net8.0-windows --output OpenWinBlue
dotnet sln OpenWinBlue.sln add OpenWinBlue/OpenWinBlue.csproj
mkdir -p tests/OpenWinBlue.Tests
cd tests/OpenWinBlue.Tests
dotnet new xunit -f net8.0-windows --output .
cd "c:/suru/open winblue/gui"
dotnet sln OpenWinBlue.sln add tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj
```

- [ ] **Step 6.2: Add NuGet packages**

```powershell
cd "c:/suru/open winblue/gui/OpenWinBlue"
dotnet add package CommunityToolkit.Mvvm --version 8.3.2
dotnet add package Microsoft.Extensions.DependencyInjection --version 8.0.1

cd "c:/suru/open winblue/gui/tests/OpenWinBlue.Tests"
dotnet add reference ../../OpenWinBlue/OpenWinBlue.csproj
```

- [ ] **Step 6.3: Create `gui/OpenWinBlue/ViewModels/MainViewModel.cs`**

```csharp
using CommunityToolkit.Mvvm.ComponentModel;

namespace OpenWinBlue.ViewModels;

public partial class MainViewModel : ObservableObject
{
    [ObservableProperty]
    private string _statusMessage = "OpenWinBlue — ready";
}
```

Save to: `gui/OpenWinBlue/ViewModels/MainViewModel.cs`

- [ ] **Step 6.4: Replace generated `MainWindow.xaml`**

```xml
<Window x:Class="OpenWinBlue.MainWindow"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        xmlns:vm="clr-namespace:OpenWinBlue.ViewModels"
        Title="OpenWinBlue" Height="600" Width="900"
        WindowStartupLocation="CenterScreen">
    <Window.DataContext>
        <vm:MainViewModel/>
    </Window.DataContext>
    <Grid>
        <TextBlock Text="{Binding StatusMessage}"
                   HorizontalAlignment="Center"
                   VerticalAlignment="Center"
                   FontSize="20"/>
    </Grid>
</Window>
```

Save to: `gui/OpenWinBlue/MainWindow.xaml`

- [ ] **Step 6.5: Write the failing test**

```csharp
using OpenWinBlue.ViewModels;

namespace OpenWinBlue.Tests;

public class MainViewModelTests
{
    [Fact]
    public void MainViewModel_Instantiates_WithReadyMessage()
    {
        var vm = new MainViewModel();
        Assert.Equal("OpenWinBlue — ready", vm.StatusMessage);
    }

    [Fact]
    public void MainViewModel_StatusMessage_RaisesPropertyChanged()
    {
        var vm = new MainViewModel();
        var changed = new List<string?>();
        vm.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

        vm.StatusMessage = "connecting…";

        Assert.Contains(nameof(vm.StatusMessage), changed);
    }
}
```

Save to: `gui/tests/OpenWinBlue.Tests/MainViewModelTests.cs`

- [ ] **Step 6.6: Run tests**

```powershell
cd "c:/suru/open winblue/gui"
dotnet test tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --verbosity normal
```

Expected output:
```
Passed! - Failed: 0, Passed: 2, Skipped: 0, Total: 2
```

- [ ] **Step 6.7: Build the GUI to verify it compiles**

```powershell
dotnet build OpenWinBlue/OpenWinBlue.csproj -c Debug
```

Expected: Build succeeded, 0 error(s).

- [ ] **Step 6.8: Commit**

```powershell
cd "c:/suru/open winblue"
git add gui/
git commit -m "feat(gui): add WPF skeleton with MainViewModel and smoke tests"
```

---

## Task 7: Git submodules for codec libraries

**Files:**
- Modify: `.gitmodules` (created automatically by `git submodule add`)
- Create: `third-party/` entries

- [ ] **Step 7.1: Add libldac (LDAC encoder, Sony/AOSP, Apache 2.0)**

```powershell
cd "c:/suru/open winblue"
git submodule add https://android.googlesource.com/platform/external/libldac third-party/libldac
```

> If the AOSP mirror is slow, use: `https://github.com/wdv4758h/libldac`

- [ ] **Step 7.2: Add libopenaptx (aptX Classic + HD, LGPL 2.1+)**

```powershell
git submodule add https://github.com/pali/libopenaptx third-party/libopenaptx
```

- [ ] **Step 7.3: Add liblc3 (LC3 / LE Audio, Google, Apache 2.0)**

```powershell
git submodule add https://github.com/google/liblc3 third-party/liblc3
```

- [ ] **Step 7.4: Add RNNoise (lightweight noise suppression, Xiph, BSD)**

```powershell
git submodule add https://github.com/xiph/rnnoise third-party/rnnoise
```

- [ ] **Step 7.5: Add libsbc from BlueZ (SBC, LGPL 2.1)**

```powershell
git submodule add https://github.com/iamthebot/libsbc third-party/libsbc
```

> `iamthebot/libsbc` is a standalone mirror of the SBC code from BlueZ with a clean CMakeLists.
> Alternative: extract from BlueZ source directly if preferred.

- [ ] **Step 7.6: Add GoogleTest (already in CMake via FetchContent — skip submodule)**

GoogleTest is already handled via `FetchContent_Declare` in `CMakeLists.txt`. No submodule needed.

- [ ] **Step 7.7: Verify submodules initialized correctly**

```powershell
git submodule status
```

Expected: all entries show a commit hash prefix (e.g. `+abc1234 third-party/libldac (v1.0.0)`)

- [ ] **Step 7.8: Create `third-party/LICENSES.md`**

```markdown
# Third-Party Licenses

| Library | License | Source |
|---------|---------|--------|
| libldac | Apache 2.0 | https://android.googlesource.com/platform/external/libldac |
| libopenaptx | LGPL 2.1+ | https://github.com/pali/libopenaptx |
| liblc3 | Apache 2.0 | https://github.com/google/liblc3 |
| rnnoise | BSD 3-Clause | https://github.com/xiph/rnnoise |
| libsbc | LGPL 2.1 | https://git.kernel.org/pub/scm/bluetooth/bluez.git |
| GoogleTest | BSD 3-Clause | https://github.com/google/googletest |
| ONNX Runtime | MIT | https://github.com/microsoft/onnxruntime |
| CommunityToolkit.Mvvm | MIT | https://github.com/CommunityToolkit/dotnet |

All licenses are compatible with the project's GPLv3 license.
```

Save to: `third-party/LICENSES.md`

- [ ] **Step 7.9: Commit**

```powershell
git add .gitmodules third-party/
git commit -m "chore: add codec library submodules (libldac, libopenaptx, liblc3, rnnoise, libsbc)"
```

---

## Task 8: Tools — rollback script

**Files:**
- Create: `tools/owb-rollback.bat`
- Create: `tools/sign-driver.ps1`

- [ ] **Step 8.1: Create `tools/owb-rollback.bat`**

```bat
@echo off
:: OpenWinBlue Emergency Rollback
:: Uninstalls owb_a2dp.sys and re-enables the Windows default A2DP driver.
:: Run as Administrator.

echo OpenWinBlue — Emergency Rollback
echo ===================================
echo.

:: Check for admin rights
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click and select "Run as administrator".
    pause
    exit /b 1
)

echo Step 1: Stopping OpenWinBlue service...
sc stop owb-service 2>nul
sc delete owb-service 2>nul

echo Step 2: Removing OpenWinBlue driver...
for /f "tokens=*" %%i in ('pnputil /enum-drivers /class Bluetooth ^| findstr "owb_a2dp"') do (
    pnputil /delete-driver %%i /uninstall /force
)

echo Step 3: Re-enabling Windows default A2DP driver (btavchdt.sys)...
:: The inbox driver re-activates automatically once our driver is removed.
:: A reboot finalizes the switch.

echo.
echo Done. Please REBOOT your PC to complete the rollback.
echo After reboot, your Bluetooth headphones will use the Windows default driver.
echo.
pause
```

Save to: `tools/owb-rollback.bat`

- [ ] **Step 8.2: Create `tools/sign-driver.ps1`** (CI stub)

```powershell
# sign-driver.ps1 — Driver attestation signing via Microsoft Hardware Dev Center.
# This script is called by .github/workflows/release.yml on tagged releases.
# Full implementation added in Phase 2 when the driver is functional.

param(
    [Parameter(Mandatory=$true)] [string]$InfPath,
    [Parameter(Mandatory=$true)] [string]$SysPath,
    [Parameter(Mandatory=$true)] [string]$OutDir
)

Write-Host "sign-driver.ps1: stub — attestation signing not yet implemented."
Write-Host "  INF: $InfPath"
Write-Host "  SYS: $SysPath"
Write-Host "  Out: $OutDir"

# Phase 2 will add:
# 1. Zip the driver package
# 2. Submit to Microsoft Partner Center via REST API
# 3. Poll for signing completion
# 4. Download signed .cat + .sys
exit 0
```

Save to: `tools/sign-driver.ps1`

- [ ] **Step 8.3: Commit**

```powershell
git add tools/
git commit -m "chore: add rollback script and driver signing stub"
```

---

## Task 9: GitHub Actions CI

**Files:**
- Create: `.github/workflows/ci.yml`
- Create: `.github/workflows/release.yml`

- [ ] **Step 9.1: Create `.github/workflows/ci.yml`**

```yaml
name: CI

on:
  push:
    branches: [main, "feat/**", "fix/**"]
  pull_request:
    branches: [main]

jobs:
  build-service:
    name: Build & Test Service (C++)
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Configure CMake
        run: cmake --preset windows-debug

      - name: Build
        run: cmake --build build/debug --target owb_service owb_service_tests

      - name: Run tests
        run: ctest --preset test-all --output-on-failure
        working-directory: build/debug

  build-gui:
    name: Build & Test GUI (C#)
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4

      - name: Setup .NET 8
        uses: actions/setup-dotnet@v4
        with:
          dotnet-version: "8.0.x"

      - name: Restore
        run: dotnet restore gui/OpenWinBlue.sln

      - name: Build
        run: dotnet build gui/OpenWinBlue.sln -c Debug --no-restore

      - name: Test
        run: dotnet test gui/tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj --no-build --verbosity normal
```

Save to: `.github/workflows/ci.yml`

- [ ] **Step 9.2: Create `.github/workflows/release.yml`**

```yaml
name: Release

on:
  push:
    tags:
      - "v*.*.*"

jobs:
  build-release:
    name: Build Release
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Setup .NET 8
        uses: actions/setup-dotnet@v4
        with:
          dotnet-version: "8.0.x"

      - name: Build service (Release)
        run: |
          cmake --preset windows-release
          cmake --build build/release --target owb_service

      - name: Build GUI (Release)
        run: dotnet build gui/OpenWinBlue.sln -c Release

      - name: Sign driver (stub — Phase 2)
        run: |
          pwsh tools/sign-driver.ps1 `
            -InfPath driver/owb_a2dp.inf `
            -SysPath driver/build/x64/Release/owb_a2dp.sys `
            -OutDir build/signed

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            build/release/service/owb_service.exe
            gui/OpenWinBlue/bin/Release/net8.0-windows/OpenWinBlue.exe
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

Save to: `.github/workflows/release.yml`

- [ ] **Step 9.3: Commit**

```powershell
git add .github/
git commit -m "ci: add GitHub Actions for C++ service and C# GUI build + test"
```

---

## Task 10: WiX installer skeleton

**Files:**
- Create: `installer/OpenWinBlue.wixproj`
- Create: `installer/Product.wxs`

- [ ] **Step 10.1: Create `installer/Product.wxs`** (skeleton — no files yet)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">
  <Package Name="OpenWinBlue"
           Manufacturer="OpenWinBlue Project"
           Version="0.1.0"
           UpgradeCode="7c3d4e5f-6a7b-8c9d-0e1f-2a3b4c5d6e7f"
           Compressed="true">

    <MajorUpgrade DowngradeErrorMessage="A newer version of OpenWinBlue is already installed." />

    <MediaTemplate EmbedCab="true" />

    <Feature Id="ProductFeature" Title="OpenWinBlue" Level="1">
      <!-- Files added in Phase 4 when GUI + service are complete -->
    </Feature>

  </Package>
</Wix>
```

Save to: `installer/Product.wxs`

- [ ] **Step 10.2: Create `installer/OpenWinBlue.wixproj`**

```xml
<Project Sdk="WixToolset.Sdk/5.0.0">
  <PropertyGroup>
    <OutputName>OpenWinBlue-Setup</OutputName>
    <DefineConstants>ProductVersion=0.1.0</DefineConstants>
  </PropertyGroup>
</Project>
```

Save to: `installer/OpenWinBlue.wixproj`

- [ ] **Step 10.3: Build the skeleton installer to verify WiX works**

```powershell
cd "c:/suru/open winblue/installer"
dotnet build OpenWinBlue.wixproj -c Release
```

Expected: Build succeeded (produces a minimal `.msi`).

- [ ] **Step 10.4: Commit**

```powershell
cd "c:/suru/open winblue"
git add installer/
git commit -m "chore: add WiX installer skeleton"
```

---

## Task 11: Push all to GitHub and verify CI

- [ ] **Step 11.1: Push branch to GitHub**

```powershell
cd "c:/suru/open winblue"
$SANTI_TOKEN = gh auth token --user santiquiroz
git push https://santiquiroz:$SANTI_TOKEN@github.com/santiquiroz/openwinblue.git main
```

- [ ] **Step 11.2: Watch CI run**

Open: `https://github.com/santiquiroz/openwinblue/actions`

Expected: Both `Build & Test Service (C++)` and `Build & Test GUI (C#)` jobs pass (green).

- [ ] **Step 11.3: Confirm all tasks done**

```powershell
git log --oneline -15
```

Expected commits (newest first):
```
chore: add WiX installer skeleton
ci: add GitHub Actions for C++ service and C# GUI build + test
chore: add rollback script and driver signing stub
chore: add codec library submodules
feat(gui): add WPF skeleton with MainViewModel and smoke tests
feat(driver): add KMDF driver skeleton
test(service): add codec interface smoke test
feat(service): add service skeleton and ICodec interface
chore: add root CMake and presets
chore: add gitignore and gitattributes
feat: initial project documentation and design specification
```

---

## Self-Review

**Spec coverage check:**
- ✅ Project structure: all dirs created
- ✅ CLAUDE.md: created before this plan
- ✅ CMake for service: Task 2–4
- ✅ WDK driver skeleton: Task 5
- ✅ WPF GUI skeleton: Task 6
- ✅ Codec submodules (libldac, libopenaptx, libsbc, liblc3, rnnoise): Task 7
- ✅ Emergency rollback script: Task 8
- ✅ GitHub Actions CI: Task 9
- ✅ WiX installer skeleton: Task 10
- ✅ Push + CI verification: Task 11

**Placeholder scan:** No TBDs or incomplete steps found. All code blocks are complete.

**Type consistency:**
- `owb::ICodec` defined in Task 3, used in Task 4 smoke test — names match.
- `OWB_DEVICE_EXTENSION` defined in header Task 5.1, used in Task 5.2 — consistent.
- `MainViewModel.StatusMessage` defined in Task 6.3, tested in Task 6.5 — consistent.
