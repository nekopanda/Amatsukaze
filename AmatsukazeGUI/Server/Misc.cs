using Livet;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    public enum LaunchType {
        Standalone,
        Server,
        Client,
        Debug
    };

    public class GUIOPtion
    {
        public LaunchType LaunchType = LaunchType.Standalone;
        public int ServerPort = 32768;

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
                    else {
                        LaunchType = LaunchType.Client;
                    }
                }
            }
        }
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

        [StructLayout(LayoutKind.Sequential)]
        public struct PROCESSORCORE
        {
            public byte Flags;
        };

        [StructLayout(LayoutKind.Sequential)]
        public struct NUMANODE
        {
            public uint NodeNumber;
        }

        public enum PROCESSOR_CACHE_TYPE
        {
            CacheUnified,
            CacheInstruction,
            CacheData,
            CacheTrace
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct CACHE_DESCRIPTOR
        {
            public byte Level;
            public byte Associativity;
            public ushort LineSize;
            public uint Size;
            public PROCESSOR_CACHE_TYPE Type;
        }

        [StructLayout(LayoutKind.Explicit)]
        public struct SYSTEM_LOGICAL_PROCESSOR_INFORMATION_UNION
        {
            [FieldOffset(0)]
            public PROCESSORCORE ProcessorCore;
            [FieldOffset(0)]
            public NUMANODE NumaNode;
            [FieldOffset(0)]
            public CACHE_DESCRIPTOR Cache;
            [FieldOffset(0)]
            private UInt64 Reserved1;
            [FieldOffset(8)]
            private UInt64 Reserved2;
        }

        public enum LOGICAL_PROCESSOR_RELATIONSHIP
        {
            RelationProcessorCore,
            RelationNumaNode,
            RelationCache,
            RelationProcessorPackage,
            RelationGroup,
            RelationAll = 0xffff
        }

        public struct SYSTEM_LOGICAL_PROCESSOR_INFORMATION
        {
            public UIntPtr ProcessorMask;
            public LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
            public SYSTEM_LOGICAL_PROCESSOR_INFORMATION_UNION ProcessorInformation;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool GetLogicalProcessorInformation(
            IntPtr Buffer,
            ref uint ReturnLength
        );

        private const int ERROR_INSUFFICIENT_BUFFER = 122;

        public static SYSTEM_LOGICAL_PROCESSOR_INFORMATION[] GetLogicalProcessorInformationMarshal()
        {
            uint ReturnLength = 0;
            GetLogicalProcessorInformation(IntPtr.Zero, ref ReturnLength);
            if (Marshal.GetLastWin32Error() != ERROR_INSUFFICIENT_BUFFER)
            {
                throw new Exception("GetLogicalProcessorInformationがERROR_INSUFFICIENT_BUFFERを返さなかった");
            }
            IntPtr Ptr = Marshal.AllocHGlobal((int)ReturnLength);
            try
            {
                if (GetLogicalProcessorInformation(Ptr, ref ReturnLength) == false)
                {
                    throw new Exception("GetLogicalProcessorInformationに失敗");
                }
                int size = Marshal.SizeOf(typeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
                int len = (int)ReturnLength / size;
                SYSTEM_LOGICAL_PROCESSOR_INFORMATION[] Buffer = new SYSTEM_LOGICAL_PROCESSOR_INFORMATION[len];
                IntPtr Item = Ptr;
                for (int i = 0; i < len; i++)
                {
                    Buffer[i] = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION)Marshal.PtrToStructure(Item, typeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
                    Item += size;
                }
                return Buffer;
            }
            finally
            {
                Marshal.FreeHGlobal(Ptr);
            }
        }
        /*
        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool GetProcessAffinityMask(
            IntPtr hProcess,
            ref ulong lpProcessAffinityMask,
            ref ulong lpSystemAffinityMask
        );

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool SetProcessAffinityMask(
            IntPtr hProcess,
            ulong dwProcessAffinityMask
        );
         * */
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
        private List<long> ordered = new List<long>();

        public int NumProcess { get; set; }

        public AffinityCreator()
        {
            // 下位ビットから cpu コア L2 L3 L4 Numa
            long[] cpulist = new long[64];

            Util.AddLog(" CPU構成を検出");
            var procInfo = Util.GetLogicalProcessorInformationMarshal();
            foreach (var info in procInfo)
            {
                int gid = BitScan(info.ProcessorMask.ToUInt64());
                int shift = -1;
                switch (info.Relationship)
                {
                    case Util.LOGICAL_PROCESSOR_RELATIONSHIP.RelationNumaNode:
                        Util.AddLog(" NUMA: " + info.ProcessorMask);
                        shift = 40;
                        break;
                    case Util.LOGICAL_PROCESSOR_RELATIONSHIP.RelationCache:
                        if(info.ProcessorInformation.Cache.Type == Util.PROCESSOR_CACHE_TYPE.CacheUnified)
                        {
                            switch (info.ProcessorInformation.Cache.Level)
                            {
                                case 2:
                                    Util.AddLog(" L2: " + info.ProcessorMask);
                                    shift = 16;
                                    break;
                                case 3:
                                    Util.AddLog(" L3: " + info.ProcessorMask);
                                    shift = 24;
                                    break;
                                case 4:
                                    Util.AddLog(" L4: " + info.ProcessorMask);
                                    shift = 32;
                                    break;
                            }
                        }
                        break;
                    case Util.LOGICAL_PROCESSOR_RELATIONSHIP.RelationProcessorCore:
                        Util.AddLog(" Core: " + info.ProcessorMask);
                        shift = 8;
                        break;
                }
                if (shift > 0)
                {
                    long tmp = (long)gid << shift;
                    long rmask = ~(0xFFL << shift);
                    ulong mask = info.ProcessorMask.ToUInt64();
                    for (int i = 0; i < 64; ++i)
                    {
                        if ((mask & (1ul << i)) != 0)
                        {
                            cpulist[i] = (cpulist[i] & rmask) | tmp;
                        }
                    }
                }
            }

            ulong avail = (ulong)Process.GetCurrentProcess().ProcessorAffinity.ToInt64();
            ordered.Clear();
            for (int i = 0; i < 64; ++i)
            {
                if ((avail & (1ul << i)) != 0)
                {
                    ordered.Add(cpulist[i] | (long)i);
                }
            }
            ordered.Sort();
        }

        public ulong GetMask(int pid)
        {
            if(pid >= ordered.Count)
            {
                throw new IndexOutOfRangeException();
            }
            int per = ordered.Count / NumProcess;
            int rem = ordered.Count - (per * NumProcess);
            int from = per * pid + Math.Min(pid, rem);
            int len = (pid < rem) ? per + 1 : per;
            ulong mask = 0;
            for (int i = from; i < from + len; ++i)
            {
                int cpuid = (int)ordered[i] & 0xFF;
                mask |= 1ul << cpuid;
            }
            return mask;
        }

        public static List<int> MaskToList(ulong bitmask)
        {
            var list = new List<int>();
            for (int i = 0; i < 64; ++i)
            {
                if ((bitmask & (1ul << i)) != 0)
                {
                    list.Add(i);
                }
            }
            return list;
        }

        public static ulong ListToMask(List<int> list)
        {
            ulong mask = 0;
            foreach (var i in list)
            {
                mask |= 1ul << i;
            }
            return mask;
        }

        private static int BitScan(ulong bitmask)
        {
            for (int i = 0; i < 64; ++i)
            {
                if ((bitmask & (1ul << i)) != 0)
                {
                    return i;
                }
            }
            return -1;
        }
    }
}
