namespace OpenWinBlue.Tests;

internal static class Fixture
{
    public static string PathOf(string name) =>
        Path.Combine(AppContext.BaseDirectory, "Fixtures", name);

    public static string Read(string name) => File.ReadAllText(PathOf(name));
}
