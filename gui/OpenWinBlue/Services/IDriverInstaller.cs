namespace OpenWinBlue.Services;

public interface IDriverInstaller
{
    bool IsInstalled { get; }
    void Install(string infPath);
    void Rollback();

    /// <summary>Stop and disable BthHFSrv — prevents automatic A2DP→HFP downgrade.</summary>
    void DisableHfpProfile();

    /// <summary>Re-enable BthHFSrv — restores Hands-Free Telephony to normal operation.</summary>
    void EnableHfpProfile();
}
