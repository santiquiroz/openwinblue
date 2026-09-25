namespace OpenWinBlue.Services;

public static class OwbDriverStore
{
    public const string InfName = "owb_a2dp.inf";

    public static IReadOnlyList<string> FindPublishedNames() =>
        PnputilParser.ParsePublishedNames(PnputilRunner.Run("/enum-drivers").Output, InfName);

    public static PnputilResult Add(string infPath) =>
        PnputilRunner.Run("/add-driver", infPath, "/install");

    // pnputil /delete-driver only accepts the published name (oemNN.inf), never owb_a2dp.inf.
    public static IReadOnlyList<PnputilResult> Remove() =>
        FindPublishedNames().Select(Delete).ToList();

    private static PnputilResult Delete(string publishedName)
    {
        var result = PnputilRunner.Run("/delete-driver", publishedName, "/uninstall", "/force");
        OWBLogger.Info($"pnputil /delete-driver {publishedName} → exit={result.ExitCode}\n{result.Output.Trim()}");
        return result;
    }
}
