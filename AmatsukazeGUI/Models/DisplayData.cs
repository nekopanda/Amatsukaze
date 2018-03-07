using Amatsukaze.Server;
using Livet;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Models
{
    public class DisplayQueueDirectory : NotificationObject
    {
        public int Id;

        #region Path変更通知プロパティ
        private string _Path;

        public string Path {
            get { return _Path; }
            set {
                if (_Path == value)
                    return;
                _Path = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Items変更通知プロパティ
        private ObservableCollection<DisplayQueueItem> _Items;

        public ObservableCollection<DisplayQueueItem> Items {
            get { return _Items; }
            set {
                if (_Items == value)
                    return;
                _Items = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ModeString変更通知プロパティ
        private string _ModeString;

        public string ModeString {
            get { return _ModeString; }
            set {
                if (_ModeString == value)
                    return;
                _ModeString = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Profile変更通知プロパティ
        private string _Profile;

        public string Profile {
            get { return _Profile; }
            set {
                if (_Profile == value)
                    return;
                _Profile = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LastUpdate変更通知プロパティ
        private string _LastUpdate;

        public string LastUpdate {
            get { return _LastUpdate; }
            set { 
                if (_LastUpdate == value)
                    return;
                _LastUpdate = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        private static string ModeToString(ProcMode mode)
        {
            switch (mode)
            {
                case ProcMode.AutoBatch:
                    return "自動追加";
                case ProcMode.Batch:
                    return "通常";
                case ProcMode.Test:
                    return "テスト";
                case ProcMode.DrcsSearch:
                    return "DRCSサーチ";
            }
            return "不明モード";
        }

        public DisplayQueueDirectory(QueueDirectory dir, ClientModel Parent)
        {
            Id = dir.Id;
            Path = dir.DirPath;
            ModeString = ModeToString(dir.Mode);
            Profile = dir.Profile.Name;
            LastUpdate = dir.Profile.LastUpdate.ToString("yyyy/MM/dd HH:mm:ss");
            Items = new ObservableCollection<DisplayQueueItem>(
                dir.Items.Select(s => new DisplayQueueItem() { Parent = Parent, Model = s }));
        }
    }

    public class DisplayQueueItem : NotificationObject
    {
        public ClientModel Parent { get; set; }

        public QueueItem Model { get; set; }

        public bool IsSelected { get; set; }

        public bool IsComplete { get { return Model.State == QueueState.Complete; } }
        public bool IsEncoding { get { return Model.State == QueueState.Encoding; } }
        public bool IsError { get { return Model.State == QueueState.Failed || Model.State == QueueState.PreFailed; } }
        public bool IsPending { get { return Model.State == QueueState.LogoPending; } }
        public bool IsPreFailed { get { return Model.State == QueueState.PreFailed; } }
        public bool IsCanceled { get { return Model.State == QueueState.Canceled; } }
        public bool IsTooSmall { get { return IsPreFailed && Model.FailReason.Contains("映像が小さすぎます"); } }
        public string TsTimeString { get { return Model.TsTime.ToString("yyyy年MM月dd日"); } }
        public string ServiceString { get { return Model.ServiceName + "(" + Model.ServiceId + ")"; } }

        public string StateString {
            get {
                switch (Model.State)
                {
                    case QueueState.Queue: return "待ち";
                    case QueueState.Encoding: return "エンコード中";
                    case QueueState.Failed: return "失敗";
                    case QueueState.PreFailed: return "失敗";
                    case QueueState.LogoPending: return "ペンディング";
                    case QueueState.Canceled: return "キャンセル";
                    case QueueState.Complete: return "完了";
                }
                return "不明";
            }
        }

        #region Priority変更通知プロパティ
        public int Priority {
            get { return Model.Priority; }
            set { 
                if (Model.Priority == value)
                    return;
                Model.Priority = value;
                Parent.Server.ChangeItem(new ChangeItemData()
                {
                    ChangeType = ChangeItemType.Priority,
                    ItemId = Model.Id,
                    Priority = value
                });
                RaisePropertyChanged();
            }
        }
        #endregion

    }

    public class DisplayProfile : NotificationObject
    {
        public ProfileSetting Model { get; set; }

        #region EncoderTypeInt変更通知プロパティ
        public int EncoderTypeInt {
            get { return (int)Model.EncoderType; }
            set {
                if ((int)Model.EncoderType == value)
                    return;
                Model.EncoderType = (EncoderType)value;
                UpdateWarningText();
                RaisePropertyChanged();
                RaisePropertyChanged("EncoderOption");
            }
        }
        #endregion

        #region EncoderOption変更通知プロパティ
        public string EncoderOption {
            get { return Model.EncoderOption; }
            set {
                if (Model.EncoderOption == value)
                    return;
                Model.EncoderOption = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region FilterPath変更通知プロパティ
        public string FilterPath {
            get { return string.IsNullOrEmpty(Model.FilterPath) ? "フィルタなし" : Model.FilterPath; }
            set {
                string val = (value == "フィルタなし") ? "" : value;
                if (Model.FilterPath == val)
                    return;
                Model.FilterPath = val;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PostFilterPath変更通知プロパティ
        public string PostFilterPath {
            get { return string.IsNullOrEmpty(Model.PostFilterPath) ? "フィルタなし" : Model.PostFilterPath; }
            set {
                string val = (value == "フィルタなし") ? "" : value;
                if (Model.PostFilterPath == val)
                    return;
                Model.PostFilterPath = val;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AutoBuffer変更通知プロパティ
        public bool AutoBuffer {
            get { return Model.AutoBuffer; }
            set {
                if (Model.AutoBuffer == value)
                    return;
                Model.AutoBuffer = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region TwoPass変更通知プロパティ
        public bool TwoPass {
            get { return Model.TwoPass; }
            set {
                if (Model.TwoPass == value)
                    return;
                Model.TwoPass = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SplitSub変更通知プロパティ
        public bool SplitSub {
            get { return Model.SplitSub; }
            set {
                if (Model.SplitSub == value)
                    return;
                Model.SplitSub = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OutputMask変更通知プロパティ
        public int OutputMask {
            get { return Model.OutputMask; }
            set {
                if (Model.OutputMask == value)
                    return;
                Model.OutputMask = value;
                UpdateWarningText();
                OutputOptionIndex = OutputMasklist.IndexOf(OutputMask);
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateA変更通知プロパティ
        public double BitrateA {
            get { return Model.Bitrate.A; }
            set {
                if (Model.Bitrate.A == value)
                    return;
                Model.Bitrate.A = value;
                UpdateBitrate();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateB変更通知プロパティ
        public double BitrateB {
            get { return Model.Bitrate.B; }
            set {
                if (Model.Bitrate.B == value)
                    return;
                Model.Bitrate.B = value;
                UpdateBitrate();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateH264変更通知プロパティ
        public double BitrateH264 {
            get { return Model.Bitrate.H264; }
            set {
                if (Model.Bitrate.H264 == value)
                    return;
                Model.Bitrate.H264 = value;
                UpdateBitrate();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateCM変更通知プロパティ
        public double BitrateCM {
            get { return Model.BitrateCM; }
            set {
                if (Model.BitrateCM == value)
                    return;
                Model.BitrateCM = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DefaultJLSCommand変更通知プロパティ
        public string DefaultJLSCommand {
            get { return Model.DefaultJLSCommand; }
            set {
                if (Model.DefaultJLSCommand == value)
                    return;
                Model.DefaultJLSCommand = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableChapter変更通知プロパティ
        public bool DisableChapter {
            get { return Model.DisableChapter; }
            set {
                if (Model.DisableChapter == value)
                    return;
                Model.DisableChapter = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableSubs変更通知プロパティ
        public bool DisableSubs {
            get { return Model.DisableSubs; }
            set {
                if (Model.DisableSubs == value)
                    return;
                Model.DisableSubs = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IgnoreNoDrcsMap変更通知プロパティ
        public bool IgnoreNoDrcsMap {
            get { return Model.IgnoreNoDrcsMap; }
            set {
                if (Model.IgnoreNoDrcsMap == value)
                    return;
                Model.IgnoreNoDrcsMap = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableNicoJK変更通知プロパティ
        public bool EnableNicoJK {
            get { return Model.EnableNicoJK; }
            set {
                if (Model.EnableNicoJK == value)
                    return;
                Model.EnableNicoJK = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IgnoreNicoJKError変更通知プロパティ
        public bool IgnoreNicoJKError {
            get { return Model.IgnoreNicoJKError; }
            set {
                if (Model.IgnoreNicoJKError == value)
                    return;
                Model.IgnoreNicoJKError = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJK18変更通知プロパティ
        public bool NicoJK18 {
            get { return Model.NicoJK18; }
            set {
                if (Model.NicoJK18 == value)
                    return;
                Model.NicoJK18 = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat720S変更通知プロパティ
        public bool NicoJKFormat720S {
            get { return Model.NicoJKFormats[0]; }
            set {
                if (Model.NicoJKFormats[0] == value)
                    return;
                Model.NicoJKFormats[0] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat720T変更通知プロパティ
        public bool NicoJKFormat720T {
            get { return Model.NicoJKFormats[1]; }
            set {
                if (Model.NicoJKFormats[1] == value)
                    return;
                Model.NicoJKFormats[1] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat1080S変更通知プロパティ
        public bool NicoJKFormat1080S {
            get { return Model.NicoJKFormats[2]; }
            set {
                if (Model.NicoJKFormats[2] == value)
                    return;
                Model.NicoJKFormats[2] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat1080T変更通知プロパティ
        public bool NicoJKFormat1080T {
            get { return Model.NicoJKFormats[3]; }
            set {
                if (Model.NicoJKFormats[3] == value)
                    return;
                Model.NicoJKFormats[3] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NoDelogo変更通知プロパティ
        public bool NoDelogo {
            get { return Model.NoDelogo; }
            set {
                if (Model.NoDelogo == value)
                    return;
                Model.NoDelogo = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MoveEDCBFiles変更通知プロパティ
        public bool MoveEDCBFiles {
            get { return Model.MoveEDCBFiles; }
            set {
                if (Model.MoveEDCBFiles == value)
                    return;
                Model.MoveEDCBFiles = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SystemAviSynthPlugin変更通知プロパティ
        public bool SystemAviSynthPlugin {
            get { return Model.SystemAviSynthPlugin; }
            set {
                if (Model.SystemAviSynthPlugin == value)
                    return;
                Model.SystemAviSynthPlugin = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableHashCheck変更通知プロパティ
        public bool DisableHashCheck {
            get { return Model.DisableHashCheck; }
            set {
                if (Model.DisableHashCheck == value)
                    return;
                Model.DisableHashCheck = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Mpeg2DecoderInt変更通知プロパティ
        public int Mpeg2DecoderInt {
            get { return (int)Model.Mpeg2Decoder; }
            set {
                if ((int)Model.Mpeg2Decoder == value)
                    return;
                Model.Mpeg2Decoder = (DecoderType)value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region H264DecoderInt変更通知プロパティ
        public int H264DecoderInt {
            get { return (int)Model.H264Deocder; }
            set {
                if ((int)Model.H264Deocder == value)
                    return;
                Model.H264Deocder = (DecoderType)value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OutputFormatInt変更通知プロパティ
        public int OutputFormatInt {
            get { return (int)Model.OutputFormat; }
            set {
                if ((int)Model.OutputFormat == value)
                    return;
                Model.OutputFormat = (FormatType)value;
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

        #region OutputOptionIndex変更通知プロパティ
        private int _OutputOptionIndex;

        public int OutputOptionIndex {
            get { return _OutputOptionIndex; }
            set {
                if (_OutputOptionIndex == value)
                    return;
                _OutputOptionIndex = value;

                if (value >= 0 && value < OutputMasklist.Count)
                {
                    OutputMask = OutputMasklist[value];
                }

                RaisePropertyChanged();
            }
        }
        #endregion

        public string[] EncoderList {
            get { return new string[] { "x264", "x265", "QSVEnc", "NVEnc" }; }
        }
        public string[] Mpeg2DecoderList {
            get { return new string[] { "デフォルト", "QSV", "CUVID" }; }
        }
        public string[] H264DecoderList {
            get { return new string[] { "デフォルト", "QSV", "CUVID" }; }
        }
        public string[] OutputOptionList {
            get { return new string[] { "通常", "CMをカット", "本編とCMを分離", "CMのみ" }; }
        }
        private List<int> OutputMasklist = new List<int> { 1, 2, 6, 4 };
        public string[] FormatList {
            get { return new string[] { "MP4", "MKV" }; }
        }

        #region IsModified変更通知プロパティ
        private bool _IsModified;

        public bool IsModified {
            get { return _IsModified; }
            set {
                if (_IsModified == value)
                    return;
                _IsModified = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public DisplayProfile()
        {
            PropertyChanged += (s, e) =>
            {
                if (e.PropertyName != "IsModified")
                {
                    IsModified = true;
                }
            };
        }

        private void UpdateWarningText()
        {
            StringBuilder sb = new StringBuilder();
            if (Model.EncoderType == EncoderType.QSVEnc && Model.TwoPass)
            {
                sb.Append("QSVEncは2パスに対応していません\r\n");
            }
            if (Model.EncoderType == EncoderType.NVEnc && Model.TwoPass)
            {
                sb.Append("NVEncは2パスに対応していません\r\n");
            }
            if (Model.EnableNicoJK && Model.NicoJKFormats.Any(s => s) == false)
            {
                sb.Append("ニコニコ実況コメントのフォーマットが１つも選択されていません。選択がない場合、出力されません\r\n");
            }
            SettingWarningText = sb.ToString();
        }

        private void UpdateBitrate()
        {
            RaisePropertyChanged("Bitrate18MPEG2");
            RaisePropertyChanged("Bitrate12MPEG2");
            RaisePropertyChanged("Bitrate7MPEG2");
            RaisePropertyChanged("Bitrate18H264");
            RaisePropertyChanged("Bitrate12H264");
            RaisePropertyChanged("Bitrate7H264");
        }

        private double CalcBitrate(double src, int encoder)
        {
            double baseBitRate = BitrateA * src + BitrateB;
            switch (encoder)
            {
                case 0:
                    return baseBitRate;
                case 1:
                    return baseBitRate * BitrateH264;
            }
            return 0;
        }

        private string BitrateString(double src, int encoder)
        {
            return ((int)CalcBitrate(src, encoder)).ToString() + "Kbps";
        }

        public string Bitrate18MPEG2 { get { return BitrateString(18000, 0); } }
        public string Bitrate12MPEG2 { get { return BitrateString(12000, 0); } }
        public string Bitrate7MPEG2 { get { return BitrateString(7000, 0); } }
        public string Bitrate18H264 { get { return BitrateString(18000, 1); } }
        public string Bitrate12H264 { get { return BitrateString(12000, 1); } }
        public string Bitrate7H264 { get { return BitrateString(7000, 1); } }

        public override string ToString()
        {
            return Model.Name;
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

        public DisplayLogo[] LogoList {
            get {
                if (_LogoList == null)
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
                    foreach (var logo in _LogoList)
                    {
                        if (logo.Setting.FileName != LogoSetting.NO_LOGO)
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
        public string JlsCommandFile {
            get { return Data.JLSCommand; }
            set {
                if (Data.JLSCommand == value)
                    return;
                Data.JLSCommand = value;
                RaisePropertyChanged();
                Model.UpdateService(this);
            }
        }
        #endregion

        #region JlsArgs変更通知プロパティ
        public string JLSOption {
            get { return Data.JLSOption; }
            set {
                if (Data.JLSOption == value)
                    return;
                Data.JLSOption = value;
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
        public bool Enabled {
            get { return Setting.Enabled; }
            set {
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

        public LogoData Data {
            get { return _Data; }
            set {
                if (_Data == value)
                    return;
                _Data = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region From変更通知プロパティ
        private DateTime _From;

        public DateTime From {
            get { return _From; }
            set {
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

        public DateTime To {
            get { return _To; }
            set {
                if (_To == value)
                    return;
                _To = value;
                RaisePropertyChanged();
                RaisePropertyChanged("DateChanged");
            }
        }
        #endregion

        #region DateChanged変更通知プロパティ
        public bool DateChanged {
            get { return Setting.From != _From || Setting.To != _To; }
        }
        #endregion

        public string ToDateString {
            get { return Setting.To.ToString("yyyy/MM/dd"); }
        }

        public string FromDateString {
            get { return Setting.From.ToString("yyyy/MM/dd"); }
        }

        public void ApplyDate()
        {
            Setting.From = _From;
            Setting.To = _To;
            Model.UpdateLogo(this);
        }
    }

    public class DisplaySetting : NotificationObject
    {
        public Setting Model { get; set; }

        #region WorkPath変更通知プロパティ
        public string WorkPath {
            get { return Model.WorkPath; }
            set {
                if (Model.WorkPath == value)
                    return;
                Model.WorkPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NumParallel変更通知プロパティ
        public int NumParallel {
            get { return Model.NumParallel; }
            set {
                if (Model.NumParallel == value)
                    return;
                Model.NumParallel = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AmatsukazePath変更通知プロパティ
        public string AmatsukazePath {
            get { return Model.AmatsukazePath; }
            set {
                if (Model.AmatsukazePath == value)
                    return;
                Model.AmatsukazePath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region X264Path変更通知プロパティ
        public string X264Path {
            get { return Model.X264Path; }
            set {
                if (Model.X264Path == value)
                    return;
                Model.X264Path = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region X265Path変更通知プロパティ
        public string X265Path {
            get { return Model.X265Path; }
            set {
                if (Model.X265Path == value)
                    return;
                Model.X265Path = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region QSVEncPath変更通知プロパティ
        public string QSVEncPath {
            get { return Model.QSVEncPath; }
            set {
                if (Model.QSVEncPath == value)
                    return;
                Model.QSVEncPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NVEncPath変更通知プロパティ
        public string NVEncPath {
            get { return Model.NVEncPath; }
            set {
                if (Model.NVEncPath == value)
                    return;
                Model.NVEncPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MuxerPath変更通知プロパティ
        public string MuxerPath {
            get { return Model.MuxerPath; }
            set {
                if (Model.MuxerPath == value)
                    return;
                Model.MuxerPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MKVMergePath変更通知プロパティ
        public string MKVMergePath {
            get { return Model.MKVMergePath; }
            set {
                if (Model.MKVMergePath == value)
                    return;
                Model.MKVMergePath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region TimelineEditorPath変更通知プロパティ
        public string TimelineEditorPath {
            get { return Model.TimelineEditorPath; }
            set {
                if (Model.TimelineEditorPath == value)
                    return;
                Model.TimelineEditorPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MP4BoxPath変更通知プロパティ
        public string MP4BoxPath {
            get { return Model.MP4BoxPath; }
            set {
                if (Model.MP4BoxPath == value)
                    return;
                Model.MP4BoxPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ChapterExePath変更通知プロパティ
        public string ChapterExePath {
            get { return Model.ChapterExePath; }
            set {
                if (Model.ChapterExePath == value)
                    return;
                Model.ChapterExePath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region JoinLogoScpPath変更通知プロパティ
        public string JoinLogoScpPath {
            get { return Model.JoinLogoScpPath; }
            set {
                if (Model.JoinLogoScpPath == value)
                    return;
                Model.JoinLogoScpPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoConvASSPath変更通知プロパティ
        public string NicoConvASSPath {
            get { return Model.NicoConvASSPath; }
            set {
                if (Model.NicoConvASSPath == value)
                    return;
                Model.NicoConvASSPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AlwaysShowDisk変更通知プロパティ
        public string AlwaysShowDisk {
            get { return Model.AlwaysShowDisk; }
            set {
                if (Model.AlwaysShowDisk == value)
                    return;
                Model.AlwaysShowDisk = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ClearWorkDirOnStart変更通知プロパティ
        public bool ClearWorkDirOnStart {
            get { return Model.ClearWorkDirOnStart; }
            set {
                if (Model.ClearWorkDirOnStart == value)
                    return;
                Model.ClearWorkDirOnStart = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region HideOneSeg変更通知プロパティ
        public bool HideOneSeg {
            get { return Model.HideOneSeg; }
            set {
                if (Model.HideOneSeg == value)
                    return;
                Model.HideOneSeg = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SupressSleep変更通知プロパティ
        public bool SupressSleep {
            get { return Model.SupressSleep; }
            set { 
                if (Model.SupressSleep == value)
                    return;
                Model.SupressSleep = value;
                RaisePropertyChanged();
            }
        }
        #endregion

    }

    public class DisplayMakeScriptData : NotificationObject
    {
        public MakeScriptData Model { get; set; }

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

        #region OutDir変更通知プロパティ
        public string OutDir {
            get { return Model.OutDir; }
            set {
                if (Model.OutDir == value)
                    return;
                Model.OutDir = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NasDir変更通知プロパティ
        public string NasDir {
            get { return Model.NasDir; }
            set {
                if (Model.NasDir == value)
                    return;
                Model.NasDir = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsNasEnabled変更通知プロパティ
        public bool IsNasEnabled {
            get { return Model.IsNasEnabled; }
            set {
                if (Model.IsNasEnabled == value)
                    return;
                Model.IsNasEnabled = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsWakeOnLan変更通知プロパティ
        public bool IsWakeOnLan {
            get { return Model.IsWakeOnLan; }
            set {
                if (Model.IsWakeOnLan == value)
                    return;
                Model.IsWakeOnLan = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MoveAfter変更通知プロパティ
        public bool MoveAfter {
            get { return Model.MoveAfter; }
            set { 
                if (Model.MoveAfter == value)
                    return;
                Model.MoveAfter = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ClearEncoded変更通知プロパティ
        public bool ClearSucceeded {
            get { return Model.ClearEncoded; }
            set { 
                if (Model.ClearEncoded == value)
                    return;
                Model.ClearEncoded = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region WithRelated変更通知プロパティ
        public bool WithRelated {
            get { return Model.WithRelated; }
            set { 
                if (Model.WithRelated == value)
                    return;
                Model.WithRelated = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsDirect変更通知プロパティ
        public bool IsDirect {
            get { return Model.IsDirect; }
            set { 
                if (Model.IsDirect == value)
                    return;
                Model.IsDirect = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Priority変更通知プロパティ
        public int Priority {
            get { return Model.Priority; }
            set { 
                if (Model.Priority == value)
                    return;
                Model.Priority = value;
                RaisePropertyChanged();
            }
        }
        #endregion

    }
}
