using System.IO;
using System.Windows;

namespace GcfGui;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        // .gcf ga double-click -> to'g'ri arxiv oynada ochiladi
        if (e.Args.Length > 0 && File.Exists(e.Args[0]) &&
            e.Args[0].EndsWith(".gcf", StringComparison.OrdinalIgnoreCase))
        {
            new ArchiveWindow(e.Args[0]).Show();
        }
        else
        {
            new MainWindow().Show();
        }
    }
}
