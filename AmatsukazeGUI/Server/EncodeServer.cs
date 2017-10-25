using Amatsukaze.Models;
using Codeplex.Data;
using Livet;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Configuration;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;

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
                    server.AddQueue((AddQueueDirectory)arg);
                    break;
                case RPCMethodId.RemoveQueue:
                    server.RemoveQueue((string)arg);
                    break;
                case RPCMethodId.PauseEncode:
                    server.PauseEncode((bool)arg);
                    break;
                case RPCMethodId.SetServiceSetting:
                    server.SetServiceSetting((ServiceSettingElement)arg);
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
                case RPCMethodId.RequestServiceSetting:
                    server.RequestServiceSetting();
                    break;
                case RPCMethodId.RequestLogoData:
                    server.RequestLogoData((string)arg);
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

        public Task OnServiceSetting(ServiceSettingElement service)
        {
            return Send(RPCMethodId.OnServiceSetting, service);
        }

        public Task OnLlsCommandFiles(JLSCommandFiles files)
        {
            return Send(RPCMethodId.OnLlsCommandFiles, files);
        }

        public Task OnLogoData(LogoData logoData)
        {
            return Send(RPCMethodId.OnLogoData, logoData);
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
            [DataMember]
            public ServiceSetting services;

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

        enum PipeCommand
        {
            FinishAnalyze = 1,
            StartFilter,
            FinishFilter,
            FinishEncode,
            Error
        }

        private class TranscodeTask
        {
            public TranscodeThread thread;
            public QueueItem src;
            public FileStream logWriter;
            public Process process;
            public PipeStream writePipe;
            public string taskjson;
            public FragmentTask[] fragments;
            public int numFinishedEncoders;
            public bool errorDetected;

            private Task nofity(PipeCommand cmd)
            {
                return writePipe.WriteAsync(BitConverter.GetBytes((int)cmd), 0, 4);
            }

            public Task notifyError()
            {
                return nofity(PipeCommand.Error);
            }

            public Task notifyEncodeFinish()
            {
                return nofity(PipeCommand.FinishEncode);
            }

            public Task startFilter()
            {
                return nofity(PipeCommand.StartFilter);
            }
        }

        private class FragmentTask
        {
            public TranscodeTask parent;
            public int index;
            public int numFrames;
            public string infoFile;
            public string tmpFile;
            public BufferBlock<bool> filterFinish;
            public Process encodeProccess;
            public long tmpFileBytes;
        }

        private class TranscodeThread
        {
            public int id;
            public TranscodeTask current;
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
                if (current != null)
                {
                    // もう殺すのでエラー通知の必要はない
                    current.errorDetected = true;

                    if (current.fragments != null)
                    {
                        // エンコードプロセスをKill
                        foreach (var frag in current.fragments)
                        {
                            if (frag.encodeProccess != null)
                            {
                                frag.encodeProccess.Kill();
                            }
                        }
                    }
                    // 本体をKill
                    if (current.process != null)
                    {
                        current.process.Kill();
                    }
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

            private async Task RedirectOut(EncodeServer server, TranscodeTask transcode, Stream stream)
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
                        if(transcode.logWriter != null)
                        {
                            transcode.logWriter.Write(buffer, 0, readBytes);
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

            private void MakeFragments(TranscodeTask task, string jsonstring)
            {
                var json = DynamicJson.Parse(jsonstring);
                task.fragments = new FragmentTask[(int)json.numparts];
                var infofiles = new List<string>();
                var tmpfiles = new List<string>();
                var frames = new List<int>();
                foreach (var obj in json.files)
                {
                    infofiles.Add(obj.infopath);
                    tmpfiles.Add(obj.tmppath);
                    frames.Add(obj.numframe);
                }
                for (int i = 0; i < task.fragments.Length; ++i)
                {
                    task.fragments[i].index = i;
                    task.fragments[i].parent = task;
                    task.fragments[i].numFrames = frames[i];
                    task.fragments[i].infoFile = infofiles[i];
                    task.fragments[i].tmpFile = tmpfiles[i];
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
                    Incident = incident
                };
            }

            private async Task ReadBytes(PipeStream readPipe, byte[] buf)
            {
                int readBytes = 0;
                while (readBytes < buf.Length)
                {
                    readBytes += await readPipe.ReadAsync(buf, readBytes, buf.Length - readBytes);
                }
            }

            private async Task<PipeCommand> ReadCommand(PipeStream readPipe)
            {
                byte[] buf = new byte[4];
                await ReadBytes(readPipe, buf);
                return (PipeCommand)BitConverter.ToInt32(buf, 0);
            }

            private async Task<string> ReadString(PipeStream readPipe)
            {
                byte[] buf = new byte[4];
                await ReadBytes(readPipe, buf);
                int bytes = BitConverter.ToInt32(buf, 0);
                buf = new byte[bytes];
                await ReadBytes(readPipe, buf);
                return Encoding.UTF8.GetString(buf);
            }

            private async Task HostThread(EncodeServer server, TranscodeTask transcode, PipeStream readPipe, PipeStream writePipe)
            {
                if (!server.appData.setting.EnableFilterTmp)
                {
                    return;
                }

                try
                {
                    int numFilterCompelte = 0;
                    // 子プロセスが終了するまでループ
                    while (true)
                    {
                        var cmd = await ReadCommand(readPipe);
                        switch (cmd)
                        {
                            case PipeCommand.FinishAnalyze:
                                MakeFragments(transcode, await ReadString(readPipe));
                                server.filterQueue.Post(transcode);
                                break;
                            case PipeCommand.FinishFilter:
                                server.encoderQueue.Post(transcode.fragments[numFilterCompelte++]);
                                break;
                        }
                    }
                }
                catch(Exception e)
                {
                    if (transcode.numFinishedEncoders >= transcode.fragments.Length)
                    {
                        // もう終了した
                        return;
                    }

                    // エラー
                    Debug.Print("Pipe exception " + e.Message);

                    // エンコード中のタスクは強制終了
                    KillProcess();

                    // 一時ファイルを消す
                    foreach(var fragment in transcode.fragments)
                    {
                        if(File.Exists(fragment.tmpFile))
                        {
                            File.Delete(fragment.tmpFile);
                        }
                    }

                    // Amatsukazeの戻り値でエラーは通知されるので
                    // ここで通知する必要はない
                }
            }

            private async Task<LogItem> ProcessItem(EncodeServer server, QueueItem src)
            {
                DateTime now = DateTime.Now;

                if (File.Exists(src.Path) == false)
                {
                    return FailLogItem(src.Path, "入力ファイルが見つかりません", now, now);
                }

                var serviceSetting = server.appData.services.ServiceMap[src.ServiceId];
                var logofiles = serviceSetting.LogoSettings
                    .Where(s => s.CanUse(src.TsTime))
                    .Select(s => s.FileName)
                    .ToArray();
                if(logofiles.Length == 0)
                {
                    // これは必要ないはず
                    src.FailReason = "ロゴ設定がありません";
                    return null;
                }
                bool errorOnNologo = logofiles.All(path => path != LogoSetting.NO_LOGO);
                var logopaths = logofiles.Where(path => path != LogoSetting.NO_LOGO).ToArray();

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

                AnonymousPipeServerStream readPipe = null, writePipe = null;
                string outHandle = null, inHandle = null;
                if(server.appData.setting.EnableFilterTmp)
                {
                    readPipe = new AnonymousPipeServerStream(PipeDirection.In, HandleInheritability.Inheritable);
                    writePipe = new AnonymousPipeServerStream(PipeDirection.Out, HandleInheritability.Inheritable);
                    outHandle = readPipe.ClientSafePipeHandle.DangerousGetHandle().ToInt64().ToString();
                    inHandle = writePipe.ClientSafePipeHandle.DangerousGetHandle().ToInt64().ToString();
                }

                string json = Path.Combine(
                    Path.GetDirectoryName(localdst),
                    Path.GetFileNameWithoutExtension(localdst)) + "-enc.json";
                string taskjson = Path.Combine(
                    Path.GetDirectoryName(localdst),
                    Path.GetFileNameWithoutExtension(localdst)) + "-task.json";
                string logpath = Path.Combine(
                    Path.GetDirectoryName(dstpath),
                    Path.GetFileNameWithoutExtension(dstpath)) + "-enc.log";
                string jlscmd = serviceSetting.DisableCMCheck ? 
                    null : 
                    (string.IsNullOrEmpty(serviceSetting.JLSCommand) ?
                    server.appData.setting.DefaultJLSCommand :
                    serviceSetting.JLSCommand);

                string args = server.MakeAmatsukazeArgs(isMp4, srcpath, localdst, json, taskjson,
                    inHandle, outHandle, src.ServiceId, logopaths, errorOnNologo, jlscmd);
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

                int exitCode = -1;
                logText.Clear();

                try
                {
                    using (var p = Process.Start(psi))
                    {
                        try
                        {
                            if (server.appData.setting.EnableFilterTmp == false)
                            {
                                // アフィニティを設定
                                IntPtr affinityMask = new IntPtr((long)server.affinityCreator.GetMask(id));
                                Util.AddLog(id, "AffinityMask: " + affinityMask.ToInt64());
                                p.ProcessorAffinity = affinityMask;
                            }
                            p.PriorityClass = ProcessPriorityClass.BelowNormal;
                        }
                        catch(InvalidOperationException)
                        {
                            // 既にプロセスが終了していると例外が出るが無視する
                        }

                        if(readPipe != null)
                        {
                            // クライアントハンドルを閉じる
                            readPipe.DisposeLocalCopyOfClientHandle();
                            writePipe.DisposeLocalCopyOfClientHandle();
                        }

                        current = new TranscodeTask()
                        {
                            thread = this,
                            src = src,
                            process = p,
                            taskjson = taskjson,
                            writePipe = writePipe
                        };

                        using (current.logWriter = File.Create(logpath))
                        {
                            await Task.WhenAll(
                                RedirectOut(server, current, p.StandardOutput.BaseStream),
                                RedirectOut(server, current, p.StandardError.BaseStream),
                                HostThread(server, current, readPipe, writePipe),
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
                catch(IOException ioe)
                {
                    Util.AddLog(id, "ログファイル生成に失敗");
                    throw ioe;
                }
                finally
                {
                    if(readPipe != null)
                    {
                        readPipe.Close();
                        writePipe.Close();
                    }
                    current.logWriter = null;
                    current = null;
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
                encoded = dir.DstPath;
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

                    LogItem logItem = null;

                    server.UpdateQueueItem(src);
                    if (src.State == QueueState.Queue)
                    {
                        src.State = QueueState.Encoding;
                        waitList.Add(server.NotifyQueueItemUpdate(src, dir));
                        logItem = await ProcessItem(server, src);
                    }

                    if (logItem == null)
                    {
                        // ペンディング
                        src.State = QueueState.LogoPending;
                        // 他の項目も更新しておく
                        waitList.AddRange(server.UpdateQueueItems());
                        // 一旦タスクを消化
                        await Task.WhenAll(waitList.ToArray());
                        waitList.Clear();
                    }
                    else
                    {
                        if (logItem.Success)
                        {
                            File.Move(src.Path, succeeded + "\\" + Path.GetFileName(src.Path));
                            failCount = 0;
                            src.State = QueueState.Complete;
                        }
                        else
                        {
                            File.Move(src.Path, failed + "\\" + Path.GetFileName(src.Path));
                            src.State = QueueState.Failed;
                            ++failCount;
                        }
                        server.log.Items.Add(logItem);
                        server.WriteLog();
                        waitList.Add(server.client.OnLogUpdate(server.log.Items.Last()));
                    }

                    waitList.Add(server.NotifyQueueItemUpdate(src, dir));
                    waitList.Add(server.RequestFreeSpace());

                    if (server.encodePaused)
                    {
                        break;
                    }
                }

                await Task.WhenAll(waitList.ToArray());
            }

            public async Task<int> ExecEncoder(EncodeServer server, FragmentTask task, int cpuIndex)
            {
                string args = server.MakeEncodeTaskArgs(task);
                string exename = server.appData.setting.AmatsukazePath;

                var psi = new ProcessStartInfo(exename, args) {
                    UseShellExecute = false,
                    WorkingDirectory = Directory.GetCurrentDirectory(),
                    RedirectStandardError = true,
                    RedirectStandardOutput = true,
                    RedirectStandardInput = false,
                    CreateNoWindow = true
                };

                try
                {
                    using (var p = Process.Start(psi))
                    {
                        // アフィニティを設定
                        IntPtr affinityMask = new IntPtr((long)server.affinityCreator.GetMask(cpuIndex));
                        Util.AddLog(task.parent.thread.id, "AffinityMask: " + affinityMask.ToInt64());
                        p.ProcessorAffinity = affinityMask;
                        p.PriorityClass = ProcessPriorityClass.BelowNormal;

                        task.encodeProccess = p;

                        await Task.WhenAll(
                            RedirectOut(server, task.parent, p.StandardOutput.BaseStream),
                            RedirectOut(server, task.parent, p.StandardError.BaseStream),
                            Task.Run(() => p.WaitForExit()));

                        return p.ExitCode;
                    }
                }
                catch (Win32Exception w32e)
                {
                    Util.AddLog(id, "Amatsukazeプロセス起動に失敗");
                    throw w32e;
                }
                finally
                {
                    task.encodeProccess = null;
                }
            }
        }

        private class EncodeException : Exception
        {
            public EncodeException(string message)
                :base(message)
            {
            }
        }

        private const int NumFilterThreads = 1;

        private IUserClient client;
        public Task ServerTask { get; private set; }
        private AppData appData;

        private List<TranscodeThread> taskList = new List<TranscodeThread>();
        private BufferBlock<TranscodeTask> filterQueue;
        private BufferBlock<FragmentTask> encoderQueue;

        private long occupiedStorage;
        private BufferBlock<int> storageQ = new BufferBlock<int>();

        private List<QueueDirectory> queue = new List<QueueDirectory>();
        private LogData log;
        private SortedDictionary<string, DiskItem> diskMap = new SortedDictionary<string, DiskItem>();

        private AffinityCreator affinityCreator = new AffinityCreator();

        private JLSCommandFiles jlsFiles = new JLSCommandFiles() { Files = new List<string>() };

        // キューに追加されるTSを解析するスレッド
        private Task queueThread;
        private BufferBlock<AddQueueDirectory> queueQ = new BufferBlock<AddQueueDirectory>();

        // ロゴファイルやJLSコマンドファイルを監視するスレッド
        private Task watchFileThread;
        private BufferBlock<int> watchFileQ = new BufferBlock<int>();
        private bool serviceListUpdated;

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
            queueThread = QueueThread();
            watchFileThread = WatchFileThread();
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

                    queueQ.Complete();
                    watchFileQ.Complete();
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

        private string GetLogoDirectoryPath()
        {
            return "logo";
        }

        private string GetLogoFilePath(string fileName)
        {
            return GetLogoDirectoryPath() + "\\" + fileName;
        }

        private string GetJLDirectoryPath()
        {
            return "JL";
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
                        EncoderType = EncoderType.x264,
                        AmatsukazePath = Path.Combine(basePath, "Amatsukaze.exe"),
                        X264Path = GetExePath(basePath, "x264"),
                        X265Path = GetExePath(basePath, "x265"),
                        MuxerPath = Path.Combine(basePath, "muxer.exe"),
                        TimelineEditorPath = Path.Combine(basePath, "timelineeditor.exe"),
                        NumParallel = 1,
                        Bitrate = new BitrateSetting()
                    },
                    services = new ServiceSetting() {
                        ServiceMap = new Dictionary<int, ServiceSettingElement>()
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
                if(appData.services == null)
                {
                    appData.services = new ServiceSetting();
                }
                if (appData.services.ServiceMap == null)
                {
                    appData.services.ServiceMap = new Dictionary<int, ServiceSettingElement>();
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
            if (appData.setting.EncoderType == EncoderType.x264)
            {
                return appData.setting.X264Path;
            }
            else if(appData.setting.EncoderType == EncoderType.x265)
            {
                return appData.setting.X265Path;
            }
            else if(appData.setting.EncoderType == EncoderType.QSVEnc)
            {
                return appData.setting.QSVEncPath;
            }
            else
            {
                return appData.setting.NVEncPath;
            }
        }

        private string GetEncoderOption()
        {
            if (appData.setting.EncoderType == EncoderType.x264)
            {
                return appData.setting.X264Option;
            }
            else if (appData.setting.EncoderType == EncoderType.x265)
            {
                return appData.setting.X265Option;
            }
            else if (appData.setting.EncoderType == EncoderType.QSVEnc)
            {
                return appData.setting.QSVEncOption;
            }
            else
            {
                return appData.setting.NVEncOption;
            }
        }

        private string GetEncoderName()
        {
            if (appData.setting.EncoderType == EncoderType.x264)
            {
                return "x264";
            }
            else if (appData.setting.EncoderType == EncoderType.x265)
            {
                return "x265";
            }
            else if (appData.setting.EncoderType == EncoderType.QSVEnc)
            {
                return "QSVEnc";
            }
            else
            {
                return "NVEnc";
            }
        }

        private string MakeAmatsukazeArgs(bool isGeneric,
            string src, string dst, string json, string taskjson, string inHandle, string outHandle,
            int serviceId, string[] logofiles, bool errorOnNoLogo, string jlscommand)
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
            if (string.IsNullOrEmpty(appData.setting.Amt32bitPath))
            {
                throw new ArgumentException("Amt32bitPathパスが指定されていません");
            }
            if (string.IsNullOrEmpty(appData.setting.ChapterExePath))
            {
                throw new ArgumentException("ChapterExePathパスが指定されていません");
            }
            if (string.IsNullOrEmpty(appData.setting.JoinLogoScpPath))
            {
                throw new ArgumentException("JoinLogoScpPathパスが指定されていません");
            }
            if (string.IsNullOrEmpty(appData.setting.FilterPath))
            {
                throw new ArgumentException("FilterPathパスが指定されていません");
            }

            double bitrateCM = appData.setting.BitrateCM;
            if(bitrateCM == 0)
            {
                bitrateCM = 1;
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
                .Append(GetEncoderName())
                .Append(" -e \"")
                .Append(encoderPath)
                .Append("\" -m \"")
                .Append(appData.setting.MuxerPath)
                .Append("\" -t \"")
                .Append(appData.setting.TimelineEditorPath)
                .Append("\" -j \"")
                .Append(json)
                .Append("\" --32bitlib \"")
                .Append(appData.setting.Amt32bitPath)
                .Append("\" --chapter-exe \"")
                .Append(appData.setting.ChapterExePath)
                .Append("\" --jls \"")
                .Append(appData.setting.JoinLogoScpPath)
                .Append("\" -f \"")
                .Append(appData.setting.FilterPath)
                .Append("\" -s \"")
                .Append(serviceId)

                .Append("\"");

            string option = GetEncoderOption();
            if (string.IsNullOrEmpty(option) == false)
            {
                sb.Append(" -eo \"")
                    .Append(option)
                    .Append("\"");
            }

            if(bitrateCM != 1)
            {
                sb.Append(" -bcm ").Append(bitrateCM);
            }

            if(string.IsNullOrEmpty(jlscommand) == false)
            {
                sb.Append(" --jls-cmd \"")
                    .Append(GetJLDirectoryPath() + "\\" + jlscommand)
                    .Append("\"");
            }

            if (string.IsNullOrEmpty(appData.setting.PostFilterPath) == false)
            {
                sb.Append(" -pf \"")
                    .Append(appData.setting.PostFilterPath)
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

            string[] decoderNames = new string[] { "default", "QSV", "CUVID" };
            if (appData.setting.Mpeg2Decoder != DecoderType.Default)
            {
                sb.Append("  --mpeg2decoder ");
                sb.Append(decoderNames[(int)appData.setting.Mpeg2Decoder]);
            }
            if (appData.setting.H264Deocder != DecoderType.Default)
            {
                sb.Append("  --h264decoder ");
                sb.Append(decoderNames[(int)appData.setting.H264Deocder]);
            }

            if (appData.setting.TwoPass)
            {
                sb.Append(" --2pass");
            }
            if (errorOnNoLogo)
            {
                sb.Append(" --error-on-no-logo");
            }
            if(inHandle != null)
            {
                sb.Append(" --in-pipe ").Append(inHandle);
                sb.Append(" --out-pipe ").Append(outHandle);
            }
            if(logofiles != null)
            {
                foreach(var logo in logofiles)
                {
                    sb.Append(" --logo \"").Append(GetLogoFilePath(logo)).Append("\"");
                }
            }

            return sb.ToString();
        }

        private string MakeEncodeTaskArgs(FragmentTask fragment)
        {
            StringBuilder sb = new StringBuilder();
            sb.Append("--mode enctask ");
            sb.Append("--info \"").Append(fragment.infoFile)
                .Append("\" -i \"").Append(fragment.tmpFile)
                .Append("\"");

            return sb.ToString();
        }

        private Task NotifyQueueItemUpdate(QueueItem item, QueueDirectory dir)
        {
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

        private Task StartEncodeWhenNotStarted()
        {
            if(nowEncoding == false && encodePaused == false)
            {
                return StartEncode();
            }
            return Task.FromResult(0);
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
                    var dir = queue.FirstOrDefault(d => d.Items.Any(item => item.State == QueueState.Queue));
                    if (dir == null)
                    {
                        // 処理できるのがない
                        break;
                    }

                    // 不正な設定は強制的に直しちゃう
                    if (appData.setting.NumParallel <= 0 ||
                        appData.setting.NumParallel > 64)
                    {
                        appData.setting.NumParallel = 1;
                    }

                    int numParallelAnalyze = appData.setting.NumParallel;
                    int numParallelEncode = appData.setting.NumParallel;

                    if (appData.setting.EnableFilterTmp)
                    {
                        numParallelAnalyze += 2;

                        // キュー作成
                        filterQueue = new BufferBlock<TranscodeTask>();
                        encoderQueue = new BufferBlock<FragmentTask>();
                    }

                    // 足りない場合は追加
                    while (taskList.Count < numParallelAnalyze)
                    {
                        taskList.Add(new TranscodeThread() {
                            id = taskList.Count,
                            consoleText = new ConsoleText(500),
                            logText = new ConsoleText(1 * 1024 * 1024)
                        });
                    }
                    // 多すぎる場合は削除
                    while (taskList.Count > numParallelAnalyze)
                    {
                        taskList.RemoveAt(taskList.Count - 1);
                    }

                    affinityCreator.NumProcess = numParallelEncode;

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

                    var threads = new List<Task>();
                    if (appData.setting.EnableFilterTmp)
                    {
                        for(int i = 0; i < NumFilterThreads; ++i)
                        {
                            threads.Add(FilterThread());
                        }
                        for (int i = 0; i < numParallelEncode; ++i)
                        {
                            threads.Add(EncodeThread(i));
                        }
                    }

                    occupiedStorage = 0;

                    var tasks = new List<Task>();
                    for (int i = 0; i < numParallelAnalyze; ++i)
                    {
                        taskList[i].hashList = hashList;
                        taskList[i].tmpBase = Util.CreateTmpFile(appData.setting.WorkPath);
                        tasks.Add(taskList[i].ProcessDiretoryItem(this, dir));
                    }

                    await Task.WhenAll(tasks);

                    if (appData.setting.EnableFilterTmp)
                    {
                        filterQueue.Complete();
                        encoderQueue.Complete();
                        await Task.WhenAll(threads);
                    }

                    for (int i = 0; i < numParallelAnalyze; ++i)
                    {
                        File.Delete(taskList[i].tmpBase);
                    }

                    if (encodePaused)
                    {
                        break;
                    }

                    // 完了したファイルを消す
                    dir.Items = dir.Items.Where(item =>
                    (item.State != QueueState.Complete && item.State != QueueState.Failed)).ToList();

                    if(dir.Items.Count == 0)
                    {
                        queue.Remove(dir);
                    }

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

        private async Task FilterThread()
        {
            var finishQ = new BufferBlock<bool>();

            try
            {
                while (await filterQueue.OutputAvailableAsync())
                {
                    TranscodeTask task = await filterQueue.ReceiveAsync();

                    foreach (var fragment in task.fragments)
                    {
                        // TODO: フィルタ出力推定値を計算
                        long estimatedTmpBytes = 100000;

                        // テンポラリ容量を見る
                        int occupied = (int)((occupiedStorage + estimatedTmpBytes) / (1024 * 1024 * 1024));
                        if (occupied > appData.setting.MaxTmpGB)
                        {
                            // 容量を超えるのでダメ
                            await storageQ.ReceiveAsync();
                            continue;
                        }

                        // 推定値を足してフィルタ処理開始
                        occupiedStorage += estimatedTmpBytes;
                        fragment.filterFinish = finishQ;

                        try
                        {
                            await task.startFilter();

                            // フィルタ終了を待つ
                            bool success = await fragment.filterFinish.ReceiveAsync();
                            if (success == false)
                            {
                                // 失敗した
                                // 最初に失敗を発見したHostThreadがエラー処理するのでここでは何もしない
                                break;
                            }

                            // 本当の値を足す
                            fragment.tmpFileBytes = new System.IO.FileInfo(fragment.tmpFile).Length;
                            occupiedStorage += fragment.tmpFileBytes;

                        }
                        catch (Exception)
                        {
                            // 何らかのエラーが発生した
                            break;
                        }
                        finally
                        {
                            fragment.filterFinish = null;

                            occupiedStorage -= estimatedTmpBytes;
                            if (storageQ.Count < NumFilterThreads)
                            {
                                storageQ.Post(0);
                            }
                        }
                    }
                }
            }
            catch(Exception exception)
            {
                await client.OnOperationResult("FilterThreadがエラー終了しました: " + exception.Message);
            }
        }

        private async Task EncodeThread(int cpuIndex)
        {
            try
            {
                while (await encoderQueue.OutputAvailableAsync())
                {
                    FragmentTask task = await encoderQueue.ReceiveAsync();

                    int exitCode = await task.parent.thread.ExecEncoder(this, task, cpuIndex);

                    // エンコードが終了したので一時ファイルを消して空ける
                    File.Delete(task.tmpFile);

                    occupiedStorage -= task.tmpFileBytes;
                    if (storageQ.Count < NumFilterThreads)
                    {
                        storageQ.Post(0);
                    }

                    if (exitCode != 0)
                    {
                        if (task.parent.errorDetected == false)
                        {
                            // まだ、エラー未通知なら通知する
                            task.parent.errorDetected = true;

                            await task.parent.notifyError();
                            // エラー通知を受けてプロセスは終了し、
                            // HostThreadがエラーを発見するはずなので、ここでは何もしない
                        }
                    }
                    else
                    {
                        if (++task.parent.numFinishedEncoders >= task.parent.fragments.Length)
                        {
                            // 全て終了したら通知
                            await task.parent.notifyEncodeFinish();
                        }
                    }
                }
            }
            catch (Exception exception)
            {
                await client.OnOperationResult("EncodeThreadがエラー終了しました: " + exception.Message);
            }
        }

        private async Task QueueThread()
        {
            try
            {
                while (await queueQ.OutputAvailableAsync())
                {
                    AddQueueDirectory dir = await queueQ.ReceiveAsync();

                    // 既に追加されているファイルは除外する
                    var ignoreSet = new HashSet<string>(queue
                        .Where(t => t.Path == dir.DirPath)
                        .SelectMany(t => t.Items)
                        .Select(item => item.Path));

                    var items = ((dir.Targets != null)
                        ? dir.Targets
                        : Directory.GetFiles(dir.DirPath)
                            .Where(s => {
                                string lower = s.ToLower();
                                return lower.EndsWith(".ts") || lower.EndsWith(".m2t") || lower.EndsWith(".mp4");
                            }))
                        .Where(f => !ignoreSet.Contains(f));

                    if (dir.DstPath != null && Directory.Exists(dir.DstPath) == false)
                    {
                        await client.OnOperationResult(
                            "出力先フォルダが存在しません:" + dir.DstPath);
                        return;
                    }

                    var target = new QueueDirectory() {
                        Path = dir.DirPath,
                        Items = new List<QueueItem>(),
                        DstPath = (dir.DstPath != null) ? dir.DstPath : Path.Combine(dir.DirPath, "encoded")
                    };

                    // TSファイル情報を読む
                    List<Task> waitItems = new List<Task>();
                    foreach (var filepath in items)
                    {
                        var info = new TsInfo(amtcontext);
                        if (info.ReadFile(filepath))
                        {
                            var list = info.GetProgramList();
                            if (list.Length > 0 && list[0].HasVideo)
                            {
                                var service = info.GetServiceList().Where(s => s.ServiceId == list[0].ServiceId).FirstOrDefault();
                                target.Items.Add(new QueueItem() {
                                    Path = filepath,
                                    ServiceId = list[0].ServiceId,
                                    ImageWidth = list[0].Width,
                                    ImageHeight = list[0].Height,
                                    TsTime = info.GetTime(),
                                    JlsCommand = appData.setting.DefaultJLSCommand,
                                    ServiceName = (service.ServiceId != 0) ? service.ServiceName : "Unknown",
                                    State = QueueState.LogoPending
                                });
                                continue;
                            }
                        }
                        waitItems.Add(client.OnOperationResult("TS情報解析失敗: " + filepath));
                    }

                    // ロゴファイルを探す
                    var map = appData.services.ServiceMap;
                    foreach (var item in target.Items)
                    {
                        if (map.ContainsKey(item.ServiceId))
                        {
                            if (map[item.ServiceId].LogoSettings.Any(s => s.CanUse(item.TsTime)))
                            {
                                // OK
                                item.State = QueueState.Queue;
                                continue;
                            }
                        }
                        else
                        {
                            // 新しいサービスを登録
                            var newElement = new ServiceSettingElement() {
                                ServiceId = item.ServiceId,
                                ServiceName = item.ServiceName,
                                LogoSettings = new List<LogoSetting>()
                            };
                            map.Add(item.ServiceId, newElement);
                            serviceListUpdated = true;
                            waitItems.Add(client.OnServiceSetting(newElement));
                        }
                    }

                    if (target.Items.Count == 0)
                    {
                        await client.OnOperationResult(
                            "エンコード対象ファイルが見つかりません。パス:" + dir.DirPath);
                        return;
                    }

                    queue.Add(target);
                    waitItems.Add(client.OnQueueUpdate(new QueueUpdate() {
                        Type = UpdateType.Add,
                        Directory = target
                    }));
                    waitItems.Add(RequestFreeSpace());

                    waitItems.Add(StartEncodeWhenNotStarted());

                    await Task.WhenAll(waitItems.ToArray());
                }
            }
            catch (Exception exception)
            {
                await client.OnOperationResult("QueueThreadがエラー終了しました: " + exception.Message);
            }
        }

        private bool UpdateQueueItem(QueueItem item)
        {
            var map = appData.services.ServiceMap;
            var prevState = item.State;
            if (map.ContainsKey(item.ServiceId) == false)
            {
                item.FailReason = "このTSに対する設定がありません";
                item.State = QueueState.LogoPending;
            }
            else if (map[item.ServiceId].LogoSettings.Any(s => s.CanUse(item.TsTime)) == false)
            {
                item.FailReason = "ロゴ設定がありません";
                item.State = QueueState.LogoPending;
            }
            else
            {
                // OK
                item.FailReason = "";
                item.State = QueueState.Queue;
            }
            return prevState != item.State;
        }

        private List<Task> UpdateQueueItems()
        {
            List<Task> tasklist = new List<Task>();
            var map = appData.services.ServiceMap;
            foreach (var dir in queue)
            {
                foreach(var item in dir.Items)
                {
                    if (item.State != QueueState.LogoPending && item.State != QueueState.Queue)
                    {
                        continue;
                    }
                    if(UpdateQueueItem(item))
                    {
                        tasklist.Add(NotifyQueueItemUpdate(item, dir));
                    }
                }
            }
            return tasklist;
        }

        private bool ReadLogoFile(LogoSetting setting, string filepath)
        {
            try
            {
                var logo = new LogoFile(amtcontext, filepath);

                setting.FileName = Path.GetFileName(filepath);
                setting.LogoName = logo.Name;
                setting.ServiceId = logo.ServiceId;

                return true;
            }
            catch(IOException)
            {
                return false;
            }
        }

        private async Task WatchFileThread()
        {
            try
            {
                var completion = watchFileQ.OutputAvailableAsync();

                var logoDirTime = DateTime.MinValue;
                var logoTime = new Dictionary<string,DateTime>();

                var jlsDirTime = DateTime.MinValue;

                // 初期化
                foreach (var service in appData.services.ServiceMap.Values)
                {
                    foreach (var logo in service.LogoSettings)
                    {
                        // 全てのロゴは存在しないところからスタート
                        logo.Exists = false;
                    }
                }

                while (true)
                {
                    string logopath = GetLogoDirectoryPath();
                    if (Directory.Exists(logopath))
                    {
                        var map = appData.services.ServiceMap;

                        var logoDict = new Dictionary<string, LogoSetting>();
                        foreach (var service in map.Values)
                        {
                            foreach (var logo in service.LogoSettings)
                            {
                                logoDict.Add(logo.FileName, logo);
                            }
                        }

                        var updatedServices = new List<int>();

                        var lastModified = Directory.GetLastWriteTime(logopath);
                        if (logoDirTime != lastModified || serviceListUpdated)
                        {
                            logoDirTime = lastModified;

                            // ファイルの個数が変わった or サービスリストが変わった

                            if (serviceListUpdated)
                            {
                                // サービスリストが分かったら再度追加処理
                                serviceListUpdated = false;
                                logoTime.Clear();
                            }

                            var newTime = new Dictionary<string, DateTime>();
                            foreach (var filepath in Directory.GetFiles(logopath)
                                .Where(s => s.EndsWith(".lgd", StringComparison.OrdinalIgnoreCase)))
                            {
                                newTime.Add(filepath, File.GetLastWriteTime(filepath));
                            }

                            foreach (var path in logoTime.Keys.Union(newTime.Keys))
                            {
                                var name = Path.GetFileName(path);
                                if (!newTime.ContainsKey(path))
                                {
                                    // 消えた
                                    if (logoDict.ContainsKey(name))
                                    {
                                        logoDict[name].Exists = false;
                                        updatedServices.Add(logoDict[name].ServiceId);
                                    }
                                }
                                else if (!logoTime.ContainsKey(path))
                                {
                                    // 追加された
                                    if (logoDict.ContainsKey(name))
                                    {
                                        if (logoDict[name].Exists == false)
                                        {
                                            logoDict[name].Exists = true;
                                            ReadLogoFile(logoDict[name], path);
                                            updatedServices.Add(logoDict[name].ServiceId);
                                        }
                                    }
                                    else
                                    {
                                        var setting = new LogoSetting();
                                        ReadLogoFile(setting, path);

                                        if (map.ContainsKey(setting.ServiceId))
                                        {
                                            setting.Exists = true;
                                            setting.Enabled = true;
                                            setting.From = new DateTime(2000, 1, 1);
                                            setting.To = new DateTime(2030, 12, 31);

                                            map[setting.ServiceId].LogoSettings.Add(setting);
                                            updatedServices.Add(setting.ServiceId);
                                        }
                                    }
                                }
                                else if (logoTime[path] != newTime[path])
                                {
                                    // 変更されたファイル
                                    if (logoDict.ContainsKey(name))
                                    {
                                        ReadLogoFile(logoDict[name], path);
                                        updatedServices.Add(logoDict[name].ServiceId);
                                    }
                                }
                            }

                            logoTime = newTime;
                        }
                        else
                        {
                            // ファイルは同じなので、個々のファイルの更新を見る
                            foreach (var key in logoTime.Keys)
                            {
                                var lastMod = File.GetLastWriteTime(key);
                                if (logoTime[key] != lastMod)
                                {
                                    logoTime[key] = lastMod;

                                    var name = Path.GetFileName(key);
                                    if (logoDict.ContainsKey(name))
                                    {
                                        ReadLogoFile(logoDict[name], key);
                                        updatedServices.Add(logoDict[name].ServiceId);
                                    }
                                }
                            }
                        }

                        if(updatedServices.Count > 0)
                        {
                            // 更新をクライアントに通知
                            foreach (var updatedServiceId in updatedServices.Distinct())
                            {
                                await client.OnServiceSetting(map[updatedServiceId]);
                            }
                            // キューを再始動
                            await Task.WhenAll(UpdateQueueItems());
                            await StartEncodeWhenNotStarted();
                        }
                    }
                    string jlspath = GetJLDirectoryPath();
                    if (Directory.Exists(jlspath))
                    {
                        var lastModified = Directory.GetLastWriteTime(jlspath);
                        if (jlsDirTime != lastModified)
                        {
                            jlsDirTime = lastModified;

                            jlsFiles.Files = Directory.GetFiles(jlspath)
                                .Select(s => Path.GetFileName(s)).ToList();
                            await client.OnLlsCommandFiles(jlsFiles);
                        }
                    }

                    if (await Task.WhenAny(completion, Task.Delay(2000)) == completion)
                    {
                        // 終了
                        return;
                    }
                }
            }
            catch (Exception exception)
            {
                await client.OnOperationResult("WatchFileThreadがエラー終了しました: " + exception.Message);
            }
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
                var diskPath = Path.GetPathRoot(item.DstPath);
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

        public Task AddQueue(AddQueueDirectory dir)
        {
            queueQ.Post(dir);
            return Task.FromResult(0);
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
            await StartEncodeWhenNotStarted();
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

        public async Task SetServiceSetting(ServiceSettingElement service)
        {
            var serviceMap = appData.services.ServiceMap;
            if(serviceMap.ContainsKey(service.ServiceId))
            {
                var old = serviceMap[service.ServiceId];
                if (old.LogoSettings.Count == service.LogoSettings.Count)
                {
                    // ロゴのExitsフラグだけはこちらのデータを継承させる
                    for (int i = 0; i < old.LogoSettings.Count; ++i)
                    {
                        service.LogoSettings[i].Exists = old.LogoSettings[i].Exists;
                    }
                    serviceMap[service.ServiceId] = service;
                    await Task.WhenAll(UpdateQueueItems());
                    await StartEncodeWhenNotStarted();
                }
            }
            await client.OnServiceSetting(service);
        }

        public async Task RequestServiceSetting()
        {
            var serviceMap = appData.services.ServiceMap;
            foreach(var service in serviceMap.Values)
            {
                await client.OnServiceSetting(service);
            }
        }

        private AMTContext amtcontext = new AMTContext();
        public Task RequestLogoData(string fileName)
        {
            string logopath = GetLogoFilePath(fileName);
            try
            {
                var logofile = new LogoFile(amtcontext, logopath);
                return client.OnLogoData(new LogoData() {
                    FileName = fileName,
                    ServiceId = logofile.ServiceId,
                    ImageWith = logofile.ImageWidth,
                    ImageHeight = logofile.ImageHeight,
                    Image = logofile.GetImage(0)
                });
            }
            catch(IOException exception)
            {
                return client.OnOperationResult(
                    "ロゴファイルを開けません。パス:" + logopath + "メッセージ: " + exception.Message);
            }
        }
    }
}
