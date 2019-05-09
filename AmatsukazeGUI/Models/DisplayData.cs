using Amatsukaze.Components;
using Amatsukaze.Server;
using Livet;
using Livet.Commands;
using Livet.EventListeners;
using Microsoft.Win32;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
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

    public class SimpleDisplayConsole : ConsoleTextBase
    {
        #region TextLines変更通知プロパティ
        private ObservableCollection<string> _TextLines = new ObservableCollection<string>();

        public ObservableCollection<string> TextLines {
            get { return _TextLines; }
            set {
                if (_TextLines == value)
                    return;
                _TextLines = value;
                RaisePropertyChanged("LastLine");
                RaisePropertyChanged();
            }
        }

        public string LastLine {
            get {
                return TextLines.LastOrDefault();
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
            RaisePropertyChanged("LastLine");
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
            RaisePropertyChanged("LastLine");
        }

        public void SetTextLines(List<string> lines)
        {
            Clear();
            TextLines.Clear();
            foreach (var s in lines)
            {
                TextLines.Add(s);
            }
            RaisePropertyChanged("LastLine");
        }
    }

    public class DisplayConsole : SimpleDisplayConsole
    {
        public int Id { get; set; }

        #region Phase変更通知プロパティ
        private ResourcePhase _Phase;

        public ResourcePhase Phase {
            get { return _Phase; }
            set {
                if (_Phase == value)
                    return;
                _Phase = value;
                RaisePropertyChanged("PhaseString");
                RaisePropertyChanged();
            }
        }

        public string PhaseString {
            get {
                switch (Phase)
                {
                    case ResourcePhase.TSAnalyze:
                        return "TS解析";
                    case ResourcePhase.CMAnalyze:
                        return "CM解析";
                    case ResourcePhase.Filter:
                        return "映像解析";
                    case ResourcePhase.Encode:
                        return "エンコード";
                    case ResourcePhase.Mux:
                        return "Mux";
                    default:
                        return "";
                }
            }
        }
        #endregion

        #region Resource変更通知プロパティ
        private Resource _Resource;

        public Resource Resource {
            get { return _Resource; }
            set {
                if (_Resource == value)
                    return;
                _Resource = value;
                RaisePropertyChanged("CPU");
                RaisePropertyChanged("HDD");
                RaisePropertyChanged("GPU");
                RaisePropertyChanged("GpuIndex");
                RaisePropertyChanged();
            }
        }

        public int CPU { get { return _Resource?.Req.CPU ?? 0; } }
        public int HDD { get { return _Resource?.Req.HDD ?? 0; } }
        public int GPU { get { return _Resource?.Req.GPU ?? 0; } }
        public int GpuIndex { get { return _Resource?.GpuIndex ?? -1; } }
        #endregion

        #region IsSuspended変更通知プロパティ
        private bool _IsSuspended;

        public bool IsSuspended {
            get { return _IsSuspended; }
            set { 
                if (_IsSuspended == value)
                    return;
                _IsSuspended = value;
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class DisplayQueueItem : NotificationObject
    {
        public ClientModel Parent { get; set; }

        #region Model変更通知プロパティ
        private QueueItem _Model;

        public QueueItem Model
        {
            get { return _Model; }
            set
            {
                if (_Model == value)
                    return;
                _Model = value;
                // 全プロパティ変更
                RaisePropertyChanged(string.Empty);
            }
        }
        #endregion

        public bool IsComplete { get { return Model.State == QueueState.Complete; } }
        public bool IsEncoding { get { return Model.State == QueueState.Encoding; } }
        public bool IsError { get { return Model.State == QueueState.Failed || Model.State == QueueState.PreFailed; } }
        public bool IsPending { get { return Model.State == QueueState.LogoPending; } }
        public bool IsPreFailed { get { return Model.State == QueueState.PreFailed; } }
        public bool IsCanceled { get { return Model.State == QueueState.Canceled; } }
        public bool IsTooSmall { get { return IsPreFailed && Model.FailReason.Contains("映像が小さすぎます"); } }
        public string TsTimeString { get { return Model.TsTime.ToString("yyyy年MM月dd日"); } }
        public string ServiceString { get { return Model.ServiceName + "(" + Model.ServiceId + ")"; } }

        public string OutDir {
            get {
                return Path.GetDirectoryName(_Model.DstPath);
            }
        }

        public TimeSpan Elapsed
        {
            get
            {
                if(Model.EncodeStart == DateTime.MinValue)
                {
                    return new TimeSpan();
                }
                return DateTime.Now - Model.EncodeStart;
            }
        }

        public string GenreString
        {
            get {
                if (Model.Genre == null) return "";
                return string.Join(", ", 
                    Model.Genre.Select(s => SubGenre.GetFromItem(s)?.FullName).Where(s => s != null));
            }
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
                        switch (Model.Mode)
                        {
                            case ProcMode.CMCheck:
                                return "CM解析中→" + (Model.ConsoleId + 1);
                            case ProcMode.DrcsCheck:
                                return "DRCSチェック中→" + (Model.ConsoleId + 1);
                        }
                        return "エンコード中→" + (Model.ConsoleId + 1);
                    case QueueState.Failed: return "失敗";
                    case QueueState.PreFailed: return "失敗";
                    case QueueState.LogoPending: return "ペンディング";
                    case QueueState.Canceled: return "キャンセル";
                    case QueueState.Complete: return "完了";
                }
                return "不明";
            }
        }

        public string ModeString
        {
            get
            {
                switch (Model.Mode)
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

        public string ProfileLastUpdate
        {
            get
            {
                if(Model.Profile == null)
                {
                    return "";
                }
                if (Model.Profile.LastUpdate == DateTime.MinValue)
                {
                    return "";
                }
                var ts = DateTime.Now - Model.Profile.LastUpdate;
                if (ts.TotalMinutes <= 1)
                {
                    return "（" + ((int)ts.TotalSeconds).ToString() + "秒前に更新）";
                }
                if (ts.TotalHours <= 1)
                {
                    return "（" + ((int)ts.TotalMinutes).ToString() + "分前に更新）";
                }
                if (ts.TotalDays <= 1)
                {
                    return "（" + ((int)ts.TotalHours).ToString() + "時間前に更新）";
                }
                return "（" + ((int)ts.TotalDays).ToString() + "日前に更新）";
            }
        }

        public string TagString {
            get {
                return string.Join(" ", Model.Tags);
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

    public abstract class DeinterlaceAlgorithmViewModel : ViewModel
    {
        public FilterSetting Data { get; set; }

        public abstract string Name { get; }
    }

    public class FilterD3DVPViewModel : DeinterlaceAlgorithmViewModel
    {
        public override string Name { get { return "D3DVP"; } }

        public static string[] GPUList { get; } = new string[]
        {
            "自動", "Intel", "NVIDIA", "Radeon"
        };

        #region GPU変更通知プロパティ
        public int GPU
        {
            get { return (int)Data.D3dvpGpu; }
            set {
                int idx = (value == -1) ? 0 : value;
                if (Data.D3dvpGpu == (D3DVPGPU)idx)
                    return;
                Data.D3dvpGpu = (D3DVPGPU)idx;
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class FilterQTGMCViewModel : DeinterlaceAlgorithmViewModel
    {
        public override string Name { get { return "QTGMC"; } }

        public static string[] PresetList { get; } = new string[]
        {
            "自動", "Faster", "Fast", "Medium", "Slow", "Slower"
        };

        #region Preset変更通知プロパティ
        public int Preset
        {
            get { return (int)Data.QtgmcPreset; }
            set
            {
                int idx = (value == -1) ? 0 : value;
                if (Data.QtgmcPreset == (QTGMCPreset)idx)
                    return;
                Data.QtgmcPreset = (QTGMCPreset)idx;
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class FilterKFMViewModel : DeinterlaceAlgorithmViewModel
    {
        public override string Name { get { return "KFM"; } }

        #region EnableNR変更通知プロパティ
        public bool EnableNR
        {
            get { return Data.KfmEnableNr; }
            set
            {
                if (Data.KfmEnableNr == value)
                    return;
                Data.KfmEnableNr = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableUCF変更通知プロパティ
        public bool EnableUCF
        {
            get { return Data.KfmEnableUcf; }
            set
            {
                if (Data.KfmEnableUcf == value)
                    return;
                Data.KfmEnableUcf = value;
                RaisePropertyChanged();
            }
        }
        #endregion
        public static string[] FPSList { get; } = new string[]
        {
            "VFR", "VFR(30fps上限)", "24fps", "60fps", "SVPによる60fps化"
        };

        #region SelectedFPS変更通知プロパティ
        public static FilterFPS[] FPSListData = new FilterFPS[]
        {
            FilterFPS.VFR, FilterFPS.VFR30, FilterFPS.CFR24, FilterFPS.CFR60, FilterFPS.SVP
        };
        public int SelectedFPS {
            get { return Array.IndexOf(FPSListData, Data.KfmFps); }
            set
            {
                int idx = (value == -1) ? 0 : value;
                if (Data.KfmFps == FPSListData[idx])
                    return;
                Data.KfmFps = FPSListData[idx];
                RaisePropertyChanged();
            }
        }
        #endregion

        public static string[] VFRFpsList {
            get { return new string[] { "60fps", "120fps" }; }
        }

        #region VFRFps変更通知プロパティ
        public int VFRFps {
            get { return Data.KfmVfr120fps ? 1 : 0; }
            set {
                bool newValue = (value == 1);
                if (Data.KfmVfr120fps == newValue)
                    return;
                Data.KfmVfr120fps = newValue;
                RaisePropertyChanged("VFRFps");
                RaisePropertyChanged("VFR120Fps");
            }
        }
        public bool VFR120Fps {
            get { return Data.KfmVfr120fps; }
            set {
                if (Data.KfmVfr120fps == value)
                    return;
                Data.KfmVfr120fps = value;
                RaisePropertyChanged("VFRFps");
                RaisePropertyChanged("VFR120Fps");
            }
        }
        #endregion
    }

    public class FilterYadifViewModel : DeinterlaceAlgorithmViewModel
    {
        public override string Name { get { return "Yadif"; } }

        public static string[] FPSList { get; } = new string[]
        {
            "24fps", "30fps", "60fps"
        };

        #region SelectedFPS変更通知プロパティ
        public static FilterFPS[] FPSListData = new FilterFPS[]
        {
            FilterFPS.CFR24, FilterFPS.CFR30, FilterFPS.CFR60
        };
        public int SelectedFPS {
            get { return Array.IndexOf(FPSListData, Data.YadifFps); }
            set
            {
                int idx = (value == -1) ? 0 : value;
                if (Data.YadifFps == FPSListData[idx])
                    return;
                Data.YadifFps = FPSListData[idx];
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class FilterAutoVfrViewModel : DeinterlaceAlgorithmViewModel
    {
        public override string Name { get { return "AutoVfr"; } }

        #region NumParallel変更通知プロパティ
        public int NumParallel {
            get { return Data.AutoVfrParallel; }
            set { 
                if (Data.AutoVfrParallel == value)
                    return;
                Data.AutoVfrParallel = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableFast変更通知プロパティ
        public bool EnableFast {
            get { return Data.AutoVfrFast; }
            set { 
                if (Data.AutoVfrFast == value)
                    return;
                Data.AutoVfrFast = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Enable30F変更通知プロパティ
        public bool Enable30F {
            get { return Data.AutoVfr30F; }
            set { 
                if (Data.AutoVfr30F == value)
                    return;
                Data.AutoVfr30F = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Enable60F変更通知プロパティ
        public bool Enable60F {
            get { return Data.AutoVfr60F; }
            set { 
                if (Data.AutoVfr60F == value)
                    return;
                Data.AutoVfr60F = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Enable24A変更通知プロパティ
        public bool Enable24A {
            get { return Data.AutoVfr24A; }
            set { 
                if (Data.AutoVfr24A == value)
                    return;
                Data.AutoVfr24A = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Enable30A変更通知プロパティ
        public bool Enable30A {
            get { return Data.AutoVfr30A; }
            set { 
                if (Data.AutoVfr30A == value)
                    return;
                Data.AutoVfr30A = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableCrop変更通知プロパティ
        public bool EnableCrop {
            get { return Data.AutoVfrCrop; }
            set { 
                if (Data.AutoVfrCrop == value)
                    return;
                Data.AutoVfrCrop = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Skip変更通知プロパティ
        public int Skip {
            get { return Data.AutoVfrSkip; }
            set { 
                if (Data.AutoVfrSkip == value)
                    return;
                Data.AutoVfrSkip = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Ref変更通知プロパティ
        public int Ref {
            get { return Data.AutoVfrRef; }
            set { 
                if (Data.AutoVfrRef == value)
                    return;
                Data.AutoVfrRef = value;
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class DisplayResource : NotificationObject
    {
        private static readonly string[] PhaseNames = new string[]
        {
            "TS解析", "CM解析", "フィルタ映像解析", "エンコード", "Mux"
        };

        public static readonly int MAX = 5;

        public ProfileSetting Model { get; set; }
        public int Phase { get; set; }

        public ReqResource Resource {
            set {
                CPU = value.CPU;
                HDD = value.HDD;
                GPU = value.GPU;
            }
        }

        public string Name {
            get { return PhaseNames[Phase]; }
        }

        #region CPU変更通知プロパティ
        public int CPU {
            get { return Model.ReqResources[Phase].CPU; }
            set { 
                if (Model.ReqResources[Phase].CPU == value)
                    return;
                Model.ReqResources[Phase].CPU = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region HDD変更通知プロパティ
        public int HDD {
            get { return Model.ReqResources[Phase].HDD; }
            set { 
                if (Model.ReqResources[Phase].HDD == value)
                    return;
                Model.ReqResources[Phase].HDD = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region GPU変更通知プロパティ
        public int GPU {
            get { return Model.ReqResources[Phase].GPU; }
            set { 
                if (Model.ReqResources[Phase].GPU == value)
                    return;
                Model.ReqResources[Phase].GPU = value;
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class FormatText
    {
        private StringBuilder builder = new StringBuilder();

        public void KeyValue(string key, string value)
        {
            builder.Append(key).Append(": ").Append(value).AppendLine();
        }
        public void KeyValue(string key, bool value)
        {
            builder.Append(key).Append(": ").Append(value ? "Yes" : "No").AppendLine();
        }
        public void KeyTable(string key, string value)
        {
            builder.Append(key).Append(": ").AppendLine();
            foreach (var line in value.
                Split(new[] { "\r\n", "\r", "\n" }, StringSplitOptions.None).
                Where(s => string.IsNullOrWhiteSpace(s) == false).
                Select(s => "\t" + s))
                builder.Append(line).AppendLine();
        }
        public override string ToString()
        {
            return builder.ToString();
        }
    }

    public class DisplayFilterSetting : ViewModel
    {
        public string Name { get { return "フィルタを設定"; } }

        public FilterSetting Data { get; private set; }

        public FilterKFMViewModel KFM { get; private set; }

        public FilterQTGMCViewModel QTGMC { get; private set; }

        public FilterD3DVPViewModel D3DVP { get; private set; }

        public FilterYadifViewModel Yadif { get; private set; }

        public FilterAutoVfrViewModel AutoVfr { get; private set; }

        public DeinterlaceAlgorithmViewModel[] DeinterlaceList { get; private set; }

        public ClientModel Model { get; private set; }

        public DisplayFilterSetting(FilterSetting data, ClientModel model)
        {
            Data = data;
            Model = model;

            KFM = new FilterKFMViewModel() { Data = data };
            D3DVP = new FilterD3DVPViewModel() { Data = data };
            QTGMC = new FilterQTGMCViewModel() { Data = data };
            Yadif = new FilterYadifViewModel() { Data = data };
            AutoVfr = new FilterAutoVfrViewModel() { Data = data };

            DeinterlaceList = new DeinterlaceAlgorithmViewModel[]
            {
                KFM, D3DVP, QTGMC, Yadif, AutoVfr
            };
        }

        #region EnableCUDA変更通知プロパティ
        public bool EnableCUDA
        {
            get { return Data.EnableCUDA; }
            set
            {
                if (Data.EnableCUDA == value)
                    return;
                Data.EnableCUDA = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableDeblock変更通知プロパティ
        public bool EnableDeblock
        {
            get { return Data.EnableDeblock; }
            set
            {
                if (Data.EnableDeblock == value)
                    return;
                Data.EnableDeblock = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public static string[] DeblockQualityList { get; } = new string[]
        {
            "高(4)", "中(3)", "低(2)"
        };

        #region DeblockQuality変更通知プロパティ
        public static int[] DeblockQualityListData = new int[]
        {
            4, 3, 2
        };
        public int DeblockQuality {
            get { return Array.IndexOf(DeblockQualityListData, Data.DeblockQuality); }
            set {
                int idx = (value == -1) ? 0 : value;
                if (Data.DeblockQuality == DeblockQualityListData[idx])
                    return;
                Data.DeblockQuality = DeblockQualityListData[idx];
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DeblockStrength変更通知プロパティ
        public static string[] DeblockStrengthList { get; } = new string[]
        {
            "強", "中", "弱", "低ビットレート用弱"
        };

        public int DeblockStrength
        {
            get { return (int)Data.DeblockStrength; }
            set
            {
                if (Data.DeblockStrength == (DeblockStrength)value)
                    return;
                Data.DeblockStrength = (DeblockStrength)value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DeblockSharpen変更通知プロパティ
        public bool DeblockSharpen {
            get { return Data.DeblockSharpen; }
            set { 
                if (Data.DeblockSharpen == value)
                    return;
                Data.DeblockSharpen = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableDeinterlace変更通知プロパティ
        public bool EnableDeinterlace
        {
            get { return Data.EnableDeinterlace; }
            set
            {
                if (Data.EnableDeinterlace == value)
                    return;
                Data.EnableDeinterlace = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DeinterlaceAlgorithm変更通知プロパティ
        public int DeinterlaceAlgorithm
        {
            get { return (int)Data.DeinterlaceAlgorithm; }
            set
            {
                if (Data.DeinterlaceAlgorithm == (DeinterlaceAlgorithm)value)
                    return;
                Data.DeinterlaceAlgorithm = (DeinterlaceAlgorithm)value;
                RaisePropertyChanged();
                RaisePropertyChanged("SelectedDeinterlace");
            }
        }
        public DeinterlaceAlgorithmViewModel SelectedDeinterlace
        {
            get
            {
                return DeinterlaceList[(int)Data.DeinterlaceAlgorithm];
            }
        }
        #endregion

        #region EnableResize変更通知プロパティ
        public bool EnableResize
        {
            get { return Data.EnableResize; }
            set
            {
                if (Data.EnableResize == value)
                    return;
                Data.EnableResize = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ResizeWidth変更通知プロパティ
        public int ResizeWidth
        {
            get { return Data.ResizeWidth; }
            set
            {
                if (Data.ResizeWidth == value)
                    return;
                Data.ResizeWidth = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ResizeHeight変更通知プロパティ
        public int ResizeHeight
        {
            get { return Data.ResizeHeight; }
            set
            {
                if (Data.ResizeHeight == value)
                    return;
                Data.ResizeHeight = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableTemporalNR変更通知プロパティ
        public bool EnableTemporalNR
        {
            get { return Data.EnableTemporalNR; }
            set
            {
                if (Data.EnableTemporalNR == value)
                    return;
                Data.EnableTemporalNR = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableDeband変更通知プロパティ
        public bool EnableDeband
        {
            get { return Data.EnableDeband; }
            set
            {
                if (Data.EnableDeband == value)
                    return;
                Data.EnableDeband = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableEdgeLevel変更通知プロパティ
        public bool EnableEdgeLevel
        {
            get { return Data.EnableEdgeLevel; }
            set
            {
                if (Data.EnableEdgeLevel == value)
                    return;
                Data.EnableEdgeLevel = value;
                RaisePropertyChanged();
            }
        }
        #endregion
        
        #region CopyFilterTextCommand
        private ViewModelCommand _CopyFilterTextCommand;

        public ViewModelCommand CopyFilterTextCommand {
            get {
                if (_CopyFilterTextCommand == null)
                {
                    _CopyFilterTextCommand = new ViewModelCommand(CopyFilterText);
                }
                return _CopyFilterTextCommand;
            }
        }

        public void CopyFilterText()
        {
            try
            {
                Clipboard.SetText(AvsScriptCreator.FilterToString(Data, Model.Setting.Model));
            }
            catch { }
        }
        #endregion

        #region SetWidthHeightCommand
        private ListenerCommand<string> _SetWidthHeightCommand;

        public ListenerCommand<string> SetWidthHeightCommand {
            get {
                if (_SetWidthHeightCommand == null)
                {
                    _SetWidthHeightCommand = new ListenerCommand<string>(SetWidthHeight);
                }
                return _SetWidthHeightCommand;
            }
        }

        public void SetWidthHeight(string parameter)
        {
            var prms = parameter.Split('x');
            ResizeWidth = int.Parse(prms[0]);
            ResizeHeight = int.Parse(prms[1]);
        }
        #endregion

    }

    public class DisplayCustomFilter : ViewModel
    {
        public string Name { get { return "カスタムフィルタを設定"; } }

        public ClientModel Model { get; set; }

        public ProfileSetting Data { get; set; }

        #region FilterPath変更通知プロパティ
        public string FilterPath
        {
            get { return string.IsNullOrEmpty(Data.FilterPath) ? "フィルタなし" : Data.FilterPath; }
            set
            {
                string val = (value == "フィルタなし") ? "" : value;
                if (Data.FilterPath == val)
                    return;
                Data.FilterPath = val;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PostFilterPath変更通知プロパティ
        public string PostFilterPath
        {
            get { return string.IsNullOrEmpty(Data.PostFilterPath) ? "フィルタなし" : Data.PostFilterPath; }
            set
            {
                string val = (value == "フィルタなし") ? "" : value;
                if (Data.PostFilterPath == val)
                    return;
                Data.PostFilterPath = val;
                RaisePropertyChanged();
            }
        }
        #endregion

    }

    public class DisplayNoFilter : ViewModel
    {
        public string Name { get { return "フィルタなし"; } }
    }

    public class DisplayProfile : ViewModel
    {
        public ProfileSetting Data { get; private set; }

        public DisplayFilterSetting Filter { get; private set; }

        public DisplayCustomFilter CustomFilter { get; private set; }

        public ClientModel Model { get; private set; }

        public DisplayResource[] Resources { get; private set; }

        public ViewModel[] FilterOptions { get; private set; }

        public DisplayProfile(ProfileSetting data, ClientModel model, DisplayResource[] resources)
        {
            Data = data;
            Filter = new DisplayFilterSetting(data.FilterSetting, model);
            CustomFilter = new DisplayCustomFilter() { Model = model, Data = data };
            Model = model;
            Resources = resources;

            FilterOptions = new ViewModel[]
            {
                new DisplayNoFilter(),
                Filter,
                CustomFilter
            };

            CompositeDisposable.Add(new PropertyChangedEventListener(
                Model, ModelPropertyChanged));
            CompositeDisposable.Add(new PropertyChangedEventListener(
                Resources[(int)ResourcePhase.Encode], EncodeResourcePropertyChanged));
        }

        private void ModelPropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == "Setting")
            {
                UpdateWarningText();
            }
        }

        private void EncodeResourcePropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            UpdateWarningText();
        }

        #region Name変更通知プロパティ
        public string Name {
            get { return Data.Name; }
            set { 
                if (Data.Name == value)
                    return;
                Data.Name = value;
                RaisePropertyChanged();
                RaisePropertyChanged("SortKey");
            }
        }
        public string SortKey {
            get {
                if (Data.Name.StartsWith("サンプル_")) return Data.Name;
                return "_" + Data.Name;
            }
        }
        #endregion

        #region FilterOption変更通知プロパティ
        public int FilterOption
        {
            get { return (int)Data.FilterOption; }
            set
            {
                if (Data.FilterOption == (FilterOption)value)
                    return;
                Data.FilterOption = (FilterOption)value;
                UpdateBitrate();
                RaisePropertyChanged();
                RaisePropertyChanged("SelectedFilterOption");
            }
        }
        public ViewModel SelectedFilterOption
        {
            get
            {
                return FilterOptions[(int)Data.FilterOption];
            }
        }
        #endregion

        #region EncoderTypeInt変更通知プロパティ
        public int EncoderTypeInt {
            get { return (int)Data.EncoderType; }
            set {
                if ((int)Data.EncoderType == value)
                    return;
                Data.EncoderType = (EncoderType)value;
                UpdateWarningText();
                RaisePropertyChanged();
                RaisePropertyChanged("EncoderOption");
            }
        }
        #endregion

        #region EncoderOption変更通知プロパティ
        public string EncoderOption {
            get {
                switch (Data.EncoderType)
                {
                    case EncoderType.x264: return Data.X264Option;
                    case EncoderType.x265: return Data.X265Option;
                    case EncoderType.QSVEnc: return Data.QSVEncOption;
                    case EncoderType.NVEnc: return Data.NVEncOption;
                }
                return null;
            }
            set {
                switch (Data.EncoderType)
                {
                    case EncoderType.x264:
                        if (Data.X264Option == value)
                            return;
                        Data.X264Option = value;
                        break;
                    case EncoderType.x265:
                        if (Data.X265Option == value)
                            return;
                        Data.X265Option = value;
                        break;
                    case EncoderType.QSVEnc:
                        if (Data.QSVEncOption == value)
                            return;
                        Data.QSVEncOption = value;
                        break;
                    case EncoderType.NVEnc:
                        if (Data.NVEncOption == value)
                            return;
                        Data.NVEncOption = value;
                        break;
                    default:
                        return;
                }
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AutoBuffer変更通知プロパティ
        public bool AutoBuffer {
            get { return Data.AutoBuffer; }
            set {
                if (Data.AutoBuffer == value)
                    return;
                Data.AutoBuffer = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region TwoPass変更通知プロパティ
        public bool TwoPass {
            get { return Data.TwoPass; }
            set {
                if (Data.TwoPass == value)
                    return;
                Data.TwoPass = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SplitSub変更通知プロパティ
        public bool SplitSub {
            get { return Data.SplitSub; }
            set {
                if (Data.SplitSub == value)
                    return;
                Data.SplitSub = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OutputMask変更通知プロパティ
        public DisplayOutputMask OutputMask {
            get {
                return
                  OutputOptionList_.FirstOrDefault(s => s.Mask == Data.OutputMask)
                  ?? OutputOptionList_[0];
            }
            set {
                if (Data.OutputMask == value?.Mask)
                    return;
                Data.OutputMask = value?.Mask ?? OutputOptionList_[0].Mask;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateA変更通知プロパティ
        public double BitrateA {
            get { return Data.Bitrate.A; }
            set {
                if (Data.Bitrate.A == value)
                    return;
                Data.Bitrate.A = value;
                UpdateBitrate();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateB変更通知プロパティ
        public double BitrateB {
            get { return Data.Bitrate.B; }
            set {
                if (Data.Bitrate.B == value)
                    return;
                Data.Bitrate.B = value;
                UpdateBitrate();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateH264変更通知プロパティ
        public double BitrateH264 {
            get { return Data.Bitrate.H264; }
            set {
                if (Data.Bitrate.H264 == value)
                    return;
                Data.Bitrate.H264 = value;
                UpdateBitrate();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region BitrateCM変更通知プロパティ
        public double BitrateCM {
            get { return Data.BitrateCM; }
            set {
                if (Data.BitrateCM == value)
                    return;
                Data.BitrateCM = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region JLSCommandFile変更通知プロパティ
        public object JLSCommandFile {
            get {
                return (object)Data.JLSCommandFile ?? NullValue.Value;
            }
            set {
                if (value == null) return; // なぜかnullがセットされることがあるので
                var newValue = (value is NullValue) ? null : (value as string);
                if (Data.JLSCommandFile == newValue)
                    return;
                Data.JLSCommandFile = newValue;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region JLSOption変更通知プロパティ
        public string JLSOption {
            get { return Data.JLSOption; }
            set {
                if (Data.JLSOption == value)
                    return;
                Data.JLSOption = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ChapterExeOptions変更通知プロパティ
        public string ChapterExeOptions
        {
            get { return Data.ChapterExeOption; }
            set
            {
                if (Data.ChapterExeOption == value)
                    return;
                Data.ChapterExeOption = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableJLSOption変更通知プロパティ
        public bool EnableJLSOption {
            get { return Data.EnableJLSOption; }
            set {
                if (Data.EnableJLSOption == value)
                    return;
                Data.EnableJLSOption = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableChapter変更通知プロパティ
        public bool DisableChapter {
            get { return Data.DisableChapter; }
            set {
                if (Data.DisableChapter == value)
                    return;
                Data.DisableChapter = value;
                RaisePropertyChanged();
                NoDelogo = value;
            }
        }
        #endregion

        #region DisableSubs変更通知プロパティ
        public bool DisableSubs {
            get { return Data.DisableSubs; }
            set {
                if (Data.DisableSubs == value)
                    return;
                Data.DisableSubs = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IgnoreNoDrcsMap変更通知プロパティ
        public bool IgnoreNoDrcsMap {
            get { return Data.IgnoreNoDrcsMap; }
            set {
                if (Data.IgnoreNoDrcsMap == value)
                    return;
                Data.IgnoreNoDrcsMap = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableNicoJK変更通知プロパティ
        public bool EnableNicoJK {
            get { return Data.EnableNicoJK; }
            set {
                if (Data.EnableNicoJK == value)
                    return;
                Data.EnableNicoJK = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IgnoreNicoJKError変更通知プロパティ
        public bool IgnoreNicoJKError {
            get { return Data.IgnoreNicoJKError; }
            set {
                if (Data.IgnoreNicoJKError == value)
                    return;
                Data.IgnoreNicoJKError = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJK18変更通知プロパティ
        public bool NicoJK18 {
            get { return Data.NicoJK18; }
            set {
                if (Data.NicoJK18 == value)
                    return;
                Data.NicoJK18 = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKLog変更通知プロパティ
        public bool NicoJKLog {
            get { return Data.NicoJKLog; }
            set {
                if (Data.NicoJKLog == value)
                    return;
                Data.NicoJKLog = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat720S変更通知プロパティ
        public bool NicoJKFormat720S {
            get { return Data.NicoJKFormats[0]; }
            set {
                if (Data.NicoJKFormats[0] == value)
                    return;
                Data.NicoJKFormats[0] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat720T変更通知プロパティ
        public bool NicoJKFormat720T {
            get { return Data.NicoJKFormats[1]; }
            set {
                if (Data.NicoJKFormats[1] == value)
                    return;
                Data.NicoJKFormats[1] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat1080S変更通知プロパティ
        public bool NicoJKFormat1080S {
            get { return Data.NicoJKFormats[2]; }
            set {
                if (Data.NicoJKFormats[2] == value)
                    return;
                Data.NicoJKFormats[2] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NicoJKFormat1080T変更通知プロパティ
        public bool NicoJKFormat1080T {
            get { return Data.NicoJKFormats[3]; }
            set {
                if (Data.NicoJKFormats[3] == value)
                    return;
                Data.NicoJKFormats[3] = value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LooseLogoDetection変更通知プロパティ
        public bool LooseLogoDetection {
            get { return Data.LooseLogoDetection; }
            set {
                if (Data.LooseLogoDetection == value)
                    return;
                Data.LooseLogoDetection = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IgnoreNoLogo変更通知プロパティ
        public bool IgnoreNoLogo {
            get { return Data.IgnoreNoLogo; }
            set {
                if (Data.IgnoreNoLogo == value)
                    return;
                Data.IgnoreNoLogo = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NoDelogo変更通知プロパティ
        public bool NoDelogo {
            get { return Data.NoDelogo; }
            set {
                if (Data.NoDelogo == value)
                    return;
                Data.NoDelogo = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MoveEDCBFiles変更通知プロパティ
        public bool MoveEDCBFiles {
            get { return Data.MoveEDCBFiles; }
            set {
                if (Data.MoveEDCBFiles == value)
                    return;
                Data.MoveEDCBFiles = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableRename変更通知プロパティ
        public bool EnableRename {
            get { return Data.EnableRename; }
            set {
                if (Data.EnableRename == value)
                    return;
                Data.EnableRename = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region RenameFormat変更通知プロパティ
        public string RenameFormat {
            get { return Data.RenameFormat; }
            set {
                if (Data.RenameFormat == value)
                    return;
                Data.RenameFormat = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableGunreFolder変更通知プロパティ
        public bool EnableGunreFolder {
            get { return Data.EnableGunreFolder; }
            set {
                if (Data.EnableGunreFolder == value)
                    return;
                Data.EnableGunreFolder = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SystemAviSynthPlugin変更通知プロパティ
        public bool SystemAviSynthPlugin {
            get { return Data.SystemAviSynthPlugin; }
            set {
                if (Data.SystemAviSynthPlugin == value)
                    return;
                Data.SystemAviSynthPlugin = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableHashCheck変更通知プロパティ
        public bool DisableHashCheck {
            get { return Data.DisableHashCheck; }
            set {
                if (Data.DisableHashCheck == value)
                    return;
                Data.DisableHashCheck = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NoRemoveTmp変更通知プロパティ
        public bool NoRemoveTmp {
            get { return Data.NoRemoveTmp; }
            set {
                if (Data.NoRemoveTmp == value)
                    return;
                Data.NoRemoveTmp = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DisableLogFile変更通知プロパティ
        public bool DisableLogFile {
            get { return Data.DisableLogFile; }
            set {
                if (Data.DisableLogFile == value)
                    return;
                Data.DisableLogFile = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Mpeg2DecoderInt変更通知プロパティ
        public int Mpeg2DecoderInt {
            get { return (int)Data.Mpeg2Decoder; }
            set {
                if ((int)Data.Mpeg2Decoder == value)
                    return;
                Data.Mpeg2Decoder = (DecoderType)value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region H264DecoderInt変更通知プロパティ
        public int H264DecoderInt {
            get { return (int)Data.H264Deocder; }
            set {
                if ((int)Data.H264Deocder == value)
                    return;
                Data.H264Deocder = (DecoderType)value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OutputFormatInt変更通知プロパティ
        public int OutputFormatInt {
            get { return (int)Data.OutputFormat; }
            set {
                if ((int)Data.OutputFormat == value)
                    return;
                Data.OutputFormat = (FormatType)value;
                UpdateWarningText();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IgnoreEncodeAffinity変更通知プロパティ
        public bool IgnoreEncodeAffinity
        {
            get { return Data.IgnoreEncodeAffinity; }
            set
            {
                if (Data.IgnoreEncodeAffinity == value)
                    return;
                Data.IgnoreEncodeAffinity = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnablePmtCut変更通知プロパティ
        public bool EnablePmtCut {
            get { return Data.EnablePmtCut; }
            set { 
                if (Data.EnablePmtCut == value)
                    return;
                Data.EnablePmtCut = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PmtCutHeadRate変更通知プロパティ
        public double PmtCutHeadRate {
            get { return Data.PmtCutHeadRate; }
            set { 
                if (Data.PmtCutHeadRate == value)
                    return;
                Data.PmtCutHeadRate = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PmtCutTailRate変更通知プロパティ
        public double PmtCutTailRate {
            get { return Data.PmtCutTailRate; }
            set { 
                if (Data.PmtCutTailRate == value)
                    return;
                Data.PmtCutTailRate = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableMaxFadeLength変更通知プロパティ
        public bool EnableMaxFadeLength {
            get { return Data.EnableMaxFadeLength; }
            set { 
                if (Data.EnableMaxFadeLength == value)
                    return;
                Data.EnableMaxFadeLength = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MaxFadeLength変更通知プロパティ
        public int MaxFadeLength {
            get { return Data.MaxFadeLength; }
            set { 
                if (Data.MaxFadeLength == value)
                    return;
                Data.MaxFadeLength = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PreBatchFile変更通知プロパティ
        public string PreBatchFile {
            get { return string.IsNullOrEmpty(Data.PreBatchFile) ? "なし" : Data.PreBatchFile; }
            set {
                string val = (value == "なし") ? "" : value;
                if (Data.PreBatchFile == val)
                    return;
                Data.PreBatchFile = val;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PostBatchFile変更通知プロパティ
        public string PostBatchFile {
            get { return string.IsNullOrEmpty(Data.PostBatchFile) ? "なし" : Data.PostBatchFile; }
            set {
                string val = (value == "なし") ? "" : value;
                if (Data.PostBatchFile == val)
                    return;
                Data.PostBatchFile = val;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NumEncodeBufferFrames変更通知プロパティ
        public int NumEncodeBufferFrames {
            get { return Data.NumEncodeBufferFrames; }
            set {
                if (Data.NumEncodeBufferFrames == value)
                    return;
                Data.NumEncodeBufferFrames = value;
                RaisePropertyChanged();
                RaisePropertyChanged("NumEncodeBufferFramesIndex");
            }
        }
        public int NumEncodeBufferFramesIndex {
            get {
                int index = (int)Math.Sqrt(Data.NumEncodeBufferFrames / 0.35 - 4);
                // 値が小さいところは誤差が大きくてずれるので調整
                if(Data.NumEncodeBufferFrames < 8)
                {
                    return index - 1;
                }
                return index;
            }
            set {
                int frames = (int)(0.35 * value * value + 4);
                if (Data.NumEncodeBufferFrames == frames)
                    return;
                Data.NumEncodeBufferFrames = frames;
                RaisePropertyChanged();
                RaisePropertyChanged("NumEncodeBufferFrames");
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

        public string[] EncoderList {
            get { return new string[] { "x264", "x265", "QSVEnc", "NVEnc" }; }
        }
        public string[] Mpeg2DecoderList {
            get { return new string[] { "デフォルト", "QSV", "CUVID" }; }
        }
        public string[] H264DecoderList {
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
        public string[] FormatList {
            get { return new string[] { "MP4", "MKV", "M2TS", "TS" }; }
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

        public string GetResourceString()
        {
            var sb = new StringBuilder();
            foreach (var res in Resources)
            {
                sb.Append(res.CPU).Append(":")
                    .Append(res.HDD).Append(":")
                    .Append(res.GPU).AppendLine();
            }
            return sb.ToString();
        }

        public void SetResourceFromString(string res)
        {
            var raw = res.Split(new string[] { "\r\n", "\r", "\n", ":" }, StringSplitOptions.None)
                .Where(s => !string.IsNullOrWhiteSpace(s))
                .Select(s => int.Parse(s)).ToArray();
            if (raw.Length == DisplayResource.MAX * 3)
            {
                for (int i = 0; i < DisplayResource.MAX; ++i)
                {
                    Resources[i].CPU = raw[i * 3 + 0];
                    Resources[i].HDD = raw[i * 3 + 1];
                    Resources[i].GPU = raw[i * 3 + 2];
                }
            }
        }

        #region CopyResourceCommand
        private ViewModelCommand _CopyResourceCommand;

        public ViewModelCommand CopyResourceCommand {
            get {
                if (_CopyResourceCommand == null)
                {
                    _CopyResourceCommand = new ViewModelCommand(CopyResource);
                }
                return _CopyResourceCommand;
            }
        }

        public void CopyResource()
        {
            try
            {
                Clipboard.SetText(GetResourceString());
            }
            catch { }
        }
        #endregion

        #region PasteResourceCommand
        private ViewModelCommand _PasteResourceCommand;

        public ViewModelCommand PasteResourceCommand {
            get {
                if (_PasteResourceCommand == null)
                {
                    _PasteResourceCommand = new ViewModelCommand(PastResource);
                }
                return _PasteResourceCommand;
            }
        }

        public void PastResource()
        {
            try
            {
                SetResourceFromString(Clipboard.GetText());
            }
            catch { }
        }
        #endregion

        #region CopySettingTextCommand
        private ViewModelCommand _CopySettingTextCommand;

        public ViewModelCommand CopySettingTextCommand
        {
            get
            {
                if (_CopySettingTextCommand == null)
                {
                    _CopySettingTextCommand = new ViewModelCommand(CopySettingText);
                }
                return _CopySettingTextCommand;
            }
        }

        public void CopySettingText()
        {
            try
            {
                Clipboard.SetText(ToLongString());
            }
            catch { }
        }
        #endregion

        public void UpdateWarningText()
        {
            StringBuilder sb = new StringBuilder();
            if (Data.EncoderType == EncoderType.QSVEnc && Data.TwoPass)
            {
                sb.Append("QSVEncは2パスに対応していません\r\n");
            }
            if (Data.EncoderType == EncoderType.NVEnc && Data.TwoPass)
            {
                sb.Append("NVEncは2パスに対応していません\r\n");
            }
            if (Data.EnableNicoJK && Data.NicoJKFormats.Any(s => s) == false)
            {
                sb.Append("ニコニコ実況コメントのフォーマットが１つも選択されていません。選択がない場合、出力されません\r\n");
            }
            if (Data.OutputFormat == FormatType.M2TS || Data.OutputFormat == FormatType.TS)
            {
                if (Data.EncoderType == EncoderType.x265 ||
                    ((Data.EncoderType != EncoderType.x264) && (EncoderOption?.Contains("hevc") ?? false)))
                {
                    sb.Append("tsMuxeR 2.6.12はHEVCを正しくmuxできない不具合があるのでご注意ください\r\n");
                }
            }
            if (Data.EncoderType == EncoderType.NVEnc)
            {
                var mes = "GeForceではNVEncによる同時エンコードは2つまでに制限されています\r\n";
                if ((Model.Setting?.NumParallel ?? 0) >= 3)
                {
                    if (Model.Setting?.SchedulingEnabled ?? false)
                    {
                        var encodeRes = Data.ReqResources[(int)ResourcePhase.Encode];
                        if (encodeRes.CPU * 3 <= 100 && encodeRes.HDD * 3 <= 100 && encodeRes.GPU * 3 <= 100)
                        {
                            sb.Append(mes);
                        }
                    }
                    else
                    {
                        sb.Append(mes);
                    }
                }
            }
            SettingWarningText = sb.ToString();
        }

        public void SetEncoderOptions(string X264Option, string X265Option, string QSVEncOption, string NVEncOption)
        {
            if (X264Option != Data.X264Option || X265Option != Data.X265Option ||
                QSVEncOption != Data.QSVEncOption || NVEncOption != Data.NVEncOption)
            {
                Data.X264Option = X264Option;
                Data.X265Option = X265Option;
                Data.QSVEncOption = QSVEncOption;
                Data.NVEncOption = NVEncOption;
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

        public static string GetProfileName(object item)
        {
            var name = (item as DisplayProfile)?.Data?.Name ??
                (item as DisplayAutoSelect)?.Model?.Name;
            if (item is DisplayAutoSelect)
            {
                name = ServerSupport.AUTO_PREFIX + name;
            }
            return name;
        }

        public string ToLongString()
        {
            var text = new FormatText();
            text.KeyValue("プロファイル名", Data.Name);
            text.KeyValue("更新日時", Data.LastUpdate.ToString("yyyy年MM月dd日 hh:mm:ss"));
            text.KeyValue("エンコーダ", EncoderList[(int)Data.EncoderType]);
            text.KeyValue("エンコーダ追加オプション", EncoderOption);
            text.KeyValue("JoinLogoScpコマンドファイル", Data.JLSCommandFile ?? "チャンネル設定に従う");
            text.KeyValue("JoinLogoScpオプション", Data.JLSOption ?? "チャンネル設定に従う");
            text.KeyValue("chapter_exeオプション", Data.ChapterExeOption);

            text.KeyValue("MPEG2デコーダ", Mpeg2DecoderList[(int)Data.Mpeg2Decoder]);
            text.KeyValue("H264デコーダ", H264DecoderList[(int)Data.H264Deocder]);
            text.KeyValue("出力フォーマット", FormatList[(int)Data.OutputFormat]);
            text.KeyValue("出力選択", OutputMask.Name);
            text.KeyValue("SCRenameによるリネームを行う", Data.EnableRename);
            text.KeyValue("SCRename書式", Data.RenameFormat);
            text.KeyValue("ジャンルごとにフォルダ分け", Data.EnableGunreFolder);

            text.KeyValue("実行前バッチ", Data.PreBatchFile ?? "なし");
            text.KeyValue("実行後バッチ", Data.PostBatchFile ?? "なし");

            if (Data.FilterOption == Server.FilterOption.None)
            {
                text.KeyValue("フィルタ", "なし");
            }
            else if (Data.FilterOption == Server.FilterOption.Setting)
            {
                text.KeyValue("フィルタ-CUDAで処理", Data.FilterSetting.EnableCUDA);
                text.KeyValue("フィルタ-インターレース解除", Data.FilterSetting.EnableDeinterlace);
                if (Data.FilterSetting.EnableDeinterlace)
                {
                    text.KeyValue("フィルタ-インターレース解除方法", Filter.SelectedDeinterlace.Name);
                    switch (Data.FilterSetting.DeinterlaceAlgorithm)
                    {
                        case DeinterlaceAlgorithm.KFM:
                            text.KeyValue("フィルタ-SMDegrainによるNR", Data.FilterSetting.KfmEnableNr);
                            text.KeyValue("フィルタ-DecombUCF", Data.FilterSetting.KfmEnableUcf);
                            text.KeyValue("フィルタ-出力fps", FilterKFMViewModel.FPSList[Filter.KFM.SelectedFPS]);
                            text.KeyValue("フィルタ-VFRフレームタイミング", FilterKFMViewModel.VFRFpsList[Filter.KFM.VFRFps]);
                            break;
                        case DeinterlaceAlgorithm.D3DVP:
                            text.KeyValue("フィルタ-使用GPU", FilterD3DVPViewModel.GPUList[(int)Data.FilterSetting.D3dvpGpu]);
                            break;
                        case DeinterlaceAlgorithm.QTGMC:
                            text.KeyValue("フィルタ-QTGMCプリセット", FilterQTGMCViewModel.PresetList[(int)Data.FilterSetting.QtgmcPreset]);
                            break;
                        case DeinterlaceAlgorithm.Yadif:
                            text.KeyValue("フィルタ-出力fps", FilterYadifViewModel.FPSList[Filter.Yadif.SelectedFPS]);
                            break;
                        case DeinterlaceAlgorithm.AutoVfr:
                            text.KeyValue("フィルタ-30fpsを使用する", Filter.AutoVfr.Enable30F);
                            text.KeyValue("フィルタ-60fpsを使用する", Filter.AutoVfr.Enable60F);
                            text.KeyValue("フィルタ-SKIP", Filter.AutoVfr.Skip.ToString());
                            text.KeyValue("フィルタ-REF", Filter.AutoVfr.Ref.ToString());
                            text.KeyValue("フィルタ-CROP", Filter.AutoVfr.EnableCrop);
                            break;
                    }
                }
                text.KeyValue("フィルタ-デブロッキング", Data.FilterSetting.EnableDeblock);
                if (Data.FilterSetting.EnableDeblock)
                {
                    text.KeyValue("フィルタ-デブロッキング強度", DisplayFilterSetting.DeblockStrengthList[(int)Data.FilterSetting.DeblockStrength]);
                    text.KeyValue("フィルタ-デブロッキング品質", DisplayFilterSetting.DeblockQualityList[Filter.DeblockQuality]);
                }
                text.KeyValue("フィルタ-リサイズ", Data.FilterSetting.EnableResize);
                if (Data.FilterSetting.EnableResize)
                {
                    text.KeyValue("フィルタ-リサイズ-縦", Data.FilterSetting.ResizeWidth.ToString());
                    text.KeyValue("フィルタ-リサイズ-横", Data.FilterSetting.ResizeHeight.ToString());
                }
                text.KeyValue("フィルタ-時間軸安定化", Data.FilterSetting.EnableTemporalNR);
                text.KeyValue("フィルタ-バンディング低減", Data.FilterSetting.EnableDeband);
                text.KeyValue("フィルタ-エッジ強調", Data.FilterSetting.EnableEdgeLevel);
            }
            else
            {
                text.KeyValue("メインフィルタ", Data.FilterPath);
                text.KeyValue("ポストフィルタ", Data.PostFilterPath);
            }

            text.KeyValue("2パス", Data.TwoPass);
            text.KeyValue("CMビットレート倍率", Data.BitrateCM.ToString());
            text.KeyValue("自動ビットレート指定", Data.AutoBuffer);
            text.KeyValue("自動ビットレート係数", string.Format("{0}:{1}:{2}", Data.Bitrate.A, Data.Bitrate.B, Data.Bitrate.H264));
            text.KeyValue("ニコニコ実況コメントを有効にする", Data.EnableNicoJK);
            text.KeyValue("ニコニコ実況コメントのエラーを無視する", Data.IgnoreNicoJKError);
            text.KeyValue("NicoJKログから優先的にコメントを取得する", Data.NicoJKLog);
            text.KeyValue("NicoJK18サーバからコメントを取得する", Data.NicoJK18);
            text.KeyValue("コメント出力フォーマット", Data.NicoJKFormatMask.ToString());
            text.KeyValue("関連ファイル(*.err,*.program.txt)も処理", Data.MoveEDCBFiles);
            text.KeyValue("字幕を無効にする", Data.DisableSubs);
            text.KeyValue("マッピングにないDRCS外字は無視する", Data.IgnoreNoDrcsMap);
            text.KeyValue("ロゴ検出判定しきい値を低くする", Data.LooseLogoDetection);
            text.KeyValue("ロゴ検出に失敗しても処理を続行する", Data.IgnoreNoLogo);
            text.KeyValue("ロゴ消ししない", Data.NoDelogo);
            text.KeyValue("メインフォーマット以外は結合しない", Data.SplitSub);
            text.KeyValue("システムにインストールされているAviSynthプラグインを有効にする", Data.SystemAviSynthPlugin);
            text.KeyValue("ネットワーク越しに転送する場合のハッシュチェックを無効にする", Data.DisableHashCheck);
            text.KeyValue("ログファイルを出力先に生成しない", Data.DisableLogFile);
            text.KeyValue("一時ファイルを削除せずに残す", Data.NoRemoveTmp);
            text.KeyValue("PMT更新によるCM認識", Data.EnablePmtCut
                ? string.Format("{0}:{1}", Data.PmtCutHeadRate, Data.PmtCutTailRate) : "なし");
            text.KeyValue("ロゴ最長フェードフレーム数指定", Data.EnableMaxFadeLength ? Data.MaxFadeLength.ToString() : "なし");
            text.KeyTable("スケジューリングリソース設定", GetResourceString());
            return text.ToString();
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

    public class DisplayGPUResource : NotificationObject
    {
        public Setting Model { get; set; }
        public int Index { get; set; }
        public int DisplayIndex { get { return Index + 1; } }

        #region Max変更通知プロパティ
        public int Max {
            get {
                if(Model?.MaxGPUResources == null)
                {
                    return 0;
                }
                return Model.MaxGPUResources[Index];
            }
            set {
                if (Model?.MaxGPUResources == null)
                    return;
                if (Model.MaxGPUResources[Index] == value)
                    return;
                Model.MaxGPUResources[Index] = value;
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class DisplayRunHour : NotificationObject
    {
        public Setting Model { get; set; }
        public int Hour { get; set; }
        public string HourText { get { return string.Format("{0:00}:00-{0:00}:59", Hour); } }

        #region Enabled変更通知プロパティ
        public bool Enabled {
            get {
                if (Model?.RunHours == null)
                {
                    return true;
                }
                return Model.RunHours[Hour];
            }
            set {
                if (Model?.RunHours == null)
                    return;
                if (Model.RunHours[Hour] == value)
                    return;
                Model.RunHours[Hour] = value;
                RaisePropertyChanged();
            }
        }
        #endregion
    }

    public class DisplaySetting : NotificationObject
    {
        public Setting Model { get; set; }

        public DisplayGPUResource[] GPUResources { get; private set; }

        public DisplayRunHour[] RunHours { get; private set; }

        private void RefreshGPUResources()
        {
            GPUResources = Enumerable.Range(0, Model.NumGPU).Select(
                i => new DisplayGPUResource() { Model = Model, Index = i }).ToArray();
            RaisePropertyChanged("GPUResources");
        }

        public void Refresh()
        {
            RefreshGPUResources();

            RunHours = Enumerable.Range(0, 24).Select(
                i => new DisplayRunHour() { Model = Model, Hour = i }).ToArray();
            RaisePropertyChanged("RunHours");
        }

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
                RaisePropertyChanged("NumParallelIndex");
                RaisePropertyChanged();
            }
        }

        private List<int> NumParallelList = new List<int>() {
            1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 20, 24, 28, 32, 40, 48, 56, 64, 80, 100, 120
        };

        public int NumParallelIndex {
            get { return NumParallelList.IndexOf(Model.NumParallel); }
            set {
                var numParallel = NumParallelList[value];
                if (Model.NumParallel == numParallel)
                    return;
                Model.NumParallel = numParallel;
                RaisePropertyChanged("NumParallel");
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

        #region AutoVfrPath変更通知プロパティ
        public string AutoVfrPath {
            get { return Model.AutoVfrPath; }
            set { 
                if (Model.AutoVfrPath == value)
                    return;
                Model.AutoVfrPath = value;
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
                // GUIの表示にしか関係しないので、
                // 設定の適用ボタンを押したときにサーバに反映されるゆるい同期を採用
                Model.HideOneSeg = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ListStyle変更通知プロパティ
        public int ListStyle {
            get { return Model.ListStyle; }
            set { 
                if (Model.ListStyle == value)
                    return;
                // GUIの表示にしか関係しないので、
                // 設定の適用ボタンを押したときにサーバに反映されるゆるい同期を採用
                Model.ListStyle = value;
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

        #region AffinitySetting変更通知プロパティ
        public int AffinitySetting {
            get { return (int)Model.AffinitySetting; }
            set { 
                if (Model.AffinitySetting == value)
                    return;
                Model.AffinitySetting = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ProcessPriority変更通知プロパティ
        public int ProcessPriority {
            get { return 1 - Model.ProcessPriority; }
            set {
                int newValue = 1 - value;
                if (Model.ProcessPriority == newValue)
                    return;
                Model.ProcessPriority = newValue;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SchedulingEnabled変更通知プロパティ
        public bool SchedulingEnabled {
            get { return Model.SchedulingEnabled; }
            set { 
                if (Model.SchedulingEnabled == value)
                    return;
                Model.SchedulingEnabled = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NumGPU変更通知プロパティ
        public int NumGPU {
            get { return Model.NumGPU; }
            set { 
                if (Model.NumGPU == value)
                    return;
                Model.NumGPU = value;
                RefreshGPUResources();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableX265VFRTimeFactor変更通知プロパティ
        public bool EnableX265VFRTimeFactor
        {
            get { return Model.EnableX265VFRTimeFactor; }
            set
            {
                if (Model.EnableX265VFRTimeFactor == value)
                    return;
                Model.EnableX265VFRTimeFactor = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region X265VFRTimeFactor変更通知プロパティ
        public double X265VFRTimeFactor {
            get { return Model.X265VFRTimeFactor; }
            set {
                if (Model.X265VFRTimeFactor == value)
                    return;
                Model.X265VFRTimeFactor = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region DumpFilter変更通知プロパティ
        public bool DumpFilter
        {
            get { return Model.DumpFilter; }
            set
            {
                if (Model.DumpFilter == value)
                    return;
                Model.DumpFilter = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PauseOnStarted変更通知プロパティ
        public bool PauseOnStarted {
            get { return Model.PauseOnStarted; }
            set { 
                if (Model.PauseOnStarted == value)
                    return;
                Model.PauseOnStarted = value;
                RaisePropertyChanged("PauseOnStarted");
            }
        }
        #endregion

        #region PrintTimePrefix変更通知プロパティ
        public bool PrintTimePrefix {
            get { return Model.PrintTimePrefix; }
            set {
                if (Model.PrintTimePrefix == value)
                    return;
                Model.PrintTimePrefix = value;
                RaisePropertyChanged("PrintTimePrefix");
            }
        }
        #endregion

        #region EnableShutdownAction変更通知プロパティ
        public bool EnableShutdownAction {
            get { return Model.EnableShutdownAction; }
            set { 
                if (Model.EnableShutdownAction == value)
                    return;
                Model.EnableShutdownAction = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EnableRunHours変更通知プロパティ
        public bool EnableRunHours {
            get { return Model.EnableRunHours; }
            set { 
                if (Model.EnableRunHours == value)
                    return;
                Model.EnableRunHours = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region RunHoursSuspendEncodersIndex変更通知プロパティ
        public string[] RunHoursSuspendEncodersList { get; private set; } = new string[]
        {
            "キューのみ", "キューと実行中のエンコーダの両方"
        };

        public int RunHoursSuspendEncodersIndex {
            get { return Model.RunHoursSuspendEncoders ? 1 : 0; }
            set {
                var valuei = (value == 0) ? false : true;
                if (Model.RunHoursSuspendEncoders == valuei)
                    return;
                Model.RunHoursSuspendEncoders = valuei;
                RaisePropertyChanged();
            }
        }
        #endregion

    }

    public class DisplayFinishSetting : NotificationObject
    {
        public ClientModel Model { get; set; }

        public FinishSetting Data { get; set; }

        #region Action変更通知プロパティ
        public int Action {
            get { return (int)Data.Action; }
            set {
                if (Data.Action == (FinishAction)value)
                    return;
                Data.Action = (FinishAction)value;
                RaisePropertyChanged();
                Model.SendFinishSetting();
            }
        }
        #endregion
    }

    public class DisplayUIState
    {
        public UIState Model { get; set; }
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

        #region AddQueueBat変更通知プロパティ
        public string AddQueueBat
        {
            get { return string.IsNullOrEmpty(Model.AddQueueBat) ? "なし" : Model.AddQueueBat; }
            set
            {
                string val = (value == "なし") ? "" : value;
                if (Model.AddQueueBat == val)
                    return;
                Model.AddQueueBat = val;
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

        #region TagEnabled変更通知プロパティ
        public bool TagEnabled {
            get { return Item.TagEnabled; }
            set { 
                if (Item.TagEnabled == value)
                    return;
                Item.TagEnabled = value;
                ApplyCondition();
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Tag変更通知プロパティ
        public string Tag {
            get { return Item.Tag; }
            set { 
                if (Item.Tag == value)
                    return;
                Item.Tag = value;
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
            if (Item.TagEnabled)
            {
                conds.Add("タグ「" + Item.Tag + "」を含む");
            }
            if (Item.FileNameEnabled)
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
            Item.Profile = (SelectedProfile as DisplayProfile)?.Data?.Name;
        }
    }

    public class DisplayAutoSelect : NotificationObject
    {
        public AutoSelectProfile Model { get; set; }

        #region Name変更通知プロパティ
        public string Name {
            get { return Model.Name; }
            set { 
                if (Model.Name == value)
                    return;
                Model.Name = value;
                RaisePropertyChanged();
            }
        }
        #endregion

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
