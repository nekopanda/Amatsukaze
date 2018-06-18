using Amatsukaze.Lib;
using Livet;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
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
            Util.AddLog(str);
        }
    }

    public static class Util
    {
        public static List<Action<string>> LogHandlers = new List<Action<string>>();

        public static void AddLog(int parallelId, string log)
        {
            AddLog("[" + parallelId + "] " + log);
        }

        public static void AddLog(string log)
        {
            foreach (var handler in LogHandlers)
            {
                handler(log);
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
                AddLog(e.Message);
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
            var suffix = "-";
            while(n > 0)
            {
                suffix += (char)('A' + (n % 27) - 1);
                n /= 27;
            }
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
                if (buf[i] == '\n' || buf[i] == '\r')
                {
                    if (rawtext.Count > 0)
                    {
                        OutLine();
                    }
                    isCR = (buf[i] == '\r');
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

        private static async Task ReadFile(BufferBlock<Buffer> bufferQ, BufferBlock<Buffer> computeQ, FileStream src)
        {
            while (true)
            {
                var buf = await bufferQ.ReceiveAsync();
                buf.size = await src.ReadAsync(buf.buffer, 0, buf.buffer.Length);
                if(buf.size == 0)
                {
                    computeQ.Complete();
                    break;
                }
                computeQ.Post(buf);
            }
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

        private static async Task WriteFile(BufferBlock<Buffer> writeQ, BufferBlock<Buffer> bufferQ, FileStream dst)
        {
            while (await writeQ.OutputAvailableAsync())
            {
                var buf = await writeQ.ReceiveAsync();
                await dst.WriteAsync(buf.buffer, 0, buf.size);
                bufferQ.Post(buf);
            }
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
        static ServerSupport()
        {
            Directory.CreateDirectory("data");
        }

        public static readonly int DEFAULT_PORT = 32768;
        public static readonly string AUTO_PREFIX = "自動選択_";
        public static readonly string SUCCESS_DIR = "succeeded";
        public static readonly string FAIL_DIR = "failed";

        public static string GetServerLogPath()
        {
            return "data\\Server.log";
        }

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

        public static void LaunchLocalServer(int port)
        {
            var exename = Path.GetDirectoryName(typeof(ServerSupport).Assembly.Location) + "\\" +
                (Environment.UserInteractive ? "AmatsukazeGUI.exe" : "AmatsukazeServerCLI.exe");
            var args = "-l server -p " + port;
            Process.Start(new ProcessStartInfo(exename, args)
            {
                WorkingDirectory = Directory.GetCurrentDirectory(),
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

        public static ProfileSetting GetDefaultProfile()
        {
            return new ProfileSetting()
            {
                EncoderType = EncoderType.x264,
                Bitrate = new BitrateSetting(),
                BitrateCM = 0.5,
                OutputMask = 1,
                NicoJKFormats = new bool[4] { true, false, false, false }
            };
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

        public static string AutoSelectProfile(string fileName, int width, int height,
            List<GenreItem> genre, int serviceId, AutoSelectProfile conds, out int priority)
        {
            var videoSize = GetVideoSize(width, height);
            foreach (var cond in conds.Conditions)
            {
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
            return AutoSelectProfile(Path.GetFileName(item.SrcPath), item.ImageWidth, item.ImageHeight,
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
                Space = GenreSpace.ARIB,
                Level1 = nibbles.Level1,
                Level2 = nibbles.Level2
            };
            if(nibbles.Level1 == 0xE)
            {
                // 拡張
                genre.Space = (GenreSpace)(nibbles.Level2 + 1);
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
            }
            throw new ArgumentException();
        }

        public static void MoveTSFile(string file, string dstDir, bool withEDCB)
        {
            string body = Path.GetFileNameWithoutExtension(file);
            string tsext = Path.GetExtension(file);
            string srcDir = Path.GetDirectoryName(file);
            foreach (var ext in ServerSupport.GetFileExtentions(tsext, withEDCB))
            {
                string srcPath = srcDir + "\\" + body + ext;
                string dstPath = dstDir + "\\" + body + ext;
                if (File.Exists(srcPath))
                {
                    if (File.Exists(dstPath))
                    {
                        // 既に存在している同名ファイルは削除
                        File.Delete(dstPath);
                    }
                    File.Move(srcPath, dstPath);
                }
            }
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
