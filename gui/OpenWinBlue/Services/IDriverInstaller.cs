namespace OpenWinBlue.Services;

public interface IDriverInstaller
{
    bool IsInstalled { get; }
    void Install(string infPath);
    void Rollback();
}
