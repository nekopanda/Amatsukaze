using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    public interface IEncodeServer
    {
        // 操作系
        Task SetSetting(Setting setting);
        Task AddQueue(string dirPath);
        Task RemoveQueue(string dirPath);
        Task PauseEncode(bool pause);

        // 情報取得系
        Task RequestSetting();
        Task RequestQueue();
        Task RequestLog();
        Task RequestConsole();
        Task RequestLogFile(LogItem item);
        Task RequestState();

        void Finish();
    }

    public interface IUserClient
    {
        Task OnSetting(Setting setting);
        Task OnQueueData(QueueData data);
        Task OnQueueUpdate(QueueUpdate update);
        Task OnLogData(LogData data);
        Task OnLogUpdate(LogItem newLog);
        Task OnConsole(List<string> str);
        Task OnConsoleUpdate(byte[] str);
        Task OnLogFile(string str);
        Task OnState(State state);
        Task OnOperationResult(string result);
        void Finish();
    }

    public enum RPCMethodId
    {
        SetSetting = 100,
        AddQueue,
        RemoveQueue,
        PauseEncode,
        RequestSetting,
        RequestQueue,
        RequestLog,
        RequestConsole,
        RequestLogFile,
        RequestState,

        OnSetting = 200,
        OnQueueData,
        OnQueueUpdate,
        OnLogData,
        OnLogUpdate,
        OnConsole,
        OnConsoleUpdate,
        OnLogFile,
        OnState,
        OnOperationResult,
    }

    public struct RPCInfo
    {
        public RPCMethodId id;
        public object arg;
    }

    public static class RPCTypes
    {
        public static readonly Dictionary<RPCMethodId, Type> ArgumentTypes = new Dictionary<RPCMethodId, Type>() {
            { RPCMethodId.SetSetting, typeof(Setting) },
            { RPCMethodId.AddQueue, typeof(string) },
            { RPCMethodId.RemoveQueue, typeof(string) },
            { RPCMethodId.PauseEncode, typeof(bool) },
            { RPCMethodId.RequestSetting, null },
            { RPCMethodId.RequestQueue, null },
            { RPCMethodId.RequestLog, null },
            { RPCMethodId.RequestConsole, null },
            { RPCMethodId.RequestLogFile, typeof(LogItem) },
            { RPCMethodId.RequestState, null },

            { RPCMethodId.OnSetting, typeof(Setting) },
            { RPCMethodId.OnQueueData, typeof(QueueData) },
            { RPCMethodId.OnQueueUpdate, typeof(QueueUpdate) },
            { RPCMethodId.OnLogData, typeof(LogData) },
            { RPCMethodId.OnLogUpdate, typeof(LogItem) },
            { RPCMethodId.OnConsole, typeof(List<string>) },
            { RPCMethodId.OnConsoleUpdate, typeof(byte[]) },
            { RPCMethodId.OnLogFile, typeof(string) },
            { RPCMethodId.OnState, typeof(State) },
            { RPCMethodId.OnOperationResult, typeof(string) }
        };

        public static readonly int HEADER_SIZE = 6;

        private static byte[] Combine(params byte[][] arrays)
        {
            byte[] rv = new byte[arrays.Sum(a => a.Length)];
            int offset = 0;
            foreach (byte[] array in arrays)
            {
                System.Buffer.BlockCopy(array, 0, rv, offset, array.Length);
                offset += array.Length;
            }
            return rv;
        }

        public static byte[] Serialize(RPCMethodId id, object obj)
        {
            Type type = ArgumentTypes[id];
            if (type == null)
            {
                return Combine(
                    BitConverter.GetBytes((short)id),
                    BitConverter.GetBytes((int)0));
            }
            var ms = new MemoryStream();
            var serializer = new DataContractSerializer(type);
            serializer.WriteObject(ms, obj);
            var objbyes = ms.ToArray();
            Debug.Print("Send: " + System.Text.Encoding.UTF8.GetString(objbyes));
            return Combine(
                BitConverter.GetBytes((short)id),
                BitConverter.GetBytes(objbyes.Length),
                objbyes);
        }

        public static async Task<RPCInfo> Deserialize(NetworkStream ns)
        {
            var headerbytes = await ReadBytes(ns, HEADER_SIZE);
            var id = (RPCMethodId)BitConverter.ToInt16(headerbytes, 0);
            var csize = BitConverter.ToInt32(headerbytes, 2);
            object arg = null;
            if (csize > 0)
            {
                var data = await RPCTypes.ReadBytes(ns, csize);
                Debug.Print("Received: " + System.Text.Encoding.UTF8.GetString(data));
                var argType = RPCTypes.ArgumentTypes[id];
                var s = new DataContractSerializer(argType);
                var ms = new MemoryStream(data);
                arg = s.ReadObject(ms);
            }
            return new RPCInfo() { id = id, arg = arg };
        }

        private static async Task<byte[]> ReadBytes(NetworkStream ns, int size)
        {
            byte[] bytes = new byte[size];
            int readBytes = 0;
            while (readBytes < size)
            {
                readBytes += await ns.ReadAsync(
                    bytes, readBytes, size - readBytes);
            }
            return bytes;
        }
    }
}
