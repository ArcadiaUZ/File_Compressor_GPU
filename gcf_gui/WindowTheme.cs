using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace GcfGui;

// Sarlavha (title bar) ni GUI rangi bilan bir xil qilish (Windows 11 DWM).
// Tugmalar (— ▢ ✕) o'z joyida qoladi, faqat rangi moslashadi.
static class WindowTheme
{
    const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
    const int DWMWA_BORDER_COLOR = 34;
    const int DWMWA_CAPTION_COLOR = 35;
    const int DWMWA_TEXT_COLOR = 36;
    const int GUI_COLOR = 0x002E1E1E; // #1E1E2E (0x00BBGGRR)
    const int TEXT_COLOR = 0x00F0E8E8;

    [DllImport("dwmapi.dll")]
    static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int val, int size);

    public static void Apply(Window w)
    {
        w.SourceInitialized += (_, _) =>
        {
            try
            {
                var hwnd = new WindowInteropHelper(w).Handle;
                int dark = 1, caption = GUI_COLOR, border = GUI_COLOR, text = TEXT_COLOR;
                DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref dark, sizeof(int));
                DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, ref caption, sizeof(int));
                DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, ref border, sizeof(int));
                DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, ref text, sizeof(int));
            }
            catch { /* eski Windows da shunchaki standart rang qoladi */ }
        };
    }
}
