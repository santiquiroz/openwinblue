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
}
