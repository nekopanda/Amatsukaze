using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Lib
{
    /// <summary>
    /// newでサスペンド抑止
    /// サスペンドしてもOKになったらDispose()を呼ぶ
    /// </summary>
    public class PreventSuspendContext : IDisposable
    {
        enum PowerRequestType
        {
            PowerRequestDisplayRequired = 0,
            PowerRequestSystemRequired,
            PowerRequestAwayModeRequired,
            PowerRequestMaximum
        }

        const int POWER_REQUEST_CONTEXT_VERSION = 0;
        const int POWER_REQUEST_CONTEXT_SIMPLE_STRING = 0x1;
        const int POWER_REQUEST_CONTEXT_DETAILED_STRING = 0x2;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct POWER_REQUEST_CONTEXT
        {
            public UInt32 Version;
            public UInt32 Flags;
            [MarshalAs(UnmanagedType.LPWStr)]
            public string
                SimpleReasonString;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct PowerRequestContextDetailedInformation
        {
            public IntPtr LocalizedReasonModule;
            public UInt32 LocalizedReasonId;
            public UInt32 ReasonStringCount;
            [MarshalAs(UnmanagedType.LPWStr)]
            public string[] ReasonStrings;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct POWER_REQUEST_CONTEXT_DETAILED
        {
            public UInt32 Version;
            public UInt32 Flags;
            public PowerRequestContextDetailedInformation DetailedInformation;
        }

        [DllImport("kernel32.dll")]
        static extern IntPtr PowerCreateRequest(ref POWER_REQUEST_CONTEXT Context);

        [DllImport("kernel32.dll")]
        static extern bool PowerSetRequest(IntPtr PowerRequestHandle, PowerRequestType RequestType);

        [DllImport("kernel32.dll")]
        static extern bool PowerClearRequest(IntPtr PowerRequestHandle, PowerRequestType RequestType);

        [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true, ExactSpelling = true)]
        internal static extern int CloseHandle(IntPtr hObject);


        POWER_REQUEST_CONTEXT _PowerRequestContext;
        IntPtr _PowerRequest; //HANDLE

        public PreventSuspendContext()
        {
            _PowerRequestContext.Version = POWER_REQUEST_CONTEXT_VERSION;
            _PowerRequestContext.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
            _PowerRequestContext.SimpleReasonString = "Amatsukazeがエンコード中です。";
            _PowerRequest = PowerCreateRequest(ref _PowerRequestContext);
            PowerSetRequest(_PowerRequest, PowerRequestType.PowerRequestSystemRequired);
        }

        #region IDisposable Support
        private bool disposedValue = false; // 重複する呼び出しを検出するには

        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                PowerClearRequest(_PowerRequest, PowerRequestType.PowerRequestSystemRequired);
                CloseHandle(_PowerRequest);
                _PowerRequest = IntPtr.Zero;
                disposedValue = true;
            }
        }

        ~PreventSuspendContext()
        {
            // このコードを変更しないでください。クリーンアップ コードを上の Dispose(bool disposing) に記述します。
            Dispose(false);
        }

        // このコードは、破棄可能なパターンを正しく実装できるように追加されました。
        public void Dispose()
        {
            // このコードを変更しないでください。クリーンアップ コードを上の Dispose(bool disposing) に記述します。
            Dispose(true);
            GC.SuppressFinalize(this);
        }
        #endregion
    }

    public static class WinAPI
    {
        [DllImport("User32.dll")]
        private static extern bool GetLastInputInfo(ref LASTINPUTINFO plii);
        [DllImport("User32.dll")]
        private static extern uint GetTickCount();

        private struct LASTINPUTINFO
        {
            public uint cbSize;
            public uint dwTime;
        }

        public static TimeSpan GetLastInputTime()
        {
            LASTINPUTINFO lastInput = new LASTINPUTINFO();
            lastInput.cbSize = (uint)Marshal.SizeOf(lastInput);
            GetLastInputInfo(ref lastInput);
            return new TimeSpan(0, 0, 0, 0, (int)(GetTickCount() - lastInput.dwTime));
        }
    }
}
