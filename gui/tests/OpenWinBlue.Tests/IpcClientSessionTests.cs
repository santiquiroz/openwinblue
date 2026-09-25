using System.Collections.Concurrent;
using System.IO.Pipes;
using System.Text;
using OpenWinBlue.Models;
using OpenWinBlue.Services;

namespace OpenWinBlue.Tests;

public sealed class IpcClientSessionTests : IDisposable
{
    private static readonly TimeSpan WaitTimeout = TimeSpan.FromSeconds(10);

    private readonly string _pipeName = $"owb-test-{Guid.NewGuid():N}";
    private readonly CancellationTokenSource _cts = new();

    public void Dispose()
    {
        _cts.Cancel();
        _cts.Dispose();
    }

    [Fact]
    public async Task SendSetCodec_SharesThePollingSessionAndReturnsAck()
    {
        using var service = new FakeService(_pipeName, ack: 1);
        var serving = service.ServeSessionAsync(maxMessages: int.MaxValue, _cts.Token);
        using var client = StartClient();

        await WaitUntil(() => client.IsConnected && service.StatusRequests > 0);
        Assert.True(client.SendSetCodec("AAC", "switch", 1));
        Assert.True(client.SendSetCodec("SBC", "bitpool", 40));

        Assert.Equal(1, service.Connections);
        Assert.Equal(new[] { ("AAC", "switch", 1L), ("SBC", "bitpool", 40L) }, service.SetCodecRequests);
        client.Stop();
        await serving;
    }

    [Fact]
    public async Task SendSetCodec_ReturnsFalseWhenServiceRejects()
    {
        using var service = new FakeService(_pipeName, ack: 0);
        var serving = service.ServeSessionAsync(maxMessages: int.MaxValue, _cts.Token);
        using var client = StartClient();

        await WaitUntil(() => client.IsConnected);
        Assert.False(client.SendSetCodec("Foo", "switch", 1));
        client.Stop();
        await serving;
    }

    [Fact]
    public async Task Client_ReconnectsAfterServiceDropsTheSession()
    {
        using var service = new FakeService(_pipeName, ack: 1);
        using var client = StartClient();

        await service.ServeSessionAsync(maxMessages: 1, _cts.Token);
        var serving = service.ServeSessionAsync(maxMessages: int.MaxValue, _cts.Token);

        await WaitUntil(() => service.Connections == 2 && client.IsConnected);
        Assert.True(client.SendSetCodec("LDAC", "switch", 1));
        client.Stop();
        await serving;
    }

    [Fact]
    public void SendSetCodec_ReturnsFalseWithoutSession()
    {
        using var client = new IpcClientService(_pipeName);
        Assert.False(client.SendSetCodec("SBC", "switch", 1));
    }

    private IpcClientService StartClient()
    {
        var client = new IpcClientService(_pipeName, TimeSpan.FromMilliseconds(50));
        client.Start();
        return client;
    }

    private static async Task WaitUntil(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow + WaitTimeout;
        while (!condition()) {
            if (DateTime.UtcNow > deadline) throw new TimeoutException("Condition not met");
            await Task.Delay(20);
        }
    }

    private sealed class FakeService(string pipeName, byte ack) : IDisposable
    {
        private readonly NamedPipeServerStream _pipe = new(
            pipeName, PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
        private readonly ConcurrentQueue<(string, string, long)> _setCodec = new();
        private int _connections;
        private int _statusRequests;

        public int Connections    => Volatile.Read(ref _connections);
        public int StatusRequests => Volatile.Read(ref _statusRequests);
        public (string, string, long)[] SetCodecRequests => _setCodec.ToArray();

        public void Dispose() => _pipe.Dispose();

        public async Task ServeSessionAsync(int maxMessages, CancellationToken ct)
        {
            await _pipe.WaitForConnectionAsync(ct);
            Interlocked.Increment(ref _connections);
            for (int handled = 0; handled < maxMessages; handled++) {
                if (!await ServeOneMessage(ct)) break;
            }
            _pipe.Disconnect();
        }

        private async Task<bool> ServeOneMessage(CancellationToken ct)
        {
            var header = new byte[MsgHeader.Size];
            if (await _pipe.ReadAtLeastAsync(header, header.Length, false, ct) < header.Length)
                return false;
            var type    = (MsgType)BitConverter.ToUInt16(header, 0);
            var payload = new byte[BitConverter.ToUInt16(header, 2)];
            await _pipe.ReadExactlyAsync(payload, ct);

            if (type == MsgType.GetStatus) await ReplyStatus(ct);
            else if (type == MsgType.SetCodec) await ReplyAck(payload, ct);
            return true;
        }

        private async Task ReplyStatus(CancellationToken ct)
        {
            Interlocked.Increment(ref _statusRequests);
            await WriteMessage(MsgType.StatusReply, new byte[StatusPayload.Size], ct);
        }

        private async Task ReplyAck(byte[] payload, CancellationToken ct)
        {
            _setCodec.Enqueue((AsciiField(payload, 0), AsciiField(payload, 16),
                               BitConverter.ToInt64(payload, 32)));
            await WriteMessage(MsgType.CodecAck, new byte[] { ack, 0, 0, 0 }, ct);
        }

        private async Task WriteMessage(MsgType type, byte[] payload, CancellationToken ct)
        {
            var message = new byte[MsgHeader.Size + payload.Length];
            BitConverter.TryWriteBytes(message.AsSpan(0), (ushort)type);
            BitConverter.TryWriteBytes(message.AsSpan(2), (ushort)payload.Length);
            payload.CopyTo(message, MsgHeader.Size);
            await _pipe.WriteAsync(message, ct);
            await _pipe.FlushAsync(ct);
        }

        private static string AsciiField(byte[] payload, int offset)
        {
            var field = payload.AsSpan(offset, 16);
            var end   = field.IndexOf((byte)0);
            return Encoding.ASCII.GetString(end >= 0 ? field[..end] : field);
        }
    }
}
