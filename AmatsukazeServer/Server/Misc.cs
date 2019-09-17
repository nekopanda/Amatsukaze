using Amatsukaze.Lib;
using Livet;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;
using System.Windows.Forms;

namespace Amatsukaze.Server
{
    public enum LaunchType {
        Standalone,
        Server,
        Client,
        Debug,
        Logo,
        Add
    };

    public class GUIOPtion
    {
        public string RootDir;
        public LaunchType LaunchType = LaunchType.Standalone;
        public int ServerPort = ServerSupport.DEFAULT_PORT;

        // ロゴGUI用
        public string FilePath;
        public string WorkPath;
        public bool SlimTs;
        public int ServiceId;

        public GUIOPtion(string[] args)
        {
            for (int i = 0; i < args.Length; ++i)
            {
                string arg = args[i];
                if (arg == "-p" || arg == "--port")
                {
                    ServerPort = int.Parse(args[i + 1]);
                    ++i;
                }
                else if(arg == "-l" || arg == "--launch")
                {
                    string opt = args[i + 1];
                    if (opt == "standalone")
                    {
                        LaunchType = LaunchType.Standalone;
                    }
                    else if (opt == "server")
                    {
                        LaunchType = LaunchType.Server;
                    }
                    else if(opt == "debug")
                    {
                        LaunchType = LaunchType.Debug;
                    }
                    else if(opt == "client") {
                        LaunchType = LaunchType.Client;
                    }
                    else if(opt == "logo")
                    {
                        LaunchType = LaunchType.Logo;
                    }
                    else if (opt == "add")
                    {
                        LaunchType = LaunchType.Add;
                    }
                }
                else if(arg == "-r" || arg == "--root")
                {
                    RootDir = args[i + 1];
                    ++i;
                }
                else if(arg == "--file")
                {
                    FilePath = args[i + 1];
                    ++i;
                }
                else if (arg == "--work")
                {
                    WorkPath = args[i + 1];
                    ++i;
                }
                else if(arg == "--slimts")
                {
                    SlimTs = true;
                }
                else if (arg == "--serviceid")
                {
                    ServiceId = int.Parse(args[i + 1]);
                    ++i;
                }
            }
        }
    }

    // Client -> Server: 開始要求
    // Server -> Client: 開始OK
    public enum ResourcePhase
    {
        TSAnalyze = 0,
        CMAnalyze,
        Filter,
        Encode,
        Mux,
        Max,

        NoWait = 0x100,
    }

    public class Debug
    {
        [Conditional("DEBUG")]
        public static void Print(string str)
        {
            Util.AddLog(str, null);
        }
    }

    public static class Util
    {
        public static List<Action<string>> LogHandlers = new List<Action<string>>();

        public static string ErrorMessage(int parallelId, string mes, Exception e)
        {
            string message = "";
            if (parallelId != -1)
            {
                message += "[" + parallelId + "] ";
            }
            if (mes != null)
            {
                message += mes;
            }
            if (e != null)
            {
                if(mes != null)
                {
                    message += "例外: ";
                }
                message += e.Message;
            }
            return message;
        }

        public static void AddLog(string log, Exception e)
        {
            AddLog(-1, log, e);
        }

        public static void AddLog(int parallelId, string log, Exception e)
        {
            string message = ErrorMessage(parallelId, log, e);
            foreach (var handler in LogHandlers)
            {
                handler(message);
                if(e != null)
                {
                    handler(e.StackTrace);
                }
            }
        }

        public static async void AttachHandler(this Task task)
        {
            try
            {
                await task;
            }
            catch (Exception e)
            {
                AddLog(null, e);
            }
        }

        public static string ToGUIString(this DateTime dt)
        {
            return dt.ToString("yyyy/MM/dd HH:mm:ss");
        }

        public static string ToGUIString(this TimeSpan ts)
        {
            return (int)ts.TotalHours + "時間" + ts.ToString("mm\\分ss\\秒");
        }

        [return: MarshalAs(UnmanagedType.Bool)]
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern bool GetDiskFreeSpaceEx(
            string lpDirectoryName, 
            out ulong lpFreeBytesAvailable, 
            out ulong lpTotalNumberOfBytes, 
            out ulong lpTotalNumberOfFreeBytes);

        [return: MarshalAs(UnmanagedType.Bool)]
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern bool MoveFileEx(string existingFileName, string newFileName, int flags);

        public static string CreateTmpFile(string baseDir)
        {
            for(int code = Environment.TickCount & 0xFFFFFF, 
                end = code + 0x1000; code != end; ++code)
            {
                string path = baseDir + "\\amt" + code;
                try
                {
                    using (FileStream fs = new FileStream(path, FileMode.CreateNew))
                    {
                        return path;
                    }
                }
                catch (IOException) { }
            }
            throw new IOException("一時ファイル作成に失敗");
        }

        public static string CreateSuffix(int n)
        {
            if (n == 0) return "";
            var suffix = "";
            while (n > 0)
            {
                suffix = (char)('A' + (n % 27) - ((n < 27) ? 1 : 0)) + suffix;
                n /= 27;
            }
            suffix = "-" + suffix;
            return suffix;
        }

        public static string CreateDstFile(string baseName, string ext)
        {
            for (int i = 0; i < 0x1000; ++i)
            {
                string fname = baseName + CreateSuffix(i);
                string path = fname + ext;
                try
                {
                    using (FileStream fs = new FileStream(path, FileMode.CreateNew))
                    {
                        return fname;
                    }
                }
                catch (IOException) { }
            }
            throw new IOException("出力ファイル作成に失敗");
        }

        private static Regex escapeFileNameRegex = new Regex("[/:*?\"<>|\\\\]");

        // ファイル名不可文字の置換
        public static string EscapeFileName(string name, bool escapeYen)
        {
            MatchEvaluator eval = match =>
            {
                switch (match.Value)
                {
                    case "/": return "／";
                    case ":": return "：";
                    case "*": return "＊";
                    case "?": return "？";
                    case "\"": return "”";
                    case "<": return "＜";
                    case ">": return "＞";
                    case "|": return "｜";
                    case "\\": return escapeYen ? "￥" : "\\";
                    default: throw new Exception("Unexpected match!");
                }
            };
            return escapeFileNameRegex.Replace(name, eval);
        }

        public static void PlayRandomSound(string dir)
        {
            if (Directory.Exists(dir) == false) return;
            var files = Directory.EnumerateFiles(dir, "*.wav").ToArray();
            if (files.Length == 0) return;
            var file = files[Environment.TickCount % files.Length];
            var player = new System.Media.SoundPlayer(file);
            player.Play();
        }
    }

    public abstract class ConsoleTextBase : NotificationObject
    {
        public abstract void OnAddLine(string text);
        public abstract void OnReplaceLine(string text);

        private List<byte> rawtext = new List<byte>();
        private bool isCR = false;

        public virtual void Clear()
        {
            rawtext.Clear();
            isCR = false;
        }

        public void AddBytes(byte[] buf, int offset, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                if (buf[i] == '\n' || buf[i] == '\r' || buf[i] == 0x08)
                {
                    if (rawtext.Count > 0)
                    {
                        OutLine();
                    }
                    isCR = (buf[i] == '\r' || buf[i] == 0x08);
                }
                else
                {
                    rawtext.Add(buf[i]);
                }
            }
        }

        public void Flush()
        {
            OutLine();
            isCR = false;
        }

        private void OutLine()
        {
            string text = Encoding.Default.GetString(rawtext.ToArray());
            if (isCR)
            {
                OnReplaceLine(text);
            }
            else
            {
                OnAddLine(text);
            }
            rawtext.Clear();
        }
    }

    public class RollingTextLines
    {
        public List<string> TextLines { get; } = new List<string>();

        private int maxLines;

        public RollingTextLines(int maxLines)
        {
            this.maxLines = maxLines;
        }

        public void Clear()
        {
            TextLines.Clear();
        }

        public void AddLine(string text)
        {
            if (TextLines.Count > maxLines)
            {
                TextLines.RemoveRange(0, 100);
            }
            TextLines.Add(text);
        }

        public void ReplaceLine(string text)
        {
            if (TextLines.Count == 0)
            {
                TextLines.Add(text);
            }
            else
            {
                TextLines[TextLines.Count - 1] = text;
            }
        }
    }

    public class AffinityCreator
    {
        private ProcessGroup[][] masks = new ProcessGroup[(int)ProcessGroupKind.Count][];

        private AMTContext amtcontext = new AMTContext();
        public AffinityCreator()
        {
            //Util.AddLog(" CPU構成を検出");
            using (var cpuInfo = new CPUInfo(amtcontext))
            {
                masks[(int)ProcessGroupKind.Core] = cpuInfo.Get(ProcessGroupKind.Core);
                masks[(int)ProcessGroupKind.L2] = cpuInfo.Get(ProcessGroupKind.L2);
                masks[(int)ProcessGroupKind.L3] = cpuInfo.Get(ProcessGroupKind.L3);
                masks[(int)ProcessGroupKind.NUMA] = cpuInfo.Get(ProcessGroupKind.NUMA);
                masks[(int)ProcessGroupKind.Group] = cpuInfo.Get(ProcessGroupKind.Group);
                // ないところは前のをコピー
                for (int i = 1; i < masks.Length; ++i)
                {
                    if ((masks[i]?.Length ?? 0) == 0)
                    {
                        masks[i] = masks[i - 1];
                    }
                }
            }
        }

        public List<int> GetClusters()
        {
            return masks.Select(s => s?.Length ?? 0).ToList();
        }

        public ProcessGroup GetMask(ProcessGroupKind setting, int pid)
        {
            if (setting == ProcessGroupKind.None)
            {
                throw new ArgumentException("ProcessGroupKind.Noneです");
            }
            var target = masks[(int)setting];
            return target[pid % target.Length];
        }
    }

    public static class HashUtil
    {
        private static readonly int HASH_LENGTH = 64;

        private class Buffer
        {
            public byte[] buffer;
            public int size;
        }

        private static Task ReadFile(BufferBlock<Buffer> bufferQ, BufferBlock<Buffer> computeQ, FileStream src)
        {
            return Task.Run(() =>
            {
                while (true)
                {
                    var buf = bufferQ.Receive();
                    buf.size = src.Read(buf.buffer, 0, buf.buffer.Length);
                    if (buf.size == 0)
                    {
                        computeQ.Complete();
                        break;
                    }
                    computeQ.Post(buf);
                }
            });
        }

        private static async Task ComputeHash(BufferBlock<Buffer> computeQ, BufferBlock<Buffer> writeQ, HashAlgorithm hash)
        {
            while (await computeQ.OutputAvailableAsync())
            {
                var buf = await computeQ.ReceiveAsync();
                await Task.Run(() => hash.TransformBlock(buf.buffer, 0, buf.size, null, 0));
                writeQ.Post(buf);
            }

            hash.TransformFinalBlock(new byte[0], 0, 0);
            writeQ.Complete();
        }

        private static Task WriteFile(BufferBlock<Buffer> writeQ, BufferBlock<Buffer> bufferQ, FileStream dst)
        {
            return Task.Run(() =>
            {
                while (writeQ.OutputAvailableAsync().Result)
                {
                    var buf = writeQ.Receive();
                    dst.Write(buf.buffer, 0, buf.size);
                    bufferQ.Post(buf);
                }
            });
        }

        public static async Task<byte[]> CopyWithHash(string srcpath, string dstpath)
        {
            var bufferQ = new BufferBlock<Buffer>();
            var computeQ = new BufferBlock<Buffer>();
            var writeQ = new BufferBlock<Buffer>();

            // バッファは適当に4つ
            for(int i = 0; i < 4; ++i)
            {
                bufferQ.Post(new Buffer() { buffer = new byte[2 * 1024 * 1024] });
            }

            var sha512 = new SHA512Cng();

            using (var src = File.OpenRead(srcpath))
            using (var dst = File.Create(dstpath))
                await Task.WhenAll(
                    ReadFile(bufferQ, computeQ, src),
                    ComputeHash(computeQ, writeQ, sha512),
                    WriteFile(writeQ, bufferQ, dst));

            return sha512.Hash;
        }

        private static readonly byte[] HexCharToNum = new byte[0x100] {
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 255, 255, 255, 255, 255, 255,
            255, 10, 11, 12, 13, 14, 15, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 10, 11, 12, 13, 14, 15, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        };

        private static byte[] ReadHex(string str)
        {
            byte[] dst = new byte[str.Length / 2];
            for (int i = 0; i < dst.Length; i++)
            {
                byte c1 = HexCharToNum[str[2 * i]];
                byte c2 = HexCharToNum[str[2 * i + 1]];
                if (c1 == 0xFF || c2 == 0xFF) throw new FormatException("16進数ではありません");
                dst[i] = (byte)((c1 << 4) | c2);
            }
            return dst;
        }

        private static readonly char[] NumToHexChar = new char[0x10] {
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
        };

        private static string WriteHex(byte[] bin)
        {
            char[] str = new char[bin.Length * 2];
            for (int i = 0; i < bin.Length; i++)
            {
                str[2 * i] = NumToHexChar[bin[i] >> 4];
                str[2 * i + 1] = NumToHexChar[bin[i] & 0xF];
            }
            return new string(str);
        }

        public static Dictionary<string, byte[]> ReadHashFile(string path)
        {
            // 簡易版なのでハッシュチェックなし
            var dic = new Dictionary<string, byte[]>();
            string[] lines = File.ReadAllLines(path);
            for(int i = 0; i < lines.Length; ++i)
            {
                string line = lines[i];
                if(i + 1 == lines.Length && line.Length <= HASH_LENGTH * 2 + 2)
                {
                    // 正常終了
                    break;
                }
                if(line.Length <= HASH_LENGTH * 2 + 2)
                {
                    throw new IOException("ハッシュファイルが破損しています");
                }
                byte[] hash = ReadHex(line.Substring(0, HASH_LENGTH * 2));
                string name = line.Substring(HASH_LENGTH * 2 + 2);
                if(dic.ContainsKey(name) == false)
                {
                    dic.Add(name, hash);
                }
            }
            return dic;
        }

        public static void AppendHash(string path, string name, byte[] hash)
        {
            using(var fs = new StreamWriter(File.Open(path, FileMode.Append), Encoding.UTF8))
            {
                fs.WriteLine(WriteHex(hash) + "  " + name);
            }
        }
    }

    public class MultipleInstanceException : Exception { }

    public static class ServerSupport
    {
        public static readonly int DEFAULT_PORT = 32768;
        public static readonly string AUTO_PREFIX = "自動選択_";
        public static readonly string SUCCESS_DIR = "succeeded";
        public static readonly string FAIL_DIR = "failed";

        public static string GetDefaultProfileName()
        {
            return "デフォルト";
        }

        public static FileStream GetLock()
        {
            if (!Directory.Exists("data"))
            {
                Directory.CreateDirectory("data");
            }
            try
            {
                return new FileStream("data\\Server.lock",
                    FileMode.Create, FileAccess.ReadWrite, FileShare.None);
            }
            catch(Exception)
            {
                throw new MultipleInstanceException();
            }
        }

        private static string GetStandaloneMailslotName(string rootDir)
        {
            return @"\\.\mailslot\" + rootDir.Replace(':', '_') + @"\Amatsukaze";
        }

        public static FileStream CreateStandaloneMailslot()
        {
            var path = GetStandaloneMailslotName(Directory.GetCurrentDirectory());
            // -1: MAILSLOT_WAIT_FOREVER
            var handle = Lib.WinAPI.CreateMailslot(path, 0, -1, IntPtr.Zero);
            if(handle.IsInvalid)
            {
                throw new IOException("Failed to create mailslot");
            }
            return new FileStream(handle, FileAccess.Read, 1, true);
        }

        public static async Task WaitStandaloneMailslot(FileStream fs)
        {
            byte[] buf = new byte[1];
            // CreateMailslotの引数を間違えたりすると
            // ここがビジーウェイトになってしまうので注意
            while (await fs.ReadAsync(buf, 0, 1) == 0) ;
        }

        public static async Task TerminateStandalone(string rootDir)
        {
            while (true)
            {
                // FileStreamの引数にmailslot名を渡すとエラーになってしまうので
                // CreateFileを直接呼び出す
                var handle = Lib.WinAPI.CreateFile(GetStandaloneMailslotName(rootDir),
                    FileDesiredAccess.GenericWrite,
                    FileShareMode.FileShareRead | FileShareMode.FileShareWrite,
                    IntPtr.Zero, FileCreationDisposition.OpenExisting, 0, IntPtr.Zero);
                if (handle.IsInvalid)
                {
                    return;
                }
                using(var fs = new FileStream(handle, FileAccess.Write))
                {
                    byte[] buf = new byte[1] { 0 };
                    fs.Write(buf, 0, 1);
                }
                await Task.Delay(10 * 1000);
            }
        }

        public static void LaunchLocalServer(int port, string rootDir)
        {
            var exename = Path.GetDirectoryName(typeof(ServerSupport).Assembly.Location) + "\\" +
                (Environment.UserInteractive ? "AmatsukazeGUI.exe" : "AmatsukazeServerCLI.exe");
            var args = "-l server -p " + port;
            Process.Start(new ProcessStartInfo(exename, args)
            {
                WorkingDirectory = rootDir,
            });
        }

        public static bool IsLocalIP(string ip)
        {
            IPHostEntry iphostentry = Dns.GetHostEntry(Dns.GetHostName());
            try
            {
                IPAddress ipAddr = IPAddress.Parse(ip);
                if (Array.IndexOf(iphostentry.AddressList, ipAddr) != -1)
                {
                    return true;
                }
            }
            catch { }
            IPHostEntry other = null;
            try
            {
                other = Dns.GetHostEntry(ip);
            }
            catch
            {
                return false;
            }
            foreach (IPAddress addr in other.AddressList)
            {
                if (IPAddress.IsLoopback(addr) || Array.IndexOf(iphostentry.AddressList, addr) != -1)
                {
                    return true;
                }
            }
            return false;
        }

        private static FilterSetting DefaultFilterSetting()
        {
            return new FilterSetting()
            {
                KfmFps = FilterFPS.VFR,
                YadifFps = FilterFPS.CFR60,
                DeblockQuality = 3,
                DeblockStrength = DeblockStrength.Medium,
                ResizeWidth = 1280,
                ResizeHeight = 720,
                AutoVfrParallel = 4,
                AutoVfr30F = true,
                AutoVfr60F = true,
                AutoVfrSkip = 2
            };
        }

        public static ProfileSetting NormalizeProfile(ProfileSetting profile)
        {
            if(profile == null)
            {
                profile = new ProfileSetting()
                {
                    EncoderType = EncoderType.x264,
                    BitrateCM = 0.5,
                    OutputMask = 1,
                    DisableChapter = true, // デフォルトはチャプター解析無効
                    DisableSubs = true, // デフォルトは字幕無効
                    FilterSetting = DefaultFilterSetting()
                };
            }
            if (profile.Bitrate == null)
            {
                profile.Bitrate = new BitrateSetting();
            }
            if (profile.NicoJKFormats == null)
            {
                profile.NicoJKFormats = new bool[4] { true, false, false, false };
            }
            if (profile.ReqResources == null)
            {
                // 5個でいいけど予備を3つ置いておく
                profile.ReqResources = new ReqResource[8];
            }
            if (profile.FilterSetting == null)
            {
                // 互換性維持
                profile.FilterOption = FilterOption.Custom;
                profile.FilterSetting = DefaultFilterSetting();
            }
            if(profile.FilterSetting.AutoVfrParallel == 0)
            {
                // 初期値
                profile.FilterSetting.AutoVfrParallel = 4;
                profile.FilterSetting.AutoVfr30F = true;
                profile.FilterSetting.AutoVfr60F = true;
                profile.FilterSetting.AutoVfrSkip = 2;
            }
            if(profile.NumEncodeBufferFrames == 0)
            {
                profile.NumEncodeBufferFrames = 16;
            }
            return profile;
        }

        public static T DeepCopy<T>(T src)
        {
            var ms = new MemoryStream();
            var s = new DataContractSerializer(typeof(T));
            s.WriteObject(ms, src);
            return (T)s.ReadObject(new MemoryStream(ms.ToArray()));
        }

        public static IPAddress GetSubnetMask(IPAddress address)
        {
            foreach (NetworkInterface adapter in NetworkInterface.GetAllNetworkInterfaces())
            {
                foreach (UnicastIPAddressInformation unicastIPAddressInformation in adapter.GetIPProperties().UnicastAddresses)
                {
                    if (unicastIPAddressInformation.Address.AddressFamily == AddressFamily.InterNetwork)
                    {
                        if (address.Equals(unicastIPAddressInformation.Address))
                        {
                            return unicastIPAddressInformation.IPv4Mask;
                        }
                    }
                }
            }
            return null;
        }

        public static byte[] GetMacAddress(IPAddress address)
        {
            foreach (NetworkInterface adapter in NetworkInterface.GetAllNetworkInterfaces())
            {
                foreach (UnicastIPAddressInformation unicastIPAddressInformation in adapter.GetIPProperties().UnicastAddresses)
                {
                    if (unicastIPAddressInformation.Address.AddressFamily == AddressFamily.InterNetwork)
                    {
                        if (address.Equals(unicastIPAddressInformation.Address))
                        {
                            return adapter.GetPhysicalAddress().GetAddressBytes();
                        }
                    }
                }
            }
            return null;
        }

        public static IEnumerable<string> GetFileExtentions(string tsext, bool withEDCB)
        {
            if (tsext != null)
            {
                yield return tsext;
            }
            if (withEDCB)
            {
                yield return ".ts.err";
                yield return ".ts.program.txt";
            }
            yield return ".ts.trim.avs";
        }

        public class ErrorDescription
        {
            public int[] Count { get; private set; }

            public string[] Message = new string[]
            {
                "デコードエラー",
                "時間処理エラー",
                "DRCSマッピングのない文字",
                "その他のエラー",
            };

            public string[] DecodeErrors = new string[]
            {
               "decode-packet-failed",
               "h264-pts-mismatch",
               "h264-unexpected-field",
            };

            public string[] TimeErrors = new string[]
            {
               "unknown-pts",
               "non-continuous-pts",
            };

            public string[] DrcsErrors = new string[]
            {
                "no-drcs-map",
            };

            public ErrorDescription(List<ErrorCount> error)
            {
                var tmp = new int[]
                {
                    error.Where(s => Array.IndexOf(DecodeErrors, s.Name) >= 0).Sum(s => s.Count),
                    error.Where(s => Array.IndexOf(TimeErrors, s.Name) >= 0).Sum(s => s.Count),
                    error.Where(s => Array.IndexOf(DrcsErrors, s.Name) >= 0).Sum(s => s.Count)
                };
                Count = tmp.Concat(new int[] { error.Sum(s => s.Count) - tmp.Sum() }).ToArray();
            }

            public override string ToString()
            {
                if(Count.Sum() == 0)
                {
                    return "";
                }
                var messages = Message
                    .Zip(Count, (message, count) => new { Message = message, Count = count })
                    .Where(s => s.Count > 0)
                    .Select(s => s.Message + " " + s.Count + "件");
                return string.Join(",", messages);
            }
        }

        public static string ErrorCountToString(List<ErrorCount> error)
        {
            return new ErrorDescription(error).ToString();
        }

        private static bool MatchContentConditions(List<GenreItem> genre, List<GenreItem> conds)
        {
            // ジャンル情報がない場合はダメ
            if (genre.Count == 0) return false;

            // １つ目のジャンルだけ見る
            var content = genre[0];
            foreach (var cond in conds)
            {
                if (cond.Level1 == content.Level1)
                {
                    if (cond.Level2 == -1 || cond.Level2 == content.Level2)
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        private static VideoSizeCondition GetVideoSize(int width, int height)
        {
            if(width > 1440)
            {
                return VideoSizeCondition.FullHD;
            }
            if(width > 720)
            {
                return VideoSizeCondition.HD1440;
            }
            if(width > 320)
            {
                return VideoSizeCondition.SD;
            }
            return VideoSizeCondition.OneSeg;
        }

        public static string AutoSelectProfile(List<string> tags, string fileName, int width, int height,
            List<GenreItem> genre, int serviceId, AutoSelectProfile conds, out int priority)
        {
            var videoSize = GetVideoSize(width, height);
            foreach (var cond in conds.Conditions)
            {
                if(cond.TagEnabled)
                {
                    if (tags.Contains(cond.Tag) == false)
                    {
                        continue;
                    }
                }
                if(cond.FileNameEnabled)
                {
                    if(fileName.Contains(cond.FileName) == false)
                    {
                        continue;
                    }
                }
                if(cond.ContentConditionEnabled)
                {
                    if (MatchContentConditions(genre, cond.ContentConditions) == false)
                    {
                        continue;
                    }
                }
                if (cond.ServiceIdEnabled)
                {
                    if (cond.ServiceIds.Contains(serviceId) == false)
                    {
                        continue;
                    }
                }
                if (cond.VideoSizeEnabled)
                {
                    if (cond.VideoSizes.Contains(videoSize) == false)
                    {
                        continue;
                    }
                }
                // 全てにマッチした
                priority = cond.Priority;
                return cond.Profile;
            }
            // マッチしたのがなかった
            priority = 0;
            return null;
        }

        public static string AutoSelectProfile(QueueItem item, AutoSelectProfile conds, out int priority)
        {
            return AutoSelectProfile(item.Tags, Path.GetFileName(item.SrcPath), item.ImageWidth, item.ImageHeight,
                item.Genre, item.ServiceId, conds, out priority);
        }

        public static string ParseProfileName(string name, out bool autoSelect)
        {
            if (name.StartsWith(AUTO_PREFIX))
            {
                autoSelect = true;
                return name.Substring(AUTO_PREFIX.Length);
            }
            else
            {
                autoSelect = false;
                return name;
            }
        }

        public static GenreItem GetGenre(ContentNibbles nibbles)
        {
            var genre = new GenreItem()
            {
                Space = (int)GenreSpace.ARIB,
                Level1 = nibbles.Level1,
                Level2 = nibbles.Level2
            };
            if(nibbles.Level1 == 0xE)
            {
                // 拡張
                genre.Space = nibbles.Level2 + 1;
                genre.Level1 = nibbles.User1;
                genre.Level2 = nibbles.User2;
            }
            return genre;
        }

        public static string GetFileExtension(FormatType format)
        {
            switch(format)
            {
                case FormatType.MP4:
                    return ".mp4";
                case FormatType.MKV:
                    return ".mkv";
                case FormatType.M2TS:
                    return ".m2ts";
                case FormatType.TS:
                    return ".ts";
            }
            throw new ArgumentException();
        }

        public static void MoveFile(string srcPath, string dstPath)
        {
            if (File.Exists(dstPath))
            {
                // 既に存在している同名ファイルは削除
                File.Delete(dstPath);
            }
            File.Move(srcPath, dstPath);
        }

        public struct MoveFileItem
        {
            public string SrcPath;
            public string DstPath;
        }

        public static List<MoveFileItem> GetMoveList(string file, string dstDir, bool withEDCB)
        {
            string body = Path.GetFileNameWithoutExtension(file);
            string tsext = Path.GetExtension(file);
            string srcDir = Path.GetDirectoryName(file);
            return ServerSupport.GetFileExtentions(tsext, withEDCB).Select(ext => new MoveFileItem()
                {
                    SrcPath = srcDir + "\\" + body + ext,
                    DstPath = dstDir + "\\" + body + ext
                })
                .Where(pair => File.Exists(pair.SrcPath))
                .ToList();
        }

        public static void MoveTSFile(string file, string dstDir, bool withEDCB)
        {
            foreach(var item in GetMoveList(file, dstDir, withEDCB))
            {
                MoveFile(item.SrcPath, item.DstPath);
            }
        }

        public static void DeleteTSFile(string file, bool withEDCB)
        {
            string body = Path.GetFileNameWithoutExtension(file);
            string tsext = Path.GetExtension(file);
            string srcDir = Path.GetDirectoryName(file);
            foreach (var ext in ServerSupport.GetFileExtentions(tsext, withEDCB))
            {
                string srcPath = srcDir + "\\" + body + ext;
                if (File.Exists(srcPath))
                {
                    File.Delete(srcPath);
                }
            }
        }

        public static string ExitCodeString(int code)
        {
            if(Math.Abs(code) < 0x10000)
            {
                return code.ToString();
            }
            return "0x" + code.ToString("x");
        }
    }

    public static class TaskSupport
    {
        class TaskItem
        {
            public SendOrPostCallback d;
            public object state;
            public bool end;
        }

        class MySynchronizationContext : SynchronizationContext
        {
            BlockingCollection<TaskItem> queue = new BlockingCollection<TaskItem>();

            public override void Post(SendOrPostCallback d, object state)
            {
                queue.Add(new TaskItem() { d = d, state = state });
            }

            public void Finish()
            {
                queue.Add(new TaskItem() { end = true });
            }

            public void MessageLoop()
            {
                while (true)
                {
                    var item = queue.Take();
                    if (item.end) return;
                    item.d(item.state);
                }
            }
        }

        private static MySynchronizationContext _Context = new MySynchronizationContext();

        public static void SetSynchronizationContext()
        {
            SynchronizationContext.SetSynchronizationContext(_Context);
        }

        public static void EnterMessageLoop()
        {
            _Context.MessageLoop();
        }

        public static void Finish()
        {
            _Context.Finish();
        }
    }

    public class PipeCommunicator : IDisposable
    {
        public AnonymousPipeServerStream ReadPipe { get; private set; } // 読み取りパイプ
        public AnonymousPipeServerStream WritePipe { get; private set; } // 書き込みパイプ
        public string OutHandle { get; private set; } // 子プロセスの書き込みパイプ
        public string InHandle { get; private set; }  // 子プロセスの読み取りパイプ

        public PipeCommunicator()
        {
            ReadPipe = new AnonymousPipeServerStream(PipeDirection.In, HandleInheritability.Inheritable);
            WritePipe = new AnonymousPipeServerStream(PipeDirection.Out, HandleInheritability.Inheritable);
            OutHandle = ReadPipe.GetClientHandleAsString();
            InHandle = WritePipe.GetClientHandleAsString();
        }

        public void DisposeLocalCopyOfClientHandle()
        {
            ReadPipe.DisposeLocalCopyOfClientHandle();
            WritePipe.DisposeLocalCopyOfClientHandle();
        }

        #region IDisposable Support
        private bool disposedValue = false; // 重複する呼び出しを検出するには

        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                if (disposing)
                {
                    // TODO: マネージ状態を破棄します (マネージ オブジェクト)。
                    ReadPipe.Dispose();
                    WritePipe.Dispose();
                }

                // TODO: アンマネージ リソース (アンマネージ オブジェクト) を解放し、下のファイナライザーをオーバーライドします。
                // TODO: 大きなフィールドを null に設定します。

                disposedValue = true;
            }
        }

        // TODO: 上の Dispose(bool disposing) にアンマネージ リソースを解放するコードが含まれる場合にのみ、ファイナライザーをオーバーライドします。
        // ~PipeCommunicator() {
        //   // このコードを変更しないでください。クリーンアップ コードを上の Dispose(bool disposing) に記述します。
        //   Dispose(false);
        // }

        // このコードは、破棄可能なパターンを正しく実装できるように追加されました。
        public void Dispose()
        {
            // このコードを変更しないでください。クリーンアップ コードを上の Dispose(bool disposing) に記述します。
            Dispose(true);
            // TODO: 上のファイナライザーがオーバーライドされる場合は、次の行のコメントを解除してください。
            // GC.SuppressFinalize(this);
        }
        #endregion
    }

    public class AvsScriptCreator
    {
        private static string GetGPUString(D3DVPGPU gpu)
        {
            switch (gpu)
            {
                case D3DVPGPU.Auto:
                    return null;
                case D3DVPGPU.Intel:
                    return "Intel";
                case D3DVPGPU.NVIDIA:
                    return "NVIDIA";
                case D3DVPGPU.Radeon:
                    return "Radeon";
                default:
                    return null;
            }
        }

        private static string GetDeblockOption(DeblockStrength strength)
        {
            switch (strength)
            {
                case DeblockStrength.Strong:
                    return "thr=18";
                case DeblockStrength.Medium:
                    return "thr=28";
                case DeblockStrength.Weak:
                    return "str=-1.0,bratio=0,thr=38";
                case DeblockStrength.Weaker:
                default:
                    return "str=-1.5,bratio=0,thr=50";
            }
        }

        private static string GetQTGMCPreseet(QTGMCPreset preset)
        {
            switch(preset)
            {
                case QTGMCPreset.Faster:
                    return "Faster";
                case QTGMCPreset.Fast:
                    return "Fast";
                case QTGMCPreset.Medium:
                    return "Medium";
                case QTGMCPreset.Slow:
                    return "Slow";
                case QTGMCPreset.Slower:
                    return "Slower";
            }
            return null;
        }

        public static string FilterToString(FilterSetting filter, Setting setting)
        {
            StringBuilder sb = new StringBuilder();

            if (filter.EnableCUDA)
            {
                sb.AppendLine("SetDeviceOpt(DEV_CUDA_PINNED_HOST) # CUDAデータ転送最適化");
                sb.AppendLine("dsrc = AMT_SOURCE.OnCPU(2)");
            }
            else
            {
                sb.AppendLine("dsrc = AMT_SOURCE");
            }

            if (filter.EnableDeinterlace)
            {
                int videoOutPass = 0;
                if (filter.DeinterlaceAlgorithm == DeinterlaceAlgorithm.D3DVP)
                {
                    var device = GetGPUString(filter.D3dvpGpu);
                    sb.AppendLine("AMT_SOURCE.D3DVP(" + ((device != null) ? "device=\"" + device + "\"" : "") + ")");
                    if (filter.EnableCUDA)
                    {
                        sb.AppendLine("OnCPU(2)");
                    }
                }
                else if (filter.DeinterlaceAlgorithm == DeinterlaceAlgorithm.QTGMC)
                {
                    var preset = GetQTGMCPreseet(filter.QtgmcPreset);
                    if(preset != null)
                    {
                        preset = ", preset=\"" + preset + "\"";
                    }
                    sb.AppendLine("dsrc.KFMDeint(mode=1" + preset + ", ucf=false, nr=false" +
                        ", cuda=" + (filter.EnableCUDA ? "true" : "false") + ")");
                }
                else if (filter.DeinterlaceAlgorithm == DeinterlaceAlgorithm.KFM)
                {
                    if (filter.KfmFps == FilterFPS.VFR || filter.KfmFps == FilterFPS.VFR30)
                    {
                        // VFR
                        sb.AppendLine("pass = Select(AMT_PASS, 1, 2, 3)");
                        sb.AppendLine("AMT_PRE_PROC = (AMT_PASS < 2)");
                        videoOutPass = 2;
                    }
                    else
                    {
                        sb.AppendLine("pass = Select(AMT_PASS, 1, 3)");
                        sb.AppendLine("AMT_PRE_PROC = (AMT_PASS < 1)");
                        videoOutPass = 1;
                    }
                    sb.AppendLine("dsrc.KFMDeint(mode=" + ((filter.KfmFps == FilterFPS.CFR24) ? 2 : 4) +
                        ", pass=pass" +
                        ", ucf=" + (filter.KfmEnableUcf ? "true" : "false") +
                        ", nr=" + (filter.KfmEnableNr ? "true" : "false") +
                        ", svp=" + ((filter.KfmFps == FilterFPS.SVP) ? "true" : "false") +
                        ", thswitch=" + ((filter.KfmFps == FilterFPS.VFR30) ? "-1" : "3") +
                        ", cuda=" + (filter.EnableCUDA ? "true" : "false") +
                        ", is120=" + filter.KfmVfr120fps +
                        ", dev=AMT_DEV, filepath=AMT_TMP)");
                }
                else if(filter.DeinterlaceAlgorithm == DeinterlaceAlgorithm.Yadif)
                {
                    if (filter.YadifFps == FilterFPS.CFR24)
                    {
                        sb.AppendLine("AMT_SOURCE.Yadifmod2(mode=0).TDecimate(mode=1)");
                    }
                    else if (filter.YadifFps == FilterFPS.CFR30)
                    {
                        sb.AppendLine("AMT_SOURCE.Yadifmod2(mode=0)");
                    }
                    else // 60fps
                    {
                        sb.AppendLine("AMT_SOURCE.Yadifmod2(mode=1)");
                    }
                    if (filter.EnableCUDA)
                    {
                        sb.AppendLine("OnCPU(2)");
                    }
                }
                else
                {
                    // AutoVfr
                    int parallel = filter.AutoVfrParallel;
                    var fname = filter.AutoVfrFast ? "Auto_Vfr_Fast" : "Auto_Vfr";
                    var crop = filter.AutoVfrFast ? "" : ",IsCrop=" + (filter.AutoVfrCrop ? "true" : "false");
                    var concatarg = "C:\\Windows\\System32\\cmd.exe /c copy " +
                        string.Join("+", Enumerable.Range(1, parallel).Select(i => "$DQ\"+AMT_TMP+\".autovfr" + i + ".log$DQ")) +
                        " $DQ\"+AMT_TMP+\".autovfr.log$DQ ";
                    var autovfrarg = "$DQ" + setting.AutoVfrPath +
                        "$DQ -i $DQ\"+logp+\"$DQ -o $DQ\"+defp+\"$DQ" +
                        " -SKIP "+filter.AutoVfrSkip+
                        " -REF "+filter.AutoVfrRef+
                        " -30F "+(filter.AutoVfr30F ? "1" : "0")+
                        " -60F " + (filter.AutoVfr60F ? "1" : "0")+
                        " -24A "+(filter.AutoVfr24A ? "1" : "0")+
                        " -30A "+(filter.AutoVfr30A ? "1" : "0");

                    concatarg = concatarg.Replace("$DQ", "\"+Chr(34)+\"").Replace("+\"\"+", "+");
                    autovfrarg = autovfrarg.Replace("$DQ", "\"+Chr(34)+\"").Replace("+\"\"+", "+");

                    sb.AppendLine("Import(\"" + Path.GetDirectoryName(setting.AutoVfrPath) + "\\" + fname + ".avs\")");
                    sb.AppendLine("AMT_PRE_PROC = (AMT_PASS < 2)");
                    sb.AppendLine("logp = AMT_TMP + \".autovfr.log\"");
                    sb.AppendLine("defp = AMT_TMP + \".autovfr.def\"");
                    if (parallel <= 1)
                    {
                        sb.AppendLine("if(AMT_PASS == 0) { AMT_SOURCE." + fname + "(logp" + crop + ") }");
                        sb.AppendLine("if(AMT_PASS == 1) { AMT_SOURCE.AMTExec(\"" + autovfrarg + "\").Trim(0,-1) }");
                    }
                    else
                    {
                        sb.AppendLine("af = function[AMT_TMP](n){\n\tMakeSource()." + fname + "(AMT_TMP+\".autovfr\"+string(n)+\".log\"" + 
                            crop + ",cut=" + parallel + ",number=n)\n}");
                        sb.AppendLine("if(AMT_PASS == 0) {\n\tAMTOrderedParallel(" +
                            string.Join(",", Enumerable.Range(1, parallel).Select(i => "af(" + i + ")")) +
                            ")\n\tPrefetch(" + parallel + ")\n}");
                        sb.AppendLine("if(AMT_PASS == 1) {\n\tAMT_SOURCE\n\tAMTExec(\"" + concatarg + "\")\n\tAMTExec(\"" + autovfrarg + "\")\n\tTrim(0,-1)\n}");
                    }
                    sb.AppendLine("if(AMT_PASS == 2) { AMT_SOURCE.Its(defp,output=AMT_TMP+\".timecode.txt\") }");
                    videoOutPass = 2;
                    if (filter.EnableCUDA)
                    {
                        sb.AppendLine("OnCPU(2)");
                    }
                }
                sb.AppendLine("AssumeBFF()");
                if (videoOutPass != 0)
                {
                    // 解析フェーズはポストプロセスをスキップ
                    sb.AppendLine("if(AMT_PASS != " + videoOutPass + ") { return last }");
                }
            }
            else
            {
                sb.AppendLine("dsrc");
            }

            // ポストプロセス
            if (filter.EnableResize || filter.EnableTemporalNR || 
                filter.EnableDeband || filter.EnableEdgeLevel ||
                filter.EnableDeblock)
            {
                if (filter.EnableDeblock)
                {
                    // QPClipのOnCPUはGPUメモリ節約のため
                    // dsrcにQTGMCパスと25フレームくらいの時間差でアクセスすることになるので
                    // キャッシュを大量に消費してしまうので、CPU側に逃がす
                    sb.AppendLine("KDeblock(qpclip=dsrc.QPClip().OnCPU(2),quality=" +
                        filter.DeblockQuality + "," +
                        GetDeblockOption(filter.DeblockStrength) + ",sharp=" +
                        filter.DeblockSharpen + ")");
                }
                if (filter.EnableResize || filter.EnableTemporalNR ||
                    filter.EnableDeband || filter.EnableEdgeLevel)
                {
                    sb.AppendLine("ConvertBits(14)");
                    if (filter.EnableResize)
                    {
                        sb.AppendLine("BlackmanResize(" + filter.ResizeWidth + "," + filter.ResizeHeight + ")");
                    }
                    if (filter.EnableTemporalNR)
                    {
                        sb.AppendLine("KTemporalNR(3, 1)");
                    }
                    if (filter.EnableDeband)
                    {
                        sb.AppendLine("KDeband(25, 1, 2, true)");
                    }
                    if (filter.EnableEdgeLevel)
                    {
                        sb.AppendLine("KEdgeLevel(16, 10, 2)");
                    }
                    sb.AppendLine("ConvertBits(10, dither=0)");
                    sb.AppendLine("if(IsProcess(\"AvsPmod.exe\")) { ConvertBits(8, dither=0) }");
                }
                sb.AppendLine(filter.EnableCUDA ? "OnCUDA(2, AMT_DEV)" : "Prefetch(4)");
            }

            return sb.ToString();
        }

    }

    public class CachedAvsScript
    {
        private static MD5CryptoServiceProvider HashProvider = new MD5CryptoServiceProvider();

        private static uint GetHashCode(byte[] bytes)
        {
            byte[] hash = HashProvider.ComputeHash(bytes);
            return BitConverter.ToUInt32(hash, 0) ^
                BitConverter.ToUInt32(hash, 4) ^
                BitConverter.ToUInt32(hash, 8) ^
                BitConverter.ToUInt32(hash, 12);
        }

        private static string CodeToName(uint code, string cacheRoot)
        {
            return cacheRoot + "\\" + code.ToString("X8") + ".avs";
        }

        // settingのスクリプトファイルへのパスを返す
        //（同じスクリプトがあればそのパス、なければ作る）
        public static string GetAvsFilePath(FilterSetting filter, Setting setting, string cacheRoot)
        {
            var textBytes = Encoding.Default.GetBytes(AvsScriptCreator.FilterToString(filter, setting));
            var code = GetHashCode(textBytes);

            if(Directory.Exists(cacheRoot) == false)
            {
                Directory.CreateDirectory(cacheRoot);
            }

            // 同じファイルを探す
            for (uint newCode = code; ; ++newCode)
            {
                var fileName = CodeToName(newCode, cacheRoot);
                if (File.Exists(fileName))
                {
                    // ファイルがある
                    if(File.ReadAllBytes(fileName).SequenceEqual(textBytes))
                    {
                        // 同じファイルだった
                        return fileName;
                    }
                    // 中身が違った
                    continue;
                }
                break;
            }

            // ない場合は作って返す
            for (uint newCode = code; ; ++newCode)
            {
                var fileName = CodeToName(newCode, cacheRoot);
                if (File.Exists(fileName))
                {
                    // 同名ファイルがある
                    continue;
                }
                File.WriteAllBytes(fileName, textBytes);
                return fileName;
            }
        }
    }

    // ログ書き込みができなくても処理を続行させる
    public class LogWriter
    {
        private string fileName;
        private Task writeThread;
        private BufferBlock<byte[]> writeQ = new BufferBlock<byte[]>();

        public LogWriter(string fileName)
        {
            this.fileName = fileName;
            this.writeThread = Task.Run(() => { WriteThread(); });
        }

        private void WriteThread()
        {
            var fs = File.Create(fileName);
            Action forceClose = () =>
            {
                if (fs != null)
                {
                    try
                    {
                        fs.Close();
                    }
                    catch (Exception) { }
                    finally
                    {
                        fs = null;
                    }
                }
            };
            while(true)
            {
                var data = writeQ.Receive();
                if(data == null)
                {
                    break;
                }
                int retry = 0;
                while(true)
                {
                    if (fs != null)
                    {
                        try
                        {
                            fs.Write(data, 0, data.Length);
                            fs.Flush();
                            break;
                        }
                        catch (Exception) { }

                        forceClose();
                    }

                    if(retry++ >= 5)
                    {
                        // 十分再試行したら、諦める
                        while (writeQ.TryReceive(out data)) ;
                        break;
                    }

                    // 書き込みに失敗したら少し待ってやり直す
                    Thread.Sleep(3000);

                    try
                    {
                        fs = File.OpenWrite(fileName);
                    }
                    catch (Exception)
                    {
                        fs = null;
                    }
                }
            }
            forceClose();
        }

        public void Close()
        {
            writeQ.Post(null);
            writeThread.Wait();
        }

        public void Write(byte[] array, int offset, int length)
        {
            byte[] data = new byte[length];
            Array.Copy(array, 0, data, 0, length);
            writeQ.Post(data);
        }
    }

    public class FinishActionRunner
    {
        public FinishAction Action { get; private set; }
        public int Seconds { get; private set; }
        private Task Thread;

        private PowerState ActionPowerState {
            get {
                switch(Action)
                {
                    case FinishAction.Suspend:
                        return PowerState.Suspend;
                    case FinishAction.Hibernate:
                        return PowerState.Hibernate;
                }
                throw new NotSupportedException();
            }
        }

        public bool Canceled;

        public FinishActionRunner(FinishAction action, int seconds)
        {
            if(action == FinishAction.None)
            {
                throw new InvalidOperationException("ActionがNoneです");
            }
            Action = action;
            Seconds = seconds;
            Thread = Run();
        }

        private async Task Run()
        {
            await Task.Delay(Seconds * 1000);

            if (Canceled) return;

            if(Action == FinishAction.Shutdown)
            {
                WinAPI.AdjustToken();
                WinAPI.ExitWindowsEx(WinAPI.ExitWindows.EWX_POWEROFF, 0);
            }
            else
            {
                Application.SetSuspendState(ActionPowerState, false, false);
            }
        }
    }

    public static class Extentions
    {
        public static TValue GetOrDefault<TKey, TValue>(
            this IDictionary<TKey, TValue> self,
            TKey key,
            TValue defaultValue = default(TValue))
        {
            TValue value;
            return self.TryGetValue(key, out value) ? value : defaultValue;
        }

        public static T Clamp<T>(this T val, T min, T max) where T : IComparable<T>
        {
            if (val.CompareTo(min) < 0) return min;
            else if (val.CompareTo(max) > 0) return max;
            else return val;
        }
    }

    public class Profile
    {
        private Stopwatch sw = new Stopwatch();

        public Profile()
        {
            sw.Start();
        }

        public void PrintTime(string name)
        {
            System.Diagnostics.Debug.Print(sw.ElapsedMilliseconds + " ms: " + name);
            sw.Restart();
        }
    }
}
