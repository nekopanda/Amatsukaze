using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Media.Imaging;

namespace Amatsukaze.Server
{
    public interface IEncodeServer
    {
        // 操作系
        Task SetProfile(ProfileUpdate data);
        Task SetAutoSelect(AutoSelectUpdate data);
        Task AddQueue(AddQueueDirectory dir);
        Task ChangeItem(ChangeItemData data);
        Task PauseEncode(bool pause);

        Task SetCommonData(CommonData data);
        Task SetServiceSetting(ServiceSettingUpdate update);
        Task AddDrcsMap(DrcsImage drcsMap);
        Task EndServer();

        // 情報取得系
        Task RequestSetting();
        Task RequestQueue();
        Task RequestLog();
        Task RequestConsole();
        Task RequestLogFile(LogItem item);
        Task RequestState();
        Task RequestFreeSpace();

        Task RequestServiceSetting();
        Task RequestLogoData(string fileName);
        Task RequestDrcsImages();

        void Finish();
    }

    public interface IUserClient
    {
        Task OnQueueData(QueueData data);
        Task OnQueueUpdate(QueueUpdate update);
        Task OnLogData(LogData data);
        Task OnLogUpdate(LogItem newLog);
        Task OnConsole(ConsoleData str);
        Task OnConsoleUpdate(ConsoleUpdate str);
        Task OnLogFile(string str);

        Task OnCommonData(CommonData data);
        Task OnProfile(ProfileUpdate data);
        Task OnAutoSelect(AutoSelectUpdate data);
        Task OnServiceSetting(ServiceSettingUpdate update);
        Task OnLogoData(LogoData logoData);
        Task OnDrcsData(DrcsImageUpdate update);
        Task OnAddResult(string requestId);

        Task OnOperationResult(OperationResult result);
        void Finish();
    }

    public enum RPCMethodId
    {
        SetProfile = 100,
        SetAutoSelect,
        AddQueue,
        ChangeItem,
        PauseEncode,
        SetCommonData,
        SetServiceSetting,
        AddDrcsMap,
        EndServer,
        RequestSetting,
        RequestQueue,
        RequestLog,
        RequestConsole,
        RequestLogFile,
        RequestState,
        RequestFreeSpace,
        RequestServiceSetting,
        RequestLogoData,
        RequestDrcsImages,

        OnQueueData = 200,
        OnQueueUpdate,
        OnLogData,
        OnLogUpdate,
        OnConsole,
        OnConsoleUpdate,
        OnLogFile,
        OnCommonData,
        OnProfile,
        OnAutoSelect,
        OnServiceSetting,
        OnLogoData,
        OnDrcsData,
        OnAddResult,
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
            { RPCMethodId.SetProfile, typeof(ProfileUpdate) },
            { RPCMethodId.SetAutoSelect, typeof(AutoSelectUpdate) },
            { RPCMethodId.AddQueue, typeof(AddQueueDirectory) },
            { RPCMethodId.ChangeItem, typeof(ChangeItemData) },
            { RPCMethodId.PauseEncode, typeof(bool) },
            { RPCMethodId.SetCommonData, typeof(CommonData) },
            { RPCMethodId.SetServiceSetting, typeof(ServiceSettingUpdate) },
            { RPCMethodId.AddDrcsMap, typeof(DrcsImage) },
            { RPCMethodId.EndServer, null },
            { RPCMethodId.RequestSetting, null },
            { RPCMethodId.RequestQueue, null },
            { RPCMethodId.RequestLog, null },
            { RPCMethodId.RequestConsole, null },
            { RPCMethodId.RequestLogFile, typeof(LogItem) },
            { RPCMethodId.RequestState, null },
            { RPCMethodId.RequestFreeSpace, null },
            { RPCMethodId.RequestServiceSetting, null },
            { RPCMethodId.RequestLogoData, typeof(string) },
            { RPCMethodId.RequestDrcsImages, null },

            { RPCMethodId.OnQueueData, typeof(QueueData) },
            { RPCMethodId.OnQueueUpdate, typeof(QueueUpdate) },
            { RPCMethodId.OnLogData, typeof(LogData) },
            { RPCMethodId.OnLogUpdate, typeof(LogItem) },
            { RPCMethodId.OnConsole, typeof(ConsoleData) },
            { RPCMethodId.OnConsoleUpdate, typeof(ConsoleUpdate) },
            { RPCMethodId.OnLogFile, typeof(string) },
            { RPCMethodId.OnCommonData, typeof(CommonData) },
            { RPCMethodId.OnProfile, typeof(ProfileUpdate) },
            { RPCMethodId.OnAutoSelect, typeof(AutoSelectUpdate) },
            { RPCMethodId.OnServiceSetting, typeof(ServiceSettingUpdate) },
            { RPCMethodId.OnLogoData, typeof(LogoData) },
            { RPCMethodId.OnDrcsData, typeof(DrcsImageUpdate) },
            { RPCMethodId.OnAddResult, typeof(string) },
            { RPCMethodId.OnOperationResult, typeof(OperationResult) }
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

        private static byte[] CombineChunks(List<byte[]> arrays)
        {
            byte[] rv = new byte[arrays.Sum(a => a.Length) + arrays.Count * 4];
            int offset = 0;
            foreach (byte[] array in arrays)
            {
                System.Buffer.BlockCopy(
                    BitConverter.GetBytes((int)array.Length), 0, rv, offset, 4);
                offset += 4;
                System.Buffer.BlockCopy(array, 0, rv, offset, array.Length);
                offset += array.Length;
            }
            return rv;
        }

        private static List<MemoryStream> SplitChunks(byte[] bytes)
        {
            var ret = new List<MemoryStream>();
            for(int offset = 0; offset < bytes.Length; )
            {
                int sz = BitConverter.ToInt32(bytes, offset);
                offset += 4;
                ret.Add(new MemoryStream(bytes, offset, sz));
                offset += sz;
            }
            return ret;
        }

        private static List<BitmapFrame> GetImage(object obj)
        {
            if(obj is LogoData)
            {
                return new List<BitmapFrame> { ((LogoData)obj).Image };
            }
            if(obj is DrcsImage)
            {
                return new List<BitmapFrame> { ((DrcsImage)obj).Image };
            }
            if(obj is DrcsImageUpdate)
            {
                var update = (DrcsImageUpdate)obj;
                var ret = new List<BitmapFrame>();
                if(update.Image != null)
                {
                    ret.Add(update.Image.Image);
                }
                if (update.ImageList != null)
                {
                    foreach(var img in update.ImageList)
                    {
                        ret.Add(img.Image);
                    }
                }
                return ret;
            }
            return null;
        }

        private static Action<List<BitmapFrame>> ImageSetter(object obj)
        {
            if (obj is LogoData)
            {
                return image => { ((LogoData)obj).Image = image[0]; };
            }
            if (obj is DrcsImage)
            {
                return image => { ((DrcsImage)obj).Image = image[0]; };
            }
            if (obj is DrcsImageUpdate)
            {
                return images => {
                    var update = (DrcsImageUpdate)obj;
                    int idx = 0;
                    if (update.Image != null)
                    {
                        update.Image.Image = images[idx++];
                    }
                    if (update.ImageList != null)
                    {
                        foreach (var img in update.ImageList)
                        {
                            img.Image = images[idx++];
                        }
                    }
                };
            }
            return null;
        }

        public static byte[] Serialize(Type type, object obj)
        {
            var data = new List<byte[]>();
            {
                var ms = new MemoryStream();
                var serializer = new DataContractSerializer(type);
                serializer.WriteObject(ms, obj);
                data.Add(ms.ToArray());
            }
            // 画像だけ特別処理
            var image = GetImage(obj);
            if (image != null)
            {
                for(int i = 0; i < image.Count; ++i)
                {
                    if(image[i] != null)
                    {
                        var ms2 = new MemoryStream();
                        var encoder = new PngBitmapEncoder();
                        encoder.Frames.Add(image[i]);
                        encoder.Save(ms2);
                        data.Add(ms2.ToArray());
                    }
                    else
                    {
                        data.Add(new byte[0]);
                    }
                }
            }
            return CombineChunks(data);
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
            var objbyes = Serialize(type, obj);
            //Debug.Print("Send: " + System.Text.Encoding.UTF8.GetString(objbyes));
            return Combine(
                BitConverter.GetBytes((short)id),
                BitConverter.GetBytes(objbyes.Length),
                objbyes);
        }

        public static object Deserialize(Type type, byte[] bytes)
        {
            var data = SplitChunks(bytes);
            var arg = new DataContractSerializer(type).ReadObject(data[0]);
            // 画像だけ特別処理
            var setter = ImageSetter(arg);
            if (setter != null)
            {
                List<BitmapFrame> images = new List<BitmapFrame>();
                for(int i = 1; i < data.Count; ++i)
                {
                    if(data[i].Length == 0)
                    {
                        images.Add(null);
                    }
                    else
                    {
                        images.Add(BitmapFrame.Create(data[i]));
                    }
                }
                setter(images);
            }
            return arg;
        }

        public static async Task<RPCInfo> Deserialize(Stream ns)
        {
            var headerbytes = await ReadBytes(ns, HEADER_SIZE);
            var id = (RPCMethodId)BitConverter.ToInt16(headerbytes, 0);
            var csize = BitConverter.ToInt32(headerbytes, 2);
            //Debug.Print("Header: id=" + id + ", size=" + csize);
            object arg = null;
            if (csize > 0)
            {
                var data = await RPCTypes.ReadBytes(ns, csize);
                //Debug.Print("Received: " + System.Text.Encoding.UTF8.GetString(data));
                arg = Deserialize(RPCTypes.ArgumentTypes[id], data);
            }
            return new RPCInfo() { id = id, arg = arg };
        }

        private static async Task<byte[]> ReadBytes(Stream ns, int size)
        {
            byte[] bytes = new byte[size];
            int readBytes = 0;
            while (readBytes < size)
            {
                var ret = await ns.ReadAsync(
                       bytes, readBytes, size - readBytes);
                if(ret == 0)
                {
                    throw new EndOfStreamException();
                }
                readBytes += ret;
            }
            return bytes;
        }

        public static Task RefreshRequest(this IEncodeServer server)
        {
            return Task.WhenAll(
                    server.RequestSetting(),
                    server.RequestQueue(),
                    server.RequestLog(),
                    server.RequestConsole(),
                    server.RequestState(),
                    server.RequestFreeSpace(),
                    server.RequestServiceSetting(),
                    server.RequestDrcsImages());
        }
    }

    // スタンドアロンでサーバ・クライアントをエミュレーションするためのアダプタ
    public class ClientAdapter : IUserClient
    {
        private IUserClient client;

        private static T Copy<T>(T obj)
        {
            return (T)RPCTypes.Deserialize(typeof(T), RPCTypes.Serialize(typeof(T), obj));
        }

        public ClientAdapter(IUserClient client)
        {
            this.client = client;
        }

        public void Finish()
        {
            client.Finish();
        }

        public Task OnConsole(ConsoleData str)
        {
            return client.OnConsole(Copy(str));
        }

        public Task OnConsoleUpdate(ConsoleUpdate str)
        {
            return client.OnConsoleUpdate(Copy(str));
        }

        public Task OnLogData(LogData data)
        {
            return client.OnLogData(Copy(data));
        }

        public Task OnLogFile(string str)
        {
            return client.OnLogFile((string)Copy(str));
        }

        public Task OnLogoData(LogoData logoData)
        {
            return client.OnLogoData(Copy(logoData));
        }

        public Task OnLogUpdate(LogItem newLog)
        {
            return client.OnLogUpdate(Copy(newLog));
        }

        public Task OnOperationResult(OperationResult result)
        {
            return client.OnOperationResult(Copy(result));
        }

        public Task OnQueueData(QueueData data)
        {
            return client.OnQueueData(Copy(data));
        }

        public Task OnQueueUpdate(QueueUpdate update)
        {
            return client.OnQueueUpdate(Copy(update));
        }

        public Task OnServiceSetting(ServiceSettingUpdate service)
        {
            return client.OnServiceSetting(Copy(service));
        }

        public Task OnCommonData(CommonData data)
        {
            return client.OnCommonData(Copy(data));
        }

        public Task OnDrcsData(DrcsImageUpdate update)
        {
            return client.OnDrcsData(Copy(update));
        }

        public Task OnAddResult(string requestId)
        {
            return client.OnAddResult(Copy(requestId));
        }

        public Task OnProfile(ProfileUpdate data)
        {
            return client.OnProfile(Copy(data));
        }

        public Task OnAutoSelect(AutoSelectUpdate data)
        {
            return client.OnAutoSelect(Copy(data));
        }
    }

    public class ServerAdapter : IEncodeServer
    {
        public EncodeServer Server { get; private set; }

        private static T Copy<T>(T obj)
        {
            return (T)RPCTypes.Deserialize(typeof(T), RPCTypes.Serialize(typeof(T), obj));
        }

        public ServerAdapter(EncodeServer server)
        {
            this.Server = server;
        }

        public Task AddDrcsMap(DrcsImage drcsMap)
        {
            return Server.AddDrcsMap(Copy(drcsMap));
        }

        public Task AddQueue(AddQueueDirectory dir)
        {
            return Server.AddQueue(Copy(dir));
        }

        public Task ChangeItem(ChangeItemData data)
        {
            return Server.ChangeItem(Copy(data));
        }

        public Task EndServer()
        {
            return Server.EndServer();
        }

        public void Finish()
        {
            Server.Finish();
        }

        public Task PauseEncode(bool pause)
        {
            return Server.PauseEncode(Copy(pause));
        }

        public Task RequestConsole()
        {
            return Server.RequestConsole();
        }

        public Task RequestDrcsImages()
        {
            return Server.RequestDrcsImages();
        }

        public Task RequestFreeSpace()
        {
            return Server.RequestFreeSpace();
        }

        public Task RequestLog()
        {
            return Server.RequestLog();
        }

        public Task RequestLogFile(LogItem item)
        {
            return Server.RequestLogFile(Copy(item));
        }

        public Task RequestLogoData(string fileName)
        {
            return Server.RequestLogoData(Copy(fileName));
        }

        public Task RequestQueue()
        {
            return Server.RequestQueue();
        }

        public Task RequestServiceSetting()
        {
            return Server.RequestServiceSetting();
        }

        public Task RequestSetting()
        {
            return Server.RequestSetting();
        }

        public Task RequestState()
        {
            return Server.RequestState();
        }

        public Task SetProfile(ProfileUpdate data)
        {
            return Server.SetProfile(Copy(data));
        }

        public Task SetAutoSelect(AutoSelectUpdate data)
        {
            return Server.SetAutoSelect(Copy(data));
        }

        public Task SetServiceSetting(ServiceSettingUpdate update)
        {
            return Server.SetServiceSetting(Copy(update));
        }

        public Task SetCommonData(CommonData data)
        {
            return Server.SetCommonData(Copy(data));
        }
    }
}
