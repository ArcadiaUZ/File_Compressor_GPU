using System.IO;
using System.Text.RegularExpressions;
using System.Windows;
using Microsoft.Win32;

namespace GcfGui;

public partial class MainWindow : Window
{
    private string _selectedFile = "";
    private readonly List<string> _logLines = new();
    private const int MAX_LOG = 60; // jurnal qisqa bo'ladi — ko'pi o'chib boradi

    public MainWindow()
    {
        InitializeComponent();
        WindowTheme.Apply(this);
        Drop += MainWindow_Drop;
        DragOver += (_, e) =>
        {
            e.Effects = e.Data.GetDataPresent(DataFormats.FileDrop)
                ? DragDropEffects.Copy : DragDropEffects.None;
            e.Handled = true;
        };
        Loaded += async (_, _) => await InitBackendAsync();
    }

    private void AddLog(string msg)
    {
        _logLines.Add(msg);
        while (_logLines.Count > MAX_LOG) _logLines.RemoveAt(0);
        LogText.Text = string.Join("\n", _logLines);
        Dispatcher.BeginInvoke(() => LogScroll.ScrollToBottom());
    }

    private void ClearLogButton_Click(object sender, RoutedEventArgs e)
    {
        _logLines.Clear();
        LogText.Text = "Jurnal tozalandi.";
    }

    // Backend ning uzun javobidan qisqa xulosa: "Siqildi: 110 MB → 54 MB (49%) • 0.8s • CUDA"
    private static string ShortSummary(string line)
    {
        var m = Regex.Match(line, @"(Siqildi|Ochildi|Arxivlandi|Papka arxivlandi):\s*(\d+)\s*->\s*(\d+)\s*B.*?([\d.]+)s?\s*(\[.*\])?",
            RegexOptions.IgnoreCase);
        if (!m.Success) return line;
        string Bytes(long b) =>
            b < 1024 ? $"{b} B" : b < 1048576 ? $"{b / 1024.0:0.0} KB" :
            b < 1073741824 ? $"{b / 1048576.0:0.0} MB" : $"{b / 1073741824.0:0.00} GB";
        long a = long.Parse(m.Groups[2].Value), b2 = long.Parse(m.Groups[3].Value);
        string pct = a > 0 ? $" ({(double)b2 / a * 100:0}%)" : "";
        string verb = m.Groups[1].Value.StartsWith("Ochildi") ? "Ochildi" : "Siqildi";
        return $"{verb}: {Bytes(a)} → {Bytes(b2)}{pct} • {m.Groups[4].Value}s {m.Groups[5].Value}";
    }

    private async Task InitBackendAsync()
    {
        try
        {
            await Backend.InitAsync();
            var (ok, outp) = await Backend.RunAsync("gpus");
            GpuBadge.Text = ok ? ParseGpuBadge(outp) : "⚠️ Backend ishlamadi";
            RefreshAssocButton();
            AddLog("GCF tayyor. Fayl tanlang yoki tashlang.");
            if (!string.IsNullOrEmpty(_selectedFile) &&
                _selectedFile.EndsWith(".gcf", StringComparison.OrdinalIgnoreCase))
                new ArchiveWindow(_selectedFile).Show();
        }
        catch (Exception ex) { GpuBadge.Text = "⚠️ Xato: " + ex.Message; }
    }

    private static string ParseGpuBadge(string output)
    {
        string name = "", cuda = "";
        foreach (var line in output.Split('\n'))
        {
            var t = line.Trim();
            if (t.StartsWith("GPU:")) name = t[4..].Trim();
            if (t.StartsWith("CUDA:")) cuda = t[5..].Trim();
        }
        if (string.IsNullOrEmpty(name)) name = "Noma'lum GPU";
        if (name.Length > 34) name = name[..34] + "…";
        return cuda.StartsWith("HA") ? $"🟢 {name} • CUDA" : $"🟡 {name} • CPU rejim";
    }

    private void SetWorking(bool working)
    {
        CompressButton.IsEnabled = DecompressButton.IsEnabled =
            PickButton.IsEnabled = PickFolderButton.IsEnabled =
            OpenArchButton.IsEnabled =
            AssocButton.IsEnabled = ClearLogButton.IsEnabled = !working;
        WorkProgress.Visibility = working ? Visibility.Visible : Visibility.Collapsed;
    }

    private void SelectFile(string path)
    {
        _selectedFile = path;
        FileLabel.Text = (Directory.Exists(path) ? "📁 " : "📄 ") + path;
        if (path.EndsWith(".gcf", StringComparison.OrdinalIgnoreCase) && !string.IsNullOrEmpty(Backend.ExePath))
            new ArchiveWindow(path).Show();
    }

    private void PickButton_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Title = "Fayl tanlang", CheckFileExists = true };
        if (dlg.ShowDialog() == true) SelectFile(dlg.FileName);
    }

    private void PickFolderButton_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFolderDialog { Title = "Siqiladigan papkani tanlang" };
        if (dlg.ShowDialog() == true) SelectFile(dlg.FolderName);
    }

    private void MainWindow_Drop(object sender, DragEventArgs e)
    {
        if (e.Data.GetData(DataFormats.FileDrop) is string[] files && files.Length > 0)
            SelectFile(files[0]);
    }

    private bool NeedFile()
    {
        if (string.IsNullOrEmpty(_selectedFile) ||
            (!File.Exists(_selectedFile) && !Directory.Exists(_selectedFile)))
        {
            AddLog("⚠️ Avval fayl yoki papka tanlang (tashlang yoki tugmani bosing).");
            return false;
        }
        return true;
    }

    private async void CompressButton_Click(object sender, RoutedEventArgs e)
    {
        if (!NeedFile()) return;
        bool isDir = Directory.Exists(_selectedFile);
        var dlg = new SaveFileDialog
        {
            Title = "Siqilgan faylni saqlash",
            Filter = "GCF fayl (*.gcf)|*.gcf",
            FileName = Path.GetFileName(_selectedFile.TrimEnd('/', '\\')) + ".gcf",
        };
        if (dlg.ShowDialog() != true) return;
        SetWorking(true);
        string cmd = isDir ? "compress-folder" : "compress";
        AddLog($"{(isDir ? "📁 Papka" : "⬇ Fayl")} siqilmoqda: {Path.GetFileName(_selectedFile)} ...");
        var (ok, output) = await Backend.RunAsync($"{cmd} {Backend.Quote(_selectedFile)} {Backend.Quote(dlg.FileName)}");
        SetWorking(false);
        AddLog(ok ? "✅ " + ShortSummary(Backend.LastLine(output))
                  : "❌ " + Backend.LastLine(output));
    }

    private async void DecompressButton_Click(object sender, RoutedEventArgs e)
    {
        if (!NeedFile()) return;
        var dlg = new SaveFileDialog
        {
            Title = "Ochilgan faylni saqlash",
            FileName = _selectedFile.EndsWith(".gcf", StringComparison.OrdinalIgnoreCase)
                ? Path.GetFileName(_selectedFile[..^4]) : Path.GetFileName(_selectedFile) + ".out",
        };
        if (dlg.ShowDialog() != true) return;
        SetWorking(true);
        AddLog($"⬆ Ochilmoqda: {Path.GetFileName(_selectedFile)} ...");
        var (ok, output) = await Backend.RunAsync($"decompress {Backend.Quote(_selectedFile)} {Backend.Quote(dlg.FileName)}");
        SetWorking(false);
        if (!ok && output.Contains("arxivda"))
        {
            AddLog("📂 Bu ko'p faylli arxiv — arxiv oynasida ochildi.");
            new ArchiveWindow(_selectedFile).Show();
            return;
        }
        AddLog(ok ? "✅ " + ShortSummary(Backend.LastLine(output))
                  : "❌ " + Backend.LastLine(output));
    }

    private void OpenArchButton_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Title = "GCF arxivni ochish",
            Filter = "GCF arxiv (*.gcf)|*.gcf",
            CheckFileExists = true,
        };
        if (dlg.ShowDialog() == true) new ArchiveWindow(dlg.FileName).Show();
    }

    private void RefreshAssocButton() =>
        AssocButton.Content = FileAssoc.IsRegistered() ? "✅ .gcf bog'langan" : "🔗 .gcf ni bog'lash";

    private void AssocButton_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            FileAssoc.Register();
            RefreshAssocButton();
            AddLog("✅ .gcf fayllar GCF ga bog'landi — ikki marta bossangiz arxiv oynada ochiladi.");
        }
        catch (Exception ex) { AddLog("❌ Bog'lanmadi: " + ex.Message); }
    }
}
