using Amatsukaze.Server;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Threading.Tasks;

namespace Amatsukaze.AddTask
{
    public class GUIOPtion
    {
        public int ServerPort = 32768;
        public string ServerIP = "localhost";

        public string FilePath;

        public string NasDir;
        public bool NoMove;

        public string Subnet = "255.255.255.0";
        public byte[] MacAddress;

        public static void PrintHelp()
        {
            string help =
                Environment.GetCommandLineArgs()[0] + " <オプション> -f <input.ts>\r\n" +
                "オプション\r\n" +
                "  -f|--file <パス>        入力ファイルパス\t\n" +
                "  -ip|--ip <IPアドレス>   AmatsukazeServerアドレス\t\n" +
                "  -p|--port <ボート番号>  AmatsukazeServerポート番号\t\n" +
                "  -d|--nasdir <パス>      NASのTSファイル置き場\t\n" +
                "  --no-move               NASにコピーしたTSファイルをtransferedフォルダに移動しない\t\n" +
                "  --subnet <サブネットマスク>  Wake On Lan用サブネットマスク\t\n" +
                "  --mac <MACアドレス>  Wake On Lan用MACアドレス\t\n";
            Console.WriteLine(help);
        }

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
                else if (arg == "-ip" || arg == "--ip")
                {
                    ServerIP = args[i + 1];
                    ++i;
                }
                else if (arg == "-f" || arg == "--file")
                {
                    FilePath = args[i + 1];
                    ++i;
                }
                else if (arg == "-d" || arg == "--nasdir")
                {
                    NasDir = args[i + 1];
                    ++i;
                }
                else if (arg == "--no-move")
                {
                    NoMove = true;
                }
                else if (arg == "--subnet")
                {
                    Subnet = args[i + 1];
                    ++i;
                }
                else if (arg == "--mac")
                {
                    var str = args[i + 1];
                    MacAddress = str
                        .Split(str.Contains(':') ? ':' : '-')
                        .Select(s => byte.Parse(s)).ToArray();
                    if(MacAddress.Length != 6)
                    {
                        throw new Exception("MACアドレスが不正です");
                    }
                    ++i;
                }
            }

            if(string.IsNullOrEmpty(FilePath))
            {
                throw new Exception("入力ファイルパスを入力してください");
            }
        }
    }

    class WakeOnLan
    {
        private static void Send(IPAddress broad, byte[] macAddress)
        {
            MemoryStream stream = new MemoryStream();
            BinaryWriter writer = new BinaryWriter(stream);
            for (int i = 0; i < 6; i++)
            {
                writer.Write((byte)0xff);
            }
            for (int i = 0; i < 16; i++)
            {
                writer.Write(macAddress);
            }
            byte[] data = stream.ToArray();

            UdpClient client = new UdpClient();
            client.EnableBroadcast = true;
            client.Send(data, data.Length, new IPEndPoint(broad, 7));
            client.Send(data, data.Length, new IPEndPoint(broad, 9));
            client.Close();
        }

        private static IPAddress GetBroadcastAddress(IPAddress address, IPAddress subnetMask)
        {
            byte[] ipAdressBytes = address.GetAddressBytes();
            byte[] subnetMaskBytes = subnetMask.GetAddressBytes();

            if (ipAdressBytes.Length != subnetMaskBytes.Length)
            {
                throw new ArgumentException("Lengths of IP address and subnet mask do not match.");
            }

            byte[] broadcastAddress = new byte[ipAdressBytes.Length];
            for (int i = 0; i < broadcastAddress.Length; i++)
            {
                broadcastAddress[i] = (byte)(ipAdressBytes[i] | (subnetMaskBytes[i] ^ 255));
            }
            return new IPAddress(broadcastAddress);
        }

        public static void Wake(IPAddress address, IPAddress subnetMask, byte[] macAddress)
        {
            Send(GetBroadcastAddress(address, subnetMask), macAddress);
        }
    }

    class AddTask : IUserClient
    {
        public GUIOPtion option;

        private CUIServerConnection server;
        private AddQueueDirectory request;
        private bool okReceived;

        private bool IsLocal()
        {
            IPHostEntry iphostentry = Dns.GetHostEntry(Dns.GetHostName());
            IPHostEntry other = null;
            try
            {
                other = Dns.GetHostEntry(option.ServerIP);
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

        public async Task Exec()
        {
            if (File.Exists(option.FilePath) == false)
            {
                throw new Exception("入力ファイルが見つかりません");
            }

            string srcpath = option.FilePath;
            byte[] hash = null;

            if (string.IsNullOrEmpty(option.NasDir) == false)
            {
                // NASにコピー
                var remotepath = option.NasDir + "\\" + Path.GetFileName(srcpath);
                hash = await HashUtil.CopyWithHash(option.FilePath, remotepath);
                srcpath = remotepath;
            }

            // リクエストを生成
            request = new AddQueueDirectory()
            {
                DirPath = Path.GetDirectoryName(srcpath),
                Targets = new List<AddQueueItem>() {
                    new AddQueueItem() { Path = srcpath, Hash = hash }
                },
                Mode = ProcMode.AutoBatch,
                RequestId = UniqueId()
            };

            server = new CUIServerConnection(this);
            bool isLocal = ServerSupport.IsLocalIP(option.ServerIP);
            int maxRetry = isLocal ? 3 : 5;

            for (int i = 0; i < maxRetry; ++i)
            {
                if(i > 0)
                {
                    Console.WriteLine("再試行します・・・");
                }

                try
                {
                    // サーバに接続
                    server.Connect(option.ServerIP, option.ServerPort);
                }
                catch(Exception)
                {
                    // サーバに繋がらなかった

                    // ローカルの場合は、起動を試みる
                    if(isLocal)
                    {
                        ServerSupport.LaunchLocalServer(option.ServerPort);

                        // 10秒待つ
                        await Task.Delay(10 * 1000);
                    }
                    else
                    {
                        // リモートの場合は、Wake On Lanする
                        if(option.MacAddress == null)
                        {
                            throw new Exception("リモートサーバへの接続に失敗しました。");
                        }

                        Console.WriteLine("Wake On Lanで起動を試みます。");

                        WakeOnLan.Wake(
                            IPAddress.Parse(option.ServerIP),
                            IPAddress.Parse(option.Subnet),
                            option.MacAddress);

                        // 1分待つ
                        await Task.Delay(60 * 1000);
                    }

                    continue;
                }

                try
                {
                    // サーバにタスク登録
                    await server.AddQueue(request);

                    // リクエストIDの完了通知ゲット or タイムアウトしたら終了
                    var timeout = Task.Delay(30 * 1000);
                    while (okReceived == false)
                    {
                        var recv = server.ProcOneMessage();
                        if(await Task.WhenAny(recv, timeout) == timeout)
                        {
                            Console.WriteLine("サーバのリクエスト受理を確認できませんでした。");
                            throw new Exception();
                        }
                    }
                }
                catch (Exception)
                {
                    // なぜか失敗した
                    continue;
                }

                break;
            }
            server.Finish();
        }

        private string UniqueId()
        {
            return Environment.MachineName +
                System.Diagnostics.Process.GetCurrentProcess().Id;
        }

        #region IUserClient

        public void Finish()
        {
            // 何もしない
        }

        public Task OnAddResult(string requestId)
        {
            if(request.RequestId == requestId)
            {
                okReceived = true;
            }
            return Task.FromResult(0);
        }

        public Task OnAvsScriptFiles(AvsScriptFiles files)
        {
            return Task.FromResult(0);
        }

        public Task OnConsole(ConsoleData str)
        {
            return Task.FromResult(0);
        }

        public Task OnConsoleUpdate(ConsoleUpdate str)
        {
            return Task.FromResult(0);
        }

        public Task OnDrcsData(DrcsImageUpdate update)
        {
            return Task.FromResult(0);
        }

        public Task OnFreeSpace(DiskFreeSpace space)
        {
            return Task.FromResult(0);
        }

        public Task OnJlsCommandFiles(JLSCommandFiles files)
        {
            return Task.FromResult(0);
        }

        public Task OnLogData(LogData data)
        {
            return Task.FromResult(0);
        }

        public Task OnLogFile(string str)
        {
            return Task.FromResult(0);
        }

        public Task OnLogoData(LogoData logoData)
        {
            return Task.FromResult(0);
        }

        public Task OnLogUpdate(LogItem newLog)
        {
            return Task.FromResult(0);
        }

        public Task OnOperationResult(string result)
        {
            return Task.FromResult(0);
        }

        public Task OnQueueData(QueueData data)
        {
            return Task.FromResult(0);
        }

        public Task OnQueueUpdate(QueueUpdate update)
        {
            return Task.FromResult(0);
        }

        public Task OnServiceSetting(ServiceSettingUpdate update)
        {
            return Task.FromResult(0);
        }

        public Task OnSetting(Setting setting)
        {
            return Task.FromResult(0);
        }

        public Task OnState(State state)
        {
            return Task.FromResult(0);
        }

        #endregion
    }

    class AddTaskMain
    {
        static void Main(string[] args)
        {
            // カレントディレクトリは正しくセットされていること前提 //
            try
            {
                TaskSupport.SetSynchronizationContext();
                AsyncExec(new GUIOPtion(args));
                TaskSupport.EnterMessageLoop();
            }
            catch (Exception e)
            {
                Console.WriteLine(e.Message);
                GUIOPtion.PrintHelp();
                return;
            }
        }

        static async void AsyncExec(GUIOPtion option)
        {
            try
            {
                await new AddTask() { option = option }.Exec();
            }
            catch(Exception e)
            {
                Console.WriteLine(e.Message);
            }
            TaskSupport.Finish();
        }
    }
}
