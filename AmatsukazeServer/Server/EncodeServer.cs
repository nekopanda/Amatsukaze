#define PROFILE
using Amatsukaze.Lib;
using Livet;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;

namespace Amatsukaze.Server
{
    public class EncodeServer : NotificationObject, IEncodeServer, IDisposable
    {
        [DataContract]
        public class AppData : IExtensibleDataObject
        {
            [DataMember]
            public Setting setting;
            [DataMember]
            public UIState uiState;
            [DataMember]
            public MakeScriptData scriptData;
            [DataMember]
            public ServiceSetting services;

            // 0: ～4.0.3
            // 1: 4.1.0～
            [DataMember]
            public int Version;

            public ExtensionDataObject ExtensionData { get; set; }
        }

        private class EncodeException : Exception
        {
            public EncodeException(string message)
                : base(message)
            {
            }
        }

        internal IUserClient Client { get; private set; }
        public Task ServerTask { get; private set; }
        internal AppData AppData_ { get; private set; }

        private Action finishRequested;

        private QueueManager queueManager;
        private ScheduledQueue scheduledQueue;
        private WorkerPool workerPool;

        private LogData logData = new LogData();
        private CheckLogData checkLogData = new CheckLogData();
        private SortedDictionary<string, DiskItem> diskMap = new SortedDictionary<string, DiskItem>();

        internal readonly AffinityCreator affinityCreator = new AffinityCreator();

        private Dictionary<string, ProfileSetting> profiles = new Dictionary<string, ProfileSetting>();
        private Dictionary<string, AutoSelectProfile> autoSelects = new Dictionary<string, AutoSelectProfile>();
        private List<string> JlsCommandFiles = new List<string>();
        private List<string> MainScriptFiles = new List<string>();
        private List<string> PostScriptFiles = new List<string>();
        private List<string> AddQueueBatFiles = new List<string>();
        private List<string> PreBatFiles = new List<string>();
        private List<string> PostBatFiles = new List<string>();
        private DRCSManager drcsManager;

        // キューに追加されるTSを解析するスレッド
        private Task queueThread;
        private BufferBlock<AddQueueRequest> queueQ = new BufferBlock<AddQueueRequest>();

        // ロゴファイルやJLSコマンドファイルを監視するスレッド
        private Task watchFileThread;
        private BufferBlock<int> watchFileQ = new BufferBlock<int>();
        private bool serviceListUpdated;

        // 設定を保存するスレッド
        private Task saveSettingThread;
        private BufferBlock<int> saveSettingQ = new BufferBlock<int>();
        private bool settingUpdated;
        private bool autoSelectUpdated;

        // DRCS処理用スレッド
        private Task drcsThread;
        private BufferBlock<int> drcsQ = new BufferBlock<int>();

        private PreventSuspendContext preventSuspend;

        // プロファイル未選択状態のダミープロファイル
        public ProfileSetting PendingProfile { get; private set; }

        internal ResourceManager ResourceManager { get; private set; } = new ResourceManager();

        // データファイル
        private DataFile<LogItem> logFile;
        private DataFile<CheckLogItem> checkLogFile;

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

        #region Progress変更通知プロパティ
        private double _Progress;

        public double Progress {
            get { return _Progress; }
            set { 
                if (_Progress == value)
                    return;
                _Progress = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public ClientManager ClientManager {
            get { return Client as ClientManager; }
        }

        public Dictionary<int, ServiceSettingElement> ServiceMap { get { return AppData_.services.ServiceMap; } }

        public string LastUsedProfile {
            get { return AppData_.uiState.LastUsedProfile; }
            set {
                if (AppData_.uiState.LastUsedProfile != value)
                {
                    AppData_.uiState.LastUsedProfile = value;
                    settingUpdated = true;
                }
            }
        }

        public string LastOutputPath
        {
            get { return AppData_.uiState.LastOutputPath; }
            set
            {
                if (AppData_.uiState.LastOutputPath != value)
                {
                    AppData_.uiState.LastOutputPath = value;
                    settingUpdated = true;
                }
            }
        }

        public string LastAddQueueBat {
            get { return AppData_.uiState.LastAddQueueBat; }
            set {
                if (AppData_.uiState.LastAddQueueBat != value)
                {
                    AppData_.uiState.LastAddQueueBat = value;
                    settingUpdated = true;
                }
            }
        }

        public EncodeServer(int port, IUserClient client, Action finishRequested)
        {
#if PROFILE
            var prof = new Profile();
#endif
            this.finishRequested = finishRequested;

            queueManager = new QueueManager(this);
            drcsManager = new DRCSManager(this);

            LoadAppData();
            LoadAutoSelectData();
            if (client != null)
            {
                // スタンドアロン
                this.Client = client;

                // 終了待機
                var fs = ServerSupport.CreateStandaloneMailslot();
                ServerSupport.WaitStandaloneMailslot(fs).ContinueWith(task =>
                {
                    // 終了リクエストが来た
                    client.Finish();
                });
            }
            else
            {
                var clientManager = new ClientManager(this);
                ServerTask = clientManager.Listen(port);
                this.Client = clientManager;
                RaisePropertyChanged("ClientManager");
            }
#if PROFILE
            prof.PrintTime("EncodeServer 1");
#endif
            PendingProfile = new ProfileSetting()
            {
                Name = "プロファイル未選択",
                LastUpdate = DateTime.MinValue,
            };

            scheduledQueue = new ScheduledQueue();
            workerPool = new WorkerPool()
            {
                Queue = scheduledQueue,
                NewWorker = id => new TranscodeWorker(id, this),
                OnStart = () => {
                    if(!Directory.Exists(GetDRCSDirectoryPath()))
                    {
                        Directory.CreateDirectory(GetDRCSDirectoryPath());
                    }
                    if(!File.Exists(GetDRCSMapPath()))
                    {
                        File.Create(GetDRCSMapPath());
                    }
                    if (AppData_.setting.ClearWorkDirOnStart)
                    {
                        CleanTmpDir();
                    }
                    NowEncoding = true;
                    Progress = 0;
                    return RequestState();
                },
                OnFinish = ()=> {
                    NowEncoding = false;
                    Progress = 1;
                    var task = RequestState();
                    if (preventSuspend != null)
                    {
                        preventSuspend.Dispose();
                        preventSuspend = null;
                    }
                    //if (appData.setting.FinishAction != FinishAction.None)
                    //{
                    //    // これは使えない
                    //    // - サービスだとユーザ操作を検知できない
                    //    // - なぜか常に操作があると認識されることがある
                    //    //if (WinAPI.GetLastInputTime().Minutes >= 3)
                    //    {
                    //        var state = (appData.setting.FinishAction == FinishAction.Suspend)
                    //                ? System.Windows.Forms.PowerState.Suspend
                    //                : System.Windows.Forms.PowerState.Hibernate;
                    //        System.Windows.Forms.Application.SetSuspendState(state, false, false);
                    //    }
                    //}
                    return task;
                },
                OnError = (id, mes, e) =>
                {
                    if(e != null)
                    {
                        return FatalError(id, mes, e);
                    }
                    else
                    {
                        return NotifyMessage(id, mes, true);
                    }
                }
            };
            workerPool.SetNumParallel(AppData_.setting.NumParallel);
            scheduledQueue.WorkerPool = workerPool;
            
            SetScheduleParam(AppData_.setting.SchedulingEnabled, 
                AppData_.setting.NumGPU, AppData_.setting.MaxGPUResources);

#if PROFILE
            prof.PrintTime("EncodeServer 2");
#endif
        }

        // コンストラクタはasyncにできないのでasyncする処理は分離
        public async Task Init()
        {
#if PROFILE
            var prof = new Profile();
#endif
            // 古いバージョンからの更新処理
            UpdateFromOldVersion();
#if PROFILE
            prof.PrintTime("EncodeServer A");
#endif
            // エンコードを開始する前にログは読み込んでおく
            logFile = new DataFile<LogItem>(GetHistoryFilePathV2());
            checkLogFile = new DataFile<CheckLogItem>(GetCheckHistoryFilePath());

            logData.Items = await logFile.Read();
            checkLogData.Items = await checkLogFile.Read();
#if PROFILE
            prof.PrintTime("EncodeServer B");
#endif
            // キュー状態を戻す
            queueManager.LoadAppData();
            if (queueManager.Queue.Any(s => s.IsActive))
            {
                // アクティブなアイテムがある状態から開始する場合はキューを凍結する
                EncodePaused = true;
                workerPool.SetPause(true);
                queueManager.UpdateQueueItems(null);
            }

            // DRCS文字情報解析に回す
            foreach (var item in logData.Items)
            {
                drcsManager.AddLogFile(GetLogFileBase(item.EncodeStartDate) + ".txt",
                    item.SrcPath, item.EncodeFinishDate);
            }
            foreach (var item in checkLogData.Items)
            {
                drcsManager.AddLogFile(GetCheckLogFileBase(item.CheckStartDate) + ".txt",
                    item.SrcPath, item.CheckStartDate);
            }
#if PROFILE
            prof.PrintTime("EncodeServer C");
#endif
            watchFileThread = WatchFileThread();
            saveSettingThread = SaveSettingThread();
            queueThread = QueueThread();
            drcsThread = DrcsThread();
#if PROFILE
            prof.PrintTime("EncodeServer D");
#endif
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

                    // キュー状態を保存する
                    try
                    {
                        queueManager.SaveQueueData(false);
                    }
                    catch(Exception)
                    {
                        // Dispose中の例外は仕方ないので無視する
                    }

                    // 終了時にプロセスが残らないようにする
                    if (workerPool != null)
                    {
                        foreach (var worker in workerPool.Workers.Cast<TranscodeWorker>())
                        {
                            if (worker != null)
                            {
                                worker.CancelCurrentItem();
                            }
                        }
                        workerPool.Finish();
                    }

                    queueQ.Complete();
                    watchFileQ.Complete();
                    saveSettingQ.Complete();
                    drcsQ.Complete();

                    if (settingUpdated)
                    {
                        settingUpdated = false;
                        try
                        {
                            SaveAppData();
                        }
                        catch (Exception)
                        {
                            // Dispose中の例外は仕方ないので無視する
                        }
                    }

                    if(autoSelectUpdated)
                    {
                        autoSelectUpdated = false;
                        try
                        {
                            SaveAutoSelectData();
                        }
                        catch (Exception)
                        {
                            // Dispose中の例外は仕方ないので無視する
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

        private string GetAutoSelectFilePath()
        {
            return "config\\AutoSelectProfile.xml";
        }

        internal string GetQueueFilePath()
        {
            return "data\\Queue.xml";
        }

        private string GetHistoryFilePathV1()
        {
            return "data\\EncodeHistory.xml";
        }

        private string GetHistoryFilePathV2()
        {
            return "data\\EncodeHistoryV2.xml";
        }

        internal string GetLogFileBase(DateTime start)
        {
            return "data\\logs\\" + start.ToString("yyyy-MM-dd_HHmmss.fff");
        }

        private string ReadLogFIle(DateTime start)
        {
            var logpath = GetLogFileBase(start) + ".txt";
            if (File.Exists(logpath) == false)
            {
                return "ログファイルが見つかりません。パス: " + logpath;
            }
            return File.ReadAllText(logpath, Encoding.Default);
        }

        private string GetCheckHistoryFilePath()
        {
            return "data\\CheckHistory.xml";
        }

        internal string GetCheckLogFileBase(DateTime start)
        {
            return "data\\checklogs\\" + start.ToString("yyyy-MM-dd_HHmmss.fff");
        }

        private string ReadCheckLogFIle(DateTime start)
        {
            var logpath = GetCheckLogFileBase(start) + ".txt";
            if (File.Exists(logpath) == false)
            {
                return "ログファイルが見つかりません。パス: " + logpath;
            }
            return File.ReadAllText(logpath, Encoding.Default);
        }

        private string GetLogoDirectoryPath()
        {
            return Path.GetFullPath("logo");
        }

        private string GetLogoFilePath(string fileName)
        {
            return GetLogoDirectoryPath() + "\\" + fileName;
        }

        private string GetJLDirectoryPath()
        {
            return Path.GetFullPath("JL");
        }

        internal string GetAvsDirectoryPath()
        {
            return Path.GetFullPath("avs");
        }

        internal string GetBatDirectoryPath()
        {
            return Path.GetFullPath("bat");
        }

        internal string GetDRCSDirectoryPath()
        {
            return Path.GetFullPath("drcs");
        }

        internal string GetDRCSImagePath(string md5)
        {
            return GetDRCSImagePath(GetDRCSDirectoryPath(), md5);
        }

        private string GetDRCSImagePath(string md5, string dirPath)
        {
            return dirPath + "\\" + md5 + ".bmp";
        }

        internal string GetDRCSMapPath()
        {
            return GetDRCSMapPath(GetDRCSDirectoryPath());
        }

        internal string GetDRCSMapPath(string dirPath)
        {
            return dirPath + "\\drcs_map.txt";
        }

        private string GetProfileDirectoryPath()
        {
            return Path.GetFullPath("profile");
        }

        private string GetProfilePath(string dirpath, string name)
        {
            return dirpath + "\\" + name + ".profile";
        }
        #endregion

        public void Finish()
        {
            // Finishはスタブの接続を切るためのインターフェースなので
            // サーバ本体では使われない
            throw new NotImplementedException();
        }

        private void SetScheduleParam(bool enable, int numGPU, int[] maxGPU)
        {
            ResourceManager.SetGPUResources(numGPU, maxGPU);
            scheduledQueue.SetGPUResources(numGPU, maxGPU);
            scheduledQueue.EnableResourceScheduling = enable;
        }

        private void UpdateFromOldVersion()
        {
            int CurrentVersion = 1;

            // 古いバージョンからのアップデート処理
            if(AppData_.Version < CurrentVersion)
            {
                // DRCS文字の並びを変更する
                drcsManager.UpdateFromOldVersion();

                // ログファイルを移行
                string path = GetHistoryFilePathV1();
                if (File.Exists(path))
                {
                    try
                    {
                        using (FileStream fs = new FileStream(path, FileMode.Open))
                        {
                            var s = new DataContractSerializer(typeof(LogData));
                            var data = (LogData)s.ReadObject(fs);
                            if (data.Items != null)
                            {
                                var file = new DataFile<LogItem>(GetHistoryFilePathV2());
                                file.Delete();
                                file.Add(data.Items);
                            }
                        }
                        File.Delete(path);
                    }
                    catch (IOException e)
                    {
                        Util.AddLog("ログファイルの移行に失敗", e);
                    }
                }

                // 現在バージョンに更新
                AppData_.Version = CurrentVersion;

                // 起動処理で落ちると２重に処理することになるので、
                // ここで設定ファイルに書き込んでおく
                SaveAppData();
            }
        }

        #region メッセージ出力
        private Task doNotify(int id, string message, Exception e, bool error, bool log)
        {
            if (log)
            {
                Util.AddLog(id, message, e);
            }
            return Client?.OnOperationResult(new OperationResult()
            {
                IsFailed = error,
                Message = Util.ErrorMessage(id, message, e),
                StackTrace = e?.StackTrace
            });
        }

        internal Task NotifyMessage(int id, string message, bool log)
        {
            return doNotify(id, message, null, false, log);
        }

        internal Task NotifyMessage(string message, bool log)
        {
            return doNotify(-1, message, null, false, log);
        }

        internal Task NotifyError(int id, string message, bool log)
        {
            return doNotify(id, message, null, true, log);
        }

        internal Task NotifyError(string message, bool log)
        {
            return doNotify(-1, message, null, true, log);
        }

        internal Task FatalError(int id, string message, Exception e)
        {
            return doNotify(id, message, e, true, true);
        }

        internal Task FatalError(string message, Exception e)
        {
            return doNotify(-1, message, e, true, true);
        }
        #endregion

        private static string GetExePath(string basePath, string pattern)
        {
            foreach (var path in Directory.GetFiles(basePath))
            {
                var fname = Path.GetFileName(path);
                if (fname.StartsWith(pattern) && fname.EndsWith(".exe"))
                {
                    return path;
                }
            }
            return null;
        }

        private Setting SetDefaultPath(Setting setting)
        {
            string basePath = Path.GetDirectoryName(GetType().Assembly.Location);
            if (string.IsNullOrEmpty(setting.AmatsukazePath))
            {
                setting.AmatsukazePath = Path.Combine(basePath, "AmatsukazeCLI.exe");
            }
            if (string.IsNullOrEmpty(setting.X264Path))
            {
                setting.X264Path = GetExePath(basePath, "x264");
            }
            if (string.IsNullOrEmpty(setting.X265Path))
            {
                setting.X265Path = GetExePath(basePath, "x265");
            }
            if (string.IsNullOrEmpty(setting.MuxerPath))
            {
                setting.MuxerPath = Path.Combine(basePath, "muxer.exe");
            }
            if (string.IsNullOrEmpty(setting.MKVMergePath))
            {
                setting.MKVMergePath = Path.Combine(basePath, "mkvmerge.exe");
            }
            if (string.IsNullOrEmpty(setting.MP4BoxPath))
            {
                setting.MP4BoxPath = Path.Combine(basePath, "mp4box.exe");
            }
            if (string.IsNullOrEmpty(setting.TimelineEditorPath))
            {
                setting.TimelineEditorPath = Path.Combine(basePath, "timelineeditor.exe");
            }
            if (string.IsNullOrEmpty(setting.ChapterExePath))
            {
                setting.ChapterExePath = GetExePath(basePath, "chapter_exe");
            }
            if (string.IsNullOrEmpty(setting.JoinLogoScpPath))
            {
                setting.JoinLogoScpPath = GetExePath(basePath, "join_logo_scp");
            }
            return setting;
        }

        private Setting GetDefaultSetting()
        {
            string basePath = Path.GetDirectoryName(GetType().Assembly.Location);
            return SetDefaultPath(new Setting() { NumParallel = 1 });
        }



        private void LoadAppData()
        {
            string path = GetSettingFilePath();
            if (File.Exists(path) == false)
            {
                AppData_ = new AppData();
            }
            else
            {
                using (FileStream fs = new FileStream(path, FileMode.Open))
                {
                    var s = new DataContractSerializer(typeof(AppData));
                    AppData_ = (AppData)s.ReadObject(fs);
                }
            }
            if (AppData_.setting == null)
            {
                AppData_.setting = GetDefaultSetting();
            }
            if(string.IsNullOrWhiteSpace(AppData_.setting.WorkPath) ||
                Directory.Exists(AppData_.setting.WorkPath) == false)
            {
                // 一時フォルダにアクセスできないときは、デフォルト一時フォルダを設定
                AppData_.setting.WorkPath = Path.GetTempPath().TrimEnd(Path.DirectorySeparatorChar);
            }
            if (AppData_.setting.NumGPU == 0)
            {
                AppData_.setting.NumGPU = 1;
            }
            if (AppData_.setting.MaxGPUResources == null ||
                AppData_.setting.MaxGPUResources.Length < ResourceManager.MAX_GPU)
            {
                AppData_.setting.MaxGPUResources = Enumerable.Repeat(100, ResourceManager.MAX_GPU).ToArray();
            }
            if (AppData_.uiState == null)
            {
                AppData_.uiState = new UIState();
            }
            if (AppData_.scriptData == null)
            {
                AppData_.scriptData = new MakeScriptData();
            }
            if (AppData_.services == null)
            {
                AppData_.services = new ServiceSetting();
            }
            if (AppData_.services.ServiceMap == null)
            {
                AppData_.services.ServiceMap = new Dictionary<int, ServiceSettingElement>();
            }
        }

        private void SaveAppData()
        {
            string path = GetSettingFilePath();
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            using (FileStream fs = new FileStream(path, FileMode.Create))
            {
                var s = new DataContractSerializer(typeof(AppData));
                s.WriteObject(fs, AppData_);
            }
        }

        [DataContract]
        public class AutoSelectData
        {
            [DataMember]
            public List<AutoSelectProfile> Profiles { get; set; }
        }

        private void LoadAutoSelectData()
        {
            string path = GetAutoSelectFilePath();
            if (File.Exists(path) == false)
            {
                return;
            }
            using (FileStream fs = new FileStream(path, FileMode.Open))
            {
                var s = new DataContractSerializer(typeof(AutoSelectData));
                try
                {
                    var data = (AutoSelectData)s.ReadObject(fs);
                    autoSelects.Clear();
                    foreach (var profile in data.Profiles)
                    {
                        foreach (var cond in profile.Conditions)
                        {
                            if (cond.ContentConditions == null)
                            {
                                cond.ContentConditions = new List<GenreItem>();
                            }
                            if (cond.ServiceIds == null)
                            {
                                cond.ServiceIds = new List<int>();
                            }
                            if (cond.VideoSizes == null)
                            {
                                cond.VideoSizes = new List<VideoSizeCondition>();
                            }
                        }
                        autoSelects.Add(profile.Name, profile);
                    }
                }
                catch(Exception e)
                {
                    FatalError("自動選択プロファイルを読み込めませんでした", e);
                }
            }
        }

        private void SaveAutoSelectData()
        {
            string path = GetAutoSelectFilePath();
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            using (FileStream fs = new FileStream(path, FileMode.Create))
            {
                var s = new DataContractSerializer(typeof(AutoSelectData));
                s.WriteObject(fs, new AutoSelectData()
                {
                    Profiles = autoSelects.Values.ToList()
                });
            }
        }

        private ProfileSetting ReadProfile(string filepath)
        {
            using (FileStream fs = new FileStream(filepath, FileMode.Open))
            {
                var s = new DataContractSerializer(typeof(ProfileSetting));
                var profile = (ProfileSetting)s.ReadObject(fs);
                if (profile.Bitrate == null)
                {
                    profile.Bitrate = new BitrateSetting();
                }
                if (profile.NicoJKFormats == null)
                {
                    profile.NicoJKFormats = new bool[4] { true, false, false, false };
                }
                if(profile.ReqResources == null)
                {
                    // 5個でいいけど予備を3つ置いておく
                    profile.ReqResources = new ReqResource[8];
                }
                return profile;
            }
        }

        private void SaveProfile(string filepath, ProfileSetting profile)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(filepath));
            using (FileStream fs = new FileStream(filepath, FileMode.Create))
            {
                var s = new DataContractSerializer(typeof(ProfileSetting));
                s.WriteObject(fs, profile);
            }
        }

        internal Task AddEncodeLog(LogItem item)
        {
            try
            {
                logData.Items.Add(item);
                logFile.Add(new List<LogItem>() { item });
                drcsManager.AddLogFile(GetLogFileBase(item.EncodeStartDate) + ".txt",
                    item.SrcPath, item.EncodeFinishDate);
                return Client.OnUIData(new UIData()
                {
                    LogItem = item
                });
            }
            catch (IOException e)
            {
                Util.AddLog("ログファイル書き込み失敗", e);
            }
            return Task.FromResult(0);
        }

        internal Task AddCheckLog(CheckLogItem item)
        {
            try
            {
                checkLogData.Items.Add(item);
                checkLogFile.Add(new List<CheckLogItem>() { item });
                drcsManager.AddLogFile(GetCheckLogFileBase(item.CheckStartDate) + ".txt",
                    item.SrcPath, item.CheckStartDate);
                return Client.OnUIData(new UIData()
                {
                    CheckLogItem = item
                });
            }
            catch (IOException e)
            {
                Util.AddLog("ログファイル書き込み失敗", e);
            }
            return Task.FromResult(0);
        }

        private static string GetEncoderPath(EncoderType encoderType, Setting setting)
        {
            if (encoderType == EncoderType.x264)
            {
                return setting.X264Path;
            }
            else if (encoderType == EncoderType.x265)
            {
                return setting.X265Path;
            }
            else if (encoderType == EncoderType.QSVEnc)
            {
                return setting.QSVEncPath;
            }
            else
            {
                return setting.NVEncPath;
            }
        }

        private string GetEncoderOption(ProfileSetting profile)
        {
            if (profile.EncoderType == EncoderType.x264)
            {
                return profile.X264Option;
            }
            else if (profile.EncoderType == EncoderType.x265)
            {
                return profile.X265Option;
            }
            else if (profile.EncoderType == EncoderType.QSVEnc)
            {
                return profile.QSVEncOption;
            }
            else
            {
                return profile.NVEncOption;
            }
        }

        private string GetEncoderName(EncoderType encoderType)
        {
            if (encoderType == EncoderType.x264)
            {
                return "x264";
            }
            else if (encoderType == EncoderType.x265)
            {
                return "x265";
            }
            else if (encoderType == EncoderType.QSVEnc)
            {
                return "QSVEnc";
            }
            else
            {
                return "NVEnc";
            }
        }

        internal string MakeAmatsukazeArgs(
            ProcMode mode,
            ProfileSetting profile,
            Setting setting,
            bool isGeneric,
            string src, string dst, string json,
            int serviceId, string[] logofiles,
            bool ignoreNoLogo, string jlscommand, string jlsopt,
            string inHandle, string outHandle, int pid)
        {
            StringBuilder sb = new StringBuilder();

            if (mode == ProcMode.CMCheck)
            {
                sb.Append("--mode cm");
            }
            else if(mode == ProcMode.DrcsCheck)
            {
                sb.Append("--mode drcs");
            }
            else if (isGeneric)
            {
                sb.Append("--mode g");
            }

            if(setting.DumpFilter)
            {
                sb.Append(" --dump-filter");
            }

            sb.Append(" -i \"")
                .Append(src)
                .Append("\" -s ")
                .Append(serviceId)
                .Append(" --drcs \"")
                .Append(GetDRCSMapPath())
                .Append("\"");

            if(inHandle != null)
            {
                sb.Append(" --resource-manager ")
                    .Append(inHandle)
                    .Append(':')
                    .Append(outHandle);
            }

            // スケジューリングが有効な場合はエンコード時にアフィニティを設定するので
            // ここでは設定しない
            if (setting.SchedulingEnabled == false &&
                setting.AffinitySetting != (int)ProcessGroupKind.None)
            {
                var mask = affinityCreator.GetMask(
                    (ProcessGroupKind)setting.AffinitySetting, pid);
                sb.Append(" --affinity ")
                    .Append(mask.Group)
                    .Append(':')
                    .Append(mask.Mask);
            }

            if (mode == ProcMode.DrcsCheck)
            {
                sb.Append(" --subtitles");
            }
            else {
                int outputMask = profile.OutputMask;
                if (outputMask == 0)
                {
                    outputMask = 1;
                }

                sb.Append(" -w \"")
                    .Append(setting.WorkPath)
                    .Append("\" --chapter-exe \"")
                    .Append(setting.ChapterExePath)
                    .Append("\" --jls \"")
                    .Append(setting.JoinLogoScpPath)
                    .Append("\" --cmoutmask ")
                    .Append(outputMask);


                if (mode == ProcMode.CMCheck)
                {
                    sb.Append(" --chapter");
                }
                else {
                    string encoderPath = GetEncoderPath(profile.EncoderType, setting);

                    double bitrateCM = profile.BitrateCM;
                    if (bitrateCM == 0)
                    {
                        bitrateCM = 1;
                    }

                    sb.Append(" -o \"")
                        .Append(dst)
                        .Append("\" -et ")
                        .Append(GetEncoderName(profile.EncoderType))
                        .Append(" -e \"")
                        .Append(encoderPath)
                        .Append("\" -j \"")
                        .Append(json)
                        .Append("\"");

                    if (profile.OutputFormat == FormatType.MP4)
                    {
                        sb.Append(" --mp4box \"")
                            .Append(setting.MP4BoxPath)
                            .Append("\" -t \"")
                            .Append(setting.TimelineEditorPath)
                            .Append("\"");
                    }

                    var encoderOption = GetEncoderOption(profile);
                    if (string.IsNullOrEmpty(encoderOption) == false)
                    {
                        sb.Append(" -eo \"")
                            .Append(encoderOption)
                            .Append("\"");
                    }

                    if (profile.OutputFormat == FormatType.MP4)
                    {
                        sb.Append(" -fmt mp4 -m \"" + setting.MuxerPath + "\"");
                    }
                    else if (profile.OutputFormat == FormatType.MKV)
                    {
                        sb.Append(" -fmt mkv -m \"" + setting.MKVMergePath + "\"");
                    }
                    else if(profile.OutputFormat == FormatType.M2TS)
                    {
                        sb.Append(" -fmt m2ts -m \"" + setting.TsMuxeRPath + "\"");
                    }
                    else if (profile.OutputFormat == FormatType.TS)
                    {
                        sb.Append(" -fmt ts -m \"" + setting.TsMuxeRPath + "\"");
                    }

                    if (bitrateCM != 1)
                    {
                        sb.Append(" -bcm ").Append(bitrateCM);
                    }
                    if (setting.EnableX265VFRTimeFactor &&
                        profile.EncoderType == EncoderType.x265)
                    {
                        sb.Append(" --x265-timefactor ")
                            .Append(setting.X265VFRTimeFactor.ToString("N2"));
                    }
                    if (profile.SplitSub)
                    {
                        sb.Append(" --splitsub");
                    }
                    if (!profile.DisableChapter)
                    {
                        sb.Append(" --chapter");
                    }
                    if (profile.VFR120fps)
                    {
                        sb.Append(" --vfr120fps");
                    }
                    if (profile.EnableNicoJK)
                    {
                        sb.Append(" --nicojk");
                        if (profile.IgnoreNicoJKError)
                        {
                            sb.Append(" --ignore-nicojk-error");
                        }
                        if (profile.NicoJK18)
                        {
                            sb.Append(" --nicojk18");
                        }
                        sb.Append(" --nicojkmask ")
                            .Append(profile.NicoJKFormatMask);
                        sb.Append(" --nicoass \"")
                            .Append(setting.NicoConvASSPath)
                            .Append("\"");
                    }

                    if (string.IsNullOrEmpty(profile.FilterPath) == false)
                    {
                        sb.Append(" -f \"")
                            .Append(GetAvsDirectoryPath() + "\\" + profile.FilterPath)
                            .Append("\"");
                    }

                    if (string.IsNullOrEmpty(profile.PostFilterPath) == false)
                    {
                        sb.Append(" -pf \"")
                            .Append(GetAvsDirectoryPath() + "\\" + profile.PostFilterPath)
                            .Append("\"");
                    }

                    if (profile.AutoBuffer)
                    {
                        sb.Append(" --bitrate ")
                            .Append(profile.Bitrate.A)
                            .Append(":")
                            .Append(profile.Bitrate.B)
                            .Append(":")
                            .Append(profile.Bitrate.H264);
                    }

                    if (profile.TwoPass)
                    {
                        sb.Append(" --2pass");
                    }
                } // if (mode != ProcMode.CMCheck)

                if (!profile.DisableSubs)
                {
                    sb.Append(" --subtitles");
                }

                if (string.IsNullOrEmpty(jlscommand) == false)
                {
                    sb.Append(" --jls-cmd \"")
                        .Append(GetJLDirectoryPath() + "\\" + jlscommand)
                        .Append("\"");
                }
                if (string.IsNullOrEmpty(jlsopt) == false)
                {
                    sb.Append(" --jls-option \"")
                        .Append(jlsopt)
                        .Append("\"");
                }

                string[] decoderNames = new string[] { "default", "QSV", "CUVID" };
                if (profile.Mpeg2Decoder != DecoderType.Default)
                {
                    sb.Append("  --mpeg2decoder ");
                    sb.Append(decoderNames[(int)profile.Mpeg2Decoder]);
                }

                if (profile.H264Deocder != DecoderType.Default)
                {
                    sb.Append("  --h264decoder ");
                    sb.Append(decoderNames[(int)profile.H264Deocder]);
                }
                if (ignoreNoLogo)
                {
                    sb.Append(" --ignore-no-logo");
                }
                if (profile.LooseLogoDetection)
                {
                    sb.Append(" --loose-logo-detection");
                }
                if (profile.IgnoreNoDrcsMap)
                {
                    sb.Append(" --ignore-no-drcsmap");
                }
                if (profile.NoDelogo)
                {
                    sb.Append(" --no-delogo");
                }
                if (profile.NoRemoveTmp)
                {
                    sb.Append(" --no-remove-tmp");
                }

                if (logofiles != null)
                {
                    foreach (var logo in logofiles)
                    {
                        sb.Append(" --logo \"").Append(GetLogoFilePath(logo)).Append("\"");
                    }
                }
                if (profile.SystemAviSynthPlugin)
                {
                    sb.Append(" --systemavsplugin");
                }
            }

            return sb.ToString();
        }

        public Task ClientQueueUpdate(QueueUpdate update)
        {
            return Client.OnUIData(new UIData()
            {
                QueueUpdate = update
            });
        }

        private void CleanTmpDir()
        {
            foreach (var dir in Directory
                .GetDirectories(AppData_.setting.ActualWorkPath)
                .Where(s => Path.GetFileName(s).StartsWith("amt")))
            {
                try
                {
                    Directory.Delete(dir, true);
                }
                catch (Exception) { } // 例外は無視
            }
            foreach (var file in Directory
                .GetFiles(AppData_.setting.ActualWorkPath)
                .Where(s => Path.GetFileName(s).StartsWith("amt")))
            {
                try
                {
                    File.Delete(file);
                }
                catch (Exception) { } // 例外は無視
            }
        }

        private static void CheckPath(string name, string path)
        {
            if(!string.IsNullOrEmpty(path) && !File.Exists(path))
            {
                throw new InvalidOperationException(name + "パスが無効です: " + path);
            }
        }

        private static void CheckSetting(ProfileSetting profile, Setting setting)
        {
            if (!File.Exists(setting.AmatsukazePath))
            {
                throw new InvalidOperationException(
                    "AmtasukazeCLIパスが無効です: " + setting.AmatsukazePath);
            }

            if(setting.WorkPath.EndsWith(Path.DirectorySeparatorChar.ToString()))
            {
                setting.WorkPath = setting.WorkPath.TrimEnd(Path.DirectorySeparatorChar);
            }
            string workPath = setting.ActualWorkPath;
            if (!Directory.Exists(workPath))
            {
                throw new InvalidOperationException(
                    "一時フォルダパスが無効です: " + workPath);
            }

            // パスが設定されていたらファイル存在チェック
            CheckPath("x264", setting.X264Path);
            CheckPath("x265", setting.X265Path);
            CheckPath("QSVEnc", setting.QSVEncPath);
            CheckPath("NVEnc", setting.NVEncPath);

            CheckPath("L-SMASH Muxer", setting.MuxerPath);
            CheckPath("MP4Box", setting.MP4BoxPath);
            CheckPath("TimelineEditor", setting.TimelineEditorPath);
            CheckPath("MKVMerge", setting.MKVMergePath);
            CheckPath("ChapterExe", setting.ChapterExePath);
            CheckPath("JoinLogoScp", setting.JoinLogoScpPath);
            CheckPath("NicoConvAss", setting.NicoConvASSPath);

            if (profile != null)
            {
                string encoderPath = GetEncoderPath(profile.EncoderType, setting);
                if (string.IsNullOrEmpty(encoderPath))
                {
                    throw new ArgumentException("エンコーダパスが設定されていません");
                }

                if (profile.OutputFormat == FormatType.MP4)
                {
                    if (string.IsNullOrEmpty(setting.MuxerPath))
                    {
                        throw new ArgumentException("L-SMASH Muxerパスが設定されていません");
                    }
                    if (string.IsNullOrEmpty(setting.MP4BoxPath))
                    {
                        throw new ArgumentException("MP4Boxパスが指定されていません");
                    }
                    if (string.IsNullOrEmpty(setting.TimelineEditorPath))
                    {
                        throw new ArgumentException("Timelineeditorパスが設定されていません");
                    }
                }
                else if(profile.OutputFormat == FormatType.MKV)
                {
                    if (string.IsNullOrEmpty(setting.MKVMergePath))
                    {
                        throw new ArgumentException("MKVMergeパスが設定されていません");
                    }
                }
                else
                {
                    if (string.IsNullOrEmpty(setting.TsMuxeRPath))
                    {
                        throw new ArgumentException("tsMuxeRパスが設定されていません");
                    }
                }

                if (!profile.DisableChapter)
                {
                    if (string.IsNullOrEmpty(setting.ChapterExePath))
                    {
                        throw new ArgumentException("ChapterExeパスが設定されていません");
                    }
                    if (string.IsNullOrEmpty(setting.JoinLogoScpPath))
                    {
                        throw new ArgumentException("JoinLogoScpパスが設定されていません");
                    }
                }

                if (profile.EnableNicoJK)
                {
                    if (string.IsNullOrEmpty(setting.NicoConvASSPath))
                    {
                        throw new ArgumentException("NicoConvASSパスが設定されていません");
                    }
                }

                if (profile.EnableRename)
                {
                    if (string.IsNullOrEmpty(setting.SCRenamePath))
                    {
                        throw new ArgumentException("SCRenameパスが設定されていません");
                    }
                    var fileName = Path.GetFileName(setting.SCRenamePath);
                    // 間違える人がいるかも知れないので一応チェックしておく
                    if(fileName.Equals("SCRename.bat", StringComparison.OrdinalIgnoreCase) ||
                        fileName.Equals("SCRenameEDCB.bat", StringComparison.OrdinalIgnoreCase))
                    {
                        throw new ArgumentException("SCRenameはbatファイルではなくvbsファイルへのパスを設定してください");
                    }
                    if (string.IsNullOrEmpty(profile.RenameFormat))
                    {
                        throw new ArgumentException("リネームフォーマットが設定されていません");
                    }
                }
            }
        }

        private void CheckAutoSelect(AutoSelectProfile profile)
        {
            foreach(var cond in profile.Conditions)
            {
                if(cond.Profile != null)
                {
                    if(!profiles.ContainsKey(cond.Profile))
                    {
                        throw new ArgumentException("プロファイル「" + cond.Profile + "」がありません");
                    }
                }
            }
        }

        private async Task QueueThread()
        {
            try
            {
                while (await queueQ.OutputAvailableAsync())
                {
                    AddQueueRequest req = await queueQ.ReceiveAsync();
                    await queueManager.AddQueue(req);
                    await Client.OnAddResult(req.RequestId);
                }
            }
            catch (Exception exception)
            {
                await FatalError("QueueThreadがエラー終了しました", exception);
            }
        }

        internal class ProfileTuple
        {
            public ProfileSetting Profile;
            public int Priority;
        }

        internal ProfileTuple GetProfile(List<string> tags, string fileName, int width, int height,
            List<GenreItem> genre, int serviceId, string profileName)
        {
            bool isAuto = false;
            int resolvedPriority = 0;
            profileName = ServerSupport.ParseProfileName(profileName, out isAuto);
            if (isAuto)
            {
                if (autoSelects.ContainsKey(profileName) == false)
                {
                    return null;
                }
                if(serviceId == -1)
                {
                    // TS情報がない
                    return null;
                }
                var resolvedProfile = ServerSupport.AutoSelectProfile(tags, fileName, width, height,
                    genre, serviceId, autoSelects[profileName], out resolvedPriority);
                if (resolvedProfile == null)
                {
                    return null;
                }
                profileName = resolvedProfile;
            }
            if (profiles.ContainsKey(profileName) == false)
            {
                return null;
            }
            return new ProfileTuple()
            {
                Profile = profiles[profileName],
                Priority = resolvedPriority
            };
        }

        internal ProfileTuple GetProfile(QueueItem item, string profileName)
        {
            return GetProfile(item.Tags, Path.GetFileName(item.SrcPath), item.ImageWidth, item.ImageHeight,
                item.Genre, item.ServiceId, profileName);
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
                var avsDirTime = DateTime.MinValue;
                var batDirTime = DateTime.MinValue;
                var profileDirTime = DateTime.MinValue;

                // 初期化
                foreach (var service in AppData_.services.ServiceMap.Values)
                {
                    foreach (var logo in service.LogoSettings)
                    {
                        // 全てのロゴは存在しないところからスタート
                        logo.Exists = (logo.FileName == LogoSetting.NO_LOGO);
                    }
                }

                while (true)
                {
                    string logopath = GetLogoDirectoryPath();
                    if (Directory.Exists(logopath))
                    {
                        var map = AppData_.services.ServiceMap;

                        var logoDict = new Dictionary<string, LogoSetting>();
                        foreach (var service in map.Values)
                        {
                            foreach (var logo in service.LogoSettings)
                            {
                                if(logo.FileName != LogoSetting.NO_LOGO)
                                {
                                    logoDict.Add(logo.FileName, logo);
                                }
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
                                // サービスリストが変わってたら再度追加処理
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

                        if (serviceListUpdated)
                        {
                            // サービスリストが変わってたら設定保存
                            serviceListUpdated = false;
                            settingUpdated = true;
                        }

                        if (updatedServices.Count > 0)
                        {
                            // 更新をクライアントに通知
                            foreach (var updatedServiceId in updatedServices.Distinct())
                            {
                                await Client.OnServiceSetting(new ServiceSettingUpdate() {
                                    Type = ServiceSettingUpdateType.Update,
                                    ServiceId = updatedServiceId,
                                    Data = map[updatedServiceId]
                                });
                            }
                            // キューを再始動
                            var waits = new List<Task>();
                            UpdateQueueItems(waits);
                            await Task.WhenAll(waits);
                        }
                    }

                    string jlspath = GetJLDirectoryPath();
                    if (Directory.Exists(jlspath))
                    {
                        var lastModified = Directory.GetLastWriteTime(jlspath);
                        if (jlsDirTime != lastModified)
                        {
                            jlsDirTime = lastModified;

                            JlsCommandFiles = Directory.GetFiles(jlspath)
                                .Select(s => Path.GetFileName(s)).ToList();
                            await Client.OnCommonData(new CommonData()
                            {
                                JlsCommandFiles = JlsCommandFiles
                            });
                        }
                    }

                    string avspath = GetAvsDirectoryPath();
                    if (Directory.Exists(avspath))
                    {
                        var lastModified = Directory.GetLastWriteTime(avspath);
                        if (avsDirTime != lastModified)
                        {
                            avsDirTime = lastModified;

                            var files = Directory.GetFiles(avspath)
                                .Where(f => f.EndsWith(".avs", StringComparison.OrdinalIgnoreCase))
                                .Select(f => Path.GetFileName(f));

                            MainScriptFiles = files
                                .Where(f => f.StartsWith("メイン_")).ToList();

                            PostScriptFiles = files
                                .Where(f => f.StartsWith("ポスト_")).ToList();

                            await Client.OnCommonData(new CommonData()
                            {
                                MainScriptFiles = MainScriptFiles,
                                PostScriptFiles = PostScriptFiles
                            });
                        }
                    }

                    string batpath = GetBatDirectoryPath();
                    if (Directory.Exists(batpath))
                    {
                        var lastModified = Directory.GetLastWriteTime(batpath);
                        if (batDirTime != lastModified)
                        {
                            batDirTime = lastModified;

                            var files = Directory.GetFiles(batpath)
                                .Where(f =>
                                    f.EndsWith(".bat", StringComparison.OrdinalIgnoreCase) ||
                                    f.EndsWith(".cmd", StringComparison.OrdinalIgnoreCase))
                                .Select(f => Path.GetFileName(f));

                            AddQueueBatFiles = files
                                .Where(f => f.StartsWith("追加時_")).ToList();
                            PreBatFiles = files
                                .Where(f => f.StartsWith("実行前_")).ToList();
                            PostBatFiles = files
                                .Where(f => f.StartsWith("実行後_")).ToList();

                            await Client.OnCommonData(new CommonData()
                            {
                                AddQueueBatFiles = AddQueueBatFiles,
                                PreBatFiles = PreBatFiles,
                                PostBatFiles = PostBatFiles
                            });
                        }
                    }

                    string profilepath = GetProfileDirectoryPath();
                    if(!Directory.Exists(profilepath))
                    {
                        Directory.CreateDirectory(profilepath);
                    }
                    {
                        var lastModified = Directory.GetLastWriteTime(profilepath);
                        if (profileDirTime != lastModified)
                        {
                            profileDirTime = lastModified;

                            var newProfiles = Directory.GetFiles(profilepath)
                                .Where(s => s.EndsWith(".profile", StringComparison.OrdinalIgnoreCase))
                                .Select(s => Path.GetFileNameWithoutExtension(s))
                                .ToArray();

                            var initialUpdate = (profiles.Count == 0);

                            foreach (var name in newProfiles.Union(profiles.Keys.ToArray()))
                            {
                                var filepath = GetProfilePath(profilepath, name);
                                if (profiles.ContainsKey(name) == false)
                                {
                                    // 追加された
                                    try
                                    {
                                        var profile = ReadProfile(filepath);
                                        profile.Name = name;
                                        profile.LastUpdate = File.GetLastWriteTime(filepath);
                                        profiles.Add(profile.Name, profile);
                                        await Client.OnProfile(new ProfileUpdate()
                                        {
                                            Type = UpdateType.Add,
                                            Profile = profile
                                        });
                                    }
                                    catch (Exception e)
                                    {
                                        await FatalError("プロファイル「" + filepath + "」の読み込みに失敗", e);
                                    }
                                }
                                else if (newProfiles.Contains(name) == false)
                                {
                                    // 削除された
                                    var profile = profiles[name];
                                    profiles.Remove(name);
                                    await Client.OnProfile(new ProfileUpdate()
                                    {
                                        Type = UpdateType.Remove,
                                        Profile = profile
                                    });
                                }
                                else
                                {
                                    var profile = profiles[name];
                                    var lastUpdate = File.GetLastWriteTime(filepath);
                                    if (profile.LastUpdate != lastUpdate)
                                    {
                                        try
                                        {
                                            // 変更された
                                            profile = ReadProfile(filepath);
                                            profile.Name = name;
                                            profile.LastUpdate = lastUpdate;
                                            await Client.OnProfile(new ProfileUpdate()
                                            {
                                                Type = UpdateType.Update,
                                                Profile = profile
                                            });
                                        }
                                        catch (Exception e)
                                        {
                                            await FatalError("プロファイル「" + filepath + "」の読み込みに失敗", e);
                                        }
                                    }
                                }
                            }
                            if (profiles.ContainsKey(ServerSupport.GetDefaultProfileName()) == false)
                            {
                                // デフォルトがない場合は追加しておく
                                var profile = ServerSupport.GetDefaultProfile();
                                profile.Name = ServerSupport.GetDefaultProfileName();
                                var filepath = GetProfilePath(profilepath, profile.Name);
                                SaveProfile(filepath, profile);
                                profile.LastUpdate = File.GetLastWriteTime(filepath);
                                profiles.Add(profile.Name, profile);
                                await Client.OnProfile(new ProfileUpdate()
                                {
                                    Type = UpdateType.Add,
                                    Profile = profile
                                });
                            }
                            if(initialUpdate)
                            {
                                // 初回の更新時はプロファイル関連付けを
                                // 更新するため再度設定を送る
                                await RequestSetting();
                            }
                        }
                    }

                    {
                        // 自動選択「デフォルト」がない場合は追加
                        if(autoSelects.ContainsKey("デフォルト") == false)
                        {
                            var def = new AutoSelectProfile()
                            {
                                Name = "デフォルト",
                                Conditions = new List<AutoSelectCondition>()
                            };
                            autoSelects.Add(def.Name, def);
                            await Client.OnAutoSelect(new AutoSelectUpdate()
                            {
                                Type = UpdateType.Add,
                                Profile = def
                            });
                            autoSelectUpdated = true;
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
                await FatalError(
                    "WatchFileThreadがエラー終了しました", exception);
            }
        }

        private async Task SaveSettingThread()
        {
            try
            {
                var completion = saveSettingQ.OutputAvailableAsync();

                while (true)
                {
                    if(settingUpdated)
                    {
                        try
                        {
                            SaveAppData();
                        }
                        catch(Exception e)
                        {
                            await FatalError("設定保存に失敗しました", e);
                        }
                        settingUpdated = false;
                    }

                    if(autoSelectUpdated)
                    {
                        try
                        {
                            SaveAutoSelectData();
                        }
                        catch (Exception e)
                        {
                            await FatalError("自動選択設定保存に失敗しました", e);
                        }
                        autoSelectUpdated = false;
                    }

                    try
                    {
                        queueManager.SaveQueueData(false);
                    }
                    catch (Exception e)
                    {
                        await FatalError("キュー状態保存に失敗しました", e);
                    }

                    if (await Task.WhenAny(completion, Task.Delay(5000)) == completion)
                    {
                        // 終了
                        return;
                    }
                }
            }
            catch (Exception exception)
            {
                await FatalError(
                    "SaveSettingThreadがエラー終了しました", exception);
            }
        }

        private async Task DrcsThread()
        {
            try
            {
                var completion = drcsQ.OutputAvailableAsync();

                while (true)
                {
                    await drcsManager.Update();

                    if (await Task.WhenAny(completion, Task.Delay(5000)) == completion)
                    {
                        // 終了
                        return;
                    }
                }
            }
            catch (Exception exception)
            {
                await FatalError(
                    "DrcsThreadがエラー終了しました", exception);
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
            if (string.IsNullOrEmpty(AppData_.setting.AlwaysShowDisk) == false)
            {
                foreach (var path in AppData_.setting.AlwaysShowDisk.Split(';'))
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
                        Util.AddLog("ディスク情報取得失敗: ", e);
                    }
                }
            }
            foreach(var item in queueManager.Queue.
                Where(s => !string.IsNullOrEmpty(s.DstPath)).
                Select(s => Path.GetPathRoot(s.DstPath)))
            {
                if (diskMap.ContainsKey(item) == false)
                {
                    diskMap.Add(item, MakeDiskItem(item));
                }
            }
            if(string.IsNullOrEmpty(AppData_.setting.WorkPath) == false) {
                var diskPath = Path.GetPathRoot(AppData_.setting.WorkPath);
                if (diskMap.ContainsKey(diskPath) == false)
                {
                    diskMap.Add(diskPath, MakeDiskItem(diskPath));
                }
            }
        }

        private static LogoSetting MakeNoLogoSetting(int serviceId)
        {
            return new LogoSetting() {
                FileName = LogoSetting.NO_LOGO,
                LogoName = "ロゴなし",
                ServiceId = serviceId,
                Exists = true,
                Enabled = false,
                From = new DateTime(2000, 1, 1),
                To = new DateTime(2030, 12, 31)
            };
        }

        private void SetDefaultFormat(ProfileSetting profile)
        {
            if (string.IsNullOrWhiteSpace(profile.RenameFormat))
            {
                profile.RenameFormat = "$SCtitle$\\$SCtitle$ $SCpart$第$SCnumber$話 「$SCsubtitle$」 ($SCservice$) [$SCdate$]";
            }
        }

        public Task SetProfile(ProfileUpdate data)
        {
            try
            {
                var waits = new List<Task>();
                var message = "プロファイル「"+ data.Profile.Name + "」が見つかりません";

                // 面倒だからAddもUpdateも同じ
                var profilepath = GetProfileDirectoryPath();
                var filepath = GetProfilePath(profilepath, data.Profile.Name);

                if (data.Type == UpdateType.Add || data.Type == UpdateType.Update)
                {
                    SetDefaultFormat(data.Profile);
                    if (data.Type == UpdateType.Update)
                    {
                        CheckSetting(data.Profile, AppData_.setting);
                    }
                    SaveProfile(filepath, data.Profile);
                    data.Profile.LastUpdate = File.GetLastWriteTime(filepath);
                    if (profiles.ContainsKey(data.Profile.Name))
                    {
                        profiles[data.Profile.Name] = data.Profile;
                        message = "プロファイル「" + data.Profile.Name + "」を更新しました";
                    }
                    else
                    {
                        profiles.Add(data.Profile.Name, data.Profile);
                        message = "プロファイル「" + data.Profile.Name + "」を追加しました";
                    }
                    // キューを再始動
                    UpdateQueueItems(waits);
                }
                else
                {
                    if(profiles.ContainsKey(data.Profile.Name))
                    {
                        File.Delete(filepath);
                        profiles.Remove(data.Profile.Name);
                        message = "プロファイル「" + data.Profile.Name + "」を削除しました";
                    }
                }
                waits.Add(Client.OnProfile(data));
                waits.Add(NotifyMessage(message, false));
                return Task.WhenAll(waits);
            }
            catch (Exception e)
            {
                return NotifyError(e.Message, false);
            }
        }

        public Task SetAutoSelect(AutoSelectUpdate data)
        {
            try
            {
                var waits = new List<Task>();
                var message = "自動選択「" + data.Profile.Name + "」が見つかりません";

                // 面倒だからAddもUpdateも同じ
                if (data.Type == UpdateType.Add || data.Type == UpdateType.Update)
                {
                    if (data.Type == UpdateType.Update)
                    {
                        CheckAutoSelect(data.Profile);
                    }
                    if (autoSelects.ContainsKey(data.Profile.Name))
                    {
                        autoSelects[data.Profile.Name] = data.Profile;
                        message = "自動選択「" + data.Profile.Name + "」を更新しました";
                        autoSelectUpdated = true;
                    }
                    else
                    {
                        autoSelects.Add(data.Profile.Name, data.Profile);
                        message = "自動選択「" + data.Profile.Name + "」を追加しました";
                        autoSelectUpdated = true;
                    }
                    // キューを再始動
                    UpdateQueueItems(waits);
                }
                else
                {
                    if (autoSelects.ContainsKey(data.Profile.Name))
                    {
                        autoSelects.Remove(data.Profile.Name);
                        message = "自動選択「" + data.Profile.Name + "」を削除しました";
                        autoSelectUpdated = true;
                    }
                }
                waits.Add(Client.OnAutoSelect(data));
                waits.Add(NotifyMessage(message, false));
                return Task.WhenAll(waits);
            }
            catch (Exception e)
            {
                return NotifyError(e.Message, false);
            }
        }

        public Task SetCommonData(CommonData data)
        {
            try
            {
                if(data.Setting != null)
                {
                    SetDefaultPath(data.Setting);
                    CheckSetting(null, data.Setting);
                    AppData_.setting = data.Setting;
                    workerPool.SetNumParallel(data.Setting.NumParallel);
                    SetScheduleParam(AppData_.setting.SchedulingEnabled,
                        AppData_.setting.NumGPU, AppData_.setting.MaxGPUResources);
                    settingUpdated = true;
                    return Task.WhenAll(
                        Client.OnCommonData(new CommonData() { Setting = AppData_.setting }),
                        RequestFreeSpace(),
                        NotifyMessage("設定を更新しました", false));
                }
                else if(data.MakeScriptData != null)
                {
                    AppData_.scriptData = data.MakeScriptData;
                    settingUpdated = true;
                    return Client.OnCommonData(new CommonData() {
                        MakeScriptData = data.MakeScriptData
                    });
                }
                return Task.FromResult(0);
            }
            catch(Exception e)
            {
                return NotifyError(e.Message, false);
            }
        }

        public Task AddQueue(AddQueueRequest req)
        {
            queueQ.Post(req);
            return Task.FromResult(0);
        }

        public async Task PauseEncode(bool pause)
        {
            EncodePaused = pause;
            Task task = RequestState();
            workerPool.SetPause(pause);
            await task;
        }

        public Task CancelAddQueue()
        {
            queueManager.CancelAddQueue();
            return Task.FromResult(0);
        }

        // 指定した名前のプロファイルを取得
        internal ProfileSetting GetProfile(string name)
        {
            return profiles.GetOrDefault(name);
        }

        // プロファイルがペンディングとなっているアイテムに対して
        // プロファイルの決定を試みる
        internal ProfileSetting SelectProfile(QueueItem item, out int itemPriority)
        {
            itemPriority = 0;

            bool isAuto = false;
            var profileName = ServerSupport.ParseProfileName(item.ProfileName, out isAuto);

            if (isAuto)
            {
                if (autoSelects.ContainsKey(profileName) == false)
                {
                    item.FailReason = "自動選択「" + profileName + "」がありません";
                    item.State = QueueState.LogoPending;
                    return null;
                }

                var resolvedProfile = ServerSupport.AutoSelectProfile(item, autoSelects[profileName], out itemPriority);
                if (resolvedProfile == null)
                {
                    item.FailReason = "自動選択「" + profileName + "」でプロファイルが選択されませんでした";
                    item.State = QueueState.LogoPending;
                    return null;
                }

                profileName = resolvedProfile;
            }

            if (profiles.ContainsKey(profileName) == false)
            {
                item.FailReason = "プロファイル「" + profileName + "」がありません";
                item.State = QueueState.LogoPending;
                return null;
            }

            return profiles[profileName];
        }

        // 実行できる状態になったアイテムをスケジューラに登録
        internal void ScheduleQueueItem(QueueItem item)
        {
            scheduledQueue.AddQueue(item);

            if (AppData_.setting.SupressSleep)
            {
                // サスペンドを抑止
                if (preventSuspend == null)
                {
                    preventSuspend = new PreventSuspendContext();
                }
            }
        }

        // 指定アイテムをキャンセルする
        internal bool CancelItem(QueueItem item)
        {
            foreach (var worker in workerPool.Workers.Cast<TranscodeWorker>())
            {
                if (worker != null)
                {
                    if(worker.CancelItem(item))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        // プロファイル更新を適用
        internal void UpdateProfile(QueueItem item)
        {
            scheduledQueue.MakeDirty();
        }

        // 指定アイテムの新しい優先度を適用
        internal void UpdatePriority(QueueItem item)
        {
            scheduledQueue.MakeDirty();
        }

        internal void ForceStartItem(QueueItem item)
        {
            workerPool.ForceStart(item);
        }

        // 新しいサービスを登録
        internal Task AddService(ServiceSettingElement newElement)
        {
            AppData_.services.ServiceMap.Add(newElement.ServiceId, newElement);
            serviceListUpdated = true;
            return Client.OnServiceSetting(new ServiceSettingUpdate()
            {
                Type = ServiceSettingUpdateType.Update,
                ServiceId = newElement.ServiceId,
                Data = newElement
            });
        }

        #region QueueManager
        // アイテム状態の更新をクライアントに通知
        internal Task NotifyQueueItemUpdate(QueueItem item)
        {
            return queueManager.NotifyQueueItemUpdate(item);
        }

        // ペンディング <=> キュー 状態を切り替える
        // ペンディングからキューになったらスケジューリングに追加する
        // notifyItem: trueの場合は、ディレクトリ・アイテム両方の更新通知、falseの場合は、ディレクトリの更新通知のみ
        // 戻り値: 状態が変わった
        internal bool UpdateQueueItem(QueueItem item, List<Task> waits)
        {
            return queueManager.UpdateQueueItem(item, waits);
        }

        internal List<Task> UpdateQueueItems(List<Task> waits)
        {
            return queueManager.UpdateQueueItems(waits);
        }

        internal QueueItem[] GetQueueItems(string srcPath)
        {
            return queueManager.Queue.Where(s => s.SrcPath == srcPath).ToArray();
        }

        public Task ChangeItem(ChangeItemData data)
        {
            return queueManager.ChangeItem(data);
        }
        #endregion

        #region Request
        private async Task RequestSetting()
        {
            await Client.OnCommonData(new CommonData() {
                Setting = AppData_.setting,
                UIState = AppData_.uiState,
                JlsCommandFiles = JlsCommandFiles,
                MainScriptFiles = MainScriptFiles,
                PostScriptFiles = PostScriptFiles,
                CpuClusters = affinityCreator.GetClusters(),
                ServerInfo = new ServerInfo()
                {
                    HostName = Dns.GetHostName(),
                    MacAddress = ClientManager?.GetMacAddress()
                }
            });

            // プロファイル
            await Client.OnProfile(new ProfileUpdate()
            {
                Type = UpdateType.Clear
            });
            foreach (var profile in profiles.Values.ToArray())
            {
                await Client.OnProfile(new ProfileUpdate() {
                    Profile = profile,
                    Type = UpdateType.Update
                });
            }

            // 自動選択
            await Client.OnAutoSelect(new AutoSelectUpdate()
            {
                Type = UpdateType.Clear
            });
            foreach (var profile in autoSelects.Values.ToArray())
            {
                await Client.OnAutoSelect(new AutoSelectUpdate()
                {
                    Profile = profile,
                    Type = UpdateType.Update
                });
            }

            // プロファイルがないと関連付けできないため、
            // プロファイルを送った後にこれを送る
            await Client.OnCommonData(new CommonData()
            {
                MakeScriptData = AppData_.scriptData,
            });
        }

        private Task RequestQueue()
        {
            return Client.OnUIData(new UIData()
            {
                QueueData = new QueueData()
                {
                    Items = queueManager.Queue
                }
            });
        }

        private Task RequestLog()
        {
            return Client.OnUIData(new UIData()
            {
                LogData = logData
            });
        }

        private Task RequestCheckLog()
        {
            return Client.OnUIData(new UIData()
            {
                CheckLogData = checkLogData
            });
        }

        private Task RequestConsole()
        {
            return Task.WhenAll(workerPool.Workers.Cast<TranscodeWorker>().Select(w =>
                Client.OnUIData(new UIData()
                {
                    ConsoleData = new ConsoleData()
                    {
                        index = w.Id,
                        text = w.TextLines
                    },
                    EncodeState = w.State
                })).Concat(new Task[] { Client.OnUIData(new UIData()
                {
                    ConsoleData = new ConsoleData()
                    {
                        index = -1,
                        text = queueManager.TextLines
                    }
                }) }));
        }

        internal Task RequestUIState()
        {
            return Client.OnCommonData(new CommonData()
            {
                UIState = AppData_.uiState
            });
        }

        internal Task RequestState()
        {
            var state = new State()
            {
                Pause = encodePaused,
                Running = nowEncoding,
                Progress = Progress
            };
            return Client.OnCommonData(new CommonData()
            {
                State = state
            });
        }

        internal Task RequestFreeSpace()
        {
            RefrechDiskSpace();
            return Client.OnCommonData(new CommonData()
            {
                Disks = diskMap.Values.ToList()
            });
        }

        private async Task RequestServiceSetting()
        {
            var serviceMap = AppData_.services.ServiceMap;
            await Client.OnServiceSetting(new ServiceSettingUpdate()
            {
                Type = ServiceSettingUpdateType.Clear
            });
            foreach (var service in serviceMap.Values.ToArray())
            {
                await Client.OnServiceSetting(new ServiceSettingUpdate()
                {
                    Type = ServiceSettingUpdateType.Update,
                    ServiceId = service.ServiceId,
                    Data = service
                });
            }
        }

        public async Task Request(ServerRequest req)
        {
            if ((req & ServerRequest.Setting) != 0)
            {
                await RequestSetting();
            }
            if ((req & ServerRequest.Queue) != 0)
            {
                await RequestQueue();
            }
            if ((req & ServerRequest.Log) != 0)
            {
                await RequestLog();
            }
            if ((req & ServerRequest.CheckLog) != 0)
            {
                await RequestCheckLog();
            }
            if ((req & ServerRequest.Console) != 0)
            {
                await RequestConsole();
            }
            if ((req & ServerRequest.State) != 0)
            {
                await RequestState();
            }
            if ((req & ServerRequest.FreeSpace) != 0)
            {
                await RequestFreeSpace();
            }
            if ((req & ServerRequest.ServiceSetting) != 0)
            {
                await RequestServiceSetting();
            }
        }
        #endregion

        public Task RequestLogFile(LogFileRequest req)
        {
            if (req.LogItem != null)
            {
                return Client.OnLogFile(ReadLogFIle(req.LogItem.EncodeStartDate));
            }
            else if (req.CheckLogItem != null)
            {
                return Client.OnLogFile(ReadCheckLogFIle(req.CheckLogItem.CheckStartDate));
            }
            return Task.FromResult(0);
        }

        public Task RequestDrcsImages()
        {
            return drcsManager.RequestDrcsImages();
        }

        public async Task SetServiceSetting(ServiceSettingUpdate update)
        {
            var serviceMap = AppData_.services.ServiceMap;
            var message = "サービスが見つかりません";
            if(serviceMap.ContainsKey(update.ServiceId))
            {
                if (update.Type == ServiceSettingUpdateType.Update)
                {
                    var old = serviceMap[update.ServiceId];
                    if (old.LogoSettings.Count == update.Data.LogoSettings.Count)
                    {
                        // ロゴのExitsフラグだけはこちらのデータを継承させる
                        for (int i = 0; i < old.LogoSettings.Count; ++i)
                        {
                            update.Data.LogoSettings[i].Exists = old.LogoSettings[i].Exists;
                        }
                        serviceMap[update.ServiceId] = update.Data;

                        var waits = new List<Task>();
                        UpdateQueueItems(waits);
                        await Task.WhenAll(waits);
                        message = "サービス「" + update.Data.ServiceName + "」の設定を更新しました";
                    }
                }
                else if (update.Type == ServiceSettingUpdateType.AddNoLogo)
                {
                    var service = serviceMap[update.ServiceId];
                    service.LogoSettings.Add(MakeNoLogoSetting(update.ServiceId));
                    update.Type = ServiceSettingUpdateType.Update;
                    update.Data = service;
                    message = "サービス「" + service.ServiceName + "」にロゴなしを追加しました";
                }
                else if (update.Type == ServiceSettingUpdateType.Remove)
                {
                    var service = serviceMap[update.ServiceId];
                    serviceMap.Remove(update.ServiceId);
                    update.Data = null;
                    message = "サービス「" + service.ServiceName + "」を削除しました";
                }
                else if (update.Type == ServiceSettingUpdateType.RemoveLogo)
                {
                    var service = serviceMap[update.ServiceId];
                    service.LogoSettings.RemoveAt(update.RemoveLogoIndex);
                    update.Type = ServiceSettingUpdateType.Update;
                    update.Data = service;
                    message = "サービス「" + service.ServiceName + "」のロゴを削除しました";
                }
                settingUpdated = true;
            }
            await Client.OnServiceSetting(update);
        }

        private AMTContext amtcontext = new AMTContext();
        public Task RequestLogoData(string fileName)
        {
            if(fileName == LogoSetting.NO_LOGO)
            {
                return NotifyError("不正な操作です", false);
            }
            string logopath = GetLogoFilePath(fileName);
            try
            {
                var logofile = new LogoFile(amtcontext, logopath);
                return Client.OnLogoData(new LogoData() {
                    FileName = fileName,
                    ServiceId = logofile.ServiceId,
                    ImageWith = logofile.ImageWidth,
                    ImageHeight = logofile.ImageHeight,
                    Image = logofile.GetImage(0)
                });
            }
            catch(IOException exception)
            {
                return FatalError(
                    "ロゴファイルを開けません。パス:" + logopath, exception);
            }
        }

        public Task AddDrcsMap(DrcsImage recvitem)
        {
            return drcsManager.AddDrcsMap(recvitem);
        }

        public Task EndServer()
        {
            finishRequested?.Invoke();
            return Task.FromResult(0);
        }
    }
}
