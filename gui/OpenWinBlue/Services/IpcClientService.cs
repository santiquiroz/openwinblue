// gui/OpenWinBlue/Services/IpcClientService.cs
using System.IO;
using System.IO.Pipes;
using OpenWinBlue.Models;

namespace OpenWinBlue.Services;

/// <summary>
/// Background service that keeps one persistent session with the OpenWinBlue
/// named pipe, polls GetStatus every second, and raises StatusReceived with the
/// result. SetCodec requests share the same session. Reconnects if it closes.
/// </summary>
public sealed class IpcClientService : IIpcSender, IDisposable
{
    public const string DefaultPipeName = "openwinblue";

    private const int ConnectTimeoutMs = 500;
    private static readonly TimeSpan PollInterval       = TimeSpan.FromSeconds(1);
    private static readonly TimeSpan DefaultRetryDelay  = TimeSpan.FromSeconds(2);
    private static readonly TimeSpan SendLockTimeout    = TimeSpan.FromSeconds(2);

    public event Action<StatusPayload>? StatusReceived;

    public bool IsConnected => _session is not null;

    private readonly string   _pipeName;
    private readonly TimeSpan _retryDelay;
    // One request/response exchange at a time on the shared session.
    private readonly Lock     _io = new();
    private volatile Stream?  _session;
    private volatile bool     _running;
    private Task?             _task;
    private readonly CancellationTokenSource _cts = new();

    public IpcClientService(string pipeName = DefaultPipeName, TimeSpan? retryDelay = null)
    {
        _pipeName   = pipeName;
        _retryDelay = retryDelay ?? DefaultRetryDelay;
    }

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
                await RunSession(ct).ConfigureAwait(false);
                retryCount = 0;
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex) {
                retryCount++;
                if (retryCount == 1 || retryCount % 10 == 0)
                    OWBLogger.Warn($"Pipe unavailable (attempt {retryCount}): {ex.Message}");
            }

            try { await Task.Delay(_retryDelay, ct).ConfigureAwait(false); }
            catch (OperationCanceledException) { break; }
        }
        OWBLogger.Info("IPC loop stopped");
    }

    private async Task RunSession(CancellationToken ct)
    {
        using var pipe = new NamedPipeClientStream(
            ".", _pipeName, PipeDirection.InOut, PipeOptions.None);
        await pipe.ConnectAsync(ConnectTimeoutMs, ct).ConfigureAwait(false);

        Attach(pipe);
        try {
            await PollUntilClosed(pipe, ct).ConfigureAwait(false);
        } finally {
            Detach();
        }
    }

    private void Attach(Stream pipe)
    {
        lock (_io) { _session = pipe; }
        OWBLogger.Info("Connected to owb-service pipe");
    }

    private void Detach()
    {
        lock (_io) { _session = null; }
        OWBLogger.Info("Disconnected from owb-service pipe");
    }

    private async Task PollUntilClosed(Stream pipe, CancellationToken ct)
    {
        while (!ct.IsCancellationRequested) {
            var status = RequestStatus(pipe);
            if (status is null) return;
            StatusReceived?.Invoke(status.Value);
            await Task.Delay(PollInterval, ct).ConfigureAwait(false);
        }
    }

    private StatusPayload? RequestStatus(Stream pipe)
    {
        lock (_io) {
            try {
                return ExchangeGetStatus(pipe);
            } catch (IOException ex) {
                OWBLogger.Warn($"Pipe closed during GetStatus: {ex.Message}");
                return null;
            }
        }
    }

    private static StatusPayload? ExchangeGetStatus(Stream pipe)
    {
        IpcMessage.WriteHeader(pipe, MsgType.GetStatus);
        pipe.Flush();

        if (!IpcMessage.TryReadHeader(pipe, out var hdr)) {
            OWBLogger.Warn("Pipe read returned no header — disconnecting");
            return null;
        }
        if (hdr.Type != MsgType.StatusReply || hdr.PayloadLen != StatusPayload.Size) {
            OWBLogger.Warn($"Unexpected IPC message type=0x{(ushort)hdr.Type:X4} len={hdr.PayloadLen} — disconnecting");
            return null;
        }
        return IpcMessage.ReadStatusPayload(pipe);
    }

    public bool SendSetCodec(string codec, string paramKey, long paramValue)
    {
        if (!_io.TryEnter(SendLockTimeout)) {
            OWBLogger.Warn($"SendSetCodec timed out waiting for the session (codec={codec})");
            return false;
        }
        try {
            return SendOnSession(codec, paramKey, paramValue);
        } finally {
            _io.Exit();
        }
    }

    private bool SendOnSession(string codec, string paramKey, long paramValue)
    {
        var session = _session;
        if (session is null) {
            OWBLogger.Warn($"SendSetCodec ignored — not connected (codec={codec})");
            return false;
        }
        try {
            bool ok = ExchangeSetCodec(session, codec, paramKey, paramValue);
            OWBLogger.Info($"SetCodec {codec}/{paramKey}={paramValue} → ack={ok}");
            return ok;
        } catch (Exception ex) {
            OWBLogger.Error(ex, $"SendSetCodec {codec}");
            return false;
        }
    }

    private static bool ExchangeSetCodec(Stream pipe, string codec, string paramKey, long paramValue)
    {
        IpcMessage.WriteSetCodec(pipe, SetCodecPayload.Create(codec, paramKey, paramValue));
        pipe.Flush();

        if (!IpcMessage.TryReadHeader(pipe, out var hdr) ||
            hdr.Type != MsgType.CodecAck || hdr.PayloadLen < 1)
            return false;

        byte[] ack = new byte[hdr.PayloadLen];
        pipe.ReadExactly(ack);
        return ack[0] == 1;
    }
}
