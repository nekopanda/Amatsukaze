using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.Serialization;
using System.Threading.Tasks;
using System.Threading;
using System.IO;

using Livet;
using Amatsukaze.Server;
using System.Collections.ObjectModel;

namespace Amatsukaze.Models
{
    public class DisplayQueueDirectory
    {
        public string Path { get; set; }
        public ObservableCollection<QueueItem> Items { get; set; }

        public DisplayQueueDirectory(QueueDirectory dir)
        {
            Path = dir.Path;
            Items = new ObservableCollection<QueueItem>(dir.Items);
        }

        public override string ToString()
        {
            return Path + " (" + Items.Count + ")";
        }
    }

    public class ConsoleText : ConsoleTextBase
    {
        #region TextLines変更通知プロパティ
        private ObservableCollection<string> _TextLines = new ObservableCollection<string>();

        public ObservableCollection<string> TextLines
        {
            get
            { return _TextLines; }
            set
            {
                if (_TextLines == value)
                    return;
                _TextLines = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public override void OnAddLine(string text)
        {
            if (TextLines.Count > 400)
            {
                TextLines.RemoveAt(0);
            }
            TextLines.Add(text);
        }

        public override void OnReplaceLine(string text)
        {
            if (TextLines.Count == 0)
            {
                TextLines.Add(text);
            }
            else
            {
                TextLines[TextLines.Count - 1] = text;
            }
        }

        public void SetTextLines(List<string> lines)
        {
            Clear();
            TextLines.Clear();
            foreach (var s in lines)
            {
                TextLines.Add(s);
            }
        }
    }

    public class ClientModel : NotificationObject, IUserClient
    {
        /*
         * NotificationObjectはプロパティ変更通知の仕組みを実装したオブジェクトです。
         */
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
        public IEncodeServer Server { get; private set; }
        public Task CommTask { get; private set; }
        private Setting setting = new Setting() { Bitrate = new BitrateSetting() };
        private State state = new State();

        public Func<object, string, Task> ServerAddressRequired;

        public string ServerIP
        {
            get { return appData.ServerIP; }
        }

        public int ServerPort
        {
            get { return appData.ServerPort; }
        }

        #region ConsoleTextLines変更通知プロパティ
        private ObservableCollection<ConsoleText> consoleList_ = new ObservableCollection<ConsoleText>();

        public ObservableCollection<ConsoleText> ConsoleList
        {
            get { return consoleList_; }
            set {
                if (consoleList_ == value)
                    return;
                consoleList_ = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region CurrentLogFile変更通知プロパティ
        private string _CurrentLogFile;

        public string CurrentLogFile
        {
            get
            { return _CurrentLogFile; }
            set
            { 
                if (_CurrentLogFile == value)
                    return;
                _CurrentLogFile = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LogItems変更通知プロパティ
        private ObservableCollection<LogItem> _LogItems = new ObservableCollection<LogItem>();

        public ObservableCollection<LogItem> LogItems
        {
            get
            { return _LogItems; }
            set
            { 
                if (_LogItems == value)
                    return;
                _LogItems = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region QueueItems変更通知プロパティ
        private ObservableCollection<DisplayQueueDirectory> _QueueItems = new ObservableCollection<DisplayQueueDirectory>();

        public ObservableCollection<DisplayQueueDirectory> QueueItems
        {
            get
            { return _QueueItems; }
            set
            { 
                if (_QueueItems == value)
                    return;
                _QueueItems = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region CurrentOperationResult変更通知プロパティ
        private string _CurrentOperationResult;

        public string CurrentOperationResult
        {
            get
            { return _CurrentOperationResult; }
            set
            { 
                if (_CurrentOperationResult == value)
                    return;
                _CurrentOperationResult = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ServerHostName変更通知プロパティ
        public string ServerHostName {
            get { return state.HostName; }
            set { 
                if (state.HostName == value)
                    return;
                state.HostName = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsPaused変更通知プロパティ
        public bool IsPaused
        {
            get { return state.Pause; }
            set
            { 
                if (state.Pause == value)
                    return;
                state.Pause = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsRunning変更通知プロパティ
        public bool IsRunning {
            get { return state.Running; }
            set { 
                if (state.Running == value)
                    return;
                state.Running = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AmatsukazePath変更通知プロパティ
        public string AmatsukazePath {
            get { return setting.AmatsukazePath; }
            set { 
                if (setting.AmatsukazePath == value)
                    return;
                setting.AmatsukazePath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region X264Path変更通知プロパティ
        public string X264Path {
            get { return setting.X264Path; }
            set { 
                if (setting.X264Path == value)
                    return;
                setting.X264Path = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region X265Path変更通知プロパティ
        public string X265Path {
            get { return setting.X265Path; }
            set { 
                if (setting.X265Path == value)
                    return;
                setting.X265Path = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region QSVEncPath変更通知プロパティ
        public string QSVEncPath {
            get { return setting.QSVEncPath; }
            set { 
                if (setting.QSVEncPath == value)
                    return;
                setting.QSVEncPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EncoderName変更通知プロパティ
        public string EncoderName {
            get { return setting.EncoderName; }
            set { 
                if (setting.EncoderName == value)
                    return;
                setting.EncoderName = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EncoderOption変更通知プロパティ
        public string EncoderOption {
            get { return setting.EncoderOption; }
            set { 
                if (setting.EncoderOption == value)
                    return;
                setting.EncoderOption = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MuxerPath変更通知プロパティ
        public string MuxerPath {
            get { return setting.MuxerPath; }
            set { 
                if (setting.MuxerPath == value)
                    return;
                setting.MuxerPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region TimelineEditorPath変更通知プロパティ
        public string TimelineEditorPath {
            get { return setting.TimelineEditorPath; }
            set { 
                if (setting.TimelineEditorPath == value)
                    return;
                setting.TimelineEditorPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region WorkPath変更通知プロパティ
        public string WorkPath {
            get { return setting.WorkPath; }
            set { 
                if (setting.WorkPath == value)
                    return;
                setting.WorkPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AlwaysShowDisk変更通知プロパティ
        public string AlwaysShowDisk
        {
            get
            { return setting.AlwaysShowDisk; }
            set
            {
                if (setting.AlwaysShowDisk == value)
                    return;
                setting.AlwaysShowDisk = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ClientLog変更通知プロパティ
        private ObservableCollection<string> _ClientLog = new ObservableCollection<string>();

        public ObservableCollection<string> ClientLog
        {
            get
            { return _ClientLog; }
            set
            { 
                if (_ClientLog == value)
                    return;
                _ClientLog = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DiskFreeSpace変更通知プロパティ
        private List<DiskItem> _DiskFreeSpace = new List<DiskItem>();

        public List<DiskItem> DiskFreeSpace {
            get { return _DiskFreeSpace; }
            set { 
                if (_DiskFreeSpace == value)
                    return;
                _DiskFreeSpace = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AutoBuffer変更通知プロパティ
        public bool AutoBuffer
        {
            get
            { return setting.AutoBuffer; }
            set
            { 
                if (setting.AutoBuffer == value)
                    return;
                setting.AutoBuffer = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region TwoPass変更通知プロパティ
        public bool TwoPass
        {
            get
            { return setting.TwoPass; }
            set
            {
                if (setting.TwoPass == value)
                    return;
                setting.TwoPass = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateA変更通知プロパティ
        public double BitrateA
        {
            get
            { return setting.Bitrate.A; }
            set
            {
                if (setting.Bitrate.A == value)
                    return;
                setting.Bitrate.A = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateB変更通知プロパティ
        public double BitrateB
        {
            get
            { return setting.Bitrate.B; }
            set
            {
                if (setting.Bitrate.B == value)
                    return;
                setting.Bitrate.B = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateH264変更通知プロパティ
        public double BitrateH264
        {
            get
            { return setting.Bitrate.H264; }
            set
            {
                if (setting.Bitrate.H264 == value)
                    return;
                setting.Bitrate.H264 = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public ClientModel()
        {
            Util.LogHandlers.Add(AddLog);

            AddLog("クライアント起動");

            LoadAppData();
        }

        private void ConsoleText_TextChanged()
        {
            RaisePropertyChanged("ConsoleText");
        }

        private void AddLog(string text)
        {
            var formatted = DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss") + " " + text;
            if (ClientLog.Count > 400)
            {
                ClientLog.RemoveAt(0);
            }
            ClientLog.Add(formatted);
        }

        public void Start()
        {
            if (App.Option.LaunchType == LaunchType.Standalone)
            {
                Server = new EncodeServer(0, this);
                Server.RefreshRequest();
            }
            else
            {
                var connection = new ServerConnection(this, AskServerAddress);
                Server = connection;
                CommTask = connection.Start();
            }
        }

        public void SetServerAddress(string serverIp, int port)
        {
            appData.ServerIP = serverIp;
            appData.ServerPort = port;
            if (Server is ServerConnection)
            {
                (Server as ServerConnection).SetServerAddress(serverIp, port);
            }
        }

        public void Reconnect()
        {
            if (Server is ServerConnection)
            {
                (Server as ServerConnection).Reconnect();
            }
        }

        public void Finish()
        {
            if (Server != null)
            {
                Server.Finish();
                Server = null;
            }
        }

        private bool firstAsked = true;
        private async Task AskServerAddress(string reason)
        {
            if (firstAsked)
            {
                (Server as ServerConnection).SetServerAddress(appData.ServerIP, appData.ServerPort);
                firstAsked = false;
            }
            else
            {
                await ServerAddressRequired(this, reason);
                SaveAppData();
            }
        }

        private string GetSettingFilePath()
        {
            return "config\\AmatsukazeClient.xml";
        }

        private void LoadAppData()
        {
            string path = GetSettingFilePath();
            if (File.Exists(path) == false)
            {
                appData = new ClientData();

                // テスト用
                appData.ServerIP = "localhost";
                appData.ServerPort = 32768;

                return;
            }
            using (FileStream fs = new FileStream(path, FileMode.Open))
            {
                var s = new DataContractSerializer(typeof(ClientData));
                appData = (ClientData)s.ReadObject(fs);
            }
        }

        private void SaveAppData()
        {
            string path = GetSettingFilePath();
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            using (FileStream fs = new FileStream(path, FileMode.Create))
            {
                var s = new DataContractSerializer(typeof(ClientData));
                s.WriteObject(fs, appData);
            }
        }

        public Task SendSetting()
        {
            return Server.SetSetting(setting);
        }

        public void ExportLogCSV(Stream fs)
        {
            var sw = new StreamWriter(fs, Encoding.UTF8);
            var sb = new StringBuilder();
            var header = new string[] {
                "結果",
                "メッセージ",
                "入力ファイル",
                "出力ファイル",
                "エンコード開始",
                "エンコード終了",
                "エンコード時間（秒）",
                "入力ファイル時間（秒）",
                "出力ファイル時間（秒）",
                "入力ファイルサイズ",
                "中間ファイルサイズ",
                "出力ファイルサイズ",
                "圧縮率（％）",
                "入力音声フレーム",
                "出力音声フレーム",
                "ユニーク出力音声フレーム",
                "未出力音声割合(%)",
                "平均音ズレ(ms)",
                "最大音ズレ(ms)",
                "最大音ズレ位置(ms)"
            };
            sw.WriteLine(string.Join(",", header));

            foreach (var item in LogItems)
            {
                var row = new string[] {
                    item.Success ? "〇" : "×",
                    item.Reason,
                    item.SrcPath,
                    string.Join(":", item.OutPath),
                    item.DisplayEncodeStart,
                    item.DisplayEncodeFinish,
                    (item.EncodeFinishDate - item.EncodeStartDate).TotalSeconds.ToString(),
                    item.SrcVideoDuration.TotalSeconds.ToString(),
                    item.OutVideoDuration.TotalSeconds.ToString(),
                    item.SrcFileSize.ToString(),
                    item.IntVideoFileSize.ToString(),
                    item.OutFileSize.ToString(),
                    item.DisplayCompressionRate,
                    item.AudioDiff.TotalSrcFrames.ToString(),
                    item.AudioDiff.TotalOutFrames.ToString(),
                    item.AudioDiff.TotalOutUniqueFrames.ToString(),
                    item.AudioDiff.NotIncludedPer.ToString(),
                    item.AudioDiff.AvgDiff.ToString(),
                    item.AudioDiff.MaxDiff.ToString(),
                    item.AudioDiff.MaxDiffPos.ToString()
                };
                sw.WriteLine(string.Join(",", row));
            }
            sw.Flush();
        }

        public Task OnSetting(Setting setting)
        {
            AmatsukazePath = setting.AmatsukazePath;
            X264Path = setting.X264Path;
            X265Path = setting.X265Path;
            QSVEncPath = setting.QSVEncPath;
            EncoderName = setting.EncoderName;
            EncoderOption = setting.EncoderOption;
            MuxerPath = setting.MuxerPath;
            TimelineEditorPath = setting.TimelineEditorPath;
            WorkPath = setting.WorkPath;
            AlwaysShowDisk = setting.AlwaysShowDisk;
            AutoBuffer = setting.AutoBuffer;
            BitrateA = setting.Bitrate.A;
            BitrateB = setting.Bitrate.B;
            BitrateH264 = setting.Bitrate.H264;
            TwoPass = setting.TwoPass;
            return Task.FromResult(0);
        }

        private void ensureConsoleNum(int index)
        {
            int numRequire = index + 1;
            while (consoleList_.Count < numRequire)
            {
                consoleList_.Add(new ConsoleText());
            }
        }

        public Task OnConsole(ConsoleData data)
        {
            ensureConsoleNum(data.index);
            consoleList_[data.index].SetTextLines(data.text);
            return Task.FromResult(0);
        }

        public Task OnConsoleUpdate(ConsoleUpdate update)
        {
            ensureConsoleNum(update.index);
            consoleList_[update.index].AddBytes(update.data, 0, update.data.Length);
            return Task.FromResult(0);
        }

        public Task OnLogData(LogData data)
        {
            LogItems.Clear();
            foreach (var item in data.Items)
            {
                LogItems.Add(item);
            }
            return Task.FromResult(0);
        }

        public Task OnLogFile(string str)
        {
            CurrentLogFile = str;
            return Task.FromResult(0);
        }

        public Task OnLogUpdate(LogItem newLog)
        {
            LogItems.Add(newLog);
            return Task.FromResult(0);
        }

        public Task OnOperationResult(string result)
        {
            CurrentOperationResult = result;
            AddLog(result);
            return Task.FromResult(0);
        }

        public Task OnQueueData(QueueData data)
        {
            QueueItems.Clear();
            foreach (var item in data.Items)
            {
                QueueItems.Add(new DisplayQueueDirectory(item));
            }
            return Task.FromResult(0);
        }

        public Task OnQueueUpdate(QueueUpdate update)
        {
            if (update.Item == null)
            {
                // ディレクトリに対する操作
                if (update.Type == UpdateType.Add)
                {
                    QueueItems.Add(new DisplayQueueDirectory(update.Directory));
                }
                else
                {
                    var dir = QueueItems.FirstOrDefault(d => d.Path == update.DirPath);
                    if(dir != null)
                    {
                        if (update.Type == UpdateType.Remove)
                        {
                            QueueItems.Remove(dir);
                        }
                        else
                        {
                            QueueItems[QueueItems.IndexOf(dir)] = new DisplayQueueDirectory(update.Directory);
                        }
                    }
                }
            }
            else
            {
                // ファイルに対する操作
                var dir = QueueItems.FirstOrDefault(d => d.Path == update.DirPath);
                if (dir != null)
                {
                    if (update.Type == UpdateType.Add)
                    {
                        dir.Items.Add(update.Item);
                    }
                    else
                    {
                        var file = dir.Items.FirstOrDefault(f => f.Path == update.Item.Path);
                        if (file != null)
                        {
                            if (update.Type == UpdateType.Remove)
                            {
                                dir.Items.Remove(file);
                            }
                            else // Update
                            {
                                dir.Items[dir.Items.IndexOf(file)] = update.Item;
                            }
                        }
                    }
                }
            }
            return Task.FromResult(0);
        }

        private string GetDisplayServerNeme(State state)
        {
            if(Server is ServerConnection) {
                return state.HostName + ":" + ServerPort;
            }
            return state.HostName;
        }

        public Task OnState(State state)
        {
            ServerHostName = GetDisplayServerNeme(state);
            IsPaused = state.Pause;
            IsRunning = state.Running;
            return Task.FromResult(0);
        }

        public Task OnFreeSpace(DiskFreeSpace space)
        {
            DiskFreeSpace = space.Disks;
            return Task.FromResult(0);
        }
    }
}
