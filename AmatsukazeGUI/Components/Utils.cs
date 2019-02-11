using System;
using System.Windows;
using System.Windows.Media;

namespace Amatsukaze.Components
{
    public static class Utils
    {
        public static void SetWindowProperties(Window win)
        {
            var version = System.Environment.OSVersion;
            if (version.Platform == PlatformID.Win32NT &&
                version.Version.Major <= 6 &&
                version.Version.Minor <= 1)
            {
                // windows7のクラシックモードでテキスト表示がにじむのを回避
                TextOptions.SetTextFormattingMode(win, TextFormattingMode.Display);
                TextOptions.SetTextRenderingMode(win, TextRenderingMode.ClearType);
            }
        }

        public static void SetWindowCenter(Window win)
        {
            Window mainWindow = Application.Current.MainWindow;
            win.Left = mainWindow.Left + (mainWindow.Width - win.ActualWidth) / 2;
            win.Top = mainWindow.Top + (mainWindow.Height - win.ActualHeight) / 2;
        }
    }
}
