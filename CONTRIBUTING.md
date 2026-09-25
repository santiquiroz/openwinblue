# Contributing to OpenWinBlue

OpenWinBlue is experimental: the audio path is wired and host-tested, but it has not yet been
validated end-to-end with real Bluetooth devices. Hardware test reports are as welcome as code.

## Before opening a pull request

1. Build and run the tests that cover your change (see [README.md](README.md#build-from-source)):
   ```powershell
   # From a Visual Studio developer prompt (vcvars64.bat)
   cmake --preset nmake-debug
   cmake --build build/nmake-debug
   ctest --preset test-nmake-debug

   dotnet test gui/tests/OpenWinBlue.Tests/OpenWinBlue.Tests.csproj
   ```
   `MainViewModelTests` builds real services that call `pnputil` and the Bluetooth APIs.
2. Driver changes (`driver/`) must still build with MSBuild + WDK 11, as the `build-driver`
   job in `.github/workflows/ci.yml` does. Say in the PR whether you tested them on hardware
   (headphones, Bluetooth adapter, Windows build) or only built them.
3. Follow the coding conventions and commit format in [CLAUDE.md](CLAUDE.md):
   `<type>: <description>` (a scope such as `fix(service):` is common), one purpose per commit.
4. Update the README when a feature changes status; mark unfinished work as planned.

## Licensing

OpenWinBlue is GPLv3 ([LICENSE](LICENSE)). Contributions are accepted under the same license,
and new dependencies must be GPLv3-compatible — list them in
[third-party/LICENSES.md](third-party/LICENSES.md).

## Security

Report vulnerabilities privately through GitHub Security Advisories, not in public issues.
