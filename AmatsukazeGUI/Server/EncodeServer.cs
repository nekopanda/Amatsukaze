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

    public class EncodeServer : NotificationObject, IEncodeServer, IDisposable
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

            private int maxLines;

            public ConsoleText(int maxLines)
            {
                this.maxLines = maxLines;
            }

            public override void Clear()
            {
                base.Clear();
                TextLines.Clear();
            }

            public override void OnAddLine(string text)
            {
                if(TextLines.Count > maxLines)
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

        private class EncodeTask
        {
            public int id;
            public Process process;
            public FileStream logWriter;
            public ConsoleText consoleText;
            public ConsoleText logText;
            public Dictionary<string, byte[]> hashList;
            public string tmpBase;

            private string succeeded;
            private string failed;
            private string encoded;
            private List<Task> waitList;

            public void KillProcess()
            {
                if(process != null)
                {
                    process.Kill();
                }
            }

            private LogItem FailLogItem(string srcpath, string reason, DateTime start, DateTime finish)
            {
                return new LogItem() {
                    Success = false,
                    Reason = reason,
                    SrcPath = srcpath,
                    MachineName = Dns.GetHostName(),
                    EncodeStartDate = start,
                    EncodeFinishDate = finish
                };
            }

            private async Task RedirectOut(EncodeServer server, Stream stream)
            {
                try
                {
                    byte[] buffer = new byte[1024];
                    while (true)
                    {
                        var readBytes = await stream.ReadAsync(buffer, 0, buffer.Length);
                        if (readBytes == 0)
                        {
                            // 終了
                            return;
                        }
                        if(logWriter != null)
                        {
                            logWriter.Write(buffer, 0, readBytes);
                        }
                        consoleText.AddBytes(buffer, 0, readBytes);
                        logText.AddBytes(buffer, 0, readBytes);

                        byte[] newbuf = new byte[readBytes];
                        Array.Copy(buffer, newbuf, readBytes);
                        await server.client.OnConsoleUpdate(new ConsoleUpdate() { index = id, data = newbuf });
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
                if (isGeneric)
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
                int incident = (int)json.incident;
                return new LogItem() {
                    Success = (incident < 10),
                    Reason = (incident < 10) ? "" : "インシデントが多すぎます",
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
                    },
                    Pulldown = ((int)json.pulldown != 0),
                    Timecode = ((int)json.timecode != 0),
                    Incident = incident
                };
            }

            private async Task<LogItem> ProcessItem(EncodeServer server, QueueItem src)
            {
                DateTime now = DateTime.Now;

                if (File.Exists(src.Path) == false)
                {
                    return FailLogItem(src.Path, "入力ファイルが見つかりません", now, now);
                }

                bool isMp4 = src.Path.ToLower().EndsWith(".mp4");
                string dstpath = Path.Combine(encoded, Path.GetFileName(src.Path));
                string srcpath = src.Path;
                string localsrc = null;
                string localdst = dstpath;

                // ハッシュがある（ネットワーク経由）の場合はローカルにコピー
                if (hashList != null)
                {
                    localsrc = tmpBase + "-in" + Path.GetExtension(srcpath);
                    string name = Path.GetFileName(srcpath);
                    if (hashList.ContainsKey(name) == false)
                    {
                        return FailLogItem(src.Path, "入力ファイルのハッシュがありません", now, now);
                    }

                    byte[] hash = await HashUtil.CopyWithHash(srcpath, localsrc);
                    var refhash = hashList[name];
                    if(hash.SequenceEqual(refhash) == false)
                    {
                        File.Delete(localsrc);
                        return FailLogItem(src.Path, "コピーしたファイルのハッシュが一致しません", now, now);
                    }

                    srcpath = localsrc;
                    localdst = tmpBase + "-out.mp4";
                }

                string json = Path.Combine(
                    Path.GetDirectoryName(localdst),
                    Path.GetFileNameWithoutExtension(localdst)) + "-enc.json";
                string logpath = Path.Combine(
                    Path.GetDirectoryName(dstpath),
                    Path.GetFileNameWithoutExtension(dstpath)) + "-enc.log";
                string args = server.MakeAmatsukazeArgs(isMp4, srcpath, localdst, json);
                string exename = server.appData.setting.AmatsukazePath;

                Util.AddLog(id, "エンコード開始: " + src.Path);
                Util.AddLog(id, "Args: " + exename + " " + args);

                DateTime start = DateTime.Now;

                var psi = new ProcessStartInfo(exename, args) {
                    UseShellExecute = false,
                    WorkingDirectory = Directory.GetCurrentDirectory(),
                    RedirectStandardError = true,
                    RedirectStandardOutput = true,
                    RedirectStandardInput = false,
                    CreateNoWindow = true
                };

                IntPtr affinityMask = new IntPtr((long)server.affinityCreator.GetMask(id));
                Util.AddLog(id, "AffinityMask: " + affinityMask.ToInt64());

                int exitCode = -1;
                logText.Clear();

                try
                {
                    using (var p = Process.Start(psi))
                    {
                        // アフィニティを設定
                        p.ProcessorAffinity = affinityMask;
                        p.PriorityClass = ProcessPriorityClass.BelowNormal;

                        process = p;

                        using (logWriter = File.Create(logpath))
                        {
                            await Task.WhenAll(
                                RedirectOut(server, p.StandardOutput.BaseStream),
                                RedirectOut(server, p.StandardError.BaseStream),
                                Task.Run(() => p.WaitForExit()));
                        }

                        exitCode = p.ExitCode;
                    }
                }
                catch (Win32Exception w32e)
                {
                    Util.AddLog(id, "Amatsukazeプロセス起動に失敗");
                    throw w32e;
                }
                finally
                {
                    logWriter = null;
                    process = null;
                }

                DateTime finish = DateTime.Now;

                if(hashList != null)
                {
                    File.Delete(localsrc);
                }

                if (exitCode == 0)
                {
                    // 成功ならログを整形したテキストに置き換える
                    using (var fs = new StreamWriter(File.Create(logpath), Encoding.Default))
                    {
                        foreach (var str in logText.TextLines)
                        {
                            fs.WriteLine(str);
                        }
                    }
                }

                // ログファイルを専用フォルダにコピー
                if (File.Exists(logpath))
                {
                    string logbase = server.GetLogFileBase(start);
                    Directory.CreateDirectory(Path.GetDirectoryName(logbase));
                    string dstlog = logbase + ".txt";
                    File.Copy(logpath, dstlog);

                    if (File.Exists(json))
                    {
                        string dstjson = logbase + ".json";
                        File.Move(json, dstjson);
                        json = dstjson;
                    }
                }

                if (exitCode == 0)
                {
                    // 成功
                    var log = LogFromJson(isMp4, json, start, finish);

                    // ハッシュがある（ネットワーク経由）の場合はリモートにコピー
                    if (hashList != null)
                    {
                        log.SrcPath = src.Path;
                        string outbase = Path.GetDirectoryName(dstpath) + "\\" + Path.GetFileNameWithoutExtension(dstpath);
                        for (int i = 0; i < log.OutPath.Count; ++i)
                        {
                            string outext = Path.GetExtension(log.OutPath[i]);
                            string outpath = outbase + ((i == 0) ? outext : ("-" + i + outext));
                            var hash = await HashUtil.CopyWithHash(log.OutPath[i], outpath);
                            string name = Path.GetFileName(outpath);
                            HashUtil.AppendHash(Path.Combine(encoded, "_mp4.hash"), name, hash);
                            File.Delete(log.OutPath[i]);
                            log.OutPath[i] = outpath;
                        }
                    }

                    return log;
                }
                else
                {
                    // 失敗
                    return FailLogItem(src.Path,
                        "Amatsukaze.exeはコード" + exitCode + "で終了しました。", start, finish);
                }
            }

            public async Task ProcessDiretoryItem(EncodeServer server, QueueDirectory dir)
            {
                succeeded = Path.Combine(dir.Path, "succeeded");
                failed = Path.Combine(dir.Path, "failed");
                encoded = Path.Combine(dir.Path, "encoded");
                Directory.CreateDirectory(succeeded);
                Directory.CreateDirectory(failed);
                Directory.CreateDirectory(encoded);

                int failCount = 0;

                // 待たなくてもいいタスクリスト
                waitList = new List<Task>();

                var itemList = dir.Items.ToArray();
                while (dir.CurrentHead < itemList.Length)
                {
                    if (failCount > 0)
                    {
                        int waitSec = (failCount * 10 + 10);
                        waitList.Add(server.AddEncodeLog("エンコードに失敗したので" + waitSec + "秒待機します。(parallel=" + id + ")"));
                        await Task.Delay(waitSec * 1000);

                        if (server.encodePaused)
                        {
                            break;
                        }
                    }

                    if (dir.CurrentHead >= itemList.Length)
                    {
                        break;
                    }
                    var src = itemList[dir.CurrentHead++];

                    waitList.Add(server.UpdateItemState(true, false, src, dir));

                    var logItem = await ProcessItem(server, src);

                    if(logItem.Success)
                    {
                        File.Move(src.Path, succeeded + "\\" + Path.GetFileName(src.Path));
                        failCount = 0;
                    }
                    else
                    {
                        File.Move(src.Path, failed + "\\" + Path.GetFileName(src.Path));
                        ++failCount;
                    }

                    server.log.Items.Add(logItem);
                    server.WriteLog();
                    waitList.Add(server.UpdateItemState(false, true, src, dir));
                    waitList.Add(server.client.OnLogUpdate(server.log.Items.Last()));
                    waitList.Add(server.RequestFreeSpace());

                    if (server.encodePaused)
                    {
                        break;
                    }
                }

                await Task.WhenAll(waitList.ToArray());
            }
        }

        private class EncodeException : Exception
        {
            public EncodeException(string message)
                :base(message)
            {
            }
        }

        private IUserClient client;
        public Task ServerTask { get; private set; }
        private AppData appData;

        private List<EncodeTask> taskList = new List<EncodeTask>();
        private List<QueueDirectory> queue = new List<QueueDirectory>();
        private LogData log;
        private SortedDictionary<string, DiskItem> diskMap = new SortedDictionary<string, DiskItem>();

        private AffinityCreator affinityCreator = new AffinityCreator();

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

        #region IDisposable Support
        private bool disposedValue = false; // 重複する呼び出しを検出するには

        protected virtual void Dispose(bool disposing)
        {
            if (!disposedValue)
            {
                if (disposing)
                {
                    // TODO: マネージ状態を破棄します (マネージ オブジェクト)。

                    // 終了時にプロセスが残らないようにする
                    foreach(var task in taskList)
                    {
                        if(task != null)
                        {
                            task.KillProcess();
                        }
                    }
                }

                // TODO: アンマネージ リソース (アンマネージ オブジェクト) を解放し、下のファイナライザーをオーバーライドします。
                // TODO: 大きなフィールドを null に設定します。

                disposedValue = true;
            }
        }

        // TODO: 上の Dispose(bool disposing) にアンマネージ リソースを解放するコードが含まれる場合にのみ、ファイナライザーをオーバーライドします。
        // ~EncodeServer() {
        //   // このコードを変更しないでください。クリーンアップ コードを上の Dispose(bool disposing) に記述します。
        //   Dispose(false);
        // }

        // このコードは、破棄可能なパターンを正しく実装できるように追加されました。
        public void Dispose()
        {
            // このコードを変更しないでください。クリーンアップ コードを上の Dispose(bool disposing) に記述します。
            Dispose(true);
            // TODO: 上のファイナライザーがオーバーライドされる場合は、次の行のコメントを解除してください。
            // GC.SuppressFinalize(this);
        }
        #endregion

        #region Path
        private string GetSettingFilePath()
        {
            return "config\\AmatsukazeServer.xml";
        }

        private string GetHistoryFilePath()
        {
            return "data\\EncodeHistory.xml";
        }

        private string GetLogFileBase(DateTime start)
        {
            return "data\\logs\\" + start.ToString("yyyy-MM-dd_HHmmss.fff");
        }

        private string ReadLogFIle(DateTime start)
        {
            var logpath = GetLogFileBase(start) + ".txt";
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

        private static string GetExePath(string basePath, string pattern)
        {
            foreach(var path in Directory.GetFiles(basePath))
            {
                var fname = Path.GetFileName(path);
                if(fname.StartsWith(pattern) && fname.EndsWith(".exe"))
                {
                    return path;
                }
            }
            return null;
        }

        private void LoadAppData()
        {
            string path = GetSettingFilePath();
            if (File.Exists(path) == false)
            {
                string basePath = Path.GetDirectoryName(GetType().Assembly.Location);
                appData = new AppData() {
                    setting = new Setting() {
                        EncoderName = "x264",
                        AmatsukazePath = Path.Combine(basePath, "Amatsukaze.exe"),
                        X264Path = GetExePath(basePath, "x264"),
                        X265Path = GetExePath(basePath, "x265"),
                        MuxerPath = Path.Combine(basePath, "muxer.exe"),
                        TimelineEditorPath = Path.Combine(basePath, "timelineeditor.exe"),
                        NumParallel = 1,
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

        private string MakeAmatsukazeArgs(bool isGeneric, string src, string dst, string json)
        {
            string workPath = string.IsNullOrEmpty(appData.setting.WorkPath)
                ? "./" : appData.setting.WorkPath;
            string encoderPath = GetEncoderPath();
            
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
            if (appData.setting.Pulldown)
            {
                sb.Append(" --pulldown");
            }

            return sb.ToString();
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

        private Task AddEncodeLog(string str)
        {
            Util.AddLog(str);
            return client.OnOperationResult(str);
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
                    // 不正な設定は強制的に直しちゃう
                    if (appData.setting.NumParallel <= 0 ||
                        appData.setting.NumParallel > 64)
                    {
                        appData.setting.NumParallel = 1;
                    }

                    int numParallel = appData.setting.NumParallel;

                    // 足りない場合は追加
                    while (taskList.Count < numParallel)
                    {
                        taskList.Add(new EncodeTask() {
                            id = taskList.Count,
                            consoleText = new ConsoleText(500),
                            logText = new ConsoleText(1 * 1024 * 1024)
                        });
                    }
                    // 多すぎる場合は削除
                    while (taskList.Count > numParallel)
                    {
                        taskList.RemoveAt(taskList.Count - 1);
                    }

                    affinityCreator.NumProcess = numParallel;

                    var dir = queue[0];

                    Dictionary<string, byte[]> hashList = null;
                    if (dir.Path.StartsWith("\\\\"))
                    {
                        var hashpath = dir.Path + ".hash";
                        if (File.Exists(hashpath) == false)
                        {
                            throw new IOException("ハッシュファイルがありません: " + hashpath + "\r\n" +
                                "ネットワーク経由の場合はBatchHashCheckerによるハッシュファイル生成が必須です。");
                        }
                        hashList = HashUtil.ReadHashFile(hashpath);
                    }

                    Task[] tasks = new Task[numParallel];
                    for (int i = 0; i < numParallel; ++i)
                    {
                        taskList[i].hashList = hashList;
                        taskList[i].tmpBase = Util.CreateTmpFile(appData.setting.WorkPath);
                        tasks[i] = taskList[i].ProcessDiretoryItem(this, dir);
                    }

                    await Task.WhenAll(tasks);

                    for (int i = 0; i < numParallel; ++i)
                    {
                        File.Delete(taskList[i].tmpBase);
                    }

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
            return Task.WhenAll(taskList.Select(task => {
                return client.OnConsole(new ConsoleData() {
                    index = task.id,
                    text = task.consoleText.TextLines as List<string>
                });
            }));
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
