using Livet;
using Amatsukaze.Lib;
using Codeplex.Data;
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
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;
using System.Threading.Tasks.Dataflow;
using System.Windows.Media.Imaging;

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

        private static Func<char, bool> IsHex = c =>
                 (c >= '0' && c <= '9') ||
                 (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F');

        internal IUserClient Client { get; private set; }
        public Task ServerTask { get; private set; }
        internal AppData AppData_ { get; private set; }

        private Action finishRequested;

        private EncodeScheduler<QueueItem> scheduler = null;

        private List<QueueDirectory> queue = new List<QueueDirectory>();
        private int nextDirId = 1;
        private int nextItemId = 1;
        internal LogData Log { get; private set; }
        private SortedDictionary<string, DiskItem> diskMap = new SortedDictionary<string, DiskItem>();

        internal readonly AffinityCreator affinityCreator = new AffinityCreator();

        private Dictionary<string, ProfileSetting> profiles = new Dictionary<string, ProfileSetting>();
        private Dictionary<string, AutoSelectProfile> autoSelects = new Dictionary<string, AutoSelectProfile>();
        private List<string> JlsCommandFiles = new List<string>();
        private List<string> MainScriptFiles = new List<string>();
        private List<string> PostScriptFiles = new List<string>();
        private Dictionary<string, BitmapFrame> drcsImageCache = new Dictionary<string, BitmapFrame>();
        private Dictionary<string, DrcsImage> drcsMap = new Dictionary<string, DrcsImage>();

        // キューに追加されるTSを解析するスレッド
        private Task queueThread;
        private BufferBlock<AddQueueDirectory> queueQ = new BufferBlock<AddQueueDirectory>();

        // ロゴファイルやJLSコマンドファイルを監視するスレッド
        private Task watchFileThread;
        private BufferBlock<int> watchFileQ = new BufferBlock<int>();
        private bool serviceListUpdated;

        // 設定を保存するスレッド
        private Task saveSettingThread;
        private BufferBlock<int> saveSettingQ = new BufferBlock<int>();
        private bool settingUpdated;
        private bool autoSelectUpdated;

        private PreventSuspendContext preventSuspend;

        // プロファイル未選択状態のダミープロファイル
        private ProfileSetting pendingProfile;

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

        public EncodeServer(int port, IUserClient client, Action finishRequested)
        {
            this.finishRequested = finishRequested;
            LoadAppData();
            LoadAutoSelectData();
            if (client != null)
            {
                this.Client = client;
            }
            else
            {
                var clientManager = new ClientManager(this);
                ServerTask = clientManager.Listen(port);
                this.Client = clientManager;
                RaisePropertyChanged("ClientManager");
            }
            ReadLog();

            pendingProfile = new ProfileSetting()
            {
                Name = "プロファイル未選択",
                LastUpdate = DateTime.MinValue,
            };

            scheduler = new EncodeScheduler<QueueItem>() {
                NewWorker = id => new TranscodeWorker() {
                    id = id,
                    server = this,
                    logText = new ConsoleText(1 * 1024 * 1024),
                    consoleText = new ConsoleText(500),
                },
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
                OnError = message => NotifyMessage(true, message, true)
            };
            scheduler.SetNumParallel(AppData_.setting.NumParallel);
            affinityCreator.NumProcess = AppData_.setting.NumParallel;

            // 古いバージョンからの更新処理
            UpdateFromOldVersion();

            queueThread = QueueThread();
            watchFileThread = WatchFileThread();
            saveSettingThread = SaveSettingThread();
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
                    if (scheduler != null)
                    {
                        foreach (var worker in scheduler.Workers.Cast<TranscodeWorker>())
                        {
                            if (worker != null)
                            {
                                worker.KillProcess();
                            }
                        }
                        scheduler.Finish();
                    }

                    queueQ.Complete();
                    watchFileQ.Complete();
                    saveSettingQ.Complete();

                    if (settingUpdated)
                    {
                        settingUpdated = false;
                        SaveAppData();
                    }

                    if(autoSelectUpdated)
                    {
                        autoSelectUpdated = false;
                        SaveAutoSelectData();
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

        private string GetHistoryFilePath()
        {
            return "data\\EncodeHistory.xml";
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

        private string GetAvsDirectoryPath()
        {
            return Path.GetFullPath("avs");
        }

        private string GetDRCSDirectoryPath()
        {
            return Path.GetFullPath("drcs");
        }

        private string GetDRCSImagePath(string md5)
        {
            return GetDRCSImagePath(GetDRCSDirectoryPath(), md5);
        }

        private string GetDRCSImagePath(string md5, string dirPath)
        {
            return dirPath + "\\" + md5 + ".bmp";
        }

        private string GetDRCSMapPath()
        {
            return GetDRCSMapPath(GetDRCSDirectoryPath());
        }

        private string GetDRCSMapPath(string dirPath)
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
            if (Client != null)
            {
                Client.Finish();
                Client = null;
            }
        }

        private void UpdateFromOldVersion()
        {
            // 古いバージョンからのアップデート処理
            if(AppData_.Version == 0)
            {
                // DRCS文字の並びを変更する
                var dirPath = GetDRCSDirectoryPath();
                var oldDirPath = dirPath + ".old";
                if(Directory.Exists(dirPath) && !Directory.Exists(oldDirPath))
                {
                    // drcs -> drcs.old にディレクトリ名変更
                    Directory.Move(dirPath, oldDirPath);

                    // drcsディレクトリを改めて作る
                    Directory.CreateDirectory(dirPath);

                    Func<string, string> RevertHash = s =>
                    {
                        var sb = new StringBuilder();
                        for (int i = 0; i < 32; i += 2)
                        {
                            sb.Append(s[i + 1]).Append(s[i]);
                        }
                        return sb.ToString();
                    };

                    // drcs_map.txtを変換
                    using (var sw = new StreamWriter(File.OpenWrite(GetDRCSMapPath()), Encoding.UTF8))
                    {
                        foreach (var line in File.ReadAllLines(GetDRCSMapPath(oldDirPath)))
                        {
                            if (line.Length >= 34 && line.IndexOf('=') == 32)
                            {
                                string md5 = line.Substring(0, 32);
                                string mapStr = line.Substring(33);
                                if (md5.All(IsHex))
                                {
                                    sw.WriteLine(RevertHash(md5) + "=" + mapStr);
                                }
                            }
                        }
                    }

                    // 文字画像ファイルを変換
                    foreach (var imgpath in Directory.GetFiles(oldDirPath))
                    {
                        var filename = Path.GetFileName(imgpath);
                        if (filename.Length == 36 && Path.GetExtension(filename).ToLower() == ".bmp")
                        {
                            string md5 = filename.Substring(0, 32);
                            File.Copy(imgpath, dirPath + "\\" + RevertHash(md5) + ".bmp");
                        }
                    }

                }

                settingUpdated = true;
            }

            // 現在バージョンに更新
            AppData_.Version = 1;
        }

        private Task NotifyMessage(bool fail, string message, bool log)
        {
            if(log)
            {
                Util.AddLog(message);
            }
            return Client.OnOperationResult(new OperationResult()
            {
                IsFailed = fail,
                Message = message
            });
        }

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
                AppData_ = new AppData() {
                    setting = GetDefaultSetting(),
                    scriptData = new MakeScriptData(),
                    services = new ServiceSetting() {
                        ServiceMap = new Dictionary<int, ServiceSettingElement>()
                    }
                };
                return;
            }
            using (FileStream fs = new FileStream(path, FileMode.Open))
            {
                var s = new DataContractSerializer(typeof(AppData));
                AppData_ = (AppData)s.ReadObject(fs);
                if (AppData_.setting == null)
                {
                    AppData_.setting = GetDefaultSetting();
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
                var data = (AutoSelectData)s.ReadObject(fs);
                autoSelects.Clear();
                foreach(var profile in data.Profiles)
                {
                    foreach(var cond in profile.Conditions)
                    {
                        if(cond.ContentConditions == null)
                        {
                            cond.ContentConditions = new List<GenreItem>();
                        }
                        if(cond.ServiceIds == null)
                        {
                            cond.ServiceIds = new List<int>();
                        }
                        if(cond.VideoSizes == null)
                        {
                            cond.VideoSizes = new List<VideoSizeCondition>();
                        }
                    }
                    autoSelects.Add(profile.Name, profile);
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

        private void ReadLog()
        {
            string path = GetHistoryFilePath();
            if (File.Exists(path) == false)
            {
                Log = new LogData() {
                    Items = new List<LogItem>()
                };
                return;
            }
            try
            {
                using (FileStream fs = new FileStream(path, FileMode.Open))
                {
                    var s = new DataContractSerializer(typeof(LogData));
                    Log = (LogData)s.ReadObject(fs);
                    if (Log.Items == null)
                    {
                        Log.Items = new List<LogItem>();
                    }
                }
            }
            catch (IOException e)
            {
                Util.AddLog("ログファイルの読み込みに失敗: " + e.Message);
            }
        }

        internal void WriteLog()
        {
            string path = GetHistoryFilePath();
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                using (FileStream fs = new FileStream(path, FileMode.Create))
                {
                    var s = new DataContractSerializer(typeof(LogData));
                    s.WriteObject(fs, Log);
                }
            }
            catch (IOException e)
            {
                Util.AddLog("ログファイル書き込み失敗: " + e.Message);
            }
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

        internal string MakeAmatsukazeSearchArgs(string src, int serviceId)
        {
            StringBuilder sb = new StringBuilder();
            sb.Append("--mode drcs")
                .Append(" --subtitles")
                .Append(" -i \"")
                .Append(src)
                .Append("\" -s ")
                .Append(serviceId)
                .Append(" --drcs \"")
                .Append(GetDRCSMapPath())
                .Append("\"");

            return sb.ToString();
        }

        internal string MakeAmatsukazeArgs(
            ProfileSetting profile,
            Setting setting,
            bool isGeneric,
            string src, string dst, string json,
            int serviceId, string[] logofiles,
            bool ignoreNoLogo, string jlscommand, string jlsopt)
        {
            string encoderPath = GetEncoderPath(profile.EncoderType, setting);

            double bitrateCM = profile.BitrateCM;
            if (bitrateCM == 0)
            {
                bitrateCM = 1;
            }

            int outputMask = profile.OutputMask;
            if(outputMask == 0)
            {
                outputMask = 1;
            }

            StringBuilder sb = new StringBuilder();
            if (isGeneric)
            {
                sb.Append("--mode g ");
            }
            sb.Append("-i \"")
                .Append(src)
                .Append("\" -o \"")
                .Append(dst)
                .Append("\" -w \"")
                .Append(setting.WorkPath)
                .Append("\" -et ")
                .Append(GetEncoderName(profile.EncoderType))
                .Append(" -e \"")
                .Append(encoderPath)
                .Append("\" -j \"")
                .Append(json)
                .Append("\" --chapter-exe \"")
                .Append(setting.ChapterExePath)
                .Append("\" --jls \"")
                .Append(setting.JoinLogoScpPath)
                .Append("\" -s ")
                .Append(serviceId)
                .Append(" --cmoutmask ")
                .Append(outputMask)
                .Append(" --drcs \"")
                .Append(GetDRCSMapPath())
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
            else if(profile.OutputFormat == FormatType.MKV)
            {
                sb.Append(" -fmt mkv -m \"" + setting.MKVMergePath + "\"");
            }
            else
            {
                sb.Append(" -fmt m2ts -m \"" + setting.TsMuxeRPath + "\"");
            }

            if (bitrateCM != 1)
            {
                sb.Append(" -bcm ").Append(bitrateCM);
            }
            if(profile.SplitSub)
            {
                sb.Append(" --splitsub");
            }
            if (!profile.DisableChapter)
            {
                sb.Append(" --chapter");
            }
            if (!profile.DisableSubs)
            {
                sb.Append(" --subtitles");
            }
            if (profile.EnableNicoJK)
            {
                sb.Append(" --nicojk");
                if(profile.IgnoreNicoJKError)
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

            if (profile.TwoPass)
            {
                sb.Append(" --2pass");
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

            return sb.ToString();
        }

        private void UpdateProgress()
        {
            // 進捗を更新
            var items = queue.SelectMany(t => t.Items);
            double enabledCount = items.Count(s =>
                s.State != QueueState.LogoPending && s.State != QueueState.PreFailed);
            double remainCount = items.Count(s =>
                s.State == QueueState.Queue || s.State == QueueState.Encoding);
            // 完全にゼロだと分からないので・・・
            Progress = ((enabledCount - remainCount) + 0.1) / (enabledCount + 0.1);
        }

        internal Task NotifyQueueItemUpdate(QueueItem item)
        {
            UpdateProgress();
            if(item.Dir.Items.Contains(item))
            {
                // ないアイテムをUpdateすると追加されてしまうので
                return Task.WhenAll(
                    Client.OnQueueUpdate(new QueueUpdate()
                    {
                        Type = UpdateType.Update,
                        DirId = item.Dir.Id,
                        Item = item
                    }),
                    RequestState());
            }
            return Task.FromResult(0);
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
            string workPath = setting.ActualWorkPath;
            if (!File.Exists(setting.AmatsukazePath))
            {
                throw new InvalidOperationException(
                    "AmtasukazeCLIパスが無効です: " + setting.AmatsukazePath);
            }
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

        private QueueDirectory GetQueueDirectory(string  dirPath, ProcMode mode, ProfileSetting profile, List<Task> waitItems)
        {
            QueueDirectory target = queue.FirstOrDefault(s =>
                    s.DirPath == dirPath &&
                    s.Mode == mode &&
                    s.Profile.Name == profile.Name &&
                    s.Profile.LastUpdate == profile.LastUpdate);

            if (target == null)
            {
                var profilei = (profile == pendingProfile) ? profile : ServerSupport.DeepCopy(profile);
                target = new QueueDirectory()
                {
                    Id = nextDirId++,
                    DirPath = dirPath,
                    Items = new List<QueueItem>(),
                    Mode = mode,
                    Profile = profilei
                };

                // ハッシュリスト取得
                if (profile != pendingProfile && // ペンディングの場合は決定したときに実行される
                    mode == ProcMode.Batch &&
                    profile.DisableHashCheck == false &&
                    dirPath.StartsWith("\\\\"))
                {
                    var hashpath = dirPath + ".hash";
                    if (File.Exists(hashpath) == false)
                    {
                        waitItems.Add(NotifyMessage(true, "ハッシュファイルがありません: " + hashpath + "\r\n" +
                            "必要ない場合はハッシュチェックを無効化して再度追加してください", false));
                        return null;
                    }
                    try
                    {
                        target.HashList = HashUtil.ReadHashFile(hashpath);
                    }
                    catch (IOException e)
                    {
                        waitItems.Add(NotifyMessage(true, "ハッシュファイルの読み込みに失敗: " + e.Message, false));
                        return null;
                    }
                }

                queue.Add(target);
                waitItems.Add(Client.OnQueueUpdate(new QueueUpdate()
                {
                    Type = UpdateType.Add,
                    Directory = target
                }));
            }

            return target;
        }

        private async Task InternalAddQueue(AddQueueDirectory dir)
        {
            List<Task> waits = new List<Task>();

            // ユーザ操作でない場合はログを記録する
            bool enableLog = (dir.Mode == ProcMode.AutoBatch);

            if(dir.Outputs.Count == 0)
            {
                await NotifyMessage(true, "出力が1つもありません", enableLog);
                return;
            }

            // 既に追加されているファイルは除外する
            var ignores = queue
                .Where(t => t.DirPath == dir.DirPath);

            // バッチのときは全ファイルが対象だが、バッチじゃなければバッチのみが対象
            if (!dir.IsBatch)
            {
                ignores = ignores.Where(t => t.IsBatch);
            }

            var ignoreSet = new HashSet<string>(
                ignores.SelectMany(t => t.Items)
                .Where(item => item.IsActive)
                .Select(item => item.SrcPath));

            var items = ((dir.Targets != null)
                ? dir.Targets
                : Directory.GetFiles(dir.DirPath)
                    .Where(s =>
                    {
                        string lower = s.ToLower();
                        return lower.EndsWith(".ts") || lower.EndsWith(".m2t");
                    })
                    .Select(f => new AddQueueItem() { Path = f }))
                    .Where(f => !ignoreSet.Contains(f.Path));

            var map = AppData_.services.ServiceMap;
            var numItems = 0;

            // TSファイル情報を読む
            foreach (var additem in items)
            {
                using (var info = new TsInfo(amtcontext))
                {
                    var fileOK = false;
                    var failReason = "";
                    if (await Task.Run(() => info.ReadFile(additem.Path)) == false)
                    {
                        failReason = "TS情報取得に失敗: " + amtcontext.GetError();
                    }
                    // 1ソースファイルに対するaddはatomicに実行したいので、
                    // このスコープでは以降awaitしないこと
                    else
                    {
                        failReason = "TSファイルに映像が見つかりませんでした";
                        var list = info.GetProgramList();
                        var videopids = new List<int>();
                        int numFiles = 0;
                        for (int i = 0; i < list.Length; ++i)
                        {
                            var prog = list[i];
                            if (prog.HasVideo &&
                                videopids.Contains(prog.VideoPid) == false)
                            {
                                videopids.Add(prog.VideoPid);

                                var serviceName = "不明";
                                var tsTime = DateTime.MinValue;
                                if (info.HasServiceInfo)
                                {
                                    var service = info.GetServiceList().Where(s => s.ServiceId == prog.ServiceId).FirstOrDefault();
                                    if (service.ServiceId != 0)
                                    {
                                        serviceName = service.ServiceName;
                                    }
                                    tsTime = info.GetTime();
                                }

                                var outname = Path.GetFileName(additem.Path);
                                if (numFiles > 0)
                                {
                                    outname = Path.GetFileNameWithoutExtension(outname) + "-マルチ" + numFiles;
                                }

                                Debug.Print("解析完了: " + additem.Path);

                                foreach (var outitem in dir.Outputs)
                                {
                                    var genre = prog.Content.Select(s => ServerSupport.GetGenre(s)).ToList();
                                    var profile = GetProfile(prog.Width, prog.Height, genre, prog.ServiceId, outitem.Profile);
                                    var target = GetQueueDirectory(dir.DirPath, dir.Mode, profile?.Profile ?? pendingProfile, waits);
                                    var priority = (profile != null && profile.Priority > 0) ? profile.Priority : outitem.Priority;

                                    var item = new QueueItem()
                                    {
                                        Id = nextItemId++,
                                        SrcPath = additem.Path,
                                        Hash = additem.Hash,
                                        DstPath = outitem.DstPath + "\\" + outname,
                                        ServiceId = prog.ServiceId,
                                        ImageWidth = prog.Width,
                                        ImageHeight = prog.Height,
                                        TsTime = tsTime,
                                        ServiceName = serviceName,
                                        EventName = prog.EventName,
                                        State = QueueState.LogoPending,
                                        Priority = priority,
                                        AddTime = DateTime.Now,
                                        ProfileName = outitem.Profile,
                                        Dir = target,
                                        Genre = genre
                                    };

                                    if (item.IsOneSeg)
                                    {
                                        item.State = QueueState.PreFailed;
                                        item.FailReason = "映像が小さすぎます(" + prog.Width + "," + prog.Height + ")";
                                    }
                                    else
                                    {
                                        // ロゴファイルを探す
                                        if (dir.Mode != ProcMode.DrcsSearch && map.ContainsKey(item.ServiceId) == false)
                                        {
                                            // 新しいサービスを登録
                                            var newElement = new ServiceSettingElement()
                                            {
                                                ServiceId = item.ServiceId,
                                                ServiceName = item.ServiceName,
                                                LogoSettings = new List<LogoSetting>()
                                            };
                                            map.Add(item.ServiceId, newElement);
                                            serviceListUpdated = true;
                                            waits.Add(Client.OnServiceSetting(new ServiceSettingUpdate()
                                            {
                                                Type = ServiceSettingUpdateType.Update,
                                                ServiceId = newElement.ServiceId,
                                                Data = newElement
                                            }));
                                        }
                                        ++numFiles;
                                    }

                                    // 追加
                                    target.Items.Add(item);
                                    // まずは内部だけで状態を更新
                                    UpdateQueueItem(item, waits, false);
                                    // 状態が決まったらクライアント側に追加通知
                                    waits.Add(Client.OnQueueUpdate(new QueueUpdate()
                                    {
                                        Type = UpdateType.Add,
                                        DirId = item.Dir.Id,
                                        Item = item
                                    }));
                                    ++numItems;
                                    fileOK = true;
                                }
                            }
                        }

                    }

                    if (fileOK == false)
                    {
                        foreach (var outitem in dir.Outputs)
                        {
                            bool isAuto = false;
                            var profileName = ServerSupport.ParseProfileName(outitem.Profile, out isAuto);
                            var profile = isAuto ? null : profiles.GetOrDefault(profileName);
                            var target = GetQueueDirectory(dir.DirPath, dir.Mode, profile ?? pendingProfile, waits);
                            var item = new QueueItem()
                            {
                                Id = nextItemId++,
                                SrcPath = additem.Path,
                                Hash = additem.Hash,
                                DstPath = "",
                                ServiceId = -1,
                                ImageWidth = -1,
                                ImageHeight = -1,
                                TsTime = DateTime.MinValue,
                                ServiceName = "不明",
                                State = QueueState.PreFailed,
                                FailReason = failReason,
                                AddTime = DateTime.Now,
                                ProfileName = outitem.Profile,
                                Dir = target
                            };

                            target.Items.Add(item);
                            waits.Add(Client.OnQueueUpdate(new QueueUpdate()
                            {
                                Type = UpdateType.Add,
                                DirId = target.Id,
                                Item = item
                            }));
                            ++numItems;
                        }
                    }

                    UpdateProgress();
                    waits.Add(RequestState());
                }
            }

            if (numItems == 0)
            {
                waits.Add(NotifyMessage(true,
                    "エンコード対象ファイルがありませんでした。パス:" + dir.DirPath, enableLog));

                await Task.WhenAll(waits);

                return;
            }
            else
            {
                waits.Add(NotifyMessage(false, "" + numItems + "件追加しました", false));
            }

            if(dir.Mode != ProcMode.AutoBatch) {
                // 最後に使ったプロファイルを記憶しておく
                bool isAuto = false;
                var profileName = ServerSupport.ParseProfileName(dir.Outputs[0].Profile, out isAuto);
                if(!isAuto)
                {
                    if (AppData_.setting.LastSelectedProfile != profileName)
                    {
                        AppData_.setting.LastSelectedProfile = profileName;
                        settingUpdated = true;
                    }
                }
            }

            waits.Add(RequestFreeSpace());

            await Task.WhenAll(waits);
        }

        private async Task QueueThread()
        {
            try
            {
                while (await queueQ.OutputAvailableAsync())
                {
                    AddQueueDirectory dir = await queueQ.ReceiveAsync();
                    await InternalAddQueue(dir);
                    await Client.OnAddResult(dir.RequestId);
                }
            }
            catch (Exception exception)
            {
                await NotifyMessage(true, "QueueThreadがエラー終了しました: " + exception.Message, true);
            }
        }

        private ProfileTuple GetProfile(int width, int height,
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
                var resolvedProfile = ServerSupport.AutoSelectProfile(width, height,
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

        private class ProfileTuple
        {
            public ProfileSetting Profile;
            public int Priority;
        }

        private ProfileTuple GetProfile(QueueItem item, string profileName)
        {
            return GetProfile(item.ImageWidth, item.ImageHeight,
                item.Genre, item.ServiceId, profileName);
        }

        private void MoveItemDirectory(QueueItem item, QueueDirectory newDir, List<Task> waits)
        {
            item.Dir.Items.Remove(item);

            // RemoveとAddをatomicに行わなければならないのでここをawaitにしないこと
            // ?.の後ろの引数はnullの場合は評価されないことに注意
            // （C#の仕様が分かりにくいけど・・・）
            waits?.Add(Client.OnQueueUpdate(new QueueUpdate()
            {
                Type = UpdateType.Remove,
                DirId = item.Dir.Id,
                Item = item
            }));

            if (item.Dir.Profile == pendingProfile && item.Dir.Items.Count == 0)
            {
                // プロファイル未選択ディレクトリは自動的に削除する
                queue.Remove(item.Dir);
                waits?.Add(Client.OnQueueUpdate(new QueueUpdate()
                {
                    Type = UpdateType.Remove,
                    DirId = item.Dir.Id,
                }));
            }

            item.Dir = newDir;
            item.Dir.Items.Add(item);
            waits?.Add(Client.OnQueueUpdate(new QueueUpdate()
            {
                Type = UpdateType.Add,
                DirId = item.Dir.Id,
                Item = item
            }));
        }

        private bool CheckProfile(QueueItem item, QueueDirectory dir, List<Task> waits, bool notifyItem)
        {
            if (dir.Profile != pendingProfile)
            {
                return true;
            }

            bool isAuto = false;
            int itemPriority = 0;
            var profileName = ServerSupport.ParseProfileName(item.ProfileName, out isAuto);

            if(isAuto)
            {
                if (autoSelects.ContainsKey(profileName) == false)
                {
                    item.FailReason = "自動選択「" + profileName + "」がありません";
                    item.State = QueueState.LogoPending;
                    return false;
                }

                var resolvedProfile = ServerSupport.AutoSelectProfile(item, autoSelects[profileName], out itemPriority);
                if (resolvedProfile == null)
                {
                    item.FailReason = "自動選択「" + profileName + "」でプロファイルが選択されませんでした";
                    item.State = QueueState.LogoPending;
                    return false;
                }

                profileName = resolvedProfile;
            }

            if (profiles.ContainsKey(profileName) == false)
            {
                item.FailReason = "プロファイル「" + profileName + "」がありません";
                item.State = QueueState.LogoPending;
                return false;
            }

            var profile = profiles[profileName];

            // プロファイルの選択ができたので、アイテムを適切なディレクトリに移動
            var newDir = GetQueueDirectory(dir.DirPath, dir.Mode, profile, waits);
            MoveItemDirectory(item, newDir, notifyItem ? waits : null);

            if(itemPriority > 0)
            {
                item.Priority = itemPriority;
            }

            return true;
        }

        // ペンディング <=> キュー 状態を切り替える
        // ペンディングからキューになったらスケジューリングに追加する
        // notifyItem: trueの場合は、ディレクトリ・アイテム両方の更新通知、falseの場合は、ディレクトリの更新通知のみ
        // 戻り値: 状態が変わった
        internal bool UpdateQueueItem(QueueItem item, List<Task> waits, bool notifyItem)
        {
            var dir = item.Dir;
            if(item.State == QueueState.LogoPending || item.State == QueueState.Queue)
            {
                var prevState = item.State;
                if (dir.Mode == ProcMode.DrcsSearch && item.State == QueueState.LogoPending)
                {
                    item.FailReason = "";
                    item.State = QueueState.Queue;
                    scheduler.QueueItem(item, item.ActualPriority);
                }
                else if (CheckProfile(item, dir, waits, notifyItem))
                {
                    var map = AppData_.services.ServiceMap;
                    if (item.ServiceId == -1)
                    {
                        item.FailReason = "TS情報取得中";
                        item.State = QueueState.LogoPending;
                    }
                    else if (map.ContainsKey(item.ServiceId) == false)
                    {
                        item.FailReason = "このTSに対する設定がありません";
                        item.State = QueueState.LogoPending;
                    }
                    else if (dir.Profile.DisableChapter == false &&
                        map[item.ServiceId].LogoSettings.Any(s => s.CanUse(item.TsTime)) == false)
                    {
                        item.FailReason = "ロゴ設定がありません";
                        item.State = QueueState.LogoPending;
                    }
                    else
                    {
                        // OK
                        if (item.State == QueueState.LogoPending)
                        {
                            item.FailReason = "";
                            item.State = QueueState.Queue;

                            scheduler.QueueItem(item, item.ActualPriority);

                            if (AppData_.setting.SupressSleep)
                            {
                                // サスペンドを抑止
                                if (preventSuspend == null)
                                {
                                    preventSuspend = new PreventSuspendContext();
                                }
                            }
                        }
                    }
                }
                return prevState != item.State;
            }
            return false;
        }

        internal List<Task> UpdateQueueItems(List<Task> waits)
        {
            var map = AppData_.services.ServiceMap;
            foreach (var dir in queue.ToArray())
            {
                foreach(var item in dir.Items.ToArray())
                {
                    if (item.State != QueueState.LogoPending && item.State != QueueState.Queue)
                    {
                        continue;
                    }
                    if(UpdateQueueItem(item, waits, true))
                    {
                        waits.Add(NotifyQueueItemUpdate(item));
                    }
                }
            }
            return waits;
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

        private BitmapFrame LoadImage(string imgpath)
        {
            if(drcsImageCache.ContainsKey(imgpath))
            {
                return drcsImageCache[imgpath];
            }
            try
            {
                var img = BitmapFrame.Create(new MemoryStream(File.ReadAllBytes(imgpath)));
                drcsImageCache.Add(imgpath, img);
                return img;
            }
            catch (Exception) { }

            return null;
        }

        private Dictionary<string, DrcsImage> LoadDrcsImages()
        {
            var ret = new Dictionary<string, DrcsImage>();

            foreach (var imgpath in Directory.GetFiles(GetDRCSDirectoryPath()))
            {
                var filename = Path.GetFileName(imgpath);
                if (filename.Length == 36 && Path.GetExtension(filename).ToLower() == ".bmp")
                {
                    string md5 = filename.Substring(0, 32);
                    ret.Add(md5, new DrcsImage()
                    {
                        MD5 = md5,
                        MapStr = null,
                        Image = LoadImage(imgpath)
                    });
                }
            }

            return ret;
        }

        private Dictionary<string, DrcsImage> LoadDrcsMap()
        {
            var ret = new Dictionary<string, DrcsImage>();

            try
            {
                foreach (var line in File.ReadAllLines(GetDRCSMapPath()))
                {
                    if (line.Length >= 34 && line.IndexOf('=') == 32)
                    {
                        string md5 = line.Substring(0, 32);
                        string mapStr = line.Substring(33);
                        if (md5.All(IsHex))
                        {
                            ret.Add(md5, new DrcsImage() { MD5 = md5, MapStr = mapStr, Image = null });
                        }
                    }
                }
            }
            catch(Exception) {
                // do nothing
            }

            return ret;
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
                var profileDirTime = DateTime.MinValue;

                var drcsDirTime = DateTime.MinValue;
                var drcsTime = DateTime.MinValue;

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

                    string drcspath = GetDRCSDirectoryPath();
                    string drcsmappath = GetDRCSMapPath();
                    if(!Directory.Exists(drcspath))
                    {
                        Directory.CreateDirectory(drcspath);
                    }
                    if(!File.Exists(drcsmappath))
                    {
                        using (File.Create(drcsmappath)) { }
                    }
                    {
                        bool needUpdate = false;
                        var lastModified = Directory.GetLastWriteTime(drcspath);
                        if (drcsDirTime != lastModified)
                        {
                            // ファイルが追加された
                            needUpdate = true;
                            drcsDirTime = lastModified;
                        }
                        lastModified = File.GetLastWriteTime(drcsmappath);
                        if (drcsTime != lastModified)
                        {
                            // マッピングが更新された
                            needUpdate = true;
                            drcsTime = lastModified;
                        }
                        if (needUpdate)
                        {
                            var newImageMap = LoadDrcsImages();
                            var newStrMap = LoadDrcsMap();

                            var newDrcsMap = new Dictionary<string, DrcsImage>();
                            foreach(var key in newImageMap.Keys.Union(newStrMap.Keys))
                            {
                                var newItem = new DrcsImage() { MD5 = key };
                                if (newImageMap.ContainsKey(key))
                                {
                                    newItem.Image = newImageMap[key].Image;
                                }
                                if(newStrMap.ContainsKey(key))
                                {
                                    newItem.MapStr = newStrMap[key].MapStr;
                                }
                                newDrcsMap.Add(key, newItem);
                            }

                            // 更新処理
                            var updateImages = new List<DrcsImage>();
                            foreach (var key in newDrcsMap.Keys.Union(drcsMap.Keys))
                            {
                                if (newDrcsMap.ContainsKey(key) == false)
                                {
                                    // 消えた
                                    await Client.OnDrcsData(new DrcsImageUpdate() {
                                        Type = DrcsUpdateType.Remove,
                                        Image = drcsMap[key]
                                    });
                                }
                                else if (drcsMap.ContainsKey(key) == false)
                                {
                                    // 追加された
                                    updateImages.Add(newDrcsMap[key]);
                                }
                                else
                                {
                                    var oldItem = drcsMap[key];
                                    var newItem = newDrcsMap[key];
                                    if(oldItem.MapStr != newItem.MapStr || oldItem.Image != newItem.Image)
                                    {
                                        // 変更された
                                        updateImages.Add(newDrcsMap[key]);
                                    }
                                }
                            }

                            if(updateImages.Count > 0)
                            {
                                await Client.OnDrcsData(new DrcsImageUpdate() {
                                    Type = DrcsUpdateType.Update,
                                    ImageList = updateImages.Distinct().ToList()
                                });
                            }

                            drcsMap = newDrcsMap;
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
                                        await Client.OnOperationResult(new OperationResult()
                                        {
                                            IsFailed = true,
                                            Message = "プロファイル「" + filepath + "」の読み込みに失敗\r\n" + e.Message
                                        });
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
                                            await Client.OnOperationResult(new OperationResult()
                                            {
                                                IsFailed = true,
                                                Message = "プロファイル「" + filepath + "」の読み込みに失敗\r\n" + e.Message
                                            });
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
                await NotifyMessage(true, 
                    "WatchFileThreadがエラー終了しました: " + exception.Message, true);
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
                        SaveAppData();
                        settingUpdated = false;
                    }

                    if(autoSelectUpdated)
                    {
                        SaveAutoSelectData();
                        autoSelectUpdated = false;
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
                await NotifyMessage(true, 
                    "WatchFileThreadがエラー終了しました: " + exception.Message, true);
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
                        Util.AddLog("ディスク情報取得失敗: " + e.Message);
                    }
                }
            }
            foreach(var item in queue.SelectMany(s => s.Items).
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
                waits.Add(NotifyMessage(false, message, false));
                return Task.WhenAll(waits);
            }
            catch (Exception e)
            {
                return NotifyMessage(true, e.Message, false);
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
                waits.Add(NotifyMessage(false, message, false));
                return Task.WhenAll(waits);
            }
            catch (Exception e)
            {
                return NotifyMessage(true, e.Message, false);
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
                    scheduler.SetNumParallel(data.Setting.NumParallel);
                    affinityCreator.NumProcess = data.Setting.NumParallel;
                    settingUpdated = true;
                    return Task.WhenAll(
                        Client.OnCommonData(new CommonData() { Setting = AppData_.setting }),
                        RequestFreeSpace(),
                        NotifyMessage(false, "設定を更新しました", false));
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
                return NotifyMessage(true, e.Message, false);
            }
        }

        public Task AddQueue(AddQueueDirectory dir)
        {
            queueQ.Post(dir);
            return Task.FromResult(0);
        }

        public async Task PauseEncode(bool pause)
        {
            EncodePaused = pause;
            Task task = RequestState();
            scheduler.SetPause(pause);
            await task;
        }

        private bool IsRemoteHost(IPHostEntry iphostentry, IPAddress address)
        {
            IPHostEntry other = null;
            try
            {
                other = Dns.GetHostEntry(address);
            }
            catch
            {
                return true;
            }
            foreach (IPAddress addr in other.AddressList)
            {
                if (IPAddress.IsLoopback(addr) || Array.IndexOf(iphostentry.AddressList, addr) != -1)
                {
                    return false;
                }
            }
            return true;
        }

        private byte[] GetMacAddress()
        {
            if (ClientManager == null) return null;

            // リモートのクライアントを見つけて、
            // 接続に使っているNICのMACアドレスを取得する
            IPHostEntry iphostentry = Dns.GetHostEntry(Dns.GetHostName());
            foreach (var client in ClientManager.ClientList)
            {
                if(IsRemoteHost(iphostentry, client.RemoteIP.Address))
                {
                    return ServerSupport.GetMacAddress(client.LocalIP.Address);
                }
            }
            return null;
        }

        public async Task RequestSetting()
        {
            await Client.OnCommonData(new CommonData() {
                Setting = AppData_.setting,
                JlsCommandFiles = JlsCommandFiles,
                MainScriptFiles = MainScriptFiles,
                PostScriptFiles = PostScriptFiles,
                ServerInfo = new ServerInfo()
                {
                    HostName = Dns.GetHostName(),
                    MacAddress = GetMacAddress()
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

        public Task RequestQueue()
        {
            QueueData data = new QueueData()
            {
                Items = queue
            };
            return Client.OnQueueData(data);
        }

        public Task RequestLog()
        {
            return Client.OnLogData(Log);
        }

        public Task RequestConsole()
        {
            return Task.WhenAll(scheduler.Workers.Cast<TranscodeWorker>().Select(w =>
                Client.OnConsole(new ConsoleData() {
                    index = w.id,
                    text = w.consoleText.TextLines as List<string>
                })));
        }

        public Task RequestLogFile(LogItem item)
        {
            return Client.OnLogFile(ReadLogFIle(item.EncodeStartDate));
        }

        public Task RequestState()
        {
            var state = new State() {
                Pause = encodePaused,
                Running = nowEncoding,
                Progress = Progress
            };
            return Client.OnCommonData(new CommonData()
            {
                State = state
            });
        }

        public Task RequestFreeSpace()
        {
            RefrechDiskSpace();
            return Client.OnCommonData(new CommonData() {
                Disks = diskMap.Values.ToList()
            });
        }

        public Task RequestDrcsImages()
        {
            return Client.OnDrcsData(new DrcsImageUpdate() {
                Type = DrcsUpdateType.Update,
                ImageList = drcsMap.Values.ToList()
            });
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

        public async Task RequestServiceSetting()
        {
            var serviceMap = AppData_.services.ServiceMap;
            await Client.OnServiceSetting(new ServiceSettingUpdate()
            {
                Type = ServiceSettingUpdateType.Clear
            });
            foreach (var service in serviceMap.Values.ToArray())
            {
                await Client.OnServiceSetting(new ServiceSettingUpdate() {
                    Type = ServiceSettingUpdateType.Update,
                    ServiceId = service.ServiceId,
                    Data = service
                });
            }
        }

        private AMTContext amtcontext = new AMTContext();
        public Task RequestLogoData(string fileName)
        {
            if(fileName == LogoSetting.NO_LOGO)
            {
                return NotifyMessage(true, "不正な操作です", false);
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
                return NotifyMessage(true,
                    "ロゴファイルを開けません。パス:" + logopath + "メッセージ: " + exception.Message, false);
            }
        }

        private void ResetStateItem(QueueItem item, List<Task> waits)
        {
            item.State = QueueState.LogoPending;
            UpdateQueueItem(item, waits, true);
            waits.Add(NotifyQueueItemUpdate(item));
        }

        private void UpdateProfileItem(QueueItem item, List<Task> waits)
        {
            var profile = GetProfile(item, item.ProfileName);
            var newDir = GetQueueDirectory(item.Dir.DirPath, item.Dir.Mode, profile?.Profile ?? pendingProfile, waits);
            if(newDir != item.Dir)
            {
                MoveItemDirectory(item, newDir, waits);
            }
            if(profile != null && profile.Priority > 0)
            {
                item.Priority = profile.Priority;
            }
        }

        private void DuplicateItem(QueueItem item, List<Task> waits)
        {
            var newItem = ServerSupport.DeepCopy(item);
            newItem.Id = nextItemId++;
            newItem.Dir = item.Dir;
            newItem.Dir.Items.Add(newItem);

            // 状態はリセットしておく
            newItem.State = QueueState.LogoPending;
            UpdateQueueItem(newItem, waits, false);

            waits.Add(Client.OnQueueUpdate(new QueueUpdate()
            {
                Type = UpdateType.Add,
                DirId = newItem.Dir.Id,
                Item = newItem
            }));
        }

        internal static void MoveTSFile(string file, string dstDir, bool withEDCB)
        {
            string body = Path.GetFileNameWithoutExtension(file);
            string tsext = Path.GetExtension(file);
            string srcDir = Path.GetDirectoryName(file);
            foreach (var ext in ServerSupport.GetFileExtentions(tsext, withEDCB))
            {
                string srcPath = srcDir + "\\" + body + ext;
                string dstPath = dstDir + "\\" + body + ext;
                if (File.Exists(srcPath))
                {
                    File.Move(srcPath, dstPath);
                }
            }
        }

        private int RemoveCompleted(QueueDirectory dir, List<Task> waits)
        {
            if (dir.Items.All(s => s.State == QueueState.Complete || s.State == QueueState.PreFailed))
            {
                // 全て完了しているのでディレクトリを削除
                queue.Remove(dir);
                waits.Add(Client.OnQueueUpdate(new QueueUpdate()
                {
                    Type = UpdateType.Remove,
                    DirId = dir.Id
                }));
                return dir.Items.Count;
            }

            var removeItems = dir.Items.Where(s => s.State == QueueState.Complete).ToArray();
            foreach (var item in removeItems)
            {
                dir.Items.Remove(item);
                waits.Add(Client.OnQueueUpdate(new QueueUpdate()
                {
                    Type = UpdateType.Remove,
                    DirId = item.Dir.Id,
                    Item = item
                }));
            }
            return removeItems.Length;
        }

        public Task ChangeItem(ChangeItemData data)
        {
            if(data.ChangeType == ChangeItemType.RemoveCompletedAll)
            {
                // 全て対象
                var waits = new List<Task>();
                int removeItems = 0;
                foreach(var dir in queue.ToArray())
                {
                    removeItems += RemoveCompleted(dir, waits);
                }
                waits.Add(NotifyMessage(false, "" + removeItems + "件削除しました", false));
                return Task.WhenAll(waits);
            }
            else if (data.ChangeType == ChangeItemType.RemoveDir ||
                data.ChangeType == ChangeItemType.RemoveCompletedItem)
            {
                // ディレクトリ操作
                var target = queue.Find(t => t.Id == data.ItemId);
                if (target == null)
                {
                    return NotifyMessage(true,
                        "指定されたキューディレクトリが見つかりません", false);
                }

                if(data.ChangeType == ChangeItemType.RemoveDir)
                {
                    // ディレクトリ削除
                    queue.Remove(target);
                    // 全てキャンセル
                    foreach (var item in target.Items)
                    {
                        item.State = QueueState.Canceled;
                    }
                    return Task.WhenAll(
                        Client.OnQueueUpdate(new QueueUpdate()
                        {
                            Type = UpdateType.Remove,
                            DirId = target.Id
                        }),
                        NotifyMessage(false, "ディレクトリ「" + target.DirPath + "」を削除しました", false));
                }
                else if(data.ChangeType == ChangeItemType.RemoveCompletedItem)
                {
                    // ディレクトリの完了削除
                    var waits = new List<Task>();
                    int removeItems = RemoveCompleted(target, waits);
                    waits.Add(NotifyMessage(false, "" + removeItems + "件削除しました", false));
                    return Task.WhenAll(waits);
                }
            }
            else
            {
                // アイテム操作
                var all = queue.SelectMany(d => d.Items);
                var target = all.FirstOrDefault(s => s.Id == data.ItemId);
                if (target == null)
                {
                    return NotifyMessage(true,
                        "指定されたアイテムが見つかりません", false);
                }

                var dir = target.Dir;

                if (data.ChangeType == ChangeItemType.ResetState ||
                    data.ChangeType == ChangeItemType.UpdateProfile ||
                    data.ChangeType == ChangeItemType.Duplicate)
                {
                    if (data.ChangeType == ChangeItemType.ResetState)
                    {
                        // 状態リセットは終わってるのだけ
                        if (target.State != QueueState.Complete &&
                            target.State != QueueState.Failed &&
                            target.State != QueueState.Canceled)
                        {
                            return NotifyMessage(true, "完了していないアイテムは状態リセットできません", false);
                        }
                    }
                    else if (data.ChangeType == ChangeItemType.UpdateProfile)
                    {
                        // エンコード中は変更できない
                        if (target.State == QueueState.Encoding)
                        {
                            return NotifyMessage(true, "このアイテムはエンコード中のためプロファイル更新できません", false);
                        }
                    }
                    else if (data.ChangeType == ChangeItemType.Duplicate)
                    {
                        // バッチモードでアクティブなやつは重複になるのでダメ
                        if (target.Dir.IsBatch && target.IsActive)
                        {
                            return NotifyMessage(true, "通常モードで追加されたアイテムは複製できません", false);
                        }
                    }

                    var waits = new List<Task>();

                    if (dir.IsBatch)
                    {
                        // バッチモードでfailed/succeededフォルダに移動されていたら戻す
                        if (target.State == QueueState.Failed || target.State == QueueState.Complete)
                        {
                            if (all.Where(s => s.SrcPath == target.SrcPath).Any(s => s.IsActive) == false)
                            {
                                var movedDir = (target.State == QueueState.Failed) ? dir.Failed : dir.Succeeded;
                                var movedPath = movedDir + "\\" + Path.GetFileName(target.FileName);
                                if (File.Exists(movedPath))
                                {
                                    // EDCB関連ファイルも移動したかどうかは分からないが、あれば戻す
                                    MoveTSFile(movedPath, dir.DirPath, true);
                                }
                            }
                        }
                    }

                    if (data.ChangeType == ChangeItemType.ResetState)
                    {
                        ResetStateItem(target, waits);
                        waits.Add(NotifyMessage(false, "状態リセットします", false));
                    }
                    else if (data.ChangeType == ChangeItemType.UpdateProfile)
                    {
                        UpdateProfileItem(target, waits);
                        waits.Add(NotifyMessage(false, "プロファイル再適用します", false));
                    }
                    else
                    {
                        DuplicateItem(target, waits);
                        waits.Add(NotifyMessage(false, "複製しました", false));
                    }

                    return Task.WhenAll(waits);
                }
                else if (data.ChangeType == ChangeItemType.Cancel)
                {
                    if (target.IsActive)
                    {
                        target.State = QueueState.Canceled;
                        return Task.WhenAll(
                            Client.OnQueueUpdate(new QueueUpdate()
                            {
                                Type = UpdateType.Update,
                                DirId = target.Dir.Id,
                                Item = target
                            }),
                            NotifyMessage(false, "キャンセルしました", false));
                    }
                    else
                    {
                        return NotifyMessage(true,
                            "このアイテムはアクティブ状態でないため、キャンセルできません", false);
                    }
                }
                else if (data.ChangeType == ChangeItemType.Priority)
                {
                    target.Priority = data.Priority;
                    scheduler.UpdatePriority(target, target.ActualPriority);
                    return Task.WhenAll(
                        Client.OnQueueUpdate(new QueueUpdate()
                        {
                            Type = UpdateType.Update,
                            DirId = target.Dir.Id,
                            Item = target
                        }),
                        NotifyMessage(false, "優先度を変更しました", false));
                }
                else if (data.ChangeType == ChangeItemType.Profile)
                {
                    if (target.State == QueueState.Encoding)
                    {
                        return NotifyMessage(true, "エンコード中はプロファイル変更できません", false);
                    }
                    if (target.State == QueueState.PreFailed)
                    {
                        return NotifyMessage(true, "このアイテムはプロファイル変更できません", false);
                    }

                    var waits = new List<Task>();
                    target.ProfileName = data.Profile;
                    var profile = GetProfile(target, target.ProfileName);
                    var newDir = GetQueueDirectory(target.Dir.DirPath, target.Dir.Mode, profile?.Profile ?? pendingProfile, waits);
                    if (newDir != target.Dir)
                    {
                        MoveItemDirectory(target, newDir, waits);
                        if (UpdateQueueItem(target, waits, true))
                        {
                            waits.Add(NotifyQueueItemUpdate(target));
                        }
                        waits.Add(NotifyMessage(false, "プロファイルを「" + data.Profile + "」に変更しました", false));
                    }

                    return Task.WhenAll(waits);
                }
                else if (data.ChangeType == ChangeItemType.RemoveItem)
                {
                    target.State = QueueState.Canceled;
                    dir.Items.Remove(target);
                    return Task.WhenAll(
                        Client.OnQueueUpdate(new QueueUpdate()
                        {
                            Type = UpdateType.Remove,
                            DirId = target.Dir.Id,
                            Item = target
                        }),
                        NotifyMessage(false, "アイテムを削除しました", false));
                }
            }
            return Task.FromResult(0);
        }

        [return: MarshalAs(UnmanagedType.Bool)]
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern bool MoveFileEx(string existingFileName, string newFileName, int flags);

        public async Task AddDrcsMap(DrcsImage recvitem)
        {
            if (drcsMap.ContainsKey(recvitem.MD5))
            {
                var item = drcsMap[recvitem.MD5];

                if (item.MapStr != recvitem.MapStr)
                {
                    var filepath = GetDRCSMapPath();
                    var updateType = DrcsUpdateType.Update;

                    // データ更新
                    if (item.MapStr == null)
                    {
                        // 既存のマッピングにないので追加
                        item.MapStr = recvitem.MapStr;
                        try
                        {
                            File.AppendAllLines(filepath,
                                new string[] { item.MD5 + "=" + recvitem.MapStr },
                                Encoding.UTF8);
                        }
                        catch (Exception e) {
                            await NotifyMessage(true,
                                "DRCSマッピングファイル書き込みに失敗: " + e.Message, false);
                        }
                    }
                    else
                    {
                        // 既にマッピングにある
                        item.MapStr = recvitem.MapStr;
                        if(item.MapStr == null)
                        {
                            // 削除
                            drcsMap.Remove(recvitem.MD5);
                            updateType = DrcsUpdateType.Remove;
                            try
                            {
                                File.Delete(GetDRCSImagePath(recvitem.MD5));
                            }
                            catch(Exception e)
                            {
                                await NotifyMessage(true,
                                    "DRCS画像ファイル削除に失敗: " + e.Message, false);
                            }
                        }

                        // まず、一時ファイルに書き込む
                        var tmppath = filepath + ".tmp";
                        // BOMありUTF-8
                        try
                        {
                            using (var sw = new StreamWriter(File.OpenWrite(tmppath), Encoding.UTF8))
                            {
                                foreach (var s in drcsMap.Values)
                                {
                                    sw.WriteLine(s.MD5 + "=" + s.MapStr);
                                }
                            }
                        }
                        catch (Exception e)
                        {
                            await NotifyMessage(true,
                                "DRCSマッピングファイル書き込みに失敗: " + e.Message, false);
                        }
                        // ファイル置き換え
                        MoveFileEx(tmppath, filepath, 11);
                    }

                    await Client.OnDrcsData(new DrcsImageUpdate() {
                        Type = updateType,
                        Image = item
                    });
                }
            }
        }

        public Task EndServer()
        {
            finishRequested?.Invoke();
            return Task.FromResult(0);
        }
    }
}
