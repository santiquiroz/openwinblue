// gui/OpenWinBlue/Services/IpcClientService.cs
using System.IO.Pipes;
using OpenWinBlue.Models;

namespace OpenWinBlue.Services;

/// <summary>
/// Background service that connects to the OpenWinBlue named pipe,
/// polls GetStatus every second, and raises StatusReceived with the result.
/// Automatically reconnects if the pipe closes.
/// </summary>
public sealed class IpcClientService : IIpcSender, IDisposable
{
    public event Action<StatusPayload>? StatusReceived;

    public bool IsConnected => _connected;

    private volatile bool _connected;
    private volatile bool _running;
    private Task?         _task;
    private readonly CancellationTokenSource _cts = new();

    public void Start()
    {
        if (_running) return;
        _running = true;
        _task = Task.Run(() => RunLoop(_cts.Token));
    }

    public void Stop()
    {
        _running = false;
        _cts.Cancel();
        try { _task?.Wait(TimeSpan.FromSeconds(2)); } catch { }
    }

    public void Dispose() => Stop();

    private async Task RunLoop(CancellationToken ct)
    {
        int retryCount = 0;
        while (!ct.IsCancellationRequested) {
            try {
                using var pipe = new NamedPipeClientStream(
                    ".", "openwinblue",
                    PipeDirection.InOut,
                    PipeOptions.None);

                await pipe.ConnectAsync(500, ct).ConfigureAwait(false);
                _connected = true;
                retryCount = 0;
                OWBLogger.Info("Connected to owb-service pipe");

                while (!ct.IsCancellationRequested) {
                    // Send GetStatus
                    IpcMessage.WriteHeader(pipe, MsgType.GetStatus);
                    await pipe.FlushAsync(ct).ConfigureAwait(false);

                    // Read response header
                    if (!IpcMessage.TryReadHeader(pipe, out var hdr)) {
                        OWBLogger.Warn("Pipe read returned no header — disconnecting");
                        break;
                    }

                    if (hdr.Type == MsgType.StatusReply &&
                        hdr.PayloadLen == StatusPayload.Size) {
                        var status = IpcMessage.ReadStatusPayload(pipe);
                        StatusReceived?.Invoke(status);
                    } else {
                        OWBLogger.Warn($"Unexpected IPC message type=0x{(ushort)hdr.Type:X4} len={hdr.PayloadLen}");
                        if (hdr.PayloadLen > 0) {
                            byte[] drain = new byte[hdr.PayloadLen];
                            pipe.ReadExactly(drain);
                        }
                    }

                    await Task.Delay(1000, ct).ConfigureAwait(false);
                }
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex) {
                retryCount++;
                if (retryCount == 1 || retryCount % 10 == 0)
                    OWBLogger.Warn($"Pipe unavailable (attempt {retryCount}): {ex.Message}");
            }
            finally {
                if (_connected) {
                    _connected = false;
                    OWBLogger.Info("Disconnected from owb-service pipe");
                }
            }

            if (!ct.IsCancellationRequested)
                await Task.Delay(2000, ct).ConfigureAwait(false);
        }
        OWBLogger.Info("IPC loop stopped");
    }

    public bool SendSetCodec(string codec, string paramKey, long paramValue)
    {
        if (!_connected) {
            OWBLogger.Warn($"SendSetCodec ignored — not connected (codec={codec})");
            return false;
        }
        try {
            using var pipe = new NamedPipeClientStream(
                ".", "openwinblue", PipeDirection.InOut, PipeOptions.None);
            pipe.Connect(500);
            var payload = SetCodecPayload.Create(codec, paramKey, paramValue);
            IpcMessage.WriteSetCodec(pipe, payload);
            pipe.Flush();

            if (IpcMessage.TryReadHeader(pipe, out var hdr) &&
                hdr.Type == MsgType.CodecAck &&
                hdr.PayloadLen >= 1) {
                byte[] ack = new byte[hdr.PayloadLen];
                pipe.ReadExactly(ack);
                bool ok = ack[0] == 1;
                OWBLogger.Info($"SetCodec {codec}/{paramKey}={paramValue} → ack={ok}");
                return ok;
            }
        } catch (Exception ex) {
            OWBLogger.Error(ex, $"SendSetCodec {codec}");
        }
        return false;
    }
}
