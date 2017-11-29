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

namespace Amatsukaze.Models
{
    public class DisplayQueueDirectory : NotificationObject
    {
        public int Id;

        #region Path変更通知プロパティ
        private string _Path;

        public string Path
        {
            get { return _Path; }
            set
            {
                if (_Path == value)
                    return;
                _Path = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Items変更通知プロパティ
        private ObservableCollection<QueueItem> _Items;

        public ObservableCollection<QueueItem> Items {
            get { return _Items; }
            set { 
                if (_Items == value)
                    return;
                _Items = value;
                RaisePropertyChanged();
            }
        }
        #endregion
        
        public DisplayQueueDirectory(QueueDirectory dir)
        {
            Id = dir.Id;
            Path = dir.Path;
            Items = new ObservableCollection<QueueItem>(dir.Items);
        }
    }

    public class DisplayService : NotificationObject
    {
        public ClientModel Model { get; set; }

        #region Data変更通知プロパティ
        private ServiceSettingElement _Data;

        public ServiceSettingElement Data {
            get { return _Data; }
            set { 
                //if (_Data == value)
                //    return;
                _Data = value;

                _LogoList = null;
                RaisePropertyChanged("LogoList");
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LogoList変更通知プロパティ
        private DisplayLogo[] _LogoList;

        public DisplayLogo[] LogoList
        {
            get
            {
                if(_LogoList == null)
                {
                    _LogoList = Data.LogoSettings
                        .Where(s => s.Exists).Select(s => new DisplayLogo()
                        {
                            Model = Model,
                            Setting = s,
                            From = s.From,
                            To = s.To,
                        }).ToArray();

                    // ロゴデータをリクエスト
                    foreach(var logo in _LogoList)
                    {
                        if(logo.Setting.FileName != LogoSetting.NO_LOGO)
                        {
                            Model.RequestLogoData(logo.Setting.FileName);
                        }
                    }
                }
                return _LogoList;
            }
        }
        #endregion

        #region JlsCommandFile変更通知プロパティ
        public string JlsCommandFile
        {
            get
            { return Data.JLSCommand; }
            set
            {
                if (Data.JLSCommand == value)
                    return;
                Data.JLSCommand = value;
                RaisePropertyChanged();
                Model.UpdateService(this);
            }
        }
        #endregion

        public override string ToString()
        {
            return Data.ServiceName + "(" + Data.ServiceId + ")";
        }
    }

    public class DisplayLogo : NotificationObject
    {
        public ClientModel Model { get; set; }

        public LogoSetting Setting { get; set; }

        #region Enabled変更通知プロパティ
        public bool Enabled
        {
            get
            { return Setting.Enabled; }
            set
            {
                if (Setting.Enabled == value)
                    return;
                Setting.Enabled = value;
                RaisePropertyChanged();
                Model.UpdateLogo(this);
            }
        }
        #endregion

        #region Data変更通知プロパティ
        private LogoData _Data;

        public LogoData Data
        {
            get
            { return _Data; }
            set
            {
                if (_Data == value)
                    return;
                _Data = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region From変更通知プロパティ
        private DateTime _From;

        public DateTime From
        {
            get
            { return _From; }
            set
            {
                if (_From == value)
                    return;
                _From = value;
                RaisePropertyChanged();
                RaisePropertyChanged("DateChanged");
            }
        }
        #endregion

        #region To変更通知プロパティ
        private DateTime _To;

        public DateTime To
        {
            get
            { return _To; }
            set
            {
                if (_To == value)
                    return;
                _To = value;
                RaisePropertyChanged();
                RaisePropertyChanged("DateChanged");
            }
        }
        #endregion

        #region DateChanged変更通知プロパティ
        public bool DateChanged
        {
            get { return Setting.From != _From || Setting.To != _To; }
        }
        #endregion

        public string ToDateString
        {
            get { return Setting.To.ToString("yyyy/MM/dd"); }
        }

        public string FromDateString
        {
            get { return Setting.From.ToString("yyyy/MM/dd"); }
        }

        public void ApplyDate()
        {
            Setting.From = _From;
            Setting.To = _To;
            Model.UpdateLogo(this);
        }
    }

    public class DisplayDrcsImage : NotificationObject
    {
        public ClientModel Model { get; set; }

        public DrcsImage Image { get; set; }

        #region MapStr変更通知プロパティ
        public string MapStr
        {
            get
            { return Image.MapStr; }
            set
            {
                if (Image.MapStr == value)
                    return;
                Image.MapStr = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SetMapStrCommand
        private ViewModelCommand _SetMapStrCommand;

        public ViewModelCommand SetMapStrCommand
        {
            get
            {
                if (_SetMapStrCommand == null)
                {
                    _SetMapStrCommand = new ViewModelCommand(SetMapStr);
                }
                return _SetMapStrCommand;
            }
        }

        public void SetMapStr()
        {
            if(MessageBox.Show("「" + MapStr + "」をマッピングに追加します。\r\nよろしいですか？",
                "AmatsukazeGUI", MessageBoxButton.OKCancel) == MessageBoxResult.OK)
            {
                Model.Server.AddDrcsMap(Image);
            }
        }
        #endregion
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
        private Setting setting = new Setting() { Bitrate = new BitrateSetting() };
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
        private string[] _JlsCommandFiles;

        public string[] JlsCommandFiles {
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

        #region NumParallel変更通知プロパティ
        public int NumParallel {
            get { return setting.NumParallel; }
            set { 
                if (setting.NumParallel == value)
                    return;
                setting.NumParallel = value;
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

        #region NVEncPath変更通知プロパティ
        public string NVEncPath {
            get { return setting.NVEncPath; }
            set { 
                if (setting.NVEncPath == value)
                    return;
                setting.NVEncPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EncoderTypeInt変更通知プロパティ
        public int EncoderTypeInt {
            get { return (int)setting.EncoderType; }
            set { 
                if ((int)setting.EncoderType == value)
                    return;
                setting.EncoderType = (EncoderType)value;
                UpdateWarningText();
                RaisePropertyChanged();
                RaisePropertyChanged("EncoderOption");
            }
        }
        #endregion

        #region EncoderOption変更通知プロパティ
        public string EncoderOption {
            get {
                switch (setting.EncoderType)
                {
                    case EncoderType.x264: return setting.X264Option;
                    case EncoderType.x265: return setting.X265Option;
                    case EncoderType.QSVEnc: return setting.QSVEncOption;
                    case EncoderType.NVEnc: return setting.NVEncOption;
                }
                return null;
            }
            set {
                switch (setting.EncoderType)
                {
                    case EncoderType.x264:
                        if (setting.X264Option == value)
                            return;
                        setting.X264Option = value;
                        break;
                    case EncoderType.x265:
                        if (setting.X265Option == value)
                            return;
                        setting.X265Option = value;
                        break;
                    case EncoderType.QSVEnc:
                        if (setting.QSVEncOption == value)
                            return;
                        setting.QSVEncOption = value;
                        break;
                    case EncoderType.NVEnc:
                        if (setting.NVEncOption == value)
                            return;
                        setting.NVEncOption = value;
                        break;
                    default:
                        return;
                }
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

        #region MKVMergePath変更通知プロパティ
        public string MKVMergePath {
            get { return setting.MKVMergePath; }
            set {
                if (setting.MKVMergePath == value)
                    return;
                setting.MKVMergePath = value;
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

        #region ChapterExePath変更通知プロパティ
        public string ChapterExePath {
            get { return setting.ChapterExePath; }
            set {
                if (setting.ChapterExePath == value)
                    return;
                setting.ChapterExePath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region JoinLogoScpPath変更通知プロパティ
        public string JoinLogoScpPath {
            get { return setting.JoinLogoScpPath; }
            set {
                if (setting.JoinLogoScpPath == value)
                    return;
                setting.JoinLogoScpPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region FilterPath変更通知プロパティ
        public string FilterPath {
            get { return string.IsNullOrEmpty(setting.FilterPath) ? "フィルタなし" : setting.FilterPath; }
            set {
                string val = (value == "フィルタなし") ? "" : value;
                if (setting.FilterPath == val)
                    return;
                setting.FilterPath = val;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PostFilterPath変更通知プロパティ
        public string PostFilterPath {
            get { return string.IsNullOrEmpty(setting.PostFilterPath) ? "フィルタなし" : setting.PostFilterPath; }
            set {
                string val = (value == "フィルタなし") ? "" : value;
                if (setting.PostFilterPath == val)
                    return;
                setting.PostFilterPath = val;
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
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SplitSub変更通知プロパティ
        public bool SplitSub
        {
            get
            { return setting.SplitSub; }
            set
            {
                if (setting.SplitSub == value)
                    return;
                setting.SplitSub = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OutputMask変更通知プロパティ
        public int OutputMask
        {
            get
            { return setting.OutputMask; }
            set
            {
                if (setting.OutputMask == value)
                    return;
                setting.OutputMask = value;
                UpdateWarningText();
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

        #region BitrateCM変更通知プロパティ
        public double BitrateCM {
            get { return setting.BitrateCM; }
            set { 
                if (setting.BitrateCM == value)
                    return;
                setting.BitrateCM = value;
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

        #region DefaultJLSCommand変更通知プロパティ
        public string DefaultJLSCommand {
            get { return setting.DefaultJLSCommand; }
            set {
                if (setting.DefaultJLSCommand == value)
                    return;
                setting.DefaultJLSCommand = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableChapter変更通知プロパティ
        public bool DisableChapter
        {
            get { return setting.DisableChapter; }
            set
            {
                if (setting.DisableChapter == value)
                    return;
                setting.DisableChapter = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableSubs変更通知プロパティ
        public bool DisableSubs
        {
            get { return setting.DisableSubs; }
            set
            {
                if (setting.DisableSubs == value)
                    return;
                setting.DisableSubs = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IgnoreNoDrcsMap変更通知プロパティ
        public bool IgnoreNoDrcsMap
        {
            get { return setting.IgnoreNoDrcsMap; }
            set
            {
                if (setting.IgnoreNoDrcsMap == value)
                    return;
                setting.IgnoreNoDrcsMap = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ClearWorkDirOnStart変更通知プロパティ
        public bool ClearWorkDirOnStart {
            get { return setting.ClearWorkDirOnStart; }
            set {
                if (setting.ClearWorkDirOnStart == value)
                    return;
                setting.ClearWorkDirOnStart = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SystemAviSynthPlugin変更通知プロパティ
        public bool SystemAviSynthPlugin {
            get { return setting.SystemAviSynthPlugin; }
            set {
                if (setting.SystemAviSynthPlugin == value)
                    return;
                setting.SystemAviSynthPlugin = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableHashCheck変更通知プロパティ
        public bool DisableHashCheck {
            get { return setting.DisableHashCheck; }
            set {
                if (setting.DisableHashCheck == value)
                    return;
                setting.DisableHashCheck = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Mpeg2DecoderInt変更通知プロパティ
        public int Mpeg2DecoderInt {
            get { return (int)setting.Mpeg2Decoder; }
            set { 
                if ((int)setting.Mpeg2Decoder == value)
                    return;
                setting.Mpeg2Decoder = (DecoderType)value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region H264DecoderInt変更通知プロパティ
        public int H264DecoderInt {
            get { return (int)setting.H264Deocder; }
            set {
                if ((int)setting.H264Deocder == value)
                    return;
                setting.H264Deocder = (DecoderType)value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OutputFormatInt変更通知プロパティ
        public int OutputFormatInt
        {
            get { return (int)setting.OutputFormat; }
            set
            {
                if ((int)setting.OutputFormat == value)
                    return;
                setting.OutputFormat = (FormatType)value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SettingWarningText変更通知プロパティ
        private string _SettingWarningText;

        public string SettingWarningText {
            get { return _SettingWarningText; }
            set { 
                if (_SettingWarningText == value)
                    return;
                _SettingWarningText = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DrcsImageList変更通知プロパティ
        private ObservableCollection<DisplayDrcsImage> drcsImageList_ = new ObservableCollection<DisplayDrcsImage>();

        public ObservableCollection<DisplayDrcsImage> DrcsImageList
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

                    if (Server is EncodeServer)
                    {
                        (Server as EncodeServer).Dispose();
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

        private void UpdateWarningText()
        {
            StringBuilder sb = new StringBuilder();
            if(setting.EncoderType == EncoderType.QSVEnc && setting.TwoPass)
            {
                sb.Append("QSVEncは2パスに対応していません\r\n");
            }
            if (setting.EncoderType == EncoderType.NVEnc && setting.TwoPass)
            {
                sb.Append("NVEncは2パスに対応していません\r\n");
            }
            SettingWarningText = sb.ToString();
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
                Server = new EncodeServer(0, new ClientAdapter(this));
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

        public Task OnSetting(Setting setting)
        {
            NumParallel = setting.NumParallel;
            AmatsukazePath = setting.AmatsukazePath;
            X264Path = setting.X264Path;
            this.setting.X264Option = setting.X264Option;
            X265Path = setting.X265Path;
            this.setting.X265Option = setting.X265Option;
            QSVEncPath = setting.QSVEncPath;
            this.setting.QSVEncOption = setting.QSVEncOption;
            NVEncPath = setting.NVEncPath;
            this.setting.NVEncOption = setting.NVEncOption;
            EncoderTypeInt = (int)setting.EncoderType;
            MuxerPath = setting.MuxerPath;
            MKVMergePath = setting.MKVMergePath;
            TimelineEditorPath = setting.TimelineEditorPath;

            ChapterExePath = setting.ChapterExePath;
            JoinLogoScpPath = setting.JoinLogoScpPath;
            FilterPath = setting.FilterPath;
            PostFilterPath = setting.PostFilterPath;

            WorkPath = setting.WorkPath;
            AlwaysShowDisk = setting.AlwaysShowDisk;
            AutoBuffer = setting.AutoBuffer;
            BitrateA = setting.Bitrate.A;
            BitrateB = setting.Bitrate.B;
            BitrateH264 = setting.Bitrate.H264;
            BitrateCM = setting.BitrateCM;
            TwoPass = setting.TwoPass;
            SplitSub = setting.SplitSub;
            OutputMask = setting.OutputMask;
            DefaultJLSCommand = setting.DefaultJLSCommand;
            DisableChapter = setting.DisableChapter;
            DisableSubs = setting.DisableSubs;
            IgnoreNoDrcsMap = setting.IgnoreNoDrcsMap;
            ClearWorkDirOnStart = setting.ClearWorkDirOnStart;
            SystemAviSynthPlugin = setting.SystemAviSynthPlugin;
            DisableHashCheck = setting.DisableHashCheck;

            Mpeg2DecoderInt = (int)setting.Mpeg2Decoder;
            H264DecoderInt = (int)setting.H264Deocder;
            OutputFormatInt = (int)setting.OutputFormat;
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
                    var dir = QueueItems.FirstOrDefault(d => d.Id == update.DirId);
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
                var dir = QueueItems.FirstOrDefault(d => d.Id == update.DirId);
                if (dir != null)
                {
                    if (update.Type == UpdateType.Add)
                    {
                        dir.Items.Add(update.Item);
                    }
                    else
                    {
                        var file = dir.Items.FirstOrDefault(f => f.DstName == update.Item.DstName);
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

            // 一時フォルダの容量があれば更新
            var diskItem = space.Disks.Find(item => WorkPath.StartsWith(item.Path));
            if(diskItem != null)
            {
                TmpDiskSpaceGB = (int)(diskItem.Capacity / (1024*1024*1024L));
            }

            return Task.FromResult(0);
        }

        public Task OnServiceSetting(ServiceSettingUpdate update)
        {
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

        public Task OnJlsCommandFiles(JLSCommandFiles files)
        {
            JlsCommandFiles = files.Files.ToArray();
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
            if(update.Type == DrcsUpdateType.Add)
            {
                drcsImageList_.Add(new DisplayDrcsImage() {
                    Model = this, Image = update.Image
                });
            }
            else if(update.Type == DrcsUpdateType.Remove)
            {
                var item = drcsImageList_.First(s => s.Image.MD5 == update.Image.MD5);
                if(item != null)
                {
                    drcsImageList_.Remove(item);
                }
            }
            return Task.FromResult(0);
        }

        public Task OnAvsScriptFiles(AvsScriptFiles files)
        {
            MainScriptFiles = new string[] { "フィルタなし" }.Concat(files.Main).ToList();
            PostScriptFiles = new string[] { "フィルタなし" }.Concat(files.Post).ToList();
            return Task.FromResult(0);
        }
    }
}
