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
        while (!ct.IsCancellationRequested) {
            try {
                using var pipe = new NamedPipeClientStream(
                    ".", "openwinblue",
                    PipeDirection.InOut,
                    PipeOptions.None);

                await pipe.ConnectAsync(500, ct).ConfigureAwait(false);
                _connected = true;

                while (!ct.IsCancellationRequested) {
                    // Send GetStatus
                    IpcMessage.WriteHeader(pipe, MsgType.GetStatus);
                    await pipe.FlushAsync(ct).ConfigureAwait(false);

                    // Read response header
                    if (!IpcMessage.TryReadHeader(pipe, out var hdr)) break;

                    if (hdr.Type == MsgType.StatusReply &&
                        hdr.PayloadLen == StatusPayload.Size) {
                        var status = IpcMessage.ReadStatusPayload(pipe);
                        StatusReceived?.Invoke(status);
                    } else {
                        // Drain unknown payload
                        if (hdr.PayloadLen > 0) {
                            byte[] drain = new byte[hdr.PayloadLen];
                            pipe.ReadExactly(drain);
                        }
                    }

                    await Task.Delay(1000, ct).ConfigureAwait(false);
                }
            }
            catch (OperationCanceledException) { break; }
            catch { /* pipe unavailable — retry */ }
            finally { _connected = false; }

            if (!ct.IsCancellationRequested)
                await Task.Delay(2000, ct).ConfigureAwait(false);
        }
    }

    /// <summary>
    /// Send a SetCodec command (fire-and-forget). Returns false if not connected.
    /// </summary>
    public bool SendSetCodec(string codec, string paramKey, long paramValue)
    {
        if (!_connected) return false;
        try {
            using var pipe = new NamedPipeClientStream(
                ".", "openwinblue", PipeDirection.InOut, PipeOptions.None);
            pipe.Connect(500);
            var payload = SetCodecPayload.Create(codec, paramKey, paramValue);
            IpcMessage.WriteSetCodec(pipe, payload);
            pipe.Flush();

            // Read ack
            if (IpcMessage.TryReadHeader(pipe, out var hdr) &&
                hdr.Type == MsgType.CodecAck &&
                hdr.PayloadLen >= 1) {
                byte[] ack = new byte[hdr.PayloadLen];
                pipe.ReadExactly(ack);
                return ack[0] == 1;
            }
        } catch { }
        return false;
    }
}
