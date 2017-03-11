using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Runtime.Serialization;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EncodeServer
{
    public class ServerConnection : IEncodeServer
    {
        private TcpClient client;
        private NetworkStream stream;
        private IUserClient userClient;
        private Action<string> askServerAddress;
        private string serverIp;
        private int port;
        private bool finished = false;

        public ServerConnection(IUserClient userClient, Action<string> askServerAddress)
        {
            this.userClient = userClient;
            this.askServerAddress = askServerAddress;
        }

        public void SetServerAddress(string serverIp, int port)
        {
            this.serverIp = serverIp;
            this.port = port;
        }

        public void Finish()
        {
            finished = true;
            Close();
        }

        private void Connect()
        {
            Close();
            client = new TcpClient(serverIp, port);
            stream = client.GetStream();
        }

        private void Close()
        {
            if(client != null)
            {
                stream.Close();
                client.Close();
                client = null;
            }
        }

        public async Task Start()
        {
            string failReason = "";
            byte[] idbytes = new byte[2];
            int readBytes = 0;
            while (true)
            {
                try
                {
                    if(serverIp == null)
                    {
                        // 未初期化
                        askServerAddress("アドレスを入力してください");
                        if(finished)
                        {
                            break;
                        }
                        Connect();
                    }
                    if(client == null)
                    {
                        // 再接続
                        askServerAddress(failReason);
                        if (finished)
                        {
                            break;
                        }
                        Connect();
                    }
                    readBytes += await stream.ReadAsync(
                        idbytes, readBytes, 2 - readBytes);
                    if (readBytes == 2)
                    {
                        readBytes = 0;
                        var methodId = (RPCMethodId)((idbytes[0] << 8) | idbytes[1]);
                        var argType = RPCTypes.ArgumentTypes[methodId];
                        object arg = null;
                        if (argType != null)
                        {
                            var s = new DataContractSerializer(argType);
                            await Task.Run(() => { arg = s.ReadObject(stream); });
                        }
                        OnRequestReceived((RPCMethodId)methodId, arg);
                    }
                }
                catch (Exception e)
                {
                    // 失敗したら一旦閉じる
                    Close();
                    if (finished)
                    {
                        break;
                    }
                    failReason = e.Message;
                }
            }
        }

        private async Task Send(RPCMethodId id, Type type, object obj)
        {
            if(client != null)
            {
                var idbytes = new byte[2] { (byte)((int)id >> 8), (byte)id };
                var ms = new MemoryStream();
                ms.Write(idbytes, 0, idbytes.Length);
                if (obj != null)
                {
                    var serializer = new DataContractSerializer(type);
                    serializer.WriteObject(ms, obj);
                }
                var bytes = ms.ToArray();
                await client.GetStream().WriteAsync(bytes, 0, bytes.Length);
            }
        }

        private void OnRequestReceived(RPCMethodId methodId, object arg)
        {
            switch (methodId)
            {
                case RPCMethodId.OnQueueData:
                    userClient.OnQueueData((QueueData)arg);
                    break;
                case RPCMethodId.OnQueueUpdate:
                    userClient.OnQueueUpdate((QueueUpdate)arg);
                    break;
                case RPCMethodId.OnLogData:
                    userClient.OnLogData((LogData)arg);
                    break;
                case RPCMethodId.OnLogUpdate:
                    userClient.OnLogUpdate((LogItem)arg);
                    break;
                case RPCMethodId.OnConsole:
                    userClient.OnConsole((string)arg);
                    break;
                case RPCMethodId.OnConsoleUpdate:
                    userClient.OnConsoleUpdate((string)arg);
                    break;
                case RPCMethodId.OnLogFile:
                    userClient.OnLogFile((string)arg);
                    break;
                case RPCMethodId.OnState:
                    userClient.OnState((State)arg);
                    break;
                case RPCMethodId.OnOperationResult:
                    userClient.OnOperationResult((string)arg);
                    break;
            }
        }

        public Task AddQueue(string dirPath)
        {
            return Send(RPCMethodId.AddQueue, typeof(string), dirPath);
        }

        public Task PauseEncode(bool pause)
        {
            return Send(RPCMethodId.PauseEncode, typeof(bool), pause);
        }

        public Task RemoveQueue(string dirPath)
        {
            return Send(RPCMethodId.RemoveQueue, typeof(string), dirPath);
        }

        public Task RequestConsole()
        {
            return Send(RPCMethodId.RequestConsole, null, null);
        }

        public Task RequestLog()
        {
            return Send(RPCMethodId.RequestLog, null, null);
        }

        public Task RequestLogFile(LogItem item)
        {
            return Send(RPCMethodId.RequestLogFile, typeof(LogItem), item);
        }

        public Task RequestQueue()
        {
            return Send(RPCMethodId.RequestQueue, null, null);
        }

        public Task RequestState()
        {
            return Send(RPCMethodId.RequestState, null, null);
        }

        public Task SetSetting(Setting setting)
        {
            return Send(RPCMethodId.SetSetting, typeof(Setting), setting);
        }
    }
    
    public class UserClient : IUserClient
    {
        [DataContract]
        private class ClientData : IExtensibleDataObject
        {
            [DataMember]
            public string ServerIP;
            [DataMember]
            public int ServerPort;

            public ExtensionDataObject ExtensionData { get; set; }
        }

        private ClientData appData;
        private ServerConnection server;

        public UserClient()
        {
            LoadAppData();

            // テスト用
            appData.ServerIP = "localhost";
            appData.ServerPort = 35224;

            server = new ServerConnection(this, askServerAddress);
        }

        private void askServerAddress(string reason)
        {
            Console.WriteLine(reason);
            Thread.Sleep(5000);
            server.SetServerAddress(appData.ServerIP, appData.ServerPort);
        }

        private string GetSettingFilePath()
        {
            return "AmatsukazeClient.xml";
        }

        private void LoadAppData()
        {
            using (FileStream fs = new FileStream(GetSettingFilePath(), FileMode.Open))
            {
                var s = new DataContractSerializer(typeof(ClientData));
                appData = (ClientData)s.ReadObject(fs);
            }
        }

        private void SaveAppData()
        {
            using (FileStream fs = new FileStream(GetSettingFilePath(), FileMode.Create))
            {
                var s = new DataContractSerializer(typeof(ClientData));
                s.WriteObject(fs, appData);
            }
        }

        public Task OnConsole(string str)
        {
            Console.WriteLine(str);
            return Task.FromResult(0);
        }

        public Task OnConsoleUpdate(string str)
        {
            Console.WriteLine(str);
            return Task.FromResult(0);
        }

        public Task OnLogData(LogData data)
        {
            Console.WriteLine(data);
            return Task.FromResult(0);
        }

        public Task OnLogFile(string str)
        {
            Console.WriteLine(str);
            return Task.FromResult(0);
        }

        public Task OnLogUpdate(LogItem newLog)
        {
            Console.WriteLine(newLog);
            return Task.FromResult(0);
        }

        public Task OnOperationResult(string result)
        {
            Console.WriteLine(result);
            return Task.FromResult(0);
        }

        public Task OnQueueData(QueueData data)
        {
            Console.WriteLine(data);
            return Task.FromResult(0);
        }

        public Task OnQueueUpdate(QueueUpdate update)
        {
            Console.WriteLine(update);
            return Task.FromResult(0);
        }

        public Task OnState(State state)
        {
            Console.WriteLine(state);
            return Task.FromResult(0);
        }
    }
}
