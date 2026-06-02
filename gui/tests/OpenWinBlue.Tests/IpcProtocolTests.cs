using OpenWinBlue.Models;
using System.Runtime.InteropServices;

namespace OpenWinBlue.Tests;

public class IpcProtocolTests
{
    [Fact]
    public void MsgHeader_SizeIs4Bytes()
        => Assert.Equal(4, MsgHeader.Size);

    [Fact]
    public void StatusPayload_SizeIs24Bytes()
        => Assert.Equal(24, StatusPayload.Size);

    [Fact]
    public void SetCodecPayload_SizeIs40Bytes()
        => Assert.Equal(40, SetCodecPayload.Size);

    [Fact]
    public void StatusPayload_CodecName_TrimsNullBytes()
    {
        var p = new StatusPayload {
            CodecNameBytes = new byte[16],
            Bitrate        = 328000,
            IsCapturing    = 1,
            HfpGuardOn     = 0,
            Pad            = 0,
        };
        System.Text.Encoding.ASCII.GetBytes("SBC", p.CodecNameBytes);
        Assert.Equal("SBC", p.CodecName);
    }

    [Fact]
    public void SetCodecPayload_Create_SetsAllFields()
    {
        var p = SetCodecPayload.Create("SBC", "bitpool", 53);
        Assert.Equal(53L, p.ParamValue);
        Assert.Equal("SBC",     System.Text.Encoding.ASCII
                                       .GetString(p.CodecNameBytes).TrimEnd('\0'));
        Assert.Equal("bitpool", System.Text.Encoding.ASCII
                                       .GetString(p.ParamKeyBytes).TrimEnd('\0'));
    }

    [Fact]
    public void WriteAndReadHeader_RoundTrips()
    {
        using var ms = new MemoryStream();
        IpcMessage.WriteHeader(ms, MsgType.GetStatus);
        ms.Position = 0;
        Assert.True(IpcMessage.TryReadHeader(ms, out var hdr));
        Assert.Equal(MsgType.GetStatus, hdr.Type);
        Assert.Equal((ushort)0, hdr.PayloadLen);
    }
}
