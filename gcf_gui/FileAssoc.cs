using Microsoft.Win32;
using System.Runtime.InteropServices;

namespace GcfGui;

// .gcf -> GCF bog'lash (HKCU — admin huquqi kerak emas).
// Bog'langach .gcf ga double-click qilinsa GCF arxiv oynada ochadi.
static class FileAssoc
{
    const string EXT = ".gcf";
    const string PROGID = "GCF.Archive";

    [DllImport("shell32.dll")]
    static extern void SHChangeNotify(int wEventId, int uFlags, IntPtr dwItem1, IntPtr dwItem2);

    public static bool IsRegistered()
    {
        using var k = Registry.CurrentUser.OpenSubKey(@"Software\Classes\" + EXT);
        if (k?.GetValue("")?.ToString() != PROGID) return false;
        // Faqat ProgID emas, command dagi exe yo'l ham joriy exe ga mosligini tekshiramiz
        // (publish\GCF.exe ko'chsa stale "true" qaytmasligi uchun).
        using var kc = Registry.CurrentUser.OpenSubKey(@"Software\Classes\" + PROGID + @"\shell\open\command");
        string? cmd = kc?.GetValue("")?.ToString();
        if (string.IsNullOrEmpty(cmd)) return false;
        string cur = Environment.ProcessPath ?? "";
        if (string.IsNullOrEmpty(cur)) return false;
        return cmd.Contains(cur.Trim('"'), StringComparison.OrdinalIgnoreCase);
    }

    public static void Unregister()
    {
        try { Registry.CurrentUser.DeleteSubKeyTree(@"Software\Classes\" + EXT, false); } catch { }
        try { Registry.CurrentUser.DeleteSubKeyTree(@"Software\Classes\" + PROGID, false); } catch { }
        SHChangeNotify(0x08000000, 0, IntPtr.Zero, IntPtr.Zero);
    }

    public static void Register()
    {
        string exe = Environment.ProcessPath ?? throw new Exception("exe yo'li topilmadi");
        using (var k = Registry.CurrentUser.CreateSubKey(@"Software\Classes\" + EXT))
            k.SetValue("", PROGID);
        using (var k = Registry.CurrentUser.CreateSubKey(@"Software\Classes\" + PROGID))
            k.SetValue("", "GCF Arxiv");
        using (var k = Registry.CurrentUser.CreateSubKey(@"Software\Classes\" + PROGID + @"\DefaultIcon"))
            k.SetValue("", $"\"{exe}\",0");
        using (var k = Registry.CurrentUser.CreateSubKey(@"Software\Classes\" + PROGID + @"\shell\open\command"))
            k.SetValue("", $"\"{exe}\" \"%1\"");
        SHChangeNotify(0x08000000, 0, IntPtr.Zero, IntPtr.Zero); // Explorer yangilansin
    }
}
