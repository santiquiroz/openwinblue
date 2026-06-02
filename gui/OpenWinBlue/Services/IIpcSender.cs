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
