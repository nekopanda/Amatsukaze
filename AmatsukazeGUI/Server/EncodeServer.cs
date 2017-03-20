using Codeplex.Data;
using Livet;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Configuration;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    public class Client : NotificationObject
    {
        private ClientManager manager;
        private TcpClient client;
        private NetworkStream stream;

        public string HostName { get; private set; }
        public int Port { get; private set; }

        #region TotalSendCount変更通知プロパティ
        private int _TotalSendCount;

        public int TotalSendCount {
            get { return _TotalSendCount; }
            set { 
                if (_TotalSendCount == value)
                    return;
                _TotalSendCount = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region TotalRecvCount変更通知プロパティ
        private int _TotalRecvCount;

        public int TotalRecvCount {
            get { return _TotalRecvCount; }
            set { 
                if (_TotalRecvCount == value)
                    return;
                _TotalRecvCount = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public Client(TcpClient client, ClientManager manager)
        {
            this.manager = manager;
            this.client = client;
            this.stream = client.GetStream();

            var endPoint = (IPEndPoint)client.Client.RemoteEndPoint;
            HostName = Dns.GetHostEntry(endPoint.Address).HostName;
            Port = endPoint.Port;

            Util.AddLog("クライアント("+ HostName + ":" + Port + ")と接続");
        }

        public async Task Start()
        {
            try
            {
                while (true)
                {
                    var rpc = await RPCTypes.Deserialize(stream);
                    manager.OnRequestReceived(this, rpc.id, rpc.arg);
                    TotalRecvCount++;
                }
            }
            catch (Exception e)
            {
                Util.AddLog("クライアント(" + HostName + ":" + Port + ")との接続が切れました");
                Util.AddLog(e.Message);
                Close();
            }
            manager.OnClientClosed(this);
        }

        public void Close()
        {
            if (client != null)
            {
                client.Close();
                client = null;
            }
        }

        public NetworkStream GetStream()
        {
            return stream;
        }
    }

    public class ClientManager : NotificationObject, IUserClient
    {
        private TcpListener listener;
        private bool finished = false;
        private List<Task> receiveTask = new List<Task>();

        public ObservableCollection<Client> ClientList { get; private set; }

        public ClientManager()
        {
            ClientList = new ObservableCollection<Client>();
        }

        public void Finish()
        {
            if (listener != null)
            {
                listener.Stop();
                listener = null;

                foreach (var client in ClientList)
                {
                    client.Close();
                }
            }
        }

        public async Task Listen(int port)
        {
            listener = new TcpListener(IPAddress.Any, port);
            listener.Start();
            Util.AddLog("サーバ開始しました。ポート: " + port);

            try
            {
                while (true)
                {
                    var client = new Client(await listener.AcceptTcpClientAsync(), this);
                    receiveTask.Add(client.Start());
                    ClientList.Add(client);
                }
            }
            catch (Exception e)
            {
                if (finished == false)
                {
                    Util.AddLog("Listen中にエラーが発生");
                    Util.AddLog(e.Message);
                }
            }
        }

        private async Task Send(RPCMethodId id, object obj)
        {
            byte[] bytes = RPCTypes.Serialize(id, obj);
            foreach (var client in ClientList.ToArray())
            {
                try
                {
                    await client.GetStream().WriteAsync(bytes, 0, bytes.Length);
                    client.TotalSendCount++;
                }
                catch (Exception e)
                {
                    Util.AddLog("クライアント(" +
                        client.HostName + ":" + client.Port + ")との接続が切れました");
                    Util.AddLog(e.Message);
                    client.Close();
                    OnClientClosed(client);
                }
            }
        }

        private IEncodeServer server;

        public ClientManager(IEncodeServer server)
        {
            this.server = server;
        }

        internal void OnRequestReceived(Client client, RPCMethodId methodId, object arg)
        {
            switch (methodId)
            {
                case RPCMethodId.SetSetting:
                    server.SetSetting((Setting)arg);
                    break;
                case RPCMethodId.AddQueue:
                    server.AddQueue((string)arg);
                    break;
                case RPCMethodId.RemoveQueue:
                    server.RemoveQueue((string)arg);
                    break;
                case RPCMethodId.PauseEncode:
                    server.PauseEncode((bool)arg);
                    break;
                case RPCMethodId.RequestSetting:
                    server.RequestSetting();
                    break;
                case RPCMethodId.RequestQueue:
                    server.RequestQueue();
                    break;
                case RPCMethodId.RequestLog:
                    server.RequestLog();
                    break;
                case RPCMethodId.RequestConsole:
                    server.RequestConsole();
                    break;
                case RPCMethodId.RequestLogFile:
                    server.RequestLogFile((LogItem)arg);
                    break;
                case RPCMethodId.RequestState:
                    server.RequestState();
                    break;
                case RPCMethodId.RequestFreeSpace:
                    server.RequestFreeSpace();
                    break;
            }
        }

        internal void OnClientClosed(Client client)
        {
            int index = ClientList.IndexOf(client);
            if (index >= 0)
            {
                receiveTask.RemoveAt(index);
                ClientList.RemoveAt(index);
            }
        }

        #region IUserClient
        public Task OnSetting(Setting data)
        {
            return Send(RPCMethodId.OnSetting, data);
        }

        public Task OnQueueData(QueueData data)
        {
            return Send(RPCMethodId.OnQueueData, data);
        }

        public Task OnQueueUpdate(QueueUpdate update)
        {
            return Send(RPCMethodId.OnQueueUpdate, update);
        }

        public Task OnLogData(LogData data)
        {
            return Send(RPCMethodId.OnLogData, data);
        }

        public Task OnLogUpdate(LogItem newLog)
        {
            return Send(RPCMethodId.OnLogUpdate, newLog);
        }

        public Task OnConsole(List<string> str)
        {
            return Send(RPCMethodId.OnConsole, str);
        }

        public Task OnConsoleUpdate(byte[] str)
        {
            return Send(RPCMethodId.OnConsoleUpdate, str);
        }

        public Task OnLogFile(string str)
        {
            return Send(RPCMethodId.OnLogFile, str);
        }

        public Task OnState(State state)
        {
            return Send(RPCMethodId.OnState, state);
        }

        public Task OnFreeSpace(DiskFreeSpace state)
        {
            return Send(RPCMethodId.OnFreeSpace, state);
        }

        public Task OnOperationResult(string result)
        {
            return Send(RPCMethodId.OnOperationResult, result);
        }
        #endregion
    }

    public class EncodeServer : NotificationObject, IEncodeServer
    {
        private class TargetDirectory
        {
            public string DirPath { get; private set; }
            public List<string> TsFiles {get;private set;}

            public TargetDirectory(string dirPath)
            {
                DirPath = dirPath;
                TsFiles = Directory.GetFiles(DirPath, "*.ts").ToList();
            }
        }

        [DataContract]
        private class AppData : IExtensibleDataObject
        {
            [DataMember]
            public Setting setting;

            public ExtensionDataObject ExtensionData { get; set; }
        }

        private class ConsoleText : ConsoleTextBase
        {
            public List<string> TextLines = new List<string>();

            public override void OnAddLine(string text)
            {
                if(TextLines.Count > 500)
                {
                    TextLines.RemoveRange(0, 100);
                }
                TextLines.Add(text);
            }

            public override void OnReplaceLine(string text)
            {
                if(TextLines.Count == 0)
                {
                    TextLines.Add(text);
                }
                else
                {
                    TextLines[TextLines.Count - 1] = text;
                }
            }
        }

        private static string LOG_FILE = "Log.xml";
        private static string LOG_DIR = "Logs";

        private IUserClient client;
        public Task ServerTask { get; private set; }
        private AppData appData;

        private List<QueueDirectory> queue = new List<QueueDirectory>();
        private LogData log;
        private ConsoleText consoleText = new ConsoleText();
        private SortedDictionary<string, DiskItem> diskMap = new SortedDictionary<string, DiskItem>();

        private FileStream logWriter;

        #region EncodePaused変更通知プロパティ
        private bool encodePaused = false;

        public bool EncodePaused {
            get { return encodePaused; }
            set { 
                if (encodePaused == value)
                    return;
                encodePaused = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NowEncoding変更通知プロパティ
        private bool nowEncoding = false;

        public bool NowEncoding {
            get { return nowEncoding; }
            set { 
                if (nowEncoding == value)
                    return;
                nowEncoding = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public ClientManager ClientManager {
            get { return client as ClientManager; }
        }

        public EncodeServer(int port, IUserClient client)
        {
            LoadAppData();
            if (client != null)
            {
                this.client = client;
            }
            else
            {
                var clientManager = new ClientManager(this);
                ServerTask = clientManager.Listen(port);
                this.client = clientManager;
            }
            ReadLog();
        }

        #region Path
        private string GetAmatsukzeCLIPath()
        {
            return Path.Combine(Path.GetDirectoryName(
                typeof(EncodeServer).Assembly.Location), "AmatsukazeCLI.exe");
        }

        private string GetSettingFilePath()
        {
            return "AmatsukazeServer.xml";
        }

        private string GetLogFilePath(DateTime start)
        {
            return LOG_DIR + "\\" + start.ToString("yyyy-MM-dd_HHmmss.fff") + ".txt";
        }

        private string ReadLogFIle(DateTime start)
        {
            var logpath = GetLogFilePath(start);
            if(File.Exists(logpath) == false)
            {
                return "ログファイルが見つかりません。パス: " + logpath;
            }
            return File.ReadAllText(logpath, Encoding.Default);
        }
        #endregion

        public void Finish()
        {
            if (client != null)
            {
                client.Finish();
                client = null;
            }
        }

        private void LoadAppData()
        {
            if(File.Exists(GetSettingFilePath()) == false)
            {
                appData = new AppData() {
                    setting = new Setting() {
                        EncoderName = "x264",
                        AmatsukazePath = Path.Combine(
                            Path.GetDirectoryName(GetType().Assembly.Location),
                            "Amatsukaze.exe")
                    }
                };
                return;
            }
            using (FileStream fs = new FileStream(GetSettingFilePath(), FileMode.Open))
            {
                var s = new DataContractSerializer(typeof(AppData));
                appData = (AppData)s.ReadObject(fs);
                if (appData.setting == null)
                {
                    appData.setting = new Setting();
                }
            }
        }

        private void SaveAppData()
        {
            using (FileStream fs = new FileStream(GetSettingFilePath(), FileMode.Create))
            {
                var s = new DataContractSerializer(typeof(AppData));
                s.WriteObject(fs, appData);
            }
        }

        private void ReadLog()
        {
            if (File.Exists(LOG_FILE) == false)
            {
                log = new LogData()
                {
                    Items = new List<LogItem>()
                };
                return;
            }
            try
            {
                using (FileStream fs = new FileStream(LOG_FILE, FileMode.Open))
                {
                    var s = new DataContractSerializer(typeof(LogData));
                    log = (LogData)s.ReadObject(fs);
                    if (log.Items == null)
                    {
                        log.Items = new List<LogItem>();
                    }
                }
            }
            catch (IOException e)
            {
                Util.AddLog("ログファイルの読み込みに失敗: " + e.Message);
            }
        }

        private void WriteLog()
        {
            try
            {
                using (FileStream fs = new FileStream(LOG_FILE, FileMode.Create))
                {
                    var s = new DataContractSerializer(typeof(LogData));
                    s.WriteObject(fs, log);
                }
            }
            catch (IOException e)
            {
                Util.AddLog("ログファイル書き込み失敗: " + e.Message);
            }
        }

        private string GetEncoderPath()
        {
            if (appData.setting.EncoderName == "x264")
            {
                return appData.setting.X264Path;
            }
            else if(appData.setting.EncoderName == "x265")
            {
                return appData.setting.X265Path;
            }
            else if(appData.setting.EncoderName == "QSVEnc")
            {
                return appData.setting.QSVEncPath;
            }
            else
            {
                throw new ArgumentException("エンコーダ名が認識できません");
            }
        }

        private string MakeAmatsukazeArgs(string src, string dst, out string json, out string log)
        {
            string workPath = string.IsNullOrEmpty(appData.setting.WorkPath)
                ? "./" : appData.setting.WorkPath;
            string encoderPath = GetEncoderPath();
            json = Path.Combine(workPath, "amt-" + Process.GetCurrentProcess().Id.ToString() + ".json");
            log = Path.Combine(
                Path.GetDirectoryName(dst),
                Path.GetFileNameWithoutExtension(dst)) + "-enc.log";
            
            if (string.IsNullOrEmpty(encoderPath))
            {
                throw new ArgumentException("エンコーダパスが指定されていません");
            }
            if (string.IsNullOrEmpty(appData.setting.MuxerPath))
            {
                throw new ArgumentException("Muxerパスが指定されていません");
            }
            if (string.IsNullOrEmpty(appData.setting.TimelineEditorPath))
            {
                throw new ArgumentException("Timelineeditorパスが指定されていません");
            }

            StringBuilder sb = new StringBuilder();
            sb.Append("-i \"")
                .Append(src)
                .Append("\" -o \"")
                .Append(dst)
                .Append("\" -w \"")
                .Append(appData.setting.WorkPath)
                .Append("\" -et ")
                .Append(appData.setting.EncoderName)
                .Append(" -e \"")
                .Append(encoderPath)
                .Append("\" -m \"")
                .Append(appData.setting.MuxerPath)
                .Append("\" -t \"")
                .Append(appData.setting.TimelineEditorPath)
                .Append("\" -j \"")
                .Append(json)
                .Append("\"");

            if (string.IsNullOrEmpty(appData.setting.EncoderOption) == false)
            {
                sb.Append(" -eo \"")
                    .Append(appData.setting.EncoderOption)
                    .Append("\"");
            }

            return sb.ToString();
        }

        private async Task RedirectOut(Stream stream)
        {
            try
            {
                byte[] buffer = new byte[1024];
                while (true)
                {
                    var readBytes = await stream.ReadAsync(buffer, 0, buffer.Length);
                    if(readBytes == 0)
                    {
                        // 終了
                        return;
                    }
                    if (logWriter != null)
                    {
                        logWriter.Write(buffer, 0, readBytes);
                    }
                    consoleText.AddBytes(buffer, 0, readBytes);

                    byte[] newbuf = new byte[readBytes];
                    Array.Copy(buffer, newbuf, readBytes);
                    await client.OnConsoleUpdate(newbuf);
                }
            }
            catch (Exception e)
            {
                Debug.Print("RedirectOut exception " + e.Message);
            }
        }

        private LogItem LogFromJson(string jsonpath, DateTime start, DateTime finish)
        {
            var json = DynamicJson.Parse(File.ReadAllText(jsonpath));
            var outpath = new List<string>();
            foreach (var path in json.outpath)
            {
                outpath.Add(path);
            }
            return new LogItem() {
                Success = true,
                SrcPath = json.srcpath,
                OutPath = outpath,
                SrcFileSize = (long)json.srcfilesize,
                IntVideoFileSize = (long)json.intvideofilesize,
                OutFileSize = (long)json.outfilesize,
                SrcVideoDuration = TimeSpan.FromSeconds(json.srcduration),
                OutVideoDuration = TimeSpan.FromSeconds(json.outduration),
                EncodeStartDate = start,
                EncodeFinishDate = finish,
                MachineName = Dns.GetHostName(),
                AudioDiff = new AudioDiff() {
                    TotalSrcFrames = (int)json.audiodiff.totalsrcframes,
                    TotalOutFrames = (int)json.audiodiff.totaloutframes,
                    TotalOutUniqueFrames = (int)json.audiodiff.totaloutuniqueframes,
                    NotIncludedPer = json.audiodiff.notincludedper,
                    AvgDiff = json.audiodiff.avgdiff,
                    MaxDiff = json.audiodiff.maxdiff,
                    MaxDiffPos = json.audiodiff.maxdiffpos
                }
            };
        }

        private LogItem FailLogItem(string reason, DateTime start, DateTime finish)
        {
            return new LogItem()
            {
                Success = false,
                Reason = reason,
                EncodeStartDate = start,
                EncodeFinishDate = finish
            };
        }

        private Task UpdateItemState(bool isEncoding, bool isComplete, QueueItem item, QueueDirectory dir)
        {
            item.IsEncoding = isEncoding;
            item.IsComplete = isComplete;

            return client.OnQueueUpdate(new QueueUpdate() {
                Type = UpdateType.Update,
                DirPath = dir.Path,
                Item = item
            });
        }

        private async Task ProcessDiretoryItem(QueueDirectory dir)
        {
            string succeeded = Path.Combine(dir.Path, "succeeded");
            string failed = Path.Combine(dir.Path, "failed");
            string encoded = Path.Combine(dir.Path, "encoded");
            Directory.CreateDirectory(succeeded);
            Directory.CreateDirectory(failed);
            Directory.CreateDirectory(encoded);

            // 待たなくてもいいタスクリスト
            var waitList = new List<Task>();

            foreach (var src in dir.Items)
            {
                waitList.Add(UpdateItemState(true, false, src, dir));

                if (File.Exists(src.Path) == false)
                {
                    DateTime now = DateTime.Now;
                    log.Items.Add(FailLogItem("入力ファイルが見つかりません", now, now));
                    WriteLog();
                    waitList.Add(UpdateItemState(false, true, src, dir));
                    waitList.Add(client.OnLogUpdate(log.Items.Last()));
                    continue;
                }

                string dst = Path.Combine(encoded, Path.GetFileName(src.Path));
                string json, logpath;
                string args = MakeAmatsukazeArgs(src.Path, dst, out json, out logpath);
                string exename = appData.setting.AmatsukazePath;
                
                Debug.Print("Args: " + exename + " " + args);

                DateTime start = DateTime.Now;

                var psi = new ProcessStartInfo(exename, args) {
                    UseShellExecute = false,
                    WorkingDirectory = Directory.GetCurrentDirectory(),
                    RedirectStandardError = true,
                    RedirectStandardOutput = true,
                    RedirectStandardInput = false,
                    CreateNoWindow = true
                };

                int exitCode = -1;
                using (var p = Process.Start(psi))
                {
                    using (logWriter = File.Create(logpath))
                    {
                        await Task.WhenAll(
                            RedirectOut(p.StandardOutput.BaseStream),
                            RedirectOut(p.StandardError.BaseStream),
                            Task.Run(() => p.WaitForExit()));
                    }

                    exitCode = p.ExitCode;
                }

                DateTime finish = DateTime.Now;

                // ログファイルを専用フォルダにコピー
                if (File.Exists(logpath))
                {
                    string logstorepath = GetLogFilePath(start);
                    Directory.CreateDirectory(Path.GetDirectoryName(logstorepath));
                    File.Copy(logpath, logstorepath);
                }

                if (exitCode == 0)
                {
                    // 成功
                    //File.Move(src, succeeded + "\\");

                    log.Items.Add(LogFromJson(json, start, finish));
                    WriteLog();
                }
                else
                {
                    // 失敗
                    //File.Move(src, failed + "\\");

                    // TODO:
                    throw new InvalidProgramException("エンコードに失敗した");
                }

                waitList.Add(UpdateItemState(false, true, src, dir));
                waitList.Add(client.OnLogUpdate(log.Items.Last()));
                waitList.Add(RequestFreeSpace());

                if (encodePaused)
                {
                    break;
                }
            }

            await Task.WhenAll(waitList.ToArray());
        }

        private async Task StartEncode()
        {
            NowEncoding = true;

            // 待たなくてもいいタスクリスト
            var waitList = new List<Task>();

            // 状態を更新
            waitList.Add(RequestState());

            try
            {
                while (queue.Count > 0)
                {
                    var dir = queue[0];
                    await ProcessDiretoryItem(dir);
                    if (encodePaused)
                    {
                        break;
                    }
                    queue.RemoveAt(0);
                    waitList.Add(client.OnQueueUpdate(new QueueUpdate() {
                        Type = UpdateType.Remove,
                        DirPath = dir.Path,
                    }));
                }
            }
            catch(Exception e)
            {
                waitList.Add(client.OnOperationResult(
                    "エラーでエンコードが停止しました: " + e.Message));
            }

            NowEncoding = false;

            // 状態を更新
            waitList.Add(RequestState());

            await Task.WhenAll(waitList.ToArray());
        }

        private DiskItem MakeDiskItem(string path)
        {
            ulong available = 0;
            ulong total = 0;
            ulong free = 0;
            Util.GetDiskFreeSpaceEx(path, out available, out total, out free);
            return new DiskItem() { Capacity = (long)total, Free = (long)available, Path = path };
        }

        private void RefrechDiskSpace()
        {
            diskMap = new SortedDictionary<string, DiskItem>();
            foreach(var item in queue)
            {
                var diskPath = Path.GetPathRoot(item.Path);
                if (diskMap.ContainsKey(diskPath) == false)
                {
                    diskMap.Add(diskPath, MakeDiskItem(diskPath));
                }
            }
        }

        public Task SetSetting(Setting setting)
        {
            appData.setting = setting;
            SaveAppData();
            return Task.WhenAll(
                RequestSetting(), 
                client.OnOperationResult("設定を更新しました"));
        }

        public async Task AddQueue(string dirPath)
        {
            if (queue.Find(t => t.Path == dirPath) != null)
            {
                await client.OnOperationResult(
                    "すでに同じパスが追加されています。パス:" + dirPath);
                return;
            }
            var target = new QueueDirectory() {
                Path = dirPath,
                Items = Directory.GetFiles(dirPath, "*.ts").
                Select(f => new QueueItem() { Path = f }).ToList()
            };
            if (target.Items.Count == 0)
            {
                await client.OnOperationResult(
                    "エンコード対象ファイルが見つかりません。パス:" + dirPath);
                return;
            }
            queue.Add(target);
            Task task = client.OnQueueUpdate(new QueueUpdate() {
                Type = UpdateType.Add,
                Directory = target
            });
            Task task2 = RequestFreeSpace();
            if (encodePaused == false && nowEncoding == false)
            {
                await StartEncode();
            }
            await task;
            await task2;
        }

        public async Task RemoveQueue(string dirPath)
        {
            var target = queue.Find(t => t.Path == dirPath);
            if (target == null)
            {
                await client.OnOperationResult(
                    "指定されたキューディレクトリが見つかりません。パス:" + dirPath);
                return;
            }
            queue.Remove(target);
            await client.OnQueueUpdate(new QueueUpdate() {
                Type = UpdateType.Remove,
                DirPath = target.Path
            });
        }

        public async Task PauseEncode(bool pause)
        {
            EncodePaused = pause;
            Task task = RequestState();
            if (encodePaused == false && nowEncoding == false)
            {
                await StartEncode();
            }
            await task;
        }

        public Task RequestSetting()
        {
            return client.OnSetting(appData.setting);
        }

        public Task RequestQueue()
        {
            QueueData data = new QueueData()
            {
                Items = queue
            };
            return client.OnQueueData(data);
        }

        public Task RequestLog()
        {
            return client.OnLogData(log);
        }

        public Task RequestConsole()
        {
            return client.OnConsole(consoleText.TextLines as List<string>);
        }

        public Task RequestLogFile(LogItem item)
        {
            return client.OnLogFile(ReadLogFIle(item.EncodeStartDate));
        }

        public Task RequestState()
        {
            var state = new State() {
                HostName = Dns.GetHostName(),
                Pause = encodePaused,
                Running = nowEncoding
            };
            return client.OnState(state);
        }

        public Task RequestFreeSpace()
        {
            RefrechDiskSpace();
            return client.OnFreeSpace(new DiskFreeSpace() {
                Disks = diskMap.Values.ToList()
            });
        }
    }
}
