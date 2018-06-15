using System;
using System.Linq;
using System.Runtime.InteropServices;
using System.IO;
using System.Windows.Media.Imaging;
using System.Windows.Media;
using System.Collections.Generic;

namespace Amatsukaze.Lib
{
    public static class AmatsukazeNatives
    {
        /*
         * NotificationObjectはプロパティ変更通知の仕組みを実装したオブジェクトです。
         */
    }

    public class AMTContext : IDisposable
    {
        public IntPtr Ptr { private set; get; }

        #region Natives
        [DllImport("Amatsukaze.dll")]
        private static extern void InitAmatsukazeDLL();

        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr AMTContext_Create();

        [DllImport("Amatsukaze.dll")]
        private static extern void ATMContext_Delete(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr AMTContext_GetError(IntPtr ctx);
        #endregion

        static AMTContext()
        {
            InitAmatsukazeDLL();
        }

        public AMTContext()
        {
            Ptr = AMTContext_Create();
        }

        #region IDisposable Support
        private bool disposedValue = false; // 重複する呼び出しを検出するには

        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                ATMContext_Delete(Ptr);
                Ptr = IntPtr.Zero;
                disposedValue = true;
            }
        }

        ~AMTContext()
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

        public string GetError()
        {
            return Marshal.PtrToStringAnsi(AMTContext_GetError(Ptr));
        }
    }

    public class ContentNibbles
    {
        public int Level1;
        public int Level2;
        public int User1;
        public int User2;
    }

    public class Program
    {
        public int ServiceId;
        public bool HasVideo;
        public int VideoPid;
        public int Stream;
        public int Width;
        public int Height;
        public int SarW;
        public int SarH;
        public string EventName;
        public string EventText;
        public ContentNibbles[] Content;
    }

    public class Service
    {
        public int ServiceId;
        public string ProviderName;
        public string ServiceName;
    }

    public class TsInfo : IDisposable
    {
        public AMTContext Ctx { private set; get; }
        public IntPtr Ptr { private set; get; }

        #region Natives
        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr TsInfo_Create(IntPtr ctx);

        [DllImport("Amatsukaze.dll")]
        private static extern void TsInfo_Delete(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern int TsInfo_ReadFile(IntPtr ptr, string filepath);

        [DllImport("Amatsukaze.dll")]
        private static extern int TsInfo_HasServiceInfo(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern void TsInfo_GetDay(IntPtr ptr, out int y, out int m, out int d);

        [DllImport("Amatsukaze.dll")]
        private static extern void TsInfo_GetTime(IntPtr ptr, out int h, out int m, out int s);

        [DllImport("Amatsukaze.dll")]
        private static extern int TsInfo_GetNumProgram(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern void TsInfo_GetProgramInfo(IntPtr ptr, int i, out int progId, out bool hasVideo, out int videoPid, out int numContent);

        [DllImport("Amatsukaze.dll")]
        private static extern void TsInfo_GetContentNibbles(IntPtr ptr, int i, int ci, out int level1, out int level2, out int user1, out int user2);

        [DllImport("Amatsukaze.dll")]
        private static extern void TsInfo_GetVideoFormat(IntPtr ptr, int i, out int stream, out int width, out int height, out int sarW, out int  sarH);

        [DllImport("Amatsukaze.dll")]
        private static extern int TsInfo_GetNumService(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern int TsInfo_GetServiceId(IntPtr ptr, int i);

        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr TsInfo_GetProviderName(IntPtr ptr, int i);

        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr TsInfo_GetServiceName(IntPtr ptr, int i);

        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr TsInfo_GetEventName(IntPtr ptr, int i);

        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr TsInfo_GetEventText(IntPtr ptr, int i);
        #endregion

        public TsInfo(AMTContext ctx)
        {
            Ctx = ctx;
            Ptr = TsInfo_Create(Ctx.Ptr);
            if(Ptr == IntPtr.Zero)
            {
                throw new IOException(Ctx.GetError());
            }
        }

        #region IDisposable Support
        private bool disposedValue = false; // 重複する呼び出しを検出するには

        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                TsInfo_Delete(Ptr);
                Ptr = IntPtr.Zero;
                disposedValue = true;
            }
        }

        ~TsInfo()
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

        public bool ReadFile(string filepath)
        {
            return TsInfo_ReadFile(Ptr, filepath) != 0;
        }

        public bool HasServiceInfo
        {
            get
            {
                return TsInfo_HasServiceInfo(Ptr) != 0;
            }
        }

        public Program[] GetProgramList()
        {
            return Enumerable.Range(0, TsInfo_GetNumProgram(Ptr))
                .Select(i => {
                    Program prog = new Program();
                    int numContent;
                    TsInfo_GetProgramInfo(Ptr, i,
                        out prog.ServiceId, out prog.HasVideo, out prog.VideoPid, out numContent);
                    TsInfo_GetVideoFormat(Ptr, i,
                        out prog.Stream, out prog.Width, out prog.Height, out prog.SarW, out prog.SarH);
                    prog.EventName = Marshal.PtrToStringUni(TsInfo_GetEventName(Ptr, i));
                    prog.EventText = Marshal.PtrToStringUni(TsInfo_GetEventText(Ptr, i));
                    prog.Content = Enumerable.Range(0, numContent)
                        .Select(ci => {
                            ContentNibbles data = new ContentNibbles();
                            TsInfo_GetContentNibbles(Ptr, i, ci,
                                out data.Level1, out data.Level2, out data.User1, out data.User2);
                            return data;
                        }).ToArray();
                    return prog;
                }).ToArray();
        }

        // ServiceInfoがある場合のみ
        public DateTime GetTime()
        {
            int year, month, day, hour, minute, second;
            TsInfo_GetDay(Ptr, out year, out month, out day);
            TsInfo_GetTime(Ptr, out hour, out minute, out second);
            return new DateTime(year, month, day, hour, minute, second);
        }

        // ServiceInfoがある場合のみ
        public Service[] GetServiceList()
        {
            return Enumerable.Range(0, TsInfo_GetNumService(Ptr))
                .Select(i => new Service() {
                    ServiceId = TsInfo_GetServiceId(Ptr, i),
                    ProviderName = Marshal.PtrToStringUni(TsInfo_GetProviderName(Ptr, i)),
                    ServiceName = Marshal.PtrToStringUni(TsInfo_GetServiceName(Ptr, i))
                }).ToArray();
        }
    }

    public class MediaFile : IDisposable
    {
        public AMTContext Ctx { private set; get; }
        public IntPtr Ptr { private set; get; }

        #region Natives
        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr MediaFile_Create(IntPtr ctx, string filepath, int serviceid);

        [DllImport("Amatsukaze.dll")]
        private static extern void MediaFile_Delete(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern int MediaFile_DecodeFrame(IntPtr ptr, float pos, ref int width, ref int height);

        [DllImport("Amatsukaze.dll")]
        private static unsafe extern void MediaFile_GetFrame(IntPtr ptr, byte* rgb, int width, int height);
        #endregion

        public MediaFile(AMTContext ctx, string filepath, int serviceid)
        {
            Ctx = ctx;
            Ptr = MediaFile_Create(Ctx.Ptr, filepath, serviceid);
            if (Ptr == IntPtr.Zero)
            {
                throw new IOException(Ctx.GetError());
            }
        }

        #region IDisposable Support
        private bool disposedValue = false; // 重複する呼び出しを検出するには

        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                MediaFile_Delete(Ptr);
                Ptr = IntPtr.Zero;
                disposedValue = true;
            }
        }

        ~MediaFile()
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

        // 失敗したらnullが返るので注意
        public BitmapSource GetFrame(float pos)
        {
            int width = 0, height = 0;
            if(MediaFile_DecodeFrame(Ptr, pos, ref width, ref height) != 0)
            {
                if(width != 0 && height != 0)
                {
                    int stride = width * 3;
                    byte[] buffer = new byte[stride * height];
                    unsafe
                    {
                        fixed (byte* pbuffer = buffer)
                        {
                            MediaFile_GetFrame(Ptr, pbuffer, width, height);
                        }
                    }
                    return BitmapSource.Create(
                        width, height, 96, 96, PixelFormats.Bgr24, null, buffer, stride);
                }
            }
            return null;
        }
    }

    public delegate bool LogoAnalyzeCallback(float progress, int nread, int total, int ngather);

    public class LogoFile : IDisposable
    {
        public AMTContext Ctx { private set; get; }
        public IntPtr Ptr { private set; get; }

        #region Natives
        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr LogoFile_Create(IntPtr ctx, string filepath);

        [DllImport("Amatsukaze.dll")]
        private static extern void LogoFile_Delete(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern int LogoFile_GetWidth(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern int LogoFile_GetHeight(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern int LogoFile_GetX(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern int LogoFile_GetY(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern int LogoFile_GetImgWidth(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern int LogoFile_GetImgHeight(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern int LogoFile_GetServiceId(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern void LogoFile_SetServiceId(IntPtr ptr, int serviceId);

        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr LogoFile_GetName(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern void LogoFile_SetName(IntPtr ptr, string name);

        [DllImport("Amatsukaze.dll")]
        private static unsafe extern void LogoFile_GetImage(IntPtr ptr, byte* buf, int stride, byte bg);

        [DllImport("Amatsukaze.dll")]
        private static extern int LogoFile_Save(IntPtr ptr, string filename);

        [DllImport("Amatsukaze.dll")]
        private static extern int ScanLogo(IntPtr ctx, string srcpath, int serviceid, string workfile, string dstpath,
            int imgx, int imgy, int w, int h, int thy, int numMaxFrames, LogoAnalyzeCallback cb);
        #endregion

        public LogoFile(AMTContext ctx, string filepath)
        {
            Ctx = ctx;
            Ptr = LogoFile_Create(Ctx.Ptr, filepath);
            if (Ptr == IntPtr.Zero)
            {
                throw new IOException(Ctx.GetError());
            }
        }

        #region IDisposable Support
        private bool disposedValue = false; // 重複する呼び出しを検出するには

        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                LogoFile_Delete(Ptr);
                Ptr = IntPtr.Zero;
                disposedValue = true;
            }
        }

        ~LogoFile()
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

        public int Width { get { return LogoFile_GetWidth(Ptr); } }

        public int Height { get { return LogoFile_GetHeight(Ptr); } }

        public int ImageX { get { return LogoFile_GetX(Ptr); } }

        public int ImageY { get { return LogoFile_GetY(Ptr); } }

        public int ImageWidth { get { return LogoFile_GetImgWidth(Ptr); } }

        public int ImageHeight { get { return LogoFile_GetImgHeight(Ptr); } }

        public int ServiceId {
            get {
                return LogoFile_GetServiceId(Ptr);
            }
            set {
                LogoFile_SetServiceId(Ptr, value);
            }
        }

        public string Name {
            get {
                return Marshal.PtrToStringAnsi(LogoFile_GetName(Ptr));
            }
            set {
                LogoFile_SetName(Ptr, value);
            }
        }

        public BitmapFrame GetImage(byte bg)
        {
            int stride = Width * 3;
            byte[] buffer = new byte[stride * Height];
            unsafe
            {
                fixed (byte* pbuffer = buffer)
                {
                    LogoFile_GetImage(Ptr, pbuffer, stride, bg);
                }
            }
            return BitmapFrame.Create(BitmapSource.Create(
                Width, Height, 96, 96, PixelFormats.Bgr24, null, buffer, stride));
        }

        public void Save(string filepath)
        {
            if(LogoFile_Save(Ptr, filepath) == 0)
            {
                throw new IOException(Ctx.GetError());
            }
        }

        public static void ScanLogo(AMTContext ctx, string srcpath, int serviceid, string workfile, string dstpath,
            int imgx, int imgy, int w, int h, int thy, int numMaxFrames, LogoAnalyzeCallback cb)
        {
            if(ScanLogo(ctx.Ptr, srcpath, serviceid, workfile, dstpath, imgx, imgy, w, h, thy, numMaxFrames, cb) == 0)
            {
                throw new IOException(ctx.GetError());
            }
        }
    }

    public delegate bool TsSlimCallback();

    public class TsSlimFilter : IDisposable
    {
        public AMTContext Ctx { private set; get; }
        public IntPtr Ptr { private set; get; }

        #region Natives
        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr TsSlimFilter_Create(IntPtr ctx, int videoPid);

        [DllImport("Amatsukaze.dll")]
        private static extern void TsSlimFilter_Delete(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern bool TsSlimFilter_Exec(IntPtr ptr, string srcpath, string dstpath, TsSlimCallback cb);
        #endregion

        public TsSlimFilter(AMTContext ctx, int videoPid)
        {
            Ctx = ctx;
            Ptr = TsSlimFilter_Create(Ctx.Ptr, videoPid);
            if (Ptr == IntPtr.Zero)
            {
                throw new IOException(Ctx.GetError());
            }
        }

        #region IDisposable Support
        private bool disposedValue = false; // 重複する呼び出しを検出するには

        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                TsSlimFilter_Delete(Ptr);
                Ptr = IntPtr.Zero;
                disposedValue = true;
            }
        }

        ~TsSlimFilter()
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

        public void Exec(string srcpath, string dstpath, TsSlimCallback cb)
        {
            if(!TsSlimFilter_Exec(Ptr, srcpath, dstpath, cb))
            {
                throw new IOException(Ctx.GetError());
            }
        }
    }

    public struct ProcessGroup
    {
        public int Group;
        public ulong Mask;
    }

    public enum ProcessGroupKind
    {
        None, Core, L2, L3, NUMA, Group, Count
    }

    public class CPUInfo : IDisposable
    {
        public AMTContext Ctx { private set; get; }
        public IntPtr Ptr { private set; get; }

        [StructLayout(LayoutKind.Sequential)]
        private struct GROUP_AFFINITY
        {
            public UIntPtr Mask;
            public ushort Group;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
            public ushort[] Reserved;
        }

        #region Natives
        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr CPUInfo_Create();

        [DllImport("Amatsukaze.dll")]
        private static extern void CPUInfo_Delete(IntPtr ptr);

        [DllImport("Amatsukaze.dll")]
        private static extern IntPtr CPUInfo_GetData(IntPtr ptr, int tag, out int count);
        #endregion

        public CPUInfo(AMTContext ctx)
        {
            Ctx = ctx;
            Ptr = CPUInfo_Create();
            if (Ptr == IntPtr.Zero)
            {
                throw new IOException(Ctx.GetError());
            }
        }

        #region IDisposable Support
        private bool disposedValue = false; // 重複する呼び出しを検出するには

        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                CPUInfo_Delete(Ptr);
                Ptr = IntPtr.Zero;
                disposedValue = true;
            }
        }

        ~CPUInfo()
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

        public ProcessGroup[] Get(ProcessGroupKind kind)
        {
            int count;
            var ptr = CPUInfo_GetData(Ptr, (int)kind, out count);
            var ret = new ProcessGroup[count];
            int size = Marshal.SizeOf(typeof(GROUP_AFFINITY));
            for(int i = 0; i < count; ++i)
            {
                var item = (GROUP_AFFINITY)Marshal.PtrToStructure(ptr, typeof(GROUP_AFFINITY));
                ret[i].Group = item.Group;
                ret[i].Mask = item.Mask.ToUInt64();
                ptr += size;
            }
            return ret;
        }
    }
}
