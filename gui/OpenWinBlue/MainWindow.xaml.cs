using System.Windows;

namespace OpenWinBlue;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        if (DataContext is ViewModels.MainViewModel vm)
            vm.StartService();
    }

    private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
    {
        if (DataContext is ViewModels.MainViewModel vm)
            vm.StopService();
    }
}
