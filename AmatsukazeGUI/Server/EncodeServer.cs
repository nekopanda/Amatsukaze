using Codeplex.Data;
using Livet;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
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

        private IEncodeServer server;

        public ClientManager(IEncodeServer server)
        {
            this.server = server;
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

        public Task OnConsole(ConsoleData str)
        {
            return Send(RPCMethodId.OnConsole, str);
        }

        public Task OnConsoleUpdate(ConsoleUpdate str)
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

        private IUserClient client;
        public Task ServerTask { get; private set; }
        private AppData appData;

        private List<QueueDirectory> queue = new List<QueueDirectory>();
        private LogData log;
        private List<ConsoleText> consoleList = new List<ConsoleText>();
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
                RaisePropertyChanged("ClientManager");
            }
            ReadLog();
        }

        #region Path
        private string GetSettingFilePath()
        {
            return "config\\AmatsukazeServer.xml";
        }

        private string GetHistoryFilePath()
        {
            return "data\\EncodeHistory.xml";
        }

        private string GetLogFilePath(DateTime start)
        {
            return "data\\logs\\" + start.ToString("yyyy-MM-dd_HHmmss.fff") + ".txt";
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

        private Task AddEncodeLog(string str)
        {
            Util.AddLog(str);
            return client.OnOperationResult(str);
        }

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
            string path = GetSettingFilePath();
            if (File.Exists(path) == false)
            {
                appData = new AppData() {
                    setting = new Setting() {
                        EncoderName = "x264",
                        AmatsukazePath = Path.Combine(
                            Path.GetDirectoryName(GetType().Assembly.Location),
                            "Amatsukaze.exe"),
                        Bitrate = new BitrateSetting()
                    }
                };
                return;
            }
            using (FileStream fs = new FileStream(path, FileMode.Open))
            {
                var s = new DataContractSerializer(typeof(AppData));
                appData = (AppData)s.ReadObject(fs);
                if (appData.setting == null)
                {
                    appData.setting = new Setting();
                }
                if (appData.setting.Bitrate == null)
                {
                    appData.setting.Bitrate = new BitrateSetting();
                }
            }
        }

        private void SaveAppData()
        {
            string path = GetSettingFilePath();
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            using (FileStream fs = new FileStream(path, FileMode.Create))
            {
                var s = new DataContractSerializer(typeof(AppData));
                s.WriteObject(fs, appData);
            }
        }

        private void ReadLog()
        {
            string path = GetHistoryFilePath();
            if (File.Exists(path) == false)
            {
                log = new LogData()
                {
                    Items = new List<LogItem>()
                };
                return;
            }
            try
            {
                using (FileStream fs = new FileStream(path, FileMode.Open))
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
            string path = GetHistoryFilePath();
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                using (FileStream fs = new FileStream(path, FileMode.Create))
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

        private string MakeAmatsukazeArgs(bool isGeneric, string src, string dst, out string json, out string log)
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
            if(isGeneric)
            {
                sb.Append("--mode g ");
            }
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

            if (appData.setting.AutoBuffer)
            {
                sb.Append(" --bitrate ")
                    .Append(appData.setting.Bitrate.A)
                    .Append(":")
                    .Append(appData.setting.Bitrate.B)
                    .Append(":")
                    .Append(appData.setting.Bitrate.H264);
            }

            if (appData.setting.TwoPass)
            {
                sb.Append(" --2pass");
            }

            return sb.ToString();
        }

        private async Task RedirectOut(int index, Stream stream)
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
                    while (consoleList.Count <= index)
                    {
                        consoleList.Add(new ConsoleText());
                    }
                    consoleList[index].AddBytes(buffer, 0, readBytes);

                    byte[] newbuf = new byte[readBytes];
                    Array.Copy(buffer, newbuf, readBytes);
                    await client.OnConsoleUpdate(new ConsoleUpdate() { index = index, data = newbuf });
                }
            }
            catch (Exception e)
            {
                Debug.Print("RedirectOut exception " + e.Message);
            }
        }

        private LogItem LogFromJson(bool isGeneric, string jsonpath, DateTime start, DateTime finish)
        {
            var json = DynamicJson.Parse(File.ReadAllText(jsonpath));
            var outpath = new List<string>();
            foreach (var path in json.outpath)
            {
                outpath.Add(path);
            }
            if(isGeneric)
            {
                return new LogItem() {
                    Success = true,
                    SrcPath = json.srcpath,
                    OutPath = outpath,
                    SrcFileSize = (long)json.srcfilesize,
                    OutFileSize = (long)json.outfilesize,
                    MachineName = Dns.GetHostName(),
                    EncodeStartDate = start,
                    EncodeFinishDate = finish
                };
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

        private async Task ProcessDiretoryItem(int parallelId, QueueDirectory dir)
        {
            string succeeded = Path.Combine(dir.Path, "succeeded");
            string failed = Path.Combine(dir.Path, "failed");
            string encoded = Path.Combine(dir.Path, "encoded");
            Directory.CreateDirectory(succeeded);
            Directory.CreateDirectory(failed);
            Directory.CreateDirectory(encoded);

            int failCount = 0;

            // 待たなくてもいいタスクリスト
            var waitList = new List<Task>();

            var itemList = dir.Items.ToArray();
            while (dir.CurrentHead < itemList.Length)
            {
                if(failCount > 0)
                {
                    int waitSec = (failCount * 10 + 10);
                    waitList.Add(AddEncodeLog("エンコードに失敗したので"+ waitSec + "秒待機します。"));
                    await Task.Delay(waitSec * 1000);
                }

                if (dir.CurrentHead >= itemList.Length)
                {
                    break;
                }
                var src = itemList[dir.CurrentHead++];

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

                bool isMp4 = src.Path.ToLower().EndsWith(".mp4");
                string dst = Path.Combine(encoded, Path.GetFileName(src.Path));
                string json, logpath;
                string args = MakeAmatsukazeArgs(isMp4, src.Path, dst, out json, out logpath);
                string exename = appData.setting.AmatsukazePath;

                Util.AddLog("エンコード開始: " + src.Path);
                Util.AddLog("Args: " + exename + " " + args);

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
                try
                {
                    using (var p = Process.Start(psi))
                    {
                        using (logWriter = File.Create(logpath))
                        {
                            await Task.WhenAll(
                                RedirectOut(parallelId, p.StandardOutput.BaseStream),
                                RedirectOut(parallelId, p.StandardError.BaseStream),
                                Task.Run(() => p.WaitForExit()));
                        }

                        exitCode = p.ExitCode;
                    }
                }
                catch (Win32Exception w32e)
                {
                    Util.AddLog("Amatsukazeプロセス起動に失敗");
                    throw w32e;
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
                    File.Move(src.Path, succeeded + "\\" + Path.GetFileName(src.Path));
                    log.Items.Add(LogFromJson(isMp4, json, start, finish));
                    WriteLog();

                    failCount = 0;
                }
                else
                {
                    // 失敗
                    File.Move(src.Path, failed + "\\" + Path.GetFileName(src.Path));

                    log.Items.Add(new LogItem() {
                        Success = false,
                        SrcPath = src.Path, 
                        MachineName = Dns.GetHostName(),
                        Reason = "Amatsukaze.exeはコード" + exitCode + "で終了しました。"
                    });
                    WriteLog();

                    ++failCount;
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

                    Task[] tasks = new Task[appData.setting.NumParallel];
                    for (int i = 0; i < tasks.Length; ++i)
                    {
                        tasks[i] = ProcessDiretoryItem(i, dir);
                    }
                    await Task.WhenAll(tasks);

                    if (encodePaused)
                    {
                        break;
                    }
                    queue.Remove(dir);
                    waitList.Add(client.OnQueueUpdate(new QueueUpdate() {
                        Type = UpdateType.Remove,
                        DirPath = dir.Path,
                    }));
                }
            }
            catch(Exception e)
            {
                waitList.Add(AddEncodeLog(
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
            if (string.IsNullOrEmpty(appData.setting.AlwaysShowDisk) == false)
            {
                foreach (var path in appData.setting.AlwaysShowDisk.Split(';'))
                {
                    if (string.IsNullOrEmpty(path))
                    {
                        continue;
                    }
                    try
                    {
                        var diskPath = Path.GetPathRoot(path);
                        diskMap.Add(diskPath, MakeDiskItem(diskPath));
                    }
                    catch (Exception e)
                    {
                        Util.AddLog("ディスク情報取得失敗: " + e.Message);
                    }
                }
            }
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
                AddEncodeLog("設定を更新しました"));
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
                Items = Directory.GetFiles(dirPath).Where(s => {
                    string lower = s.ToLower();
                    return lower.EndsWith(".ts") || lower.EndsWith(".m2t") || lower.EndsWith(".mp4");
                }).Select(f => new QueueItem() { Path = f }).ToList()
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
            Task[] tasks = new Task[consoleList.Count];
            for (int i = 0; i < consoleList.Count; ++i)
            {
                tasks[i] = client.OnConsole(new ConsoleData()
                {
                    index = i,
                    text = consoleList[i].TextLines as List<string>
                });
            }
            return Task.WhenAll(tasks);
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
