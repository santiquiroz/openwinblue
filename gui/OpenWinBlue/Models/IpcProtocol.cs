// gui/OpenWinBlue/Models/IpcProtocol.cs
// Binary IPC protocol — C# port of service/src/ipc_protocol.h.
// All structs are packed (1-byte alignment) to match the C++ layout.
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace OpenWinBlue.Models;

public enum MsgType : ushort {
    Ping        = 0x0001,
    Pong        = 0x0002,
    GetStatus   = 0x0010,
    StatusReply = 0x0011,
    SetCodec    = 0x0020,
    CodecAck    = 0x0021,
    Error       = 0x00FF,
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct MsgHeader {
    public MsgType  Type;        // 2 bytes
    public ushort   PayloadLen;  // 2 bytes
    public const int Size = 4;
}

/// <summary>Payload for StatusReply (24 bytes).</summary>
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct StatusPayload {
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
    public byte[] CodecNameBytes;   // 16 bytes, null-terminated ASCII

    public uint   Bitrate;          // 4 bytes — effective bits/sec
    public byte   IsCapturing;      // 1 byte — 0 or 1
    public byte   HfpGuardOn;       // 1 byte — 0 or 1
    public ushort Pad;              // 2 bytes

    public const int Size = 24;

    public string CodecName =>
        Encoding.ASCII.GetString(CodecNameBytes ?? Array.Empty<byte>())
                       .TrimEnd('\0');
}

/// <summary>Payload for SetCodec (40 bytes).</summary>
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct SetCodecPayload {
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
    public byte[] CodecNameBytes;   // 16 bytes

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
    public byte[] ParamKeyBytes;    // 16 bytes

    public long ParamValue;         // 8 bytes

    public const int Size = 40;

    public static SetCodecPayload Create(string codec, string key, long value) {
        var p = new SetCodecPayload {
            CodecNameBytes = new byte[16],
            ParamKeyBytes  = new byte[16],
            ParamValue     = value,
        };
        Encoding.ASCII.GetBytes(codec, 0, Math.Min(codec.Length, 15), p.CodecNameBytes, 0);
        Encoding.ASCII.GetBytes(key,   0, Math.Min(key.Length,   15), p.ParamKeyBytes,  0);
        return p;
    }
}

/// <summary>Reads/writes binary protocol messages over any stream.</summary>
public static class IpcMessage {
    public const string PipeName = @"\\.\pipe\openwinblue";

    /// <summary>Write a header-only message (Ping, GetStatus).</summary>
    public static void WriteHeader(Stream s, MsgType type, ushort payloadLen = 0) {
        Span<byte> buf = stackalloc byte[MsgHeader.Size];
        BitConverter.TryWriteBytes(buf,       (ushort)type);
        BitConverter.TryWriteBytes(buf[2..],  payloadLen);
        s.Write(buf);
    }

    /// <summary>Read the 4-byte header. Returns false if stream closed.</summary>
    public static bool TryReadHeader(Stream s, out MsgHeader hdr) {
        hdr = default;
        Span<byte> buf = stackalloc byte[MsgHeader.Size];
        int read = s.ReadAtLeast(buf, MsgHeader.Size, false);
        if (read < MsgHeader.Size) return false;
        hdr = new MsgHeader {
            Type       = (MsgType)BitConverter.ToUInt16(buf),
            PayloadLen = BitConverter.ToUInt16(buf[2..]),
        };
        return true;
    }

    /// <summary>Read StatusPayload after a StatusReply header.</summary>
    public static StatusPayload ReadStatusPayload(Stream s) {
        byte[] buf = new byte[StatusPayload.Size];
        s.ReadExactly(buf);
        var h = GCHandle.Alloc(buf, GCHandleType.Pinned);
        try {
            return Marshal.PtrToStructure<StatusPayload>(h.AddrOfPinnedObject());
        } finally {
            h.Free();
        }
    }

    /// <summary>Write a SetCodec message.</summary>
    public static void WriteSetCodec(Stream s, SetCodecPayload payload) {
        WriteHeader(s, MsgType.SetCodec, SetCodecPayload.Size);
        byte[] buf = new byte[SetCodecPayload.Size];
        var h = GCHandle.Alloc(buf, GCHandleType.Pinned);
        try {
            Marshal.StructureToPtr(payload, h.AddrOfPinnedObject(), false);
        } finally {
            h.Free();
        }
        s.Write(buf);
    }
}
