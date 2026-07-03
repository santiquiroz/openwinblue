using OpenWinBlue.ViewModels;

namespace OpenWinBlue.Tests;

public class MainViewModelTests
{
    [Fact]
    public void MainViewModel_Instantiates_WithDefaultState()
    {
        var vm = new MainViewModel();
        Assert.NotNull(vm.Status);
        Assert.NotNull(vm.Codec);
        Assert.NotNull(vm.Driver);
        Assert.NotNull(vm.Controls);
        Assert.NotNull(vm.Devices);
    }

    [Fact]
    public void StatusViewModel_Update_SetsCodecAndBitrate()
    {
        var vm = new StatusViewModel();
        var payload = new OpenWinBlue.Models.StatusPayload {
            CodecNameBytes = new byte[16],
            Bitrate        = 328000,
            IsCapturing    = 1,
            HfpGuardOn     = 0,
            Pad            = 0,
        };
        System.Text.Encoding.ASCII.GetBytes("SBC", payload.CodecNameBytes);
        vm.Update(payload);
        Assert.Equal("SBC",      vm.ActiveCodec);
        Assert.Equal("328 kbps", vm.BitrateText);
        Assert.True(vm.IsStreaming);
    }

    [Fact]
    public void StatusViewModel_SetConnected_UpdatesConnectionState()
    {
        var vm = new StatusViewModel();
        vm.SetConnected(true);
        Assert.Equal("Connected", vm.ConnectionState);
        vm.SetConnected(false);
        Assert.Equal("Disconnected", vm.ConnectionState);
    }
}
