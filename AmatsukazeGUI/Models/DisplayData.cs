using Amatsukaze.Components;
using Amatsukaze.Server;
using Livet;
using Livet.Commands;
using Livet.EventListeners;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Data;

namespace Amatsukaze.Models
{
    // nullを値として使いたい場合、WPFだとコンボボックスで選択できなかったり等、
    // 通常の値とは挙動が変わってしまうので、null用のオブジェクトを定義
    public class NullValue {
        public static readonly NullValue Value = new NullValue();
        public static readonly NullValue[] Array = new NullValue[] { Value };

        public override bool Equals(object obj)
        {
            // どのNullValueオブジェクトも同じ
            return obj is NullValue;
        }
        public override int GetHashCode()
        {
            return 0;
        }
    }

    public class DisplayQueueDirectory : NotificationObject
    {
        private static readonly string TIME_FORMAT = "yyyy/MM/dd HH:mm:ss";

        public int Id;

        public string Path { get; set; }
        public ObservableCollection<DisplayQueueItem> Items { get; set; }
        public ProcMode Mode { get; set; }
        public string Profile { get; set; }
        public string ProfileLastUpdate { get; set; }

        private Components.CollectionItemListener<DisplayQueueItem> itemListener;

        #region IsSelected変更通知プロパティ
        private bool _IsSelected;

        public bool IsSelected {
            get { return _IsSelected; }
            set { 
                if (_IsSelected == value)
                    return;
                _IsSelected = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public string LastAdd
        {
            get
            {
                if (Items.Count == 0) return "";
                return Items.Max(s => s.Model.AddTime).ToString(TIME_FORMAT);
            }
        }

        public int Active { get { return Items.Count(s => s.Model.IsActive); } }
        public int Encoding { get { return Items.Count(s => s.Model.State == QueueState.Encoding); } }
        public int Complete { get { return Items.Count(s => s.Model.State == QueueState.Complete); } }
        public int Pending { get { return Items.Count(s => s.Model.State == QueueState.LogoPending); } }
        public int Fail { get { return Items.Count(s => s.Model.State == QueueState.Failed); } }

        public void ItemStateUpdated()
        {
            RaisePropertyChanged("LastAdd");
            RaisePropertyChanged("Active");
            RaisePropertyChanged("Encoding");
            RaisePropertyChanged("Complete");
            RaisePropertyChanged("Pending");
            RaisePropertyChanged("Fail");
        }

        private void ItemPropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == "IsSelected")
            {
                RaisePropertyChanged("IsQueueFileSelected");
            }
        }

        public bool IsQueueFileSelected
        {
            get { return Items.Any(s => s.IsSelected); }
        }

        public string ModeString {
            get {
                switch (Mode)
                {
                    case ProcMode.AutoBatch:
                        return "自動追加";
                    case ProcMode.Batch:
                        return "通常";
                    case ProcMode.Test:
                        return "テスト";
                    case ProcMode.DrcsCheck:
                        return "DRCSチェック";
                    case ProcMode.CMCheck:
                        return "CM解析";
                }
                return "不明モード";
            }
        }

        public DisplayQueueDirectory(QueueDirectory dir, ClientModel Parent)
        {
            Id = dir.Id;
            Path = dir.DirPath;
            Mode = dir.Mode;
            Profile = dir.Profile.Name;
            ProfileLastUpdate = dir.Profile.LastUpdate.ToString(TIME_FORMAT);

            Items = new ObservableCollection<DisplayQueueItem>();
            itemListener = new Components.CollectionItemListener<DisplayQueueItem>(Items,
                item => item.PropertyChanged += ItemPropertyChanged,
                item => item.PropertyChanged -= ItemPropertyChanged);
            foreach(var item in dir.Items.Select(s => new DisplayQueueItem() { Parent = Parent, Model = s, Dir = this }))
            {
                Items.Add(item);
            }
        }
    }

    public class DisplayQueueItem : NotificationObject
    {
        public ClientModel Parent { get; set; }
        public QueueItem Model { get; set; }
        public DisplayQueueDirectory Dir { get; set; }

        public bool IsComplete { get { return Model.State == QueueState.Complete; } }
        public bool IsEncoding { get { return Model.State == QueueState.Encoding; } }
        public bool IsError { get { return Model.State == QueueState.Failed || Model.State == QueueState.PreFailed; } }
        public bool IsPending { get { return Model.State == QueueState.LogoPending; } }
        public bool IsPreFailed { get { return Model.State == QueueState.PreFailed; } }
        public bool IsCanceled { get { return Model.State == QueueState.Canceled; } }
        public bool IsTooSmall { get { return IsPreFailed && Model.FailReason.Contains("映像が小さすぎます"); } }
        public string TsTimeString { get { return Model.TsTime.ToString("yyyy年MM月dd日"); } }
        public string ServiceString { get { return Model.ServiceName + "(" + Model.ServiceId + ")"; } }

        public string GenreString
        {
            get { return string.Join(", ", Model.Genre.Select(s => SubGenre.GetFromItem(s).FullName)); }
        }

        public string VideoSizeString
        {
            get { return Model.ImageWidth + "x" + Model.ImageHeight; }
        }

        public string StateString
        {
            get
            {
                switch (Model.State)
                {
                    case QueueState.Queue: return "待ち";
                    case QueueState.Encoding:
                        switch (Dir.Mode)
                        {
                            case ProcMode.CMCheck:
                                return "CM解析中";
                            case ProcMode.DrcsCheck:
                                return "DRCSチェック中";
                        }
                        return "エンコード中";
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
        public int Priority
        {
            get { return Model.Priority; }
            set
            {
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

        #region IsSelected変更通知プロパティ
        private bool _IsSelected;

        public bool IsSelected {
            get { return _IsSelected; }
            set { 
                if (_IsSelected == value)
                    return;
                _IsSelected = value;
                RaisePropertyChanged();
            }
        }
        #endregion

    }

    public class DisplayOutputMask : NotificationObject
    {
        public string Name { get; set; }
        public int Mask { get; set; }

        public override string ToString()
        {
            return Name;
        }
    }

    public class DisplayProfile : NotificationObject
    {
        public ProfileSetting Model { get; set; }

        #region EncoderTypeInt変更通知プロパティ
        public int EncoderTypeInt
        {
            get { return (int)Model.EncoderType; }
            set
            {
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
        public string EncoderOption
        {
            get
            {
                switch (Model.EncoderType)
                {
                    case EncoderType.x264: return Model.X264Option;
                    case EncoderType.x265: return Model.X265Option;
                    case EncoderType.QSVEnc: return Model.QSVEncOption;
                    case EncoderType.NVEnc: return Model.NVEncOption;
                }
                return null;
            }
            set
            {
                switch (Model.EncoderType)
                {
                    case EncoderType.x264:
                        if (Model.X264Option == value)
                            return;
                        Model.X264Option = value;
                        break;
                    case EncoderType.x265:
                        if (Model.X265Option == value)
                            return;
                        Model.X265Option = value;
                        break;
                    case EncoderType.QSVEnc:
                        if (Model.QSVEncOption == value)
                            return;
                        Model.QSVEncOption = value;
                        break;
                    case EncoderType.NVEnc:
                        if (Model.NVEncOption == value)
                            return;
                        Model.NVEncOption = value;
                        break;
                    default:
                        return;
                }
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region FilterPath変更通知プロパティ
        public string FilterPath
        {
            get { return string.IsNullOrEmpty(Model.FilterPath) ? "フィルタなし" : Model.FilterPath; }
            set
            {
                string val = (value == "フィルタなし") ? "" : value;
                if (Model.FilterPath == val)
                    return;
                Model.FilterPath = val;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PostFilterPath変更通知プロパティ
        public string PostFilterPath
        {
            get { return string.IsNullOrEmpty(Model.PostFilterPath) ? "フィルタなし" : Model.PostFilterPath; }
            set
            {
                string val = (value == "フィルタなし") ? "" : value;
                if (Model.PostFilterPath == val)
                    return;
                Model.PostFilterPath = val;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AutoBuffer変更通知プロパティ
        public bool AutoBuffer
        {
            get { return Model.AutoBuffer; }
            set
            {
                if (Model.AutoBuffer == value)
                    return;
                Model.AutoBuffer = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region TwoPass変更通知プロパティ
        public bool TwoPass
        {
            get { return Model.TwoPass; }
            set
            {
                if (Model.TwoPass == value)
                    return;
                Model.TwoPass = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SplitSub変更通知プロパティ
        public bool SplitSub
        {
            get { return Model.SplitSub; }
            set
            {
                if (Model.SplitSub == value)
                    return;
                Model.SplitSub = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OutputMask変更通知プロパティ
        public DisplayOutputMask OutputMask
        {
            get { return 
                    OutputOptionList_.FirstOrDefault(s => s.Mask == Model.OutputMask)
                    ?? OutputOptionList_[0]; }
            set
            {
                if (Model.OutputMask == value?.Mask)
                    return;
                Model.OutputMask = value?.Mask ?? OutputOptionList_[0].Mask;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateA変更通知プロパティ
        public double BitrateA
        {
            get { return Model.Bitrate.A; }
            set
            {
                if (Model.Bitrate.A == value)
                    return;
                Model.Bitrate.A = value;
                UpdateBitrate();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateB変更通知プロパティ
        public double BitrateB
        {
            get { return Model.Bitrate.B; }
            set
            {
                if (Model.Bitrate.B == value)
                    return;
                Model.Bitrate.B = value;
                UpdateBitrate();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateH264変更通知プロパティ
        public double BitrateH264
        {
            get { return Model.Bitrate.H264; }
            set
            {
                if (Model.Bitrate.H264 == value)
                    return;
                Model.Bitrate.H264 = value;
                UpdateBitrate();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateCM変更通知プロパティ
        public double BitrateCM
        {
            get { return Model.BitrateCM; }
            set
            {
                if (Model.BitrateCM == value)
                    return;
                Model.BitrateCM = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region JLSCommandFile変更通知プロパティ
        public object JLSCommandFile
        {
            get {
                return (object)Model.JLSCommandFile ?? NullValue.Value;
            }
            set
            {
                var newValue = (value is NullValue) ? null : (value as string);
                if (Model.JLSCommandFile == newValue)
                    return;
                Model.JLSCommandFile = newValue;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region JLSOption変更通知プロパティ
        public string JLSOption {
            get { return Model.JLSOption; }
            set { 
                if (Model.JLSOption == value)
                    return;
                Model.JLSOption = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableJLSOption変更通知プロパティ
        public bool EnableJLSOption {
            get { return Model.EnableJLSOption; }
            set { 
                if (Model.EnableJLSOption == value)
                    return;
                Model.EnableJLSOption = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableChapter変更通知プロパティ
        public bool DisableChapter
        {
            get { return Model.DisableChapter; }
            set
            {
                if (Model.DisableChapter == value)
                    return;
                Model.DisableChapter = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableSubs変更通知プロパティ
        public bool DisableSubs
        {
            get { return Model.DisableSubs; }
            set
            {
                if (Model.DisableSubs == value)
                    return;
                Model.DisableSubs = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IgnoreNoDrcsMap変更通知プロパティ
        public bool IgnoreNoDrcsMap
        {
            get { return Model.IgnoreNoDrcsMap; }
            set
            {
                if (Model.IgnoreNoDrcsMap == value)
                    return;
                Model.IgnoreNoDrcsMap = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableNicoJK変更通知プロパティ
        public bool EnableNicoJK
        {
            get { return Model.EnableNicoJK; }
            set
            {
                if (Model.EnableNicoJK == value)
                    return;
                Model.EnableNicoJK = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IgnoreNicoJKError変更通知プロパティ
        public bool IgnoreNicoJKError
        {
            get { return Model.IgnoreNicoJKError; }
            set
            {
                if (Model.IgnoreNicoJKError == value)
                    return;
                Model.IgnoreNicoJKError = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJK18変更通知プロパティ
        public bool NicoJK18
        {
            get { return Model.NicoJK18; }
            set
            {
                if (Model.NicoJK18 == value)
                    return;
                Model.NicoJK18 = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat720S変更通知プロパティ
        public bool NicoJKFormat720S
        {
            get { return Model.NicoJKFormats[0]; }
            set
            {
                if (Model.NicoJKFormats[0] == value)
                    return;
                Model.NicoJKFormats[0] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat720T変更通知プロパティ
        public bool NicoJKFormat720T
        {
            get { return Model.NicoJKFormats[1]; }
            set
            {
                if (Model.NicoJKFormats[1] == value)
                    return;
                Model.NicoJKFormats[1] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat1080S変更通知プロパティ
        public bool NicoJKFormat1080S
        {
            get { return Model.NicoJKFormats[2]; }
            set
            {
                if (Model.NicoJKFormats[2] == value)
                    return;
                Model.NicoJKFormats[2] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat1080T変更通知プロパティ
        public bool NicoJKFormat1080T
        {
            get { return Model.NicoJKFormats[3]; }
            set
            {
                if (Model.NicoJKFormats[3] == value)
                    return;
                Model.NicoJKFormats[3] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LooseLogoDetection変更通知プロパティ
        public bool LooseLogoDetection
        {
            get { return Model.LooseLogoDetection; }
            set
            {
                if (Model.LooseLogoDetection == value)
                    return;
                Model.LooseLogoDetection = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IgnoreNoLogo変更通知プロパティ
        public bool IgnoreNoLogo {
            get { return Model.IgnoreNoLogo; }
            set { 
                if (Model.IgnoreNoLogo == value)
                    return;
                Model.IgnoreNoLogo = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NoDelogo変更通知プロパティ
        public bool NoDelogo
        {
            get { return Model.NoDelogo; }
            set
            {
                if (Model.NoDelogo == value)
                    return;
                Model.NoDelogo = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region VFRFps変更通知プロパティ
        public int VFRFps {
            get { return Model.VFR120fps ? 1 : 0; }
            set {
                bool newValue = (value == 1);
                if (Model.VFR120fps == newValue)
                    return;
                Model.VFR120fps = newValue;
                RaisePropertyChanged("VFRFps");
                RaisePropertyChanged("VFR120Fps");
            }
        }
        public bool VFR120Fps {
            get { return Model.VFR120fps; }
            set {
                if (Model.VFR120fps == value)
                    return;
                Model.VFR120fps = value;
                RaisePropertyChanged("VFRFps");
                RaisePropertyChanged("VFR120Fps");
            }
        }
        #endregion

        #region MoveEDCBFiles変更通知プロパティ
        public bool MoveEDCBFiles
        {
            get { return Model.MoveEDCBFiles; }
            set
            {
                if (Model.MoveEDCBFiles == value)
                    return;
                Model.MoveEDCBFiles = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableRename変更通知プロパティ
        public bool EnableRename {
            get { return Model.EnableRename; }
            set { 
                if (Model.EnableRename == value)
                    return;
                Model.EnableRename = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region RenameFormat変更通知プロパティ
        public string RenameFormat {
            get { return Model.RenameFormat; }
            set {
                if (Model.RenameFormat == value)
                    return;
                Model.RenameFormat = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableGunreFolder変更通知プロパティ
        public bool EnableGunreFolder {
            get { return Model.EnableGunreFolder; }
            set { 
                if (Model.EnableGunreFolder == value)
                    return;
                Model.EnableGunreFolder = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SystemAviSynthPlugin変更通知プロパティ
        public bool SystemAviSynthPlugin
        {
            get { return Model.SystemAviSynthPlugin; }
            set
            {
                if (Model.SystemAviSynthPlugin == value)
                    return;
                Model.SystemAviSynthPlugin = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableHashCheck変更通知プロパティ
        public bool DisableHashCheck
        {
            get { return Model.DisableHashCheck; }
            set
            {
                if (Model.DisableHashCheck == value)
                    return;
                Model.DisableHashCheck = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NoRemoveTmp変更通知プロパティ
        public bool NoRemoveTmp {
            get { return Model.NoRemoveTmp; }
            set { 
                if (Model.NoRemoveTmp == value)
                    return;
                Model.NoRemoveTmp = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MoveLogFile変更通知プロパティ
        public bool MoveLogFile
        {
            get { return Model.DisableLogFile; }
            set
            {
                if (Model.DisableLogFile == value)
                    return;
                Model.DisableLogFile = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Mpeg2DecoderInt変更通知プロパティ
        public int Mpeg2DecoderInt
        {
            get { return (int)Model.Mpeg2Decoder; }
            set
            {
                if ((int)Model.Mpeg2Decoder == value)
                    return;
                Model.Mpeg2Decoder = (DecoderType)value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region H264DecoderInt変更通知プロパティ
        public int H264DecoderInt
        {
            get { return (int)Model.H264Deocder; }
            set
            {
                if ((int)Model.H264Deocder == value)
                    return;
                Model.H264Deocder = (DecoderType)value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OutputFormatInt変更通知プロパティ
        public int OutputFormatInt
        {
            get { return (int)Model.OutputFormat; }
            set
            {
                if ((int)Model.OutputFormat == value)
                    return;
                Model.OutputFormat = (FormatType)value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SettingWarningText変更通知プロパティ
        private string _SettingWarningText;

        public string SettingWarningText
        {
            get { return _SettingWarningText; }
            set
            {
                if (_SettingWarningText == value)
                    return;
                _SettingWarningText = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public string[] EncoderList
        {
            get { return new string[] { "x264", "x265", "QSVEnc", "NVEnc" }; }
        }
        public string[] Mpeg2DecoderList
        {
            get { return new string[] { "デフォルト", "QSV", "CUVID" }; }
        }
        public string[] H264DecoderList
        {
            get { return new string[] { "デフォルト", "QSV", "CUVID" }; }
        }
        public DisplayOutputMask[] OutputOptionList_ = new DisplayOutputMask[]
        {
            new DisplayOutputMask()
            {
                Name = "通常", Mask = 1
            },
            new DisplayOutputMask()
            {
                Name = "CMをカット", Mask = 2
            },
            new DisplayOutputMask()
            {
                Name = "本編とCMを分離", Mask = 6
            },
            new DisplayOutputMask()
            {
                Name = "CMのみ", Mask = 4
            }
        };
        public DisplayOutputMask[] OutputOptionList { get { return OutputOptionList_; } }
        public string[] FormatList
        {
            get { return new string[] { "MP4", "MKV", "M2TS" }; }
        }
        public string[] VFRFpsList {
            get { return new string[] { "60fps", "120fps" }; }
        }

        #region IsModified変更通知プロパティ
        private bool _IsModified;

        public bool IsModified
        {
            get { return _IsModified; }
            set
            {
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

        public void UpdateWarningText()
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
            if(Model.OutputFormat == FormatType.M2TS)
            {
                if (Model.EncoderType == EncoderType.x265 ||
                    ((Model.EncoderType != EncoderType.x264) && (EncoderOption?.Contains("hevc") ?? false)))
                {
                    sb.Append("tsMuxeR 2.6.12はHEVCを正しくmuxできない不具合があるのでご注意ください。");
                }
            }
            SettingWarningText = sb.ToString();
        }

        public void SetEncoderOptions(string X264Option, string X265Option, string QSVEncOption, string NVEncOption)
        {
            if (X264Option != Model.X264Option || X265Option != Model.X265Option ||
                QSVEncOption != Model.QSVEncOption || NVEncOption != Model.NVEncOption)
            {
                Model.X264Option = X264Option;
                Model.X265Option = X265Option;
                Model.QSVEncOption = QSVEncOption;
                Model.NVEncOption = NVEncOption;
                RaisePropertyChanged("EncoderOption");
            }
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

        public static string GetProfileName(object item)
        {
            var name = (item as DisplayProfile)?.Model?.Name ??
                (item as DisplayAutoSelect)?.Model?.Name;
            if (item is DisplayAutoSelect)
            {
                name = ServerSupport.AUTO_PREFIX + name;
            }
            return name;
        }
    }

    public class DisplayService : NotificationObject
    {
        public ClientModel Model { get; set; }

        #region Data変更通知プロパティ
        private ServiceSettingElement _Data;

        public ServiceSettingElement Data
        {
            get { return _Data; }
            set
            {
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
        public string JlsCommandFile
        {
            get { return Data.JLSCommand; }
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

        #region JlsArgs変更通知プロパティ
        public string JLSOption
        {
            get { return Data.JLSOption; }
            set
            {
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
        public bool Enabled
        {
            get { return Setting.Enabled; }
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
            get { return _Data; }
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
            get { return _From; }
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
            get { return _To; }
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

    public class DisplaySetting : NotificationObject
    {
        public Setting Model { get; set; }

        #region WorkPath変更通知プロパティ
        public string WorkPath
        {
            get { return Model.WorkPath; }
            set
            {
                if (Model.WorkPath == value)
                    return;
                Model.WorkPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NumParallel変更通知プロパティ
        public int NumParallel
        {
            get { return Model.NumParallel; }
            set
            {
                if (Model.NumParallel == value)
                    return;
                Model.NumParallel = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AmatsukazePath変更通知プロパティ
        public string AmatsukazePath
        {
            get { return Model.AmatsukazePath; }
            set
            {
                if (Model.AmatsukazePath == value)
                    return;
                Model.AmatsukazePath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region X264Path変更通知プロパティ
        public string X264Path
        {
            get { return Model.X264Path; }
            set
            {
                if (Model.X264Path == value)
                    return;
                Model.X264Path = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region X265Path変更通知プロパティ
        public string X265Path
        {
            get { return Model.X265Path; }
            set
            {
                if (Model.X265Path == value)
                    return;
                Model.X265Path = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region QSVEncPath変更通知プロパティ
        public string QSVEncPath
        {
            get { return Model.QSVEncPath; }
            set
            {
                if (Model.QSVEncPath == value)
                    return;
                Model.QSVEncPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NVEncPath変更通知プロパティ
        public string NVEncPath
        {
            get { return Model.NVEncPath; }
            set
            {
                if (Model.NVEncPath == value)
                    return;
                Model.NVEncPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MuxerPath変更通知プロパティ
        public string MuxerPath
        {
            get { return Model.MuxerPath; }
            set
            {
                if (Model.MuxerPath == value)
                    return;
                Model.MuxerPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MKVMergePath変更通知プロパティ
        public string MKVMergePath
        {
            get { return Model.MKVMergePath; }
            set
            {
                if (Model.MKVMergePath == value)
                    return;
                Model.MKVMergePath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region TimelineEditorPath変更通知プロパティ
        public string TimelineEditorPath
        {
            get { return Model.TimelineEditorPath; }
            set
            {
                if (Model.TimelineEditorPath == value)
                    return;
                Model.TimelineEditorPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MP4BoxPath変更通知プロパティ
        public string MP4BoxPath
        {
            get { return Model.MP4BoxPath; }
            set
            {
                if (Model.MP4BoxPath == value)
                    return;
                Model.MP4BoxPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ChapterExePath変更通知プロパティ
        public string ChapterExePath
        {
            get { return Model.ChapterExePath; }
            set
            {
                if (Model.ChapterExePath == value)
                    return;
                Model.ChapterExePath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region JoinLogoScpPath変更通知プロパティ
        public string JoinLogoScpPath
        {
            get { return Model.JoinLogoScpPath; }
            set
            {
                if (Model.JoinLogoScpPath == value)
                    return;
                Model.JoinLogoScpPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoConvASSPath変更通知プロパティ
        public string NicoConvASSPath
        {
            get { return Model.NicoConvASSPath; }
            set
            {
                if (Model.NicoConvASSPath == value)
                    return;
                Model.NicoConvASSPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region TsMuxeRPath変更通知プロパティ
        public string TsMuxeRPath
        {
            get { return Model.TsMuxeRPath; }
            set
            {
                if (Model.TsMuxeRPath == value)
                    return;
                Model.TsMuxeRPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SCRenamePath変更通知プロパティ
        public string SCRenamePath {
            get { return Model.SCRenamePath; }
            set { 
                if (Model.SCRenamePath == value)
                    return;
                Model.SCRenamePath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AlwaysShowDisk変更通知プロパティ
        public string AlwaysShowDisk
        {
            get { return Model.AlwaysShowDisk; }
            set
            {
                if (Model.AlwaysShowDisk == value)
                    return;
                Model.AlwaysShowDisk = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ClearWorkDirOnStart変更通知プロパティ
        public bool ClearWorkDirOnStart
        {
            get { return Model.ClearWorkDirOnStart; }
            set
            {
                if (Model.ClearWorkDirOnStart == value)
                    return;
                Model.ClearWorkDirOnStart = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region HideOneSeg変更通知プロパティ
        public bool HideOneSeg
        {
            get { return Model.HideOneSeg; }
            set
            {
                if (Model.HideOneSeg == value)
                    return;
                Model.HideOneSeg = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SupressSleep変更通知プロパティ
        public bool SupressSleep
        {
            get { return Model.SupressSleep; }
            set
            {
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
        private object _SelectedProfile;

        public object SelectedProfile
        {
            get { return _SelectedProfile; }
            set
            {
                if (_SelectedProfile == value)
                    return;
                _SelectedProfile = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OutDir変更通知プロパティ
        public string OutDir
        {
            get { return Model.OutDir; }
            set
            {
                if (Model.OutDir == value)
                    return;
                Model.OutDir = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NasDir変更通知プロパティ
        public string NasDir
        {
            get { return Model.NasDir; }
            set
            {
                if (Model.NasDir == value)
                    return;
                Model.NasDir = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsNasEnabled変更通知プロパティ
        public bool IsNasEnabled
        {
            get { return Model.IsNasEnabled; }
            set
            {
                if (Model.IsNasEnabled == value)
                    return;
                Model.IsNasEnabled = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsWakeOnLan変更通知プロパティ
        public bool IsWakeOnLan
        {
            get { return Model.IsWakeOnLan; }
            set
            {
                if (Model.IsWakeOnLan == value)
                    return;
                Model.IsWakeOnLan = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MoveAfter変更通知プロパティ
        public bool MoveAfter
        {
            get { return Model.MoveAfter; }
            set
            {
                if (Model.MoveAfter == value)
                    return;
                Model.MoveAfter = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ClearEncoded変更通知プロパティ
        public bool ClearSucceeded
        {
            get { return Model.ClearEncoded; }
            set
            {
                if (Model.ClearEncoded == value)
                    return;
                Model.ClearEncoded = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region WithRelated変更通知プロパティ
        public bool WithRelated
        {
            get { return Model.WithRelated; }
            set
            {
                if (Model.WithRelated == value)
                    return;
                Model.WithRelated = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsDirect変更通知プロパティ
        public bool IsDirect
        {
            get { return Model.IsDirect; }
            set
            {
                if (Model.IsDirect == value)
                    return;
                Model.IsDirect = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Priority変更通知プロパティ
        public int Priority
        {
            get { return Model.Priority; }
            set
            {
                if (Model.Priority == value)
                    return;
                Model.Priority = value;
                RaisePropertyChanged();
            }
        }
        #endregion

    }

    public class GenreSelectItem : NotificationObject
    {
        public SubGenre Item { get; set; }
        public MainGenreSelectItem MainGenre { get; set; }
        public DisplayCondition Cond { get; set; }

        #region IsChecked変更通知プロパティ
        private bool _IsChecked;

        public bool IsChecked
        {
            get { return _IsChecked; }
            set
            {
                if (_IsChecked == value)
                    return;
                _IsChecked = value;
                MainGenre?.ChildrenUpdated();
                Cond?.ApplyCondition();
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class MainGenreSelectItem : NotificationObject
    {
        public MainGenre Item { get; set; }
        public List<GenreSelectItem> SubGenres { get; set; }
        public DisplayCondition Cond { get; set; }

        #region IsChecked変更通知プロパティ
        private bool? _IsChecked;

        public bool? IsChecked
        {
            get { return _IsChecked; }
            set
            {
                if (_IsChecked == value)
                    return;
                _IsChecked = value;
                if(value != null)
                {
                    CheckNodes(value == true);
                }
                Cond?.ApplyCondition();
                RaisePropertyChanged();
            }
        }
        #endregion

        private void CheckNodes(bool value)
        {
            foreach(var item in SubGenres)
            {
                item.IsChecked = value;
            }
        }

        public void ChildrenUpdated()
        {
            if(SubGenres.All(s => s.IsChecked))
            {
                IsChecked = true;
            }
            else if(SubGenres.All(s => !s.IsChecked))
            {
                IsChecked = false;
            }
            else
            {
                IsChecked = null;
            }
        }
    }

    public class ServiceSelectItem : NotificationObject
    {
        public DisplayService Service { get; set; }
        public DisplayCondition Cond { get; set; }

        #region IsChecked変更通知プロパティ
        private bool _IsChecked;

        public bool IsChecked
        {
            get { return _IsChecked; }
            set
            {
                if (_IsChecked == value)
                    return;
                _IsChecked = value;
                Cond?.ApplyCondition();
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class VideoSizeSelectItem : NotificationObject
    {
        public string Name { get; set; }
        public VideoSizeCondition Item { get; set; }
        public DisplayCondition Cond { get; set; }

        #region IsChecked変更通知プロパティ
        private bool _IsChecked;

        public bool IsChecked
        {
            get { return _IsChecked; }
            set
            {
                if (_IsChecked == value)
                    return;
                _IsChecked = value;
                Cond?.ApplyCondition();
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class DisplayCondition : ViewModel
    {
        public static readonly SortedList<VideoSizeCondition, string> VIDEO_SIZE_TABLE = new SortedList<VideoSizeCondition, string>()
        {
            { VideoSizeCondition.FullHD, "1920x1080" },
            { VideoSizeCondition.HD1440, "1440x1080" },
            { VideoSizeCondition.SD, "720x480" },
            { VideoSizeCondition.OneSeg, "320x240" },
        };

        // 最初に外から与える
        public ClientModel Model { get; set; }
        public AutoSelectCondition Item { get; set; }

        // Initializeで初期化
        public List<NotificationObject> GenreItems { get; private set; }
        public ObservableCollection<ServiceSelectItem> ServiceList { get; private set; }
        public List<VideoSizeSelectItem> VideoSizes { get; private set; }

        // Item -> DisplayCondition
        public void Initialize()
        {
            GenreItems = new List<NotificationObject>();
            foreach(var mainEntry in SubGenre.GENRE_TABLE.SelectMany(s => s.Value.MainGenres))
            {
                var main = mainEntry.Value;
                var mainItem = new MainGenreSelectItem()
                {
                    Item = main,
                    SubGenres = new List<GenreSelectItem>()
                };
                GenreItems.Add(mainItem);
                foreach (var subEntry in main.SubGenres)
                {
                    var sub = subEntry.Value;
                    var subItem = new GenreSelectItem()
                    {
                        Item = sub,
                        IsChecked = Item.ContentConditions.Any(s => SubGenre.IsInclude(s, sub.Item)),
                        MainGenre = mainItem,
                        Cond = this
                    };
                    mainItem.SubGenres.Add(subItem);
                    GenreItems.Add(subItem);
                }
                mainItem.ChildrenUpdated();
                mainItem.Cond = this;
            }

            ServiceList = new ObservableViewModelCollection<ServiceSelectItem, DisplayService>(
                Model.ServiceSettings, service => new ServiceSelectItem()
                {
                    Service = service,
                    IsChecked = Item.ServiceIds.Contains(service.Data.ServiceId),
                    Cond = this
                });

            VideoSizes = new List<VideoSizeSelectItem>();
            Func<VideoSizeCondition, VideoSizeSelectItem> NewVideoSize = (item) => {
                return new VideoSizeSelectItem()
                {
                    Name = VIDEO_SIZE_TABLE[item],
                    Item = item,
                    IsChecked = Item.VideoSizes.Any(s => s == item),
                    Cond = this
                };
            };
            VideoSizes.Add(NewVideoSize(VideoSizeCondition.FullHD));
            VideoSizes.Add(NewVideoSize(VideoSizeCondition.HD1440));
            VideoSizes.Add(NewVideoSize(VideoSizeCondition.SD));

            CompositeDisposable.Add(new CollectionChangedEventListener(Model.ServiceSettings, (s, e) =>
            {
                if(ServiceEnabled)
                {
                    // サービスリストが変わったら、表示を変える必要がある可能性があるので更新しておく
                    ApplyCondition();
                }
            }));

            ApplyCondition();
        }

        #region Condition変更通知プロパティ
        private string _Condition;

        public string Condition
        {
            get { return _Condition; }
            set
            {
                if (_Condition == value)
                    return;
                _Condition = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region WarningText変更通知プロパティ
        private string _WarningText;

        public string WarningText
        {
            get { return _WarningText; }
            set
            {
                if (_WarningText == value)
                    return;
                _WarningText = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Description変更通知プロパティ
        public string Description
        {
            get { return Item.Description; }
            set
            {
                if (Item.Description == value)
                    return;
                Item.Description = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SelectedProfile変更通知プロパティ
        private object _SelectedProfile;

        public object SelectedProfile {
            get { return _SelectedProfile ?? "ペンディングにする"; }
            set { 
                if (_SelectedProfile == value)
                    return;
                _SelectedProfile = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Priority変更通知プロパティ
        public object Priority
        {
            get {
                if(Item.Priority == 0)
                {
                    return "デフォルト";
                }
                return Item.Priority;
            }
            set
            {
                int ival = (value as int?) ?? 0;
                if (Item.Priority == ival)
                    return;
                Item.Priority = ival;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region FileNameEnabled変更通知プロパティ
        public bool FileNameEnabled {
            get { return Item.FileNameEnabled; }
            set { 
                if (Item.FileNameEnabled == value)
                    return;
                Item.FileNameEnabled = value;
                ApplyCondition();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region FileName変更通知プロパティ
        public string FileName {
            get { return Item.FileName; }
            set { 
                if (Item.FileName == value)
                    return;
                Item.FileName = value;
                ApplyCondition();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ContentConditionEnabled変更通知プロパティ
        public bool ContentConditionEnabled
        {
            get { return Item.ContentConditionEnabled; }
            set
            {
                if (Item.ContentConditionEnabled == value)
                    return;
                Item.ContentConditionEnabled = value;
                ApplyCondition();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ServiceEnabled変更通知プロパティ
        public bool ServiceEnabled
        {
            get { return Item.ServiceIdEnabled; }
            set
            {
                if (Item.ServiceIdEnabled == value)
                    return;
                Item.ServiceIdEnabled = value;
                ApplyCondition();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region VideoSizeEnabled変更通知プロパティ
        public bool VideoSizeEnabled
        {
            get { return Item.VideoSizeEnabled; }
            set
            {
                if (Item.VideoSizeEnabled == value)
                    return;
                Item.VideoSizeEnabled = value;
                ApplyCondition();
                RaisePropertyChanged();
            }
        }
        #endregion

        // 有効な必要な項目だけ抜き出す
        private static GenreItem SelectItemToGenreItem(NotificationObject s)
        {
            var main = s as MainGenreSelectItem;
            var sub = s as GenreSelectItem;
            if (main != null)
            {
                if (main.IsChecked == true)
                {
                    return main.Item.Item;
                }
            }
            else
            {
                if (sub.MainGenre.IsChecked == true)
                {
                    // 親ジャンルが選択されていれば自分はなくていい
                    return null;
                }
                if (sub.IsChecked == true)
                {
                    return sub.Item.Item;
                }
            }
            return null;
        }

        public void ApplyCondition()
        {
            bool never = false;
            WarningText = "";
            var conds = new List<string>();
            if(Item.FileNameEnabled)
            {
                conds.Add("ファイル名に「" + Item.FileName + "」を含む");
            }
            if(Item.ContentConditionEnabled)
            {
                var cond = string.Join(",", GenreItems.Where(s => s is MainGenreSelectItem)
                    .Select(m =>
                    {
                        var main = m as MainGenreSelectItem;
                        if (main.IsChecked == true)
                        {
                            return main.Item.Name;
                        }
                        if (main.IsChecked == false)
                        {
                            return null;
                        }
                        return main.Item.Name + "(" + string.Join(",", main.SubGenres
                            .Where(s => s.IsChecked).Select(s => s.Item.Name)) + ")";
                    }).Where(s => s != null));
                if(string.IsNullOrEmpty(cond))
                {
                    cond = "該当ジャンルなし";
                    never = true;
                }
                else
                {
                    cond = "ジャンル:" + cond;
                }
                conds.Add(cond);
            }
            if(Item.ServiceIdEnabled)
            {
                var cond = string.Join(",",
                    ServiceList.Where(s => s.IsChecked).Select(s => s.Service.ToString()));
                if (string.IsNullOrEmpty(cond))
                {
                    cond = "該当チャンネルなし";
                    never = true;
                }
                else
                {
                    cond = "チャンネル:" + cond;
                }
                conds.Add(cond);
            }
            if (Item.VideoSizeEnabled)
            {
                var cond = string.Join(",",
                    VideoSizes.Where(s => s.IsChecked).Select(s => s.Name));
                if (string.IsNullOrEmpty(cond))
                {
                    cond = "該当サイズなし";
                    never = true;
                }
                else
                {
                    cond = "映像サイズ:" + cond;
                }
                conds.Add(cond);
            }
            if(conds.Count == 0)
            {
                conds.Add("無条件（常に条件を満たす）");
            }
            else if(never)
            {
                WarningText = "この条件を満たすことはありません";
            }
            Condition = string.Join(" かつ ", conds);
        }

        // DisplayCondition -> Item
        public void UpdateItem()
        {
            Item.ContentConditions = GenreItems
                .Select(SelectItemToGenreItem).Where(s => s != null).ToList();
            // 今のサービスリストにないサービスは含める
            Item.ServiceIds = Item.ServiceIds.Where(id => !ServiceList.Any(s => s.Service.Data.ServiceId == id))
                .Concat(ServiceList.Where(s => s.IsChecked).Select(s => s.Service.Data.ServiceId)).ToList();
            Item.VideoSizes = VideoSizes.Where(s => s.IsChecked).Select(s => s.Item).ToList();
            Item.Profile = (SelectedProfile as DisplayProfile)?.Model?.Name;
        }
    }

    public class DisplayAutoSelect : NotificationObject
    {
        public AutoSelectProfile Model { get; set; }

        #region Conditions変更通知プロパティ
        public ObservableCollection<DisplayCondition> _Conditions = new ObservableCollection<DisplayCondition>();

        public ObservableCollection<DisplayCondition> Conditions
        {
            get { return _Conditions; }
            set
            {
                if (_Conditions == value)
                    return;
                _Conditions = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SelectedIndex変更通知プロパティ
        private int _SelectedIndex = -1;

        // 更新されたときにアイテムを追跡する術がないので順番で覚えておく
        public int SelectedIndex
        {
            get { return _SelectedIndex; }
            set
            {
                if (_SelectedIndex == value)
                    return;
                _SelectedIndex = value;
                RaisePropertyChanged();
                RaisePropertyChanged("SelectedCondition");
            }
        }

        public DisplayCondition SelectedCondition
        {
            get
            {
                if(_SelectedIndex >= 0 && _SelectedIndex < Conditions.Count) {
                    return Conditions[_SelectedIndex];
                }
                return null;
            }
        }
        #endregion

        public override string ToString()
        {
            return Model.Name;
        }
    }

    // デフォルトJLSコマンドファイル選択用のプレースホルダ
    public class DefaultJLSCommand { }
}
