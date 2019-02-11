using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.Serialization;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    public abstract class AbstracrtServerConnection : IEncodeServer
    {
        internal TcpClient client;
        internal NetworkStream stream;

        private IUserClient userClient;

        public AbstracrtServerConnection(IUserClient userClient)
        {
            this.userClient = userClient;
        }

        public abstract void Finish();

        private async Task Send(RPCMethodId id, object obj)
        {
            if (client != null)
            {
                byte[] bytes = RPCTypes.Serialize(id, obj);
                await client.GetStream().WriteAsync(bytes, 0, bytes.Length);
            }
        }

        internal void OnRequestReceived(RPCMethodId methodId, object arg)
        {
            switch (methodId)
            {
                case RPCMethodId.OnUIData:
                    userClient.OnUIData((UIData)arg);
                    break;
                case RPCMethodId.OnConsoleUpdate:
                    userClient.OnConsoleUpdate((ConsoleUpdate)arg);
                    break;
                case RPCMethodId.OnEncodeState:
                    userClient.OnEncodeState((EncodeState)arg);
                    break;
                case RPCMethodId.OnLogFile:
                    userClient.OnLogFile((string)arg);
                    break;
                case RPCMethodId.OnCommonData:
                    userClient.OnCommonData((CommonData)arg);
                    break;
                case RPCMethodId.OnProfile:
                    userClient.OnProfile((ProfileUpdate)arg);
                    break;
                case RPCMethodId.OnAutoSelect:
                    userClient.OnAutoSelect((AutoSelectUpdate)arg);
                    break;
                case RPCMethodId.OnServiceSetting:
                    userClient.OnServiceSetting((ServiceSettingUpdate)arg);
                    break;
                case RPCMethodId.OnLogoData:
                    userClient.OnLogoData((LogoData)arg);
                    break;
                case RPCMethodId.OnDrcsData:
                    userClient.OnDrcsData((DrcsImageUpdate)arg);
                    break;
                case RPCMethodId.OnAddResult:
                    userClient.OnAddResult((string)arg);
                    break;
                case RPCMethodId.OnOperationResult:
                    userClient.OnOperationResult((OperationResult)arg);
                    break;
            }
        }

        public Task SetProfile(ProfileUpdate data)
        {
            return Send(RPCMethodId.SetProfile, data);
        }

        public Task SetAutoSelect(AutoSelectUpdate data)
        {
            return Send(RPCMethodId.SetAutoSelect, data);
        }

        public Task AddQueue(AddQueueRequest dir)
        {
            return Send(RPCMethodId.AddQueue, dir);
        }

        public Task ChangeItem(ChangeItemData data)
        {
            return Send(RPCMethodId.ChangeItem, data);
        }

        public Task PauseEncode(bool pause)
        {
            return Send(RPCMethodId.PauseEncode, pause);
        }

        public Task CancelAddQueue()
        {
            return Send(RPCMethodId.CancelAddQueue, null);
        }

        public Task CancelSleep()
        {
            return Send(RPCMethodId.CancelSleep, null);
        }

        public Task SetCommonData(CommonData setting)
        {
            return Send(RPCMethodId.SetCommonData, setting);
        }

        public Task SetServiceSetting(ServiceSettingUpdate update)
        {
            return Send(RPCMethodId.SetServiceSetting, update);
        }

        public Task AddDrcsMap(DrcsImage drcsMap)
        {
            return Send(RPCMethodId.AddDrcsMap, drcsMap);
        }

        public Task EndServer()
        {
            return Send(RPCMethodId.EndServer, null);
        }

        public Task Request(ServerRequest req)
        {
            return Send(RPCMethodId.Request, req);
        }

        public Task RequestLogFile(LogFileRequest item)
        {
            return Send(RPCMethodId.RequestLogFile, item);
        }

        public Task RequestDrcsImages()
        {
            return Send(RPCMethodId.RequestDrcsImages, null);
        }

        public Task RequestLogoData(string fileName)
        {
            return Send(RPCMethodId.RequestLogoData, fileName);
        }
    }

    public class ServerConnection : AbstracrtServerConnection
    {
        private Func<string, Task> askServerAddress;
        private string serverIp;
        private int port;
        private bool finished = false;
        private bool reconnect = false;

        public EndPoint LocalIP {
            get {
                return client?.Client?.LocalEndPoint;
            }
        }

        public ServerConnection(IUserClient userClient, Func<string, Task> askServerAddress)
            : base(userClient)
        {
            this.askServerAddress = askServerAddress;
        }

        public void SetServerAddress(string serverIp, int port)
        {
            this.serverIp = serverIp;
            this.port = port;
        }

        public override void Finish()
        {
            finished = true;
            Close();
        }

        public void Reconnect()
        {
            reconnect = true;
            Close();
        }

        private async Task Connect()
        {
            Close();
            client = new TcpClient();
            await client.ConnectAsync(serverIp, port);
            Util.AddLog("サーバ(" + serverIp + ":" + port + ")に接続しました", null);
            stream = client.GetStream();

            // 接続後一通りデータを要求する
            await this.RefreshRequest();
        }

        private void Close()
        {
            if(stream != null)
            {
                stream.Close();
                stream = null;
            }
            if(client != null)
            {
                client.Close();
                client = null;
            }
        }

        public async Task Start()
        {
            string failReason = "";
            int failCount = 0;
            int nextWaitSec = 0;
            while (true)
            {
                try
                {
                    if (nextWaitSec > 0)
                    {
                        await Task.Delay(nextWaitSec * 1000);
                        nextWaitSec = 0;
                    }
                    if(serverIp == null)
                    {
                        // 未初期化
                        await askServerAddress("アドレスを入力してください");
                        if(finished)
                        {
                            break;
                        }
                        await Connect();
                    }
                    if(client == null)
                    {
                        // 再接続
                        if (reconnect == false)
                        {
                            await askServerAddress(failReason);
                        }
                        if (finished)
                        {
                            break;
                        }
                        reconnect = false;
                        await Connect();
                    }
                    var rpc = await RPCTypes.Deserialize(stream);
                    OnRequestReceived(rpc.id, rpc.arg);
                    failCount = 0;
                }
                catch (Exception e)
                {
                    // 失敗したら一旦閉じる
                    Close();
                    if (finished)
                    {
                        break;
                    }
                    if (reconnect == false)
                    {
                        nextWaitSec = failCount * 10;
                        Util.AddLog("接続エラー: ", e);
                        Util.AddLog(nextWaitSec.ToString() + "秒後にリトライします", null);
                        failReason = e.Message;
                        ++failCount;
                    }
                }
            }
        }
    }

    /// <summary>
    /// monoでも動くように最小限の依存だけで実装したサーバインターフェース
    /// </summary>
    public class CUIServerConnection : IAddTaskServer
    {
        internal TcpClient client;
        internal NetworkStream stream;

        private IAddTaskClient userClient;

        public CUIServerConnection(IAddTaskClient userClient)
        {
            this.userClient = userClient;
        }

        public void Connect(string serverIp, int port)
        {
            Close();
            client = new TcpClient(serverIp, port);
            stream = client.GetStream();
        }

        private void Close()
        {
            if (client != null)
            {
                stream.Close();
                client.Close();
                client = null;
            }
        }

        private async Task Send(RPCMethodId id, AddQueueRequest obj)
        {
            if (client != null)
            {
                var data = new List<byte[]>();
                var ms = new MemoryStream();
                var serializer = new DataContractSerializer(typeof(AddQueueRequest));
                serializer.WriteObject(ms, obj);
                data.Add(ms.ToArray());
                var objbyes = RPCData.CombineChunks(data);
                byte[] bytes = RPCData.Combine(
                    BitConverter.GetBytes((short)id),
                    BitConverter.GetBytes(objbyes.Length),
                    objbyes);
                await client.GetStream().WriteAsync(bytes, 0, bytes.Length);
            }
        }

        internal void OnRequestReceived(RPCMethodId methodId, object arg)
        {
            switch (methodId)
            {
                case RPCMethodId.OnAddResult:
                    userClient.OnAddResult((string)arg);
                    break;
                case RPCMethodId.OnOperationResult:
                    userClient.OnOperationResult((OperationResult)arg);
                    break;
            }
        }

        public Task AddQueue(AddQueueRequest dir)
        {
            return Send(RPCMethodId.AddQueue, dir);
        }

        public void Finish()
        {
            Close();
        }

        public async Task ProcOneMessage()
        {
            var headerbytes = await RPCData.ReadBytes(stream, RPCData.HEADER_SIZE);
            var id = (RPCMethodId)BitConverter.ToInt16(headerbytes, 0);
            var csize = BitConverter.ToInt32(headerbytes, 2);
            object arg = null;
            if (csize > 0)
            {
                var data = RPCData.SplitChunks(await RPCData.ReadBytes(stream, csize));
                Type type = null;
                if(id == RPCMethodId.OnAddResult)
                {
                    type = typeof(string);
                }
                else if(id == RPCMethodId.OnOperationResult)
                {
                    type = typeof(OperationResult);
                }
                if(type != null)
                {
                    arg = new DataContractSerializer(type).ReadObject(data[0]);
                }
            }
            OnRequestReceived(id, arg);
        }
    }
}
