using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Sockets;
using System.Runtime.Serialization;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Amatsukaze.Server
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

        private Task Connect()
        {
            Debug.Print("Connect");
            Close();
            client = new TcpClient(serverIp, port);
            stream = client.GetStream();

            // 接続後一通りデータを要求する
            return this.RefreshRequest();
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
                        await Connect();
                    }
                    if(client == null)
                    {
                        // 再接続
                        askServerAddress(failReason);
                        if (finished)
                        {
                            break;
                        }
                        await Connect();
                    }
                    var rpc = await RPCTypes.Deserialize(stream);
                    OnRequestReceived(rpc.id, rpc.arg);
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

        private async Task Send(RPCMethodId id, object obj)
        {
            if(client != null)
            {
                byte[] bytes = RPCTypes.Serialize(id, obj);
                await client.GetStream().WriteAsync(bytes, 0, bytes.Length);
            }
        }

        private void OnRequestReceived(RPCMethodId methodId, object arg)
        {
            switch (methodId)
            {
                case RPCMethodId.OnSetting:
                    userClient.OnSetting((Setting)arg);
                    break;
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
                    userClient.OnConsole((List<string>)arg);
                    break;
                case RPCMethodId.OnConsoleUpdate:
                    userClient.OnConsoleUpdate((byte[])arg);
                    break;
                case RPCMethodId.OnLogFile:
                    userClient.OnLogFile((string)arg);
                    break;
                case RPCMethodId.OnState:
                    userClient.OnState((State)arg);
                    break;
                case RPCMethodId.OnFreeSpace:
                    userClient.OnFreeSpace((DiskFreeSpace)arg);
                    break;
                case RPCMethodId.OnOperationResult:
                    userClient.OnOperationResult((string)arg);
                    break;
            }
        }

        public Task AddQueue(string dirPath)
        {
            return Send(RPCMethodId.AddQueue, dirPath);
        }

        public Task PauseEncode(bool pause)
        {
            return Send(RPCMethodId.PauseEncode, pause);
        }

        public Task RemoveQueue(string dirPath)
        {
            return Send(RPCMethodId.RemoveQueue, dirPath);
        }

        public Task RequestSetting()
        {
            return Send(RPCMethodId.RequestSetting, null);
        }

        public Task RequestConsole()
        {
            return Send(RPCMethodId.RequestConsole, null);
        }

        public Task RequestLog()
        {
            return Send(RPCMethodId.RequestLog, null);
        }

        public Task RequestLogFile(LogItem item)
        {
            return Send(RPCMethodId.RequestLogFile, item);
        }

        public Task RequestQueue()
        {
            return Send(RPCMethodId.RequestQueue, null);
        }

        public Task RequestState()
        {
            return Send(RPCMethodId.RequestState, null);
        }

        public Task RequestFreeSpace()
        {
            return Send(RPCMethodId.RequestFreeSpace, null);
        }

        public Task SetSetting(Setting setting)
        {
            return Send(RPCMethodId.SetSetting, setting);
        }
    }
}
