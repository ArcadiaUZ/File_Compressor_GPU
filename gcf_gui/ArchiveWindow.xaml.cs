using Microsoft.Win32;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Input;

namespace GcfGui;

public class ArchEntry
{
    public int Index { get; set; }
    public string Name { get; set; } = "";
    public long Orig { get; set; }
    public long Packed { get; set; }
    public string Method { get; set; } = "huff"; // huff | store | lz
    public string SizeText => FormatBytes(Orig);
    public string PackedText => FormatBytes(Packed);
    public string TypeName => FileTypes.TypeOf(Name) + (Method == "lz" ? " •LZ" : Method == "store" ? " •RAW" : "");
    public string Icon => FileTypes.IconOf(Name);
    public static string FormatBytes(long b) =>
        b < 1024 ? $"{b} B" : b < 1048576 ? $"{b / 1024.0:0.0} KB" :
        b < 1073741824 ? $"{b / 1048576.0:0.0} MB" : $"{b / 1073741824.0:0.00} GB";
}

// Kengaytmadan tur nomi + belgi (WinRAR dagi "Type" ustuni kabi)
static class FileTypes
{
    public static string TypeOf(string name)
    {
        // Eski v1 arxivlardagi nomsiz yozuv
        if (name == "file") return "Fayl (eski arxiv)";
        string ext = Path.GetExtension(name).ToLowerInvariant();
        return ext switch
        {
            ".txt" or ".log" or ".md" => "Matn hujjati",
            ".jpg" or ".jpeg" or ".png" or ".gif" or ".bmp" or ".webp" or ".svg" => "Rasm",
            ".mp3" or ".wav" or ".flac" or ".ogg" => "Audio",
            ".mp4" or ".avi" or ".mkv" or ".mov" => "Video",
            ".zip" or ".rar" or ".7z" or ".gcf" or ".gz" => "Arxiv",
            ".exe" or ".msi" => "Dastur",
            ".pdf" => "PDF hujjat",
            ".doc" or ".docx" => "Word hujjat",
            ".xls" or ".xlsx" => "Excel jadval",
            ".json" or ".xml" or ".yml" or ".yaml" => "Ma'lumot fayli",
            ".dll" or ".sys" => "Tizim fayli",
            ".py" or ".cs" or ".cpp" or ".c" or ".h" or ".js" or ".html" => "Kod fayli",
            "" => "Fayl",
            _ => ext.TrimStart('.').ToUpperInvariant() + " fayl",
        };
    }

    public static string IconOf(string name)
    {
        string ext = Path.GetExtension(name).ToLowerInvariant();
        return ext switch
        {
            ".jpg" or ".jpeg" or ".png" or ".gif" or ".bmp" or ".webp" or ".svg" => "🖼",
            ".mp3" or ".wav" or ".flac" or ".ogg" => "🎵",
            ".mp4" or ".avi" or ".mkv" or ".mov" => "🎬",
            ".zip" or ".rar" or ".7z" or ".gcf" or ".gz" => "📦",
            ".exe" or ".msi" => "⚙",
            ".pdf" => "📕",
            ".doc" or ".docx" => "📝",
            ".xls" or ".xlsx" => "📊",
            ".py" or ".cs" or ".cpp" or ".c" or ".h" or ".js" or ".html" => "💻",
            _ => "📄",
        };
    }
}

public partial class ArchiveWindow : Window
{
    private readonly string _archPath;
    private readonly ObservableCollection<ArchEntry> _entries = new();

    public ArchiveWindow(string archPath)
    {
        _archPath = archPath;
        InitializeComponent();
        WindowTheme.Apply(this);
        Title = $"{Path.GetFileName(archPath)} — GCF Arxiv";
        FileList.ItemsSource = _entries;
        FileList.MouseDoubleClick += (_, _) => OpenSelectedAsync();
        PreviewKeyDown += (_, e) =>
        {
            if (e.Key == Key.C && (Keyboard.Modifiers & ModifierKeys.Control) != 0) CopyNames();
            if (e.Key == Key.A && (Keyboard.Modifiers & ModifierKeys.Control) != 0) FileList.SelectAll();
        };
        Loaded += async (_, _) => await InitAsync();
    }

    private async Task InitAsync()
    {
        try { await Backend.InitAsync(); }
        catch (Exception ex) { StatusText.Text = "⚠️ " + ex.Message; return; }
        await ReloadAsync();
    }

    private async Task ReloadAsync()
    {
        var (ok, output) = await Backend.RunAsync($"list {Backend.Quote(_archPath)}");
        _entries.Clear();
        if (!ok) { StatusText.Text = "❌ " + Backend.LastLine(output); return; }
        long totalOrig = 0, totalPacked = 0;
        foreach (var line in output.Split('\n'))
        {
            var t = line.Trim();
            if (t.Length == 0) continue;
            // Format: idx|nom|orig|packed|[method]. Nom ichida '|' bo'lishi mumkin —
            // shuning uchun boshidan idx, oxiridan orig/packed/method ajratamiz.
            var p = t.Split('|');
            if (p.Length < 4 || !int.TryParse(p[0], out int i)) continue;
            string method = "huff";
            string last = p[^1].Trim();
            int numStart = p.Length - 3; // [.. name .., orig, packed] yoki [.., orig, packed, method]
            if (last == "huff" || last == "store" || last == "lz")
            {
                method = last;
                numStart = p.Length - 4;
            }
            if (numStart < 1) continue;
            if (!long.TryParse(p[numStart], out long o) || !long.TryParse(p[numStart + 1], out long c)) continue;
            if (o < 0 || c < 0) continue;
            string name = string.Join("|", p, 1, numStart - 1);
            if (string.IsNullOrEmpty(name)) continue;
            _entries.Add(new ArchEntry { Index = i, Name = name, Orig = o, Packed = c, Method = method });
            totalOrig += o; totalPacked += c;
        }
        string fn = Path.GetFileName(_archPath);
        ArchPath.Text = $"📦 {_archPath} — GCF arxivi, ochilgan hajm {ArchEntry.FormatBytes(totalOrig)}";
        ArchSummary.Text = $"{_entries.Count} ta fayl • {ArchEntry.FormatBytes(totalOrig)} → " +
                           $"{ArchEntry.FormatBytes(totalPacked)}" +
                           (totalOrig > 0 ? $" ({(double)totalPacked / totalOrig * 100:0}%)" : "");
        StatusText.Text = $"Jami: {_entries.Count} ta fayl, {ArchEntry.FormatBytes(totalOrig)} • " +
                          "Ochish: ikki marta bosing yoki Ko'rish • Nusxa: Ctrl+C";
    }

    private List<int> SelectedIndices() =>
        FileList.SelectedItems.Cast<ArchEntry>().Select(e => e.Index).ToList();

    private void SetBusy(bool b) => StatusText.Text = b ? "⏳ Ishlanmoqda..." : StatusText.Text;

    // 📤 Chiqarish — tanlanganlar (belgilanmagan bo'lsa hammasi) papkaga
    private async void ExtractButton_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFolderDialog { Title = "Qayerga chiqarilsin?" };
        if (dlg.ShowDialog() != true) return;
        SetBusy(true);
        var sel = SelectedIndices();
        string idx = sel.Count > 0 ? " " + string.Join(" ", sel) : "";
        var (ok, output) = await Backend.RunAsync($"extract {Backend.Quote(_archPath)} {Backend.Quote(dlg.FolderName)}{idx}");
        SetBusy(false);
        StatusText.Text = ok ? "✅ " + Backend.LastLine(output) + $" → {dlg.FolderName}"
                             : "❌ " + Backend.LastLine(output);
    }

    // ➕ Qo'shish — fayllar arxivga
    private async void AddButton_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog { Title = "Arxivga qo'shish", Multiselect = true };
        if (dlg.ShowDialog() != true) return;
        SetBusy(true);
        string files = string.Join(" ", dlg.FileNames.Select(f => Backend.Quote(f)));
        var (ok, output) = await Backend.RunAsync($"add {Backend.Quote(_archPath)} {files}");
        SetBusy(false);
        StatusText.Text = ok ? "✅ " + Backend.LastLine(output) : "❌ " + Backend.LastLine(output);
        if (ok) await ReloadAsync();
    }

    // ✅ Tekshirish — butunlik testi
    private async void TestButton_Click(object sender, RoutedEventArgs e)
    {
        SetBusy(true);
        var (ok, output) = await Backend.RunAsync($"test {Backend.Quote(_archPath)}");
        SetBusy(false);
        StatusText.Text = ok ? "✅ " + Backend.LastLine(output) : "❌ " + Backend.LastLine(output);
    }

    // 👁 Ko'rish — tanlangan faylni ochish
    private void ViewButton_Click(object sender, RoutedEventArgs e) => OpenSelectedAsync();

    // 🗑 O'chirish — tanlangan yozuvlar
    private async void DeleteButton_Click(object sender, RoutedEventArgs e)
    {
        var sel = SelectedIndices();
        if (sel.Count == 0) { StatusText.Text = "⚠️ Avval ro'yxatdan fayl tanlang."; return; }
        if (MessageBox.Show($"{sel.Count} ta yozuv o'chirilsinmi?", "Tasdiq",
                MessageBoxButton.YesNo, MessageBoxImage.Question) != MessageBoxResult.Yes) return;
        SetBusy(true);
        var (ok, output) = await Backend.RunAsync($"remove {Backend.Quote(_archPath)} {string.Join(" ", sel)}");
        SetBusy(false);
        StatusText.Text = ok ? "✅ " + Backend.LastLine(output) : "❌ " + Backend.LastLine(output);
        if (ok) await ReloadAsync();
    }

    // ℹ Ma'lumot — arxiv tafsiloti
    private async void InfoButton_Click(object sender, RoutedEventArgs e)
    {
        var (ok, output) = await Backend.RunAsync($"info {Backend.Quote(_archPath)}");
        MessageBox.Show(ok ? output : Backend.LastLine(output),
            "Arxiv ma'lumoti", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    // 📋 Nusxa — tanlangan fayl nomlari (Ctrl+C)
    private void CopyNames()
    {
        if (FileList.SelectedItems.Count == 0)
        {
            StatusText.Text = "⚠️ Avval ro'yxatdan fayl tanlang (Ctrl+A — hammasi).";
            return;
        }
        string names = string.Join("\n",
            FileList.SelectedItems.Cast<ArchEntry>().Select(x => x.Name));
        Clipboard.SetText(names);
        StatusText.Text = $"📋 {FileList.SelectedItems.Count} ta nom nusxalandi.";
    }

    // Ikki marta bosish / Ko'rish — vaqtincha chiqarib tizimda ochish
    private async void OpenSelectedAsync()
    {
        if (FileList.SelectedItem is not ArchEntry en) return;
        // Path traversal himoyasi: arxivdagi nom tajovuzkor nazorat qiladi.
        // Backend extract himoyalangan bo'lsa ham GUI taraf yana tekshiradi.
        if (!IsSafeEntryName(en.Name))
        {
            StatusText.Text = "⚠️ Xavfli fayl nomi bloklandi: " + en.Name;
            return;
        }
        // Bajariladigan fayllarni ochishdan oldin aniq tasdiq (RCE himoyasi)
        string ext = Path.GetExtension(en.Name).ToLowerInvariant();
        if (ext is ".exe" or ".msi" or ".bat" or ".cmd" or ".ps1" or ".vbs" or ".js" or ".lnk" or ".scr" or ".com" or ".pif" or ".reg")
        {
            if (MessageBox.Show($"\"{en.Name}\" bajariladigan fayl. Ochilsinmi?\n\nFaqat ishonchli arxivlardan oching.",
                    "Xavfsizlik tasdig'i", MessageBoxButton.YesNo, MessageBoxImage.Warning) != MessageBoxResult.Yes)
                return;
        }
        SetBusy(true);
        string tmp = Path.Combine(Path.GetTempPath(), "GCF", "open");
        Directory.CreateDirectory(tmp);
        var (ok, output) = await Backend.RunAsync($"extract {Backend.Quote(_archPath)} {Backend.Quote(tmp)} {en.Index}");
        SetBusy(false);
        if (!ok) { StatusText.Text = "❌ " + Backend.LastLine(output); return; }
        try
        {
            // tmp prefiksini GetFullPath bilan tasdiqlaymiz (traversal yakuniy check)
            string full = Path.GetFullPath(Path.Combine(tmp, en.Name));
            string tmpFull = Path.GetFullPath(tmp) + Path.DirectorySeparatorChar;
            if (!full.StartsWith(tmpFull, StringComparison.OrdinalIgnoreCase))
            {
                StatusText.Text = "⚠️ Xavfli yo'l bloklandi.";
                return;
            }
            Process.Start(new ProcessStartInfo(full)
                { UseShellExecute = true });
            StatusText.Text = "📂 " + en.Name;
        }
        catch (Exception ex) { StatusText.Text = "⚠️ Ochilmadi: " + ex.Message; }
    }

    private static bool IsSafeEntryName(string name)
    {
        if (string.IsNullOrEmpty(name) || name.Length > 512) return false;
        if (name[0] == '/' || name[0] == '\\') return false;
        if (name.Contains(':')) return false; // drive / ADS
        if (Path.IsPathRooted(name)) return false;
        foreach (var comp in name.Split('/', '\\'))
        {
            if (comp == ".." || comp == ".") return false;
            if (comp.Length == 0) return false;
        }
        return true;
    }
}
