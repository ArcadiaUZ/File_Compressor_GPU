using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;

namespace GcfGui;

// C++ backend (gcf.exe) bilan aloqa — bitta joyda (Main + Archive oynalari).
static class Backend
{
    public static string ExePath { get; private set; } = "";
    public static string BackendVersion { get; } = "1.0.0";

    // CommandLineToArgvW qoidasiga mos quote:
    // - " -> \" , \ -> \\ (faqat " oldidan va oxirida ikkilanadi)
    // - oxiri \ bilan tugasa yopuvchi " ni escape qilmasligi uchun ikkilanadi
    // Yangi kod ArgumentList ishlatishi kerak; bu faqat eski string-API uchun.
    public static string Quote(string path)
    {
        var sb = new System.Text.StringBuilder("\"");
        int backslashes = 0;
        foreach (char c in path)
        {
            if (c == '\\') { backslashes++; continue; }
            if (c == '"')
            {
                sb.Append('\\', backslashes * 2 + 1);
                sb.Append('"');
            }
            else
            {
                sb.Append('\\', backslashes);
                sb.Append(c);
            }
            backslashes = 0;
        }
        sb.Append('\\', backslashes * 2); // oxirgi \ lar
        sb.Append('"');
        return sb.ToString();
    }

    public static async Task InitAsync()
    {
        // Oldindan taxmin qilinadigan %TEMP%\GCF o'rniga versiyalangan LocalAppData
        // (%TEMP% ko'p foydalanuvchili mashinada zaif, ACL kuchsiz).
        var baseDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "GCF", BackendVersion);
        Directory.CreateDirectory(baseDir);
        ExePath = Path.Combine(baseDir, "gcf.exe");
        // Eski Temp nusxa qolib ketsa yangi backend ishlamay qoladi (stale exe).
        // Shuning uchun har ishga tushishda embedded nusxani qayta yozamiz.
        // Fayl qulflangan bo'lsa (oldinigi jarayon ishlayotgan bo'lsa) 3 marta urinib ko'ramiz.
        using var res = Assembly.GetExecutingAssembly().GetManifestResourceStream("gcf.exe");
        if (res == null) throw new Exception("backend topilmadi (native\\gcf.exe EmbeddedResource ga qo'shilmagan)");
        byte[] embeddedHash;
        using (var ms = new MemoryStream())
        {
            await res.CopyToAsync(ms);
            embeddedHash = SHA256.HashData(ms.ToArray());
            res.Seek(0, SeekOrigin.Begin);
        }
        bool ok = false;
        Exception? lastErr = null;
        for (int attempt = 0; attempt < 3 && !ok; attempt++)
        {
            try
            {
                // Mavjud exe hash bir xil bo'lsa qayta yozmaymiz (tez start)
                if (File.Exists(ExePath))
                {
                    try
                    {
                        byte[] cur = SHA256.HashData(await File.ReadAllBytesAsync(ExePath));
                        if (CryptographicOperations.FixedTimeEquals(cur, embeddedHash))
                        { ok = true; break; }
                    }
                    catch { /* o'qilmasa qayta yozamiz */ }
                }
                using var out_ = File.Create(ExePath);
                res.Seek(0, SeekOrigin.Begin);
                await res.CopyToAsync(out_);
                ok = true;
            }
            catch (IOException ex) when (attempt < 2)
            {
                lastErr = ex;
                await Task.Delay(200);
                try
                {
                    string alt = Path.Combine(baseDir, $"gcf_{Environment.ProcessId}.exe");
                    using var out2 = File.Create(alt);
                    res.Seek(0, SeekOrigin.Begin);
                    await res.CopyToAsync(out2);
                    ExePath = alt;
                    ok = true;
                }
                catch (Exception ex2) { lastErr = ex2; }
            }
            catch (Exception ex) { lastErr = ex; break; }
        }
        if (!ok)
        {
            // Qulflangan/yarim yozilgan faylni ko'rsatib jimgina qaytish o'rniga aniq xato
            ExePath = "";
            throw new Exception("backend chiqarilmadi: " + (lastErr?.Message ?? "noma'lum xato"));
        }
        // Eski PID fallback exe larni tozalash (Temp yig'ilmasligi uchun)
        try
        {
            foreach (var f in Directory.GetFiles(baseDir, "gcf_*.exe"))
            {
                try
                {
                    // Hozirgi jarayonnikidan tashqari, 1 kundan eski bo'lsa o'chiramiz
                    var fi = new FileInfo(f);
                    if (f != ExePath && (DateTime.Now - fi.CreationTime).TotalDays >= 1)
                        File.Delete(f);
                }
                catch { }
            }
        }
        catch { }
    }

    // Xavfsiz variant: har argument alohida (shell siz, quote siz) — injection/bo'linish yo'q.
    public static async Task<(bool ok, string output)> RunAsync(IEnumerable<string> args)
    {
        if (string.IsNullOrEmpty(ExePath) || !File.Exists(ExePath))
            return (false, "XATO: backend tayyor emas (InitAsync chaqirilmagan yoki exe yo'q)");
        try
        {
            var psi = new ProcessStartInfo(ExePath)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                StandardOutputEncoding = System.Text.Encoding.UTF8,
                StandardErrorEncoding = System.Text.Encoding.UTF8,
            };
            foreach (var a in args) psi.ArgumentList.Add(a);
            using var p = Process.Start(psi)!;
            string stdout = await p.StandardOutput.ReadToEndAsync();
            string stderr = await p.StandardError.ReadToEndAsync();
            await p.WaitForExitAsync();
            return (p.ExitCode == 0, stdout + stderr);
        }
        catch (Exception ex) { return (false, ex.Message); }
    }

    // Eski string-API (qolgan chaqiruvlar uchun). Yangi kod IEnumerable<string> ishlatsin.
    public static async Task<(bool ok, string output)> RunAsync(string args)
    {
        if (string.IsNullOrEmpty(ExePath) || !File.Exists(ExePath))
            return (false, "XATO: backend tayyor emas (InitAsync chaqirilmagan yoki exe yo'q)");
        try
        {
            var psi = new ProcessStartInfo(ExePath, args)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                // Backend UTF-8 yozadi (wmain + SetConsoleOutputCP). Default ANSI bo'lsa
                // kirill/emoji nomlar "????" bo'lib parsing buziladi.
                StandardOutputEncoding = System.Text.Encoding.UTF8,
                StandardErrorEncoding = System.Text.Encoding.UTF8,
            };
            using var p = Process.Start(psi)!;
            string stdout = await p.StandardOutput.ReadToEndAsync();
            string stderr = await p.StandardError.ReadToEndAsync();
            await p.WaitForExitAsync();
            return (p.ExitCode == 0, stdout + stderr);
        }
        catch (Exception ex) { return (false, ex.Message); }
    }

    public static string LastLine(string output)
    {
        var lines = output.Split('\n').Select(l => l.Trim()).Where(l => l.Length > 0).ToArray();
        for (int i = lines.Length - 1; i >= 0; i--)
            if (lines[i].StartsWith("Siqildi:") || lines[i].StartsWith("Arxivlandi") ||
                lines[i].StartsWith("Papka arxivlandi:") || lines[i].StartsWith("Ochildi:") ||
                lines[i].StartsWith("Chiqarildi:") ||
                lines[i].StartsWith("Qo'shildi:") || lines[i].StartsWith("O'chirildi:") ||
                lines[i].StartsWith("XATO:") || lines[i].StartsWith("Tekshiruv OK:"))
                return lines[i];
        return lines.Length > 0 ? lines[^1] : "(bo'sh javob)";
    }
}
