using NSubstitute;
using OpenWinBlue.Services;
using OpenWinBlue.ViewModels;

namespace OpenWinBlue.Tests;

public class CodecViewModelTests
{
    [Fact]
    public void CodecViewModel_DefaultBitpool_Is53()
    {
        var ipc = Substitute.For<IIpcSender>();
        var vm = new CodecViewModel(ipc);
        Assert.Equal(53, vm.Bitpool);
    }

    [Fact]
    public void CodecViewModel_ApplyCommand_SendsSetCodecBitpool()
    {
        var ipc = Substitute.For<IIpcSender>();
        ipc.IsConnected.Returns(true);
        var vm = new CodecViewModel(ipc) { Bitpool = 40 };
        vm.ApplyCodecCommand.Execute(null);
        ipc.Received().SendSetCodec("SBC", "bitpool", 40);
    }

    [Fact]
    public void CodecViewModel_ApplyCommand_DisabledWhenNotConnected()
    {
        var ipc = Substitute.For<IIpcSender>();
        ipc.IsConnected.Returns(false);
        var vm = new CodecViewModel(ipc);
        Assert.False(vm.ApplyCodecCommand.CanExecute(null));
    }

    [Fact]
    public void CodecViewModel_DefaultCodec_IsSBC()
    {
        var ipc = Substitute.For<IIpcSender>();
        var vm = new CodecViewModel(ipc);
        Assert.Equal(0, vm.SelectedCodecIndex);
        Assert.Equal("SBC", vm.AvailableCodecs[vm.SelectedCodecIndex]);
    }

    [Fact]
    public void CodecViewModel_ChannelModes_IndexMatchesSbcModeConstants()
    {
        var vm = new CodecViewModel(Substitute.For<IIpcSender>());
        Assert.Equal(0, Array.IndexOf(vm.ChannelModes, "Mono"));
        Assert.Equal(1, Array.IndexOf(vm.ChannelModes, "Dual Channel"));
        Assert.Equal(2, Array.IndexOf(vm.ChannelModes, "Stereo"));
        Assert.Equal(3, Array.IndexOf(vm.ChannelModes, "Joint Stereo"));
    }

    [Fact]
    public void CodecViewModel_ApplyCommand_SendsSbcStereoModeValue()
    {
        var ipc = ConnectedSender(accepts: true);
        var vm = new CodecViewModel(ipc);
        vm.ChannelModeIndex = Array.IndexOf(vm.ChannelModes, "Stereo");
        vm.ApplyCodecCommand.Execute(null);
        ipc.Received().SendSetCodec("SBC", "mode", 2);
    }

    [Fact]
    public void CodecViewModel_ApplyCommand_NonSbcCodecSkipsSbcParams()
    {
        var ipc = ConnectedSender(accepts: true);
        var vm = new CodecViewModel(ipc);
        vm.SelectedCodecIndex = Array.IndexOf(vm.AvailableCodecs, "LDAC");
        vm.ApplyCodecCommand.Execute(null);
        ipc.Received().SendSetCodec("LDAC", "switch", 1);
        ipc.DidNotReceive().SendSetCodec("LDAC", "bitpool", Arg.Any<long>());
        ipc.DidNotReceive().SendSetCodec("LDAC", "mode", Arg.Any<long>());
    }

    [Fact]
    public void CodecViewModel_ApplyCommand_ReportsSuccessWhenEveryRequestIsAcked()
    {
        var vm = new CodecViewModel(ConnectedSender(accepts: true));
        vm.ApplyCodecCommand.Execute(null);
        Assert.Equal("SBC applied.", vm.ApplyStatus);
    }

    [Fact]
    public void CodecViewModel_ApplyCommand_ReportsRejectedParams()
    {
        var ipc = ConnectedSender(accepts: true);
        ipc.SendSetCodec("SBC", "freq", Arg.Any<long>()).Returns(false);
        var vm = new CodecViewModel(ipc);
        vm.ApplyCodecCommand.Execute(null);
        Assert.Equal("Service rejected: freq.", vm.ApplyStatus);
    }

    [Fact]
    public void CodecViewModel_ApplyCommand_DisabledAboveA2dpMaxBitpool()
    {
        var vm = new CodecViewModel(ConnectedSender(accepts: true)) { Bitpool = 54 };
        Assert.False(vm.ApplyCodecCommand.CanExecute(null));
    }

    private static IIpcSender ConnectedSender(bool accepts)
    {
        var ipc = Substitute.For<IIpcSender>();
        ipc.IsConnected.Returns(true);
        ipc.SendSetCodec(Arg.Any<string>(), Arg.Any<string>(), Arg.Any<long>()).Returns(accepts);
        return ipc;
    }
}
