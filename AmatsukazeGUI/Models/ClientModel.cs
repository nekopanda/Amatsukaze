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
            [DataMember]
            public byte[] windowPlacement;

            public ExtensionDataObject ExtensionData { get; set; }
        }

        private ClientData appData;
        public IEncodeServer Server { get; private set; }
        public Task CommTask { get; private set; }
        private FileStream lockFile;
        private ServerInfo serverInfo = new ServerInfo();
        private State state = new State();

        public Func<object, string, Task> ServerAddressRequired;
        public Action FinishRequested;

        private BufferBlock<string> requestLogoQ = new BufferBlock<string>();
        private Task requestLogoThread;

        private string currentNewProfile;
        private string currentNewAutoSelect;

        public bool IsStandalone {
            get {
                return App.Option.LaunchType == LaunchType.Standalone;
            }
        }

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

        public string[] AffinityList { get { return new string[] { "なし", "コア", "L2", "L3", "NUMA", "Group" }; } }

        public string[] ProcessPriorityList { get { return new string[] { "通常", "通常以下", "低" }; } }

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

        public ObservableCollection<DisplayConsole> ConsoleList { get; } = new ObservableCollection<DisplayConsole>();

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

        #region CheckLogItems変更通知プロパティ
        private ObservableCollection<CheckLogItem> _CheckLogItems = new ObservableCollection<CheckLogItem>();

        public ObservableCollection<CheckLogItem> CheckLogItems {
            get { return _CheckLogItems; }
            set {
                if (_CheckLogItems == value)
                    return;
                _CheckLogItems = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region QueueItems変更通知プロパティ
        private ObservableCollection<DisplayQueueItem> _QueueItems = new ObservableCollection<DisplayQueueItem>();

        public ObservableCollection<DisplayQueueItem> QueueItems
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
        }
        #endregion

        #region AutoSelectList変更通知プロパティ
        private ObservableCollection<DisplayAutoSelect> _AutoSelectList = new ObservableCollection<DisplayAutoSelect>();

        public ObservableCollection<DisplayAutoSelect> AutoSelectList
        {
            get { return _AutoSelectList; }
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
                _SelectedProfile?.UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SelectedAutoSelect変更通知プロパティ
        private DisplayAutoSelect _SelectedAutoSelect;

        public DisplayAutoSelect SelectedAutoSelect
        {
            get { return _SelectedAutoSelect; }
            set
            {
                if (_SelectedAutoSelect == value)
                    return;
                _SelectedAutoSelect = value;
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

        #region AddQueueBatFiles変更通知プロパティ
        private List<string> _AddQueueBatFiles;

        public List<string> AddQueueBatFiles {
            get { return _AddQueueBatFiles; }
            set { 
                if (_AddQueueBatFiles == value)
                    return;
                _AddQueueBatFiles = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PreBatFiles変更通知プロパティ
        private List<string> _PreBatFiles;

        public List<string> PreBatFiles {
            get { return _PreBatFiles; }
            set { 
                if (_PreBatFiles == value)
                    return;
                _PreBatFiles = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PostBatFiles変更通知プロパティ
        private List<string> _PostBatFiles;

        public List<string> PostBatFiles {
            get { return _PostBatFiles; }
            set { 
                if (_PostBatFiles == value)
                    return;
                _PostBatFiles = value;
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

        #region IsCurrentResultFail変更通知プロパティ
        private bool _IsCurrentResultFail;

        public bool IsCurrentResultFail {
            get { return _IsCurrentResultFail; }
            set { 
                if (_IsCurrentResultFail == value)
                    return;
                _IsCurrentResultFail = value;
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
        private DisplaySetting _Setting = new DisplaySetting() { Model = new Amatsukaze.Server.Setting() };

        public DisplaySetting Setting {
            get { return _Setting; }
            set { 
                if (_Setting == value)
                    return;
                // SettingのPropertyChangedも波及させる
                if (_Setting != null)
                    _Setting.PropertyChanged -= SettingChanged;
                _Setting = value;
                _Setting.PropertyChanged += SettingChanged;
                RaisePropertyChanged("CurrentClusters");
                RaisePropertyChanged();
            }
        }

        private void SettingChanged(object sender, PropertyChangedEventArgs args)
        {
            RaisePropertyChanged("Setting." + args.PropertyName);

            if(args.PropertyName == "AffinitySetting")
            {
                RaisePropertyChanged("CurrentClusters");
            }
        }
        #endregion

        #region UIState変更通知プロパティ
        private DisplayUIState _UIState = new DisplayUIState();

        public DisplayUIState UIState
        {
            get { return _UIState; }
            set
            {
                if (_UIState == value)
                    return;
                _UIState = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MakeScriptData変更通知プロパティ
        private DisplayMakeScriptData _MakeScriptData = new DisplayMakeScriptData() { Model = new MakeScriptData() };

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

        #region CpuClusters変更通知プロパティ
        private List<int> _AffinityClusters;

        public List<int> CpuClusters {
            get { return _AffinityClusters; }
            set { 
                if (_AffinityClusters == value)
                    return;
                _AffinityClusters = value;
                RaisePropertyChanged();
                RaisePropertyChanged("CurrentClusters");
            }
        }

        public int CurrentClusters {
            get {
                if(CpuClusters == null)
                {
                    return 0;
                }
                return CpuClusters[Setting.AffinitySetting];
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

        public SimpleDisplayConsole AddQueueConsole { get; } = new SimpleDisplayConsole();

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

                    if(lockFile != null)
                    {
                        lockFile.Close();
                        lockFile = null;
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

        public void RestoreWindowPlacement(Window w)
        {
            if(appData.windowPlacement != null)
            {
                Lib.WINDOWPLACEMENT placement;
                var hwnd = new System.Windows.Interop.WindowInteropHelper(w).Handle;
                var stream = new MemoryStream(appData.windowPlacement);
                var formatter = new System.Runtime.Serialization.Formatters.Binary.BinaryFormatter();
                placement = (Lib.WINDOWPLACEMENT)formatter.Deserialize(stream);
                Lib.WinAPI.SetWindowPlacement(hwnd, ref placement);
            }
        }

        public void SaveWindowPlacement(Window w)
        {
            Lib.WINDOWPLACEMENT placement;
            var hwnd = new System.Windows.Interop.WindowInteropHelper(w).Handle;
            Lib.WinAPI.GetWindowPlacement(hwnd, out placement);
            var stream = new MemoryStream();
            var formatter = new System.Runtime.Serialization.Formatters.Binary.BinaryFormatter();
            formatter.Serialize(stream, placement);
            appData.windowPlacement = stream.ToArray();
            SaveAppData();
        }

        private async Task RequestLogoThread()
        {
            try
            {
                while (await requestLogoQ.OutputAvailableAsync())
                {
                    var file = await requestLogoQ.ReceiveAsync();
                    await Server?.RequestLogoData(file);
                }
            }
            catch (Exception exception)
            {
                await OnOperationResult(new OperationResult()
                {
                    IsFailed = true,
                    Message = "RequestLogoThreadがエラー終了しました: " + exception.Message,
                    StackTrace = exception.StackTrace
                });
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
                return Server?.SetServiceSetting(new ServiceSettingUpdate() {
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
                    return Server?.SetServiceSetting(new ServiceSettingUpdate() {
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

        private async Task<EncodeServer> MakeEncodeServer()
        {
            var server = new EncodeServer(0, new ClientAdapter(this), null);
            await server.Init();
            return server;
        }

        public async Task Start()
        {
            if (App.Option.LaunchType == LaunchType.Standalone)
            {
                lockFile = ServerSupport.GetLock();
                Server = new ServerAdapter(await MakeEncodeServer());
                await Server.RefreshRequest();
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

            // データ初期化
            drcsImageList_.Clear();
            ConsoleList.Clear();
            _LogItems.Clear();
            _CheckLogItems.Clear();
            _QueueItems.Clear();
            _ProfileList.Clear();
            _AutoSelectList.Clear();
        }

        public void Finish()
        {
            FinishRequested?.Invoke();
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
                appData.ServerIP = "localhost";
                appData.ServerPort = ServerSupport.DEFAULT_PORT;
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
            return Server?.SetCommonData(new CommonData() { Setting = Setting.Model });
        }

        public Task SendMakeScriptData()
        {
            MakeScriptData.Model.Profile = DisplayProfile.GetProfileName(MakeScriptData.SelectedProfile);
            return Server?.SetCommonData(new CommonData() { MakeScriptData = MakeScriptData.Model });
        }

        public Task AddProfile(ProfileSetting profile)
        {
            currentNewProfile = profile.Name;
            return Server?.SetProfile(new ProfileUpdate()
            {
                Type = UpdateType.Add,
                Profile = profile
            });
        }

        public Task UpdateProfile(ProfileSetting profile)
        {
            return Server?.SetProfile(new ProfileUpdate() {
                Type = UpdateType.Update, Profile = profile
            });
        }

        public Task RemoveProfile(ProfileSetting profile)
        {
            return Server?.SetProfile(new ProfileUpdate()
            {
                Type = UpdateType.Remove,
                Profile = profile
            });
        }

        public Task AddAutoSelect(AutoSelectProfile profile)
        {
            currentNewAutoSelect = profile.Name;
            return Server?.SetAutoSelect(new AutoSelectUpdate()
            {
                Type = UpdateType.Add,
                Profile = profile
            });
        }

        public Task UpdateAutoSelect(AutoSelectProfile profile)
        {
            return Server?.SetAutoSelect(new AutoSelectUpdate()
            {
                Type = UpdateType.Update,
                Profile = profile
            });
        }

        public Task RemoveAutoSelect(AutoSelectProfile profile)
        {
            return Server?.SetAutoSelect(new AutoSelectUpdate()
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

        public Task OnUIData(UIData data)
        {
            if(data.QueueData != null)
            {
                foreach(int id in QueueItems.Select(s => s.Model.Id)
                    .Concat(data.QueueData.Items.Select(s => s.Id)).Distinct().ToArray())
                {
                    var old = QueueItems.FirstOrDefault(s => s.Model.Id == id);
                    var update = data.QueueData.Items.FirstOrDefault(s => s.Id == id);
                    if(old != null && update != null)
                    {
                        // 更新
                        old.Model = update;
                    }
                    else if(old != null)
                    {
                        // 削除
                        QueueItems.Remove(old);
                    }
                    else
                    {
                        // 追加
                        QueueItems.Add(new DisplayQueueItem() { Parent = this, Model = update });
                    }
                }
            }
            if(data.QueueUpdate != null)
            {
                var update = data.QueueUpdate;
                var item = QueueItems.FirstOrDefault(f => f.Model.Id == update.Item.Id);
                if (update.Type == UpdateType.Remove)
                {
                    if (item != null)
                    {
                        QueueItems.Remove(item);
                    }
                }
                else
                {
                    // Add, Updte
                    if(item != null)
                    {
                        item.Model = update.Item;
                    }
                    else
                    {
                        QueueItems.Add(new DisplayQueueItem() { Parent = this, Model = update.Item });
                    }
                }
            }
            if(data.LogData != null)
            {
                LogItems.Clear();
                foreach (var item in data.LogData.Items.Reverse<LogItem>())
                {
                    LogItems.Add(item);
                }
            }
            if(data.LogItem != null)
            {
                LogItems.Insert(0, data.LogItem);
            }
            if(data.CheckLogData != null)
            {
                CheckLogItems.Clear();
                foreach (var item in data.CheckLogData.Items.Reverse<CheckLogItem>())
                {
                    CheckLogItems.Add(item);
                }
            }
            if(data.CheckLogItem != null)
            {
                CheckLogItems.Insert(0, data.CheckLogItem);
            }
            if(data.ConsoleData != null)
            {
                if(data.ConsoleData.index == -1)
                {
                    // キュー追加コンソール
                    AddQueueConsole.SetTextLines(data.ConsoleData.text);
                }
                else
                {
                    ensureConsoleNum(data.ConsoleData.index);
                    ConsoleList[data.ConsoleData.index].SetTextLines(data.ConsoleData.text);
                }
            }
            if(data.EncodeState != null)
            {
                ensureConsoleNum(data.EncodeState.ConsoleId);
                var console = ConsoleList[data.EncodeState.ConsoleId];
                console.Phase = data.EncodeState.Phase;
                console.Resource = data.EncodeState.Resource;
            }
            return Task.FromResult(0);
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
                Setting.RefreshGPUResources();
            }
            if (data.UIState != null)
            {
                UIState = new DisplayUIState() { Model = data.UIState };
            }
            if(data.MakeScriptData != null)
            {
                MakeScriptData = new DisplayMakeScriptData()
                {
                    SelectedProfile = ProfileList.FirstOrDefault(
                        s => s.Data.Name == data.MakeScriptData.Profile),
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
            if(data.CpuClusters != null)
            {
                CpuClusters = data.CpuClusters;
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
            if (data.AddQueueBatFiles != null)
            {
                AddQueueBatFiles = new string[] { "なし" }.Concat(data.AddQueueBatFiles).ToList();
            }
            if (data.PreBatFiles != null)
            {
                PreBatFiles = new string[] { "なし" }.Concat(data.PreBatFiles).ToList();
            }
            if (data.PostBatFiles != null)
            {
                PostBatFiles = new string[] { "なし" }.Concat(data.PostBatFiles).ToList();
            }
            return Task.FromResult(0);
        }

        private void ensureConsoleNum(int index)
        {
            int numRequire = index + 1;
            while (ConsoleList.Count < numRequire)
            {
                ConsoleList.Add(new DisplayConsole() { Id = ConsoleList.Count + 1 });
            }
        }

        public Task OnConsoleUpdate(ConsoleUpdate update)
        {
            if(update.index == -1)
            {
                // キュー追加コンソール
                AddQueueConsole.AddBytes(update.data, 0, update.data.Length);
            }
            else
            {
                ensureConsoleNum(update.index);
                ConsoleList[update.index].AddBytes(update.data, 0, update.data.Length);
            }
            return Task.FromResult(0);
        }

        public Task OnEncodeState(EncodeState state)
        {
            ensureConsoleNum(state.ConsoleId);
            var console = ConsoleList[state.ConsoleId];
            console.Phase = state.Phase;
            console.Resource = state.Resource;
            return Task.FromResult(0);
        }

        public Task OnLogFile(string str)
        {
            CurrentLogFile = str;
            return Task.FromResult(0);
        }

        public Task OnOperationResult(OperationResult result)
        {
            IsCurrentResultFail = result.IsFailed;
            CurrentOperationResult = DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss") + " " + result.Message;
            AddLog(result.Message);
            if(result.StackTrace != null)
            {
                AddLog(result.StackTrace);
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

        public DisplayProfile WrapProfile(ProfileSetting profile)
        {
            return new DisplayProfile(profile, this,
                Enumerable.Range(0, DisplayResource.MAX).Select(
                    s => new DisplayResource() { Model = profile, Phase = s }).ToArray());
        }

        public Task OnProfile(ProfileUpdate data)
        {
            if(data.Type == UpdateType.Clear)
            {
                ProfileList.Clear();
                return Task.FromResult(0);
            }
            var profile = ProfileList.FirstOrDefault(s => s.Data.Name == data.Profile.Name);
            if (data.Type == UpdateType.Add || data.Type == UpdateType.Update)
            {
                if(profile == null)
                {
                    profile = WrapProfile(data.Profile);
                    ProfileList.Insert(
                        ProfileList.Count(s => !s.Data.Name.StartsWith("サンプル_")),
                        profile);

                    if(currentNewProfile != null && data.Profile.Name == currentNewProfile)
                    {
                        // 新しく追加したプロファイル
                        SelectedProfile = profile;
                    }
                    else if(SelectedProfile != null && SelectedProfile.Data.Name == data.Profile.Name)
                    {
                        // 選択中のプロファイルが更新された
                        SelectedProfile = profile;
                    }
                }

                currentNewProfile = null;

                profile.SetEncoderOptions(
                    data.Profile.X264Option, data.Profile.X265Option,
                    data.Profile.QSVEncOption, data.Profile.NVEncOption);
                profile.EncoderTypeInt = (int)data.Profile.EncoderType;
                profile.CustomFilter.FilterPath = data.Profile.FilterPath;
                profile.CustomFilter.PostFilterPath = data.Profile.PostFilterPath;
                profile.AutoBuffer = data.Profile.AutoBuffer;
                profile.BitrateA = data.Profile.Bitrate.A;
                profile.BitrateB = data.Profile.Bitrate.B;
                profile.BitrateH264 = data.Profile.Bitrate.H264;
                profile.BitrateCM = data.Profile.BitrateCM;
                profile.TwoPass = data.Profile.TwoPass;
                profile.SplitSub = data.Profile.SplitSub;
                profile.OutputMask = profile.OutputOptionList.FirstOrDefault(s => s.Mask == data.Profile.OutputMask);
                profile.JLSCommandFile = data.Profile.JLSCommandFile;
                profile.JLSOption = data.Profile.JLSOption;
                profile.EnableJLSOption = data.Profile.EnableJLSOption;
                profile.ChapterExeOptions = data.Profile.ChapterExeOption;
                profile.DisableChapter = data.Profile.DisableChapter;
                profile.DisableSubs = data.Profile.DisableSubs;
                profile.IgnoreNoDrcsMap = data.Profile.IgnoreNoDrcsMap;
                profile.NoDelogo = data.Profile.NoDelogo;
                profile.VFR120Fps = data.Profile.VFR120fps;
                profile.EnableNicoJK = data.Profile.EnableNicoJK;
                profile.IgnoreNicoJKError = data.Profile.IgnoreNicoJKError;
                profile.NicoJK18 = data.Profile.NicoJK18;
                profile.NicoJKLog = data.Profile.NicoJKLog;
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
                profile.EnableGunreFolder = data.Profile.EnableGunreFolder;
                profile.EnableRename = data.Profile.EnableRename;
                profile.RenameFormat = data.Profile.RenameFormat;
                profile.EnablePmtCut = data.Profile.EnablePmtCut;
                profile.PmtCutHeadRate = data.Profile.PmtCutHeadRate;
                profile.PmtCutTailRate = data.Profile.PmtCutTailRate;
                for (int i = 0; i < DisplayResource.MAX; ++i)
                {
                    profile.Resources[i].Resource = data.Profile.ReqResources[i];
                }

                // filter
                profile.FilterOption = (int)data.Profile.FilterOption;
                profile.Filter.EnableCUDA = data.Profile.FilterSetting.EnableCUDA;
                profile.Filter.EnableDeblock = data.Profile.FilterSetting.EnableDeblock;
                profile.Filter.DeblockQuality = Array.IndexOf(DisplayFilterSetting.DeblockQualityListData, data.Profile.FilterSetting.DeblockQuality);
                profile.Filter.DeblockStrength = (int)data.Profile.FilterSetting.DeblockStrength;
                profile.Filter.DeinterlaceAlgorithm = (int)data.Profile.FilterSetting.DeinterlaceAlgorithm;
                profile.Filter.D3DVP.GPU = (int)data.Profile.FilterSetting.D3dvpGpu;
                profile.Filter.QTGMC.Preset = (int)data.Profile.FilterSetting.QtgmcPreset;
                profile.Filter.KFM.EnableNR = data.Profile.FilterSetting.KfmEnableNr;
                profile.Filter.KFM.EnableUCF = data.Profile.FilterSetting.KfmEnableUcf;
                profile.Filter.KFM.SelectedFPS = Array.IndexOf(FilterKFMViewModel.FPSListData, data.Profile.FilterSetting.KfmFps);
                profile.Filter.Yadif.SelectedFPS = Array.IndexOf(FilterYadifViewModel.FPSListData, data.Profile.FilterSetting.YadifFps);
                profile.Filter.ResizeWidth = data.Profile.FilterSetting.ResizeWidth;
                profile.Filter.ResizeHeight = data.Profile.FilterSetting.ResizeHeight;
                profile.Filter.EnableTemporalNR = data.Profile.FilterSetting.EnableTemporalNR;
                profile.Filter.EnableDeband = data.Profile.FilterSetting.EnableDeband;
                profile.Filter.EnableEdgeLevel = data.Profile.FilterSetting.EnableEdgeLevel;

                profile.IsModified = false;

                if(SelectedProfile == null)
                {
                    if(profile.Data.Name == "デフォルト")
                    {
                        // 選択中のプロファイルがなかったらデフォルトを選択
                        SelectedProfile = profile;
                    }
                }
            }
            else
            {
                if(profile != null)
                {
                    if (SelectedProfile == profile && ProfileList.Count >= 2)
                    {
                        // 選択中だった場合は別のを選択させる
                        if (ProfileList.Last() == profile)
                        {
                            SelectedProfile = ProfileList[ProfileList.Count - 2];
                        }
                        else
                        {
                            SelectedProfile = ProfileList[ProfileList.IndexOf(profile) + 1];
                        }
                    }
                    ProfileList.Remove(profile);
                }
            }
            return Task.FromResult(0);
        }

        public Task OnAutoSelect(AutoSelectUpdate data)
        {
            if (data.Type == UpdateType.Clear)
            {
                AutoSelectList.Clear();
                return Task.FromResult(0);
            }
            var profile = AutoSelectList.FirstOrDefault(s => s.Model.Name == data.Profile.Name);
            if (data.Type == UpdateType.Add || data.Type == UpdateType.Update)
            {
                if (profile == null)
                {
                    profile = new DisplayAutoSelect() {
                        Model = data.Profile
                    };
                    AutoSelectList.Add(profile);

                    if(currentNewAutoSelect != null && data.Profile.Name == currentNewAutoSelect)
                    {
                        // 新しく追加した自動選択
                        SelectedAutoSelect = profile;
                    }
                    else if (SelectedAutoSelect != null && SelectedAutoSelect.Model.Name == data.Profile.Name)
                    {
                        // 選択中の自動選択プロファイルが更新された
                        SelectedAutoSelect = profile;
                    }
                }

                currentNewAutoSelect = null;

                var conds = new ObservableCollection<DisplayCondition>();
                foreach (var cond in data.Profile.Conditions.Select(s =>
                {
                    var cond = new DisplayCondition()
                    {
                        Model = this,
                        Item = s,
                        SelectedProfile = ProfileList.FirstOrDefault(p => p.Data.Name == s.Profile)
                    };
                    cond.Initialize();
                    return cond;
                }))
                {
                    conds.Add(cond);
                }
                int selectedIndex = profile.SelectedIndex;
                profile.Conditions = conds;
                profile.SelectedIndex = selectedIndex;

                if (SelectedAutoSelect == null && UIState.Model != null)
                {
                    if (profile.Model.Name == "デフォルト")
                    {
                        // 選択中のプロファイルがなかったらデフォルトを選択
                        SelectedAutoSelect = profile;
                    }
                }
            }
            else
            {
                if (profile != null)
                {
                    if(SelectedAutoSelect == profile && AutoSelectList.Count >= 2)
                    {
                        // 選択中だった場合は別のを選択させる
                        if(AutoSelectList.Last() == profile)
                        {
                            SelectedAutoSelect = AutoSelectList[AutoSelectList.Count - 2];
                        }
                        else
                        {
                            SelectedAutoSelect = AutoSelectList[AutoSelectList.IndexOf(profile) + 1];
                        }
                    }
                    AutoSelectList.Remove(profile);
                }
            }
            return Task.FromResult(0);
        }
    }
}
