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
using System.Threading.Tasks.Dataflow;
using System.Windows.Data;
using Livet.Commands;
using System.Windows;
using System.ComponentModel;
using System.Net;
using System.Windows.Shell;

namespace Amatsukaze.Models
{
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
            if (TextLines.Count > 800)
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

    public class ClientModel : NotificationObject, IUserClient, IDisposable
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
        private ServerInfo serverInfo = new ServerInfo();
        private State state = new State();

        public Func<object, string, Task> ServerAddressRequired;

        private BufferBlock<string> requestLogoQ = new BufferBlock<string>();
        private Task requestLogoThread;

        public string ServerIP
        {
            get { return appData.ServerIP; }
        }

        public int ServerPort
        {
            get { return appData.ServerPort; }
        }

        public EndPoint LocalIP {
            get {
                return (Server as ServerConnection)?.LocalIP;
            }
        }

        public byte[] MacAddress { get { return serverInfo.MacAddress; } }

        public int[] PriorityList { get { return new int[]{ 1, 2, 3, 4, 5 }; } }

        #region ServerHostName変更通知プロパティ
        private string _ServerHostName;

        public string ServerHostName {
            get { return _ServerHostName; }
            set { 
                if (_ServerHostName == value)
                    return;
                _ServerHostName = value;
                RaisePropertyChanged();
            }
        }
        #endregion

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
        private string _CurrentLogFile = "ここに表示するにはログパネルの項目をダブルクリックしてください";

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

        #region ProfileList変更通知プロパティ
        private ObservableCollection<DisplayProfile> _ProfileList = new ObservableCollection<DisplayProfile>();

        public ObservableCollection<DisplayProfile> ProfileList {
            get { return _ProfileList; }
            set { 
                if (_ProfileList == value)
                    return;
                _ProfileList = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SelectedProfile変更通知プロパティ
        private DisplayProfile _SelectedProfile;

        public DisplayProfile SelectedProfile {
            get { return _SelectedProfile; }
            set {
                if (_SelectedProfile == value)
                    return;
                _SelectedProfile = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ServiceSettings変更通知プロパティ
        private ObservableCollection<DisplayService> _ServiceSettings = new ObservableCollection<DisplayService>();

        public ObservableCollection<DisplayService> ServiceSettings
        {
            get { return _ServiceSettings; }
            set { 
                if (_ServiceSettings == value)
                    return;
                _ServiceSettings = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region JlsCommandFiles変更通知プロパティ
        private List<string> _JlsCommandFiles;

        public List<string> JlsCommandFiles {
            get { return _JlsCommandFiles; }
            set { 
                if (_JlsCommandFiles == value)
                    return;
                _JlsCommandFiles = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MainScriptFiles変更通知プロパティ
        private List<string> _MainScriptFiles;

        public List<string> MainScriptFiles {
            get { return _MainScriptFiles; }
            set { 
                if (_MainScriptFiles == value)
                    return;
                _MainScriptFiles = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PostScriptFiles変更通知プロパティ
        private List<string> _PostScriptFiles;

        public List<string> PostScriptFiles {
            get { return _PostScriptFiles; }
            set { 
                if (_PostScriptFiles == value)
                    return;
                _PostScriptFiles = value;
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
        
        #region TmpDiskSpaceGB変更通知プロパティ
        private int _TmpDiskSpaceGB = 500;

        public int TmpDiskSpaceGB {
            get { return _TmpDiskSpaceGB; }
            set { 
                if (_TmpDiskSpaceGB == value)
                    return;
                _TmpDiskSpaceGB = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DrcsImageList変更通知プロパティ
        private ObservableCollection<DrcsImage> drcsImageList_ = new ObservableCollection<DrcsImage>();

        public ObservableCollection<DrcsImage> DrcsImageList
        {
            get { return drcsImageList_; }
            set
            {
                if (drcsImageList_ == value)
                    return;
                drcsImageList_ = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Setting変更通知プロパティ
        private DisplaySetting _Setting = new DisplaySetting();

        public DisplaySetting Setting {
            get { return _Setting; }
            set { 
                if (_Setting == value)
                    return;
                _Setting = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MakeScriptData変更通知プロパティ
        private DisplayMakeScriptData _MakeScriptData = new DisplayMakeScriptData();

        public DisplayMakeScriptData MakeScriptData {
            get { return _MakeScriptData; }
            set { 
                if (_MakeScriptData == value)
                    return;
                _MakeScriptData = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ProgressState変更通知プロパティ
        private TaskbarItemProgressState _ProgressState;

        public TaskbarItemProgressState ProgressState {
            get { return _ProgressState; }
            set { 
                if (_ProgressState == value)
                    return;
                _ProgressState = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ProgressValue変更通知プロパティ
        private double _ProgressValue;

        public double ProgressValue {
            get { return _ProgressValue; }
            set { 
                if (_ProgressValue == value)
                    return;
                _ProgressValue = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public ClientModel()
        {
            Util.LogHandlers.Add(AddLog);

            AddLog("クライアント起動");

            LoadAppData();
            requestLogoThread = RequestLogoThread();
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
                    requestLogoQ.Complete();

                    if (Server is ServerAdapter)
                    {
                        (Server as ServerAdapter).Server.Dispose();
                    }
                }

                // TODO: アンマネージ リソース (アンマネージ オブジェクト) を解放し、下のファイナライザーをオーバーライドします。
                // TODO: 大きなフィールドを null に設定します。

                disposedValue = true;
            }
        }

        // TODO: 上の Dispose(bool disposing) にアンマネージ リソースを解放するコードが含まれる場合にのみ、ファイナライザーをオーバーライドします。
        // ~ClientModel() {
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

        private async Task RequestLogoThread()
        {
            try
            {
                while (await requestLogoQ.OutputAvailableAsync())
                {
                    var file = await requestLogoQ.ReceiveAsync();
                    await Server.RequestLogoData(file);
                }
            }
            catch (Exception exception)
            {
                await OnOperationResult("RequestLogoThreadがエラー終了しました: " + exception.Message);
            }
        }

        public void RequestLogoData(string file)
        {
            requestLogoQ.Post(file);
        }

        private Task UpdateService(int serviceId)
        {
            var service = _ServiceSettings
                .FirstOrDefault(s => s.Data.ServiceId == serviceId);
            if (service != null)
            {
                return Server.SetServiceSetting(new ServiceSettingUpdate() {
                    Type = ServiceSettingUpdateType.Update,
                    ServiceId = service.Data.ServiceId,
                    Data = service.Data
                });
            }
            return Task.FromResult(0);
        }

        public Task UpdateLogo(DisplayLogo logo)
        {
            return UpdateService(logo.Setting.ServiceId);
        }

        public Task RemoveLogo(DisplayLogo logo)
        {
            var service = _ServiceSettings
                .FirstOrDefault(s => s.Data.ServiceId == logo.Setting.ServiceId);
            if (service != null)
            {
                int index = service.Data.LogoSettings.IndexOf(logo.Setting);
                if (index != -1)
                {
                    return Server.SetServiceSetting(new ServiceSettingUpdate() {
                        Type = ServiceSettingUpdateType.RemoveLogo,
                        ServiceId = logo.Setting.ServiceId,
                        RemoveLogoIndex = index
                    });
                }
            }
            return Task.FromResult(0);
        }

        public Task UpdateService(DisplayService service)
        {
            return UpdateService(service.Data.ServiceId);
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
                Server = new ServerAdapter(new EncodeServer(0, new ClientAdapter(this), null));
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
            return Server.SetCommonData(new CommonData() { Setting = Setting.Model });
        }

        public Task SendMakeScriptData()
        {
            MakeScriptData.Model.Profile = MakeScriptData.SelectedProfile?.Model?.Name;
            return Server.SetCommonData(new CommonData() { MakeScriptData = MakeScriptData.Model });
        }

        public Task AddProfile(ProfileSetting profile)
        {
            return Server.SetProfile(new ProfileUpdate()
            {
                Type = UpdateType.Add,
                Profile = profile
            });
        }

        public Task SendProfile(ProfileSetting profile)
        {
            return Server.SetProfile(new ProfileUpdate() {
                Type = UpdateType.Update, Profile = profile
            });
        }

        public Task RemoveProfile(ProfileSetting profile)
        {
            return Server.SetProfile(new ProfileUpdate()
            {
                Type = UpdateType.Remove,
                Profile = profile
            });
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
                "出力ファイル数",
                "エンコード開始",
                "エンコード終了",
                "エンコード時間（秒）",
                "入力ファイル時間（秒）",
                "出力ファイル時間（秒）",
                "インシデント数",
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

            foreach (var item in LogItems.Reverse())
            {
                var row = new string[] {
                    item.Success ? ((item.Incident > 0) ? "△" : "〇") : "×",
                    item.Reason,
                    item.SrcPath,
                    string.Join(":", item.OutPath),
                    item.OutPath.Count.ToString(),
                    item.DisplayEncodeStart,
                    item.DisplayEncodeFinish,
                    (item.EncodeFinishDate - item.EncodeStartDate).TotalSeconds.ToString(),
                    item.SrcVideoDuration.TotalSeconds.ToString(),
                    item.OutVideoDuration.TotalSeconds.ToString(),
                    item.Incident.ToString(),
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

        private string GetDisplayServerNeme()
        {
            if (Server is ServerConnection)
            {
                return serverInfo.HostName + ":" + ServerPort;
            }
            return serverInfo.HostName;
        }

        public Task OnCommonData(CommonData data)
        {
            if(data.Setting != null)
            {
                Setting = new DisplaySetting() { Model = data.Setting };

                if (SelectedProfile == null)
                {
                    SelectedProfile = ProfileList.FirstOrDefault(
                        p => p.Model.Name == Setting.Model.LastSelectedProfile);
                }
            }
            if(data.MakeScriptData != null)
            {
                MakeScriptData = new DisplayMakeScriptData()
                {
                    SelectedProfile = ProfileList.FirstOrDefault(
                        s => s.Model.Name == data.MakeScriptData.Profile),
                    Model = data.MakeScriptData
                };
                if(MakeScriptData.Priority == 0)
                {
                    MakeScriptData.Priority = 3;
                }
            }
            if(data.JlsCommandFiles != null)
            {
                JlsCommandFiles = data.JlsCommandFiles;
            }
            if(data.MainScriptFiles != null)
            {
                MainScriptFiles = new string[] { "フィルタなし" }.Concat(data.MainScriptFiles).ToList();
            }
            if (data.PostScriptFiles != null)
            {
                PostScriptFiles = new string[] { "フィルタなし" }.Concat(data.PostScriptFiles).ToList();
            }
            if(data.Disks != null)
            {
                DiskFreeSpace = data.Disks;

                // 一時フォルダの容量があれば更新
                if (Setting.WorkPath != null)
                {
                    var diskItem = data.Disks.Find(item => Setting.WorkPath.StartsWith(item.Path));
                    if (diskItem != null)
                    {
                        TmpDiskSpaceGB = (int)(diskItem.Capacity / (1024 * 1024 * 1024L));
                    }
                }
            }
            if(data.ServerInfo != null)
            {
                serverInfo = data.ServerInfo;
                ServerHostName = GetDisplayServerNeme();
            }
            if(data.State != null)
            {
                IsPaused = data.State.Pause;
                IsRunning = data.State.Running;
                ProgressState = IsRunning ? TaskbarItemProgressState.Normal : TaskbarItemProgressState.None;
                ProgressValue = data.State.Progress;
            }
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
            foreach (var item in data.Items.Reverse<LogItem>())
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
            LogItems.Insert(0, newLog);
            return Task.FromResult(0);
        }

        public Task OnOperationResult(string result)
        {
            CurrentOperationResult = DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss") + " " + result;
            AddLog(result);
            return Task.FromResult(0);
        }

        public Task OnQueueData(QueueData data)
        {
            QueueItems.Clear();
            foreach (var item in data.Items)
            {
                QueueItems.Add(new DisplayQueueDirectory(item, this));
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
                    QueueItems.Add(new DisplayQueueDirectory(update.Directory, this));
                }
                else
                {
                    var dir = QueueItems.FirstOrDefault(d => d.Id == update.DirId);
                    if(dir != null)
                    {
                        if (update.Type == UpdateType.Remove)
                        {
                            QueueItems.Remove(dir);
                        }
                        else
                        {
                            QueueItems[QueueItems.IndexOf(dir)] = new DisplayQueueDirectory(update.Directory, this);
                        }
                    }
                }
            }
            else
            {
                // ファイルに対する操作
                var dir = QueueItems.FirstOrDefault(d => d.Id == update.DirId);
                if (dir != null)
                {
                    if (update.Type == UpdateType.Add)
                    {
                        dir.Items.Add(new DisplayQueueItem() { Parent = this, Model = update.Item });
                    }
                    else
                    {
                        var file = dir.Items.FirstOrDefault(f => f.Model.Id == update.Item.Id);
                        if (file != null)
                        {
                            if (update.Type == UpdateType.Remove)
                            {
                                dir.Items.Remove(file);
                            }
                            else // Update
                            {
                                var index = dir.Items.IndexOf(file);
                                dir.Items[index] = new DisplayQueueItem() {
                                    Parent = this,
                                    Model = update.Item,
                                    IsSelected = dir.Items[index].IsSelected
                                };
                            }
                        }
                    }
                }
            }
            return Task.FromResult(0);
        }

        public Task OnServiceSetting(ServiceSettingUpdate update)
        {
            if(update.Type == ServiceSettingUpdateType.Clear)
            {
                _ServiceSettings.Clear();
                return Task.FromResult(0);
            }
            for(int i = 0; i < _ServiceSettings.Count; ++i)
            {
                if(_ServiceSettings[i].Data.ServiceId == update.ServiceId)
                {
                    if (update.Type == ServiceSettingUpdateType.Update)
                    {
                        _ServiceSettings[i].Data = update.Data;
                    }
                    else if(update.Type == ServiceSettingUpdateType.Remove)
                    {
                        _ServiceSettings.RemoveAt(i);
                    }
                    return Task.FromResult(0);
                }
            }
            if (update.Type == ServiceSettingUpdateType.Update)
            {
                _ServiceSettings.Add(new DisplayService() { Model = this, Data = update.Data });
            }
            return Task.FromResult(0);
        }

        public Task OnLogoData(LogoData logoData)
        {
            var service = _ServiceSettings
                .FirstOrDefault(s => s.Data.ServiceId == logoData.ServiceId);
            if (service != null)
            {
                var logo = service.LogoList
                    .FirstOrDefault(s => s.Setting.FileName == logoData.FileName);
                if (logo != null)
                {
                    logo.Data = logoData;
                }
            }
            return Task.FromResult(0);
        }

        public Task OnDrcsData(DrcsImageUpdate update)
        {
            Action<DrcsImage> procItem = image => {
                var item = drcsImageList_.FirstOrDefault(s => s.MD5 == image.MD5);
                if (item == null)
                {
                    if(update.Type == DrcsUpdateType.Update)
                    {
                        drcsImageList_.Add(image);
                    }
                }
                else
                {
                    if(update.Type == DrcsUpdateType.Remove)
                    {
                        drcsImageList_.Remove(item);
                    }
                    else
                    {
                        drcsImageList_[drcsImageList_.IndexOf(item)] = image;
                    }
                }
            };
            if(update.Image != null)
            {
                procItem(update.Image);
            }
            if(update.ImageList != null)
            {
                foreach (var item in update.ImageList)
                {
                    procItem(item);
                }
            }
            return Task.FromResult(0);
        }

        public Task OnAddResult(string requestId)
        {
            // 何もしなくていい
            return Task.FromResult(0);
        }

        public Task OnProfile(ProfileUpdate data)
        {
            var profile = ProfileList.FirstOrDefault(s => s.Model.Name == data.Profile.Name);
            if (data.Type == UpdateType.Add || data.Type == UpdateType.Update)
            {
                if(profile == null)
                {
                    profile = new DisplayProfile() { Model = data.Profile };
                    ProfileList.Add(profile);
                }

                profile.SetEncoderOptions(
                    data.Profile.X264Option, data.Profile.X265Option,
                    data.Profile.QSVEncOption, data.Profile.NVEncOption);
                profile.EncoderTypeInt = (int)data.Profile.EncoderType;
                profile.FilterPath = data.Profile.FilterPath;
                profile.PostFilterPath = data.Profile.PostFilterPath;
                profile.AutoBuffer = data.Profile.AutoBuffer;
                profile.BitrateA = data.Profile.Bitrate.A;
                profile.BitrateB = data.Profile.Bitrate.B;
                profile.BitrateH264 = data.Profile.Bitrate.H264;
                profile.BitrateCM = data.Profile.BitrateCM;
                profile.TwoPass = data.Profile.TwoPass;
                profile.SplitSub = data.Profile.SplitSub;
                profile.OutputMask = data.Profile.OutputMask;
                profile.DefaultJLSCommand = data.Profile.DefaultJLSCommand;
                profile.DisableChapter = data.Profile.DisableChapter;
                profile.DisableSubs = data.Profile.DisableSubs;
                profile.IgnoreNoDrcsMap = data.Profile.IgnoreNoDrcsMap;
                profile.NoDelogo = data.Profile.NoDelogo;
                profile.EnableNicoJK = data.Profile.EnableNicoJK;
                profile.IgnoreNicoJKError = data.Profile.IgnoreNicoJKError;
                profile.NicoJK18 = data.Profile.NicoJK18;
                profile.NicoJKFormat720S = data.Profile.NicoJKFormats[0];
                profile.NicoJKFormat720T = data.Profile.NicoJKFormats[1];
                profile.NicoJKFormat1080S = data.Profile.NicoJKFormats[2];
                profile.NicoJKFormat1080T = data.Profile.NicoJKFormats[3];
                profile.MoveEDCBFiles = data.Profile.MoveEDCBFiles;
                profile.SystemAviSynthPlugin = data.Profile.SystemAviSynthPlugin;
                profile.DisableHashCheck = data.Profile.DisableHashCheck;
                profile.Mpeg2DecoderInt = (int)data.Profile.Mpeg2Decoder;
                profile.H264DecoderInt = (int)data.Profile.H264Deocder;
                profile.OutputFormatInt = (int)data.Profile.OutputFormat;

                profile.IsModified = false;

                if(SelectedProfile == null && Setting.Model != null &&
                    profile.Model.Name == Setting.Model.LastSelectedProfile)
                {
                    SelectedProfile = profile;
                }
            }
            else
            {
                if(profile != null)
                {
                    ProfileList.Remove(profile);
                }
            }
            return Task.FromResult(0);
        }
    }
}
