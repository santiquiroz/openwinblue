using System.Diagnostics;

namespace OpenWinBlue.Tests;

// Runs tools\owb-rollback.bat only in --list-from mode, which reads a captured pnputil sample
// and never calls pnputil or deletes anything.
public class OwbRollbackScriptTests
{
    [Theory]
    [InlineData("pnputil-enum-drivers-en.txt", "oem42.inf")]
    [InlineData("pnputil-enum-drivers-es.txt", "oem108.inf")]
    public void ListFrom_PrintsOnlyThePublishedNameOfOwbA2dpInf(string fixture, string expected)
    {
        var (exitCode, output) = RunListFrom(Fixture.PathOf(fixture));

        Assert.Equal(0, exitCode);
        Assert.Equal([expected], PublishedNamesIn(output));
    }

    [Fact]
    public void ListFrom_WithoutOwbPackagePrintsNothing()
    {
        var (exitCode, output) = RunListFrom(Fixture.PathOf("pnputil-enum-devices-ids-en.txt"));

        Assert.Equal(0, exitCode);
        Assert.Empty(PublishedNamesIn(output));
    }

    private static string[] PublishedNamesIn(string output) =>
        output.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

    private static (int exitCode, string output) RunListFrom(string samplePath)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName               = "cmd.exe",
            Arguments              = $"/d /s /c \"\"{FindRollbackScript()}\" --list-from \"{samplePath}\"\"",
            RedirectStandardOutput = true,
            UseShellExecute        = false,
            CreateNoWindow         = true,
        };

        using var proc = Process.Start(startInfo)!;
        var output = proc.StandardOutput.ReadToEnd();
        Assert.True(proc.WaitForExit(30_000), "owb-rollback.bat did not finish");
        return (proc.ExitCode, output);
    }

    private static string FindRollbackScript()
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
        {
            var candidate = Path.Combine(dir.FullName, "tools", "owb-rollback.bat");
            if (File.Exists(candidate)) return candidate;
        }
        throw new FileNotFoundException("tools\\owb-rollback.bat not found above the test output directory");
    }
}
