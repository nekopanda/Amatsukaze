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
    public interface IAddTaskServer
    {
        Task AddQueue(AddQueueRequest dir);

        // スタブ用接続終了
        void Finish();
    }

    public interface IEncodeServer : IAddTaskServer
    {
        // 操作系
        Task SetProfile(ProfileUpdate data);
        Task SetAutoSelect(AutoSelectUpdate data);
        Task ChangeItem(ChangeItemData data);
        Task PauseEncode(bool pause);
        Task CancelAddQueue();
        Task CancelSleep();

        Task SetCommonData(CommonData data);
        Task SetServiceSetting(ServiceSettingUpdate update);
        Task AddDrcsMap(DrcsImage drcsMap);
        Task EndServer();

        // 情報取得系
        Task Request(ServerRequest req);
        Task RequestLogFile(LogFileRequest req);
        Task RequestLogoData(string fileName);
        Task RequestDrcsImages();
    }

    public interface IAddTaskClient
    {
        Task OnAddResult(string requestId);
        Task OnOperationResult(OperationResult result);

        // スタンドアロン用終了
        void Finish();
    }

    public interface IUserClient : IAddTaskClient
    {
        Task OnUIData(UIData data);
        Task OnConsoleUpdate(ConsoleUpdate str);
        Task OnEncodeState(EncodeState state);
        Task OnLogFile(string str);

        Task OnCommonData(CommonData data);
        Task OnProfile(ProfileUpdate data);
        Task OnAutoSelect(AutoSelectUpdate data);
        Task OnServiceSetting(ServiceSettingUpdate update);
        Task OnLogoData(LogoData logoData);
        Task OnDrcsData(DrcsImageUpdate update);
    }

    // サーバとクライアント両方で扱うのでその共通インターフェース
    public interface ISleepCancel
    {
        FinishSetting SleepCancel { get; set; }
        Task CancelSleep();
    }

    public interface ICommandHost
    {
        string AddTag(string tag);
        string OutInfo(string options);
    }

    public enum RPCMethodId
    {
        SetProfile = 100,
        SetAutoSelect,
        AddQueue,
        ChangeItem,
        PauseEncode,
        CancelAddQueue,
        CancelSleep,
        SetCommonData,
        SetServiceSetting,
        AddDrcsMap,
        EndServer,
        Request,
        RequestLogFile,
        RequestLogoData,
        RequestDrcsImages,

        OnUIData = 200,
        OnConsoleUpdate,
        OnEncodeState,
        OnLogFile,
        OnCommonData,
        OnProfile,
        OnAutoSelect,
        OnServiceSetting,
        OnLogoData,
        OnDrcsData,
        OnAddResult,
        OnOperationResult,

        AddTag = 300,
        SetOutDir,
        SetPriority,
        GetOutFiles,
        CancelItem
    }

    public struct RPCInfo
    {
        public RPCMethodId id;
        public object arg;
    }

    public static class RPCData
    {
        public static readonly int HEADER_SIZE = 6;

        public static byte[] Combine(params byte[][] arrays)
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

        public static byte[] CombineChunks(List<byte[]> arrays)
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

        public static List<MemoryStream> SplitChunks(byte[] bytes)
        {
            var ret = new List<MemoryStream>();
            for (int offset = 0; offset < bytes.Length;)
            {
                int sz = BitConverter.ToInt32(bytes, offset);
                offset += 4;
                ret.Add(new MemoryStream(bytes, offset, sz));
                offset += sz;
            }
            return ret;
        }

        public static async Task<byte[]> ReadBytes(Stream ns, int size)
        {
            byte[] bytes = new byte[size];
            int readBytes = 0;
            while (readBytes < size)
            {
                var ret = await ns.ReadAsync(
                       bytes, readBytes, size - readBytes);
                if (ret == 0)
                {
                    throw new EndOfStreamException();
                }
                readBytes += ret;
            }
            return bytes;
        }
    }

    public static class RPCTypes
    {
        public static readonly Dictionary<RPCMethodId, Type> ArgumentTypes = new Dictionary<RPCMethodId, Type>() {
            { RPCMethodId.SetProfile, typeof(ProfileUpdate) },
            { RPCMethodId.SetAutoSelect, typeof(AutoSelectUpdate) },
            { RPCMethodId.AddQueue, typeof(AddQueueRequest) },
            { RPCMethodId.ChangeItem, typeof(ChangeItemData) },
            { RPCMethodId.PauseEncode, typeof(bool) },
            { RPCMethodId.CancelAddQueue, null },
            { RPCMethodId.CancelSleep, null },
            { RPCMethodId.SetCommonData, typeof(CommonData) },
            { RPCMethodId.SetServiceSetting, typeof(ServiceSettingUpdate) },
            { RPCMethodId.AddDrcsMap, typeof(DrcsImage) },
            { RPCMethodId.EndServer, null },
            { RPCMethodId.Request, typeof(ServerRequest) },
            { RPCMethodId.RequestLogFile, typeof(LogFileRequest) },
            { RPCMethodId.RequestLogoData, typeof(string) },
            { RPCMethodId.RequestDrcsImages, null },

            { RPCMethodId.OnUIData, typeof(UIData) },
            { RPCMethodId.OnConsoleUpdate, typeof(ConsoleUpdate) },
            { RPCMethodId.OnEncodeState, typeof(EncodeState) },
            { RPCMethodId.OnLogFile, typeof(string) },
            { RPCMethodId.OnCommonData, typeof(CommonData) },
            { RPCMethodId.OnProfile, typeof(ProfileUpdate) },
            { RPCMethodId.OnAutoSelect, typeof(AutoSelectUpdate) },
            { RPCMethodId.OnServiceSetting, typeof(ServiceSettingUpdate) },
            { RPCMethodId.OnLogoData, typeof(LogoData) },
            { RPCMethodId.OnDrcsData, typeof(DrcsImageUpdate) },
            { RPCMethodId.OnAddResult, typeof(string) },
            { RPCMethodId.OnOperationResult, typeof(OperationResult) },

            { RPCMethodId.AddTag, typeof(string) },
            { RPCMethodId.SetOutDir, typeof(string) },
            { RPCMethodId.SetPriority, typeof(string) },
            { RPCMethodId.GetOutFiles, typeof(string) },
            { RPCMethodId.CancelItem, typeof(string) }
        };

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
            return RPCData.CombineChunks(data);
        }

        public static byte[] Serialize(RPCMethodId id, object obj)
        {
            Type type = ArgumentTypes[id];
            if (type == null)
            {
                return RPCData.Combine(
                    BitConverter.GetBytes((short)id),
                    BitConverter.GetBytes((int)0));
            }
            var objbyes = Serialize(type, obj);
            //Debug.Print("Send: " + System.Text.Encoding.UTF8.GetString(objbyes));
            return RPCData.Combine(
                BitConverter.GetBytes((short)id),
                BitConverter.GetBytes(objbyes.Length),
                objbyes);
        }

        public static object Deserialize(Type type, byte[] bytes)
        {
            var data = RPCData.SplitChunks(bytes);
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
            var headerbytes = await RPCData.ReadBytes(ns, RPCData.HEADER_SIZE);
            var id = (RPCMethodId)BitConverter.ToInt16(headerbytes, 0);
            var csize = BitConverter.ToInt32(headerbytes, 2);
            //Debug.Print("Header: id=" + id + ", size=" + csize);
            object arg = null;
            if (csize > 0)
            {
                var data = await RPCData.ReadBytes(ns, csize);
                //Debug.Print("Received: " + System.Text.Encoding.UTF8.GetString(data));
                arg = Deserialize(RPCTypes.ArgumentTypes[id], data);
            }
            return new RPCInfo() { id = id, arg = arg };
        }

        public static Task RefreshRequest(this IEncodeServer server)
        {
            return Task.WhenAll(
                    server.Request(ServerRequest.Setting | 
                    ServerRequest.Queue |
                    ServerRequest.Log |
                    ServerRequest.CheckLog |
                    ServerRequest.Console |
                    ServerRequest.State |
                    ServerRequest.FreeSpace |
                    ServerRequest.ServiceSetting),
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

        public Task OnUIData(UIData data)
        {
            return client.OnUIData(Copy(data));
        }

        public Task OnConsoleUpdate(ConsoleUpdate str)
        {
            return client.OnConsoleUpdate(Copy(str));
        }

        public Task OnEncodeState(EncodeState state)
        {
            return client.OnEncodeState(Copy(state));
        }

        public Task OnLogFile(string str)
        {
            return client.OnLogFile(Copy(str));
        }

        public Task OnOperationResult(OperationResult result)
        {
            return client.OnOperationResult(Copy(result));
        }

        public Task OnServiceSetting(ServiceSettingUpdate service)
        {
            return client.OnServiceSetting(Copy(service));
        }

        public Task OnLogoData(LogoData logoData)
        {
            return client.OnLogoData(Copy(logoData));
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

        public Task AddQueue(AddQueueRequest dir)
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

        public Task CancelAddQueue()
        {
            return Server.CancelAddQueue();
        }

        public Task CancelSleep()
        {
            return Server.CancelSleep();
        }

        public Task Request(ServerRequest req)
        {
            return Server.Request(Copy(req));
        }

        public Task RequestLogFile(LogFileRequest req)
        {
            return Server.RequestLogFile(Copy(req));
        }

        public Task RequestLogoData(string fileName)
        {
            return Server.RequestLogoData(Copy(fileName));
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

        public Task RequestDrcsImages()
        {
            return Server.RequestDrcsImages();
        }
    }
}
