using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Windows.Media.Imaging;

namespace Amatsukaze.Server
{
    [DataContract]
    public class BitrateSetting : IExtensibleDataObject
    {
        [DataMember]
        public double A { get; set; }
        [DataMember]
        public double B { get; set; }
        [DataMember]
        public double H264 { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }
    }

    public enum EncoderType
    {
        x264 = 0,
        x265,
        QSVEnc,
        NVEnc
    }

    public enum DecoderType
    {
        Default = 0,
        QSV,
        CUVID
    }

    public enum FormatType
    {
        MP4 = 0,
        MKV,
        M2TS,
        TS
    }

    public enum FinishAction
    {
        None, Suspend, Hibernate, Shutdown
    }

    // サーバに要求する情報の識別フラグ
    [Flags]
    public enum ServerRequest
    {
        Setting = 1,
        Queue = (1 << 1),
        Log = (1 << 2),
        CheckLog = (1 << 3),
        Console = (1 << 4),
        State = (1 << 5),
        FreeSpace = (1 << 6),
        ServiceSetting = (1 << 7),
    }

    public struct ReqResource
    {
        public int CPU, HDD, GPU;

        public int Canonical()
        {
            return (CPU << 16) + (HDD << 8) + GPU;
        }

        public static ReqResource FromCanonical(int c)
        {
            return new ReqResource()
            {
                CPU = (c >> 16) & 0xFF,
                HDD = (c >> 8) & 0xFF,
                GPU = c & 0xFF
            };
        }
    }

    public class Resource
    {
        public ReqResource Req;
        public int GpuIndex;
        public int EncoderIndex;
    }

    public enum DeblockStrength
    {
        Strong, Medium, Weak, Weaker
    }

    public enum DeinterlaceAlgorithm
    {
        KFM, D3DVP, QTGMC, Yadif, AutoVfr
    }

    public enum D3DVPGPU
    {
        Auto, Intel, NVIDIA, Radeon
    }

    public enum FilterFPS
    {
        VFR, CFR24, CFR30, CFR60, SVP, VFR30
    }

    public enum FilterOption
    {
        None, Setting, Custom
    }

    public enum QTGMCPreset
    {
        Auto, Faster, Fast, Medium, Slow, Slower
    }

    [DataContract]
    public class FilterSetting : IExtensibleDataObject
    {
        [DataMember]
        public bool EnableCUDA;
        [DataMember]
        public bool EnableDeblock;
        [DataMember]
        public int DeblockQuality;
        [DataMember]
        public DeblockStrength DeblockStrength;
        [DataMember]
        public bool DeblockSharpen;
        [DataMember]
        public bool EnableDeinterlace;
        [DataMember]
        public DeinterlaceAlgorithm DeinterlaceAlgorithm;
        [DataMember]
        public D3DVPGPU D3dvpGpu;
        [DataMember]
        public QTGMCPreset QtgmcPreset;
        [DataMember]
        public bool KfmEnableNr;
        [DataMember]
        public bool KfmEnableUcf;
        [DataMember]
        public bool KfmVfr120fps;
        [DataMember]
        public FilterFPS KfmFps;
        [DataMember]
        public FilterFPS YadifFps;
        [DataMember]
        public int AutoVfrParallel;
        [DataMember]
        public bool AutoVfrFast;
        [DataMember]
        public bool AutoVfr30F;
        [DataMember]
        public bool AutoVfr60F;
        [DataMember]
        public bool AutoVfr24A;
        [DataMember]
        public bool AutoVfr30A;
        [DataMember]
        public bool AutoVfrCrop;
        [DataMember]
        public int AutoVfrSkip;
        [DataMember]
        public int AutoVfrRef;
        [DataMember]
        public bool EnableResize;
        [DataMember]
        public int ResizeWidth;
        [DataMember]
        public int ResizeHeight;
        [DataMember]
        public bool EnableTemporalNR;
        [DataMember]
        public bool EnableDeband;
        [DataMember]
        public bool EnableEdgeLevel;

        public ExtensionDataObject ExtensionData { get; set; }
    }

    // プロファイル設定データ
    public class ProfileSetting : IExtensibleDataObject
    {
        [DataMember]
        public string Name { get; set; }
        [DataMember]
        public DateTime LastUpdate { get; set; }
        [DataMember]
        public EncoderType EncoderType { get; set; }
        [DataMember]
        public string X264Option { get; set; }
        [DataMember]
        public string X265Option { get; set; }
        [DataMember]
        public string QSVEncOption { get; set; }
        [DataMember]
        public string NVEncOption { get; set; }

        [DataMember]
        public DecoderType Mpeg2Decoder { get; set; }
        [DataMember]
        public DecoderType H264Deocder { get; set; }
        [DataMember]
        public FormatType OutputFormat { get; set; }

        [DataMember]
        public string FilterPath { get; set; }
        [DataMember]
        public string PostFilterPath { get; set; }

        [DataMember]
        public bool TwoPass { get; set; }
        [DataMember]
        public bool SplitSub { get; set; }
        [DataMember]
        public int OutputMask { get; set; }
        [DataMember]
        public bool AutoBuffer { get; set; }
        [DataMember]
        public BitrateSetting Bitrate { get; set; }
        [DataMember]
        public double BitrateCM { get; set; }

        [DataMember]
        public string JLSCommandFile { get; set; }
        [DataMember]
        public string JLSOption { get; set; }
        [DataMember]
        public string ChapterExeOption { get; set; }
        [DataMember]
        public bool EnableJLSOption { get; set; }
        [DataMember]
        public bool DisableChapter { get; set; }
        [DataMember]
        public bool DisableSubs { get; set; }
        [DataMember]
        public bool IgnoreNoDrcsMap { get; set; }
        [DataMember]
        public bool IgnoreAudioFormat { get; set; }
        [DataMember]
        public bool LooseLogoDetection { get; set; }
        [DataMember]
        public bool IgnoreNoLogo { get; set; }
        [DataMember]
        public bool NoDelogo { get; set; }
        [DataMember]
        public bool SystemAviSynthPlugin { get; set; }
        [DataMember]
        public bool DisableHashCheck { get; set; }
        [DataMember]
        public bool EnableNicoJK { get; set; }
        [DataMember]
        public bool IgnoreNicoJKError { get; set; }
        [DataMember]
        public bool NicoJK18 { get; set; }
        [DataMember]
        public bool NicoJKLog { get; set; }
        [DataMember]
        public bool[] NicoJKFormats { get; set; }
        [DataMember]
        public bool MoveEDCBFiles { get; set; }
        [DataMember]
        public bool NoRemoveTmp { get; set; }
        [DataMember]
        public bool DisableLogFile { get; set; }

        [DataMember]
        public bool EnableMaxFadeLength { get; set; }
        [DataMember]
        public int MaxFadeLength { get; set; }

        [DataMember]
        public bool EnableRename { get; set; }
        [DataMember]
        public string RenameFormat { get; set; }
        [DataMember]
        public bool EnableGunreFolder { get; set; }
        [DataMember]
        public ReqResource[] ReqResources { get; set; }

        [DataMember]
        public bool EnablePmtCut { get; set; }
        [DataMember]
        public double PmtCutHeadRate { get; set; }
        [DataMember]
        public double PmtCutTailRate { get; set; }

        [DataMember]
        public bool IgnoreEncodeAffinity { get; set; }

        [DataMember]
        public string PreBatchFile { get; set; }
        [DataMember]
        public string PostBatchFile { get; set; }

        [DataMember]
        public FilterOption FilterOption { get; set; }
        [DataMember]
        public FilterSetting FilterSetting { get; set; }
        [DataMember]
        public int NumEncodeBufferFrames { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }

        public int NicoJKFormatMask
        {
            get
            {
                int mask = 0;
                for (int i = NicoJKFormats.Length - 1; i >= 0; --i)
                {
                    mask <<= 1;
                    mask |= NicoJKFormats[i] ? 1 : 0;
                }
                return mask;
            }
        }
    }

    // プロファイルの更新情報
    [DataContract]
    public class ProfileUpdate
    {
        [DataMember]
        public UpdateType Type { get; set; }
        [DataMember]
        public ProfileSetting Profile { get; set; }
        [DataMember]
        public string NewName { get; set; }
    }

    // 基本設定データ
    [DataContract]
    public class Setting : IExtensibleDataObject
    {
        [DataMember]
        public string AmatsukazePath { get; set; }
        [DataMember]
        public string X264Path { get; set; }
        [DataMember]
        public string X265Path { get; set; }
        [DataMember]
        public string QSVEncPath { get; set; }
        [DataMember]
        public string NVEncPath { get; set; }
        [DataMember]
        public string MuxerPath { get; set; }
        [DataMember]
        public string MKVMergePath { get; set; }
        [DataMember]
        public string MP4BoxPath { get; set; }
        [DataMember]
        public string TimelineEditorPath { get; set; }
        [DataMember]
        public string ChapterExePath { get; set; }
        [DataMember]
        public string JoinLogoScpPath { get; set; }
        [DataMember]
        public string NicoConvASSPath { get; set; }
        [DataMember]
        public string TsMuxeRPath { get; set; }
        [DataMember]
        public string SCRenamePath { get; set; }
        [DataMember]
        public string AutoVfrPath { get; set; }

        [DataMember]
        public string WorkPath { get; set; }
        [DataMember]
        public string AlwaysShowDisk { get; set; }

        [DataMember]
        public int NumParallel { get; set; }
        [DataMember]
        public int AffinitySetting { get; set; } // 中身はProcessGroupKind
        [DataMember]
        public int ProcessPriority { get; set; } // 0が"通常以下"

        [DataMember]
        public bool ClearWorkDirOnStart { get; set; }
        [DataMember]
        public bool HideOneSeg { get; set; }
        [DataMember]
        public int ListStyle { get; set; }
        [DataMember]
        public bool SupressSleep { get; set; }
        [DataMember]
        public bool DumpFilter { get; set; }
        [DataMember]
        public bool SchedulingEnabled { get; set; }
        [DataMember]
        public int NumGPU { get; set; }
        [DataMember]
        public int[] MaxGPUResources { get; set; } // 長さは常に16

        [DataMember]
        public bool EnableX265VFRTimeFactor { get; set; }
        [DataMember]
        public double X265VFRTimeFactor { get; set; }

        [DataMember]
        public bool PauseOnStarted { get; set; }
        [DataMember]
        public bool PrintTimePrefix { get; set; }
        [DataMember]
        public bool EnableShutdownAction { get; set; }

        [DataMember]
        public bool EnableRunHours { get; set; }
        [DataMember]
        public bool RunHoursSuspendEncoders { get; set; }
        [DataMember]
        public bool[] RunHours { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }

        public string ActualWorkPath
        {
            get
            {
                return string.IsNullOrEmpty(WorkPath) ? "./" : WorkPath;
            }
        }

        public System.Diagnostics.ProcessPriorityClass ProcessPriorityClass {
            get {
                return new System.Diagnostics.ProcessPriorityClass[]
                {
                    System.Diagnostics.ProcessPriorityClass.Idle,
                    System.Diagnostics.ProcessPriorityClass.BelowNormal,
                    System.Diagnostics.ProcessPriorityClass.Normal
                }[ProcessPriority + 1];
            }
        }
    }

    // 基本設定データ
    [DataContract]
    public class UIState : IExtensibleDataObject
    {
        [DataMember]
        public string LastUsedProfile { get; set; }
        [DataMember]
        public string LastOutputPath { get; set; }
        [DataMember]
        public string LastAddQueueBat { get; set; }
        [DataMember]
        public List<string> OutputPathHistory { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }
    }

    // ロゴ設定
    [DataContract]
    public class LogoSetting : IExtensibleDataObject
    {
        [DataMember]
        public bool Exists { get; set; }
        [DataMember]
        public string FileName { get; set; }
        [DataMember]
        public string LogoName { get; set; }
        [DataMember]
        public int ServiceId { get; set; }
        [DataMember]
        public bool Enabled { get; set; }
        [DataMember]
        public DateTime From { get; set; }
        [DataMember]
        public DateTime To { get; set; }

        public bool CanUse(DateTime tstime)
        {
            return Exists && Enabled &&
                (tstime == DateTime.MinValue || (From <= tstime && tstime <= To));
        }

        public ExtensionDataObject ExtensionData { get; set; }

        public static readonly string NO_LOGO = "### NO LOGO ###";
    }

    // チャンネル設定の1チャンネル分
    [DataContract]
    public class ServiceSettingElement : IExtensibleDataObject
    {
        [DataMember]
        public int ServiceId { get; set; }
        [DataMember]
        public string ServiceName { get; set; }
        [DataMember]
        public bool DisableCMCheck { get; set; }
        [DataMember]
        public string JLSCommand { get; set; }
        [DataMember]
        public string JLSOption { get; set; }
        [DataMember]
        public List<LogoSetting> LogoSettings { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }
    }

    // チャンネル設定（全チャンネル分）
    [DataContract]
    public class ServiceSetting : IExtensibleDataObject
    {
        [DataMember]
        public Dictionary<int, ServiceSettingElement> ServiceMap { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }
    }

    public enum ProcMode
    {
        Batch,
        AutoBatch,
        Test,
        DrcsCheck,
        CMCheck,
    }

    [DataContract]
    public class AddQueueItem
    {
        [DataMember]
        public string Path; // フルパス
        [DataMember]
        public byte[] Hash; // null可
    }

    [DataContract]
    public class OutputInfo
    {
        [DataMember]
        public string DstPath { get; set; }
        [DataMember]
        public string Profile { get; set; }
        [DataMember]
        public int Priority { get; set; }
    }

    [DataContract]
    public class AddQueueRequest
    {
        [DataMember]
        public string DirPath { get; set; }
        [DataMember]
        public List<AddQueueItem> Targets { get; set; }
        [DataMember]
        public ProcMode Mode { get; set; }
        [DataMember]
        public List<OutputInfo> Outputs { get; set; }
        [DataMember]
        public string RequestId { get; set; }
        [DataMember]
        public string AddQueueBat { get; set; }

        public bool IsBatch { get { return Mode == ProcMode.Batch || Mode == ProcMode.AutoBatch; } }
        public bool IsCheck { get { return Mode == ProcMode.DrcsCheck || Mode == ProcMode.CMCheck; } }
    }

    public enum QueueState
    {
        Queue,          // キュー状態
        Encoding,       // エンコード中
        Complete,       // 完了
        Failed,         // 失敗
        PreFailed,      // エンコード始める前に失敗
        LogoPending,    // ペンディング
        Canceled,       // キャンセルされた
    }

    public enum GenreSpace
    {
        ARIB = 0,
        CS = 2,
    }

    // ジャンル
    [DataContract]
    public class GenreItem
    {
        // Level1==0xE(拡張)かどうかで入れられる値が変わる
        // 拡張でない場合
        //   Space は GenreSpace.ARIB
        //   Level1,Level2は通常のLevel1,Level2
        // 拡張の場合
        //   Space は Level2 + 1
        //   Level1,Level2はUser1,User2
        // SpaceはGenreSpaceが対応しているが、事業者定義の値であるため
        // GenreSpaceにない値が使用されることがあるのでint型とする
        // （GenreSpaceにするとシリアライズでエラーとなるので）
        [DataMember]
        public int Space;
        [DataMember]
        public int Level1;
        [DataMember]
        public int Level2;
    }

    [DataContract]
    public class QueueItem
    {
        [DataMember]
        public int Id { get; set; }
        [DataMember]
        public ProcMode Mode { get; set; }
        [DataMember]
        public ProfileSetting Profile { get; set; }
        [DataMember]
        public string SrcPath { get; set; }
        [DataMember]
        public string DstPath { get; set; }
        [DataMember]
        public byte[] Hash { get; set; }
        [DataMember]
        public QueueState State { get; set; }
        [DataMember]
        public int Priority { get; set; }
        [DataMember]
        public DateTime AddTime { get; set; }

        [DataMember]
        public int ServiceId { get; set; }
        [DataMember]
        public string ServiceName { get; set; }
        [DataMember]
        public int ImageWidth { get; set; }
        [DataMember]
        public int ImageHeight { get; set; }
        [DataMember]
        public DateTime TsTime { get; set; }

        [DataMember]
        public string FailReason { get; set; }
        [DataMember]
        public string JlsCommand { get; set; }

        [DataMember]
        public string ProfileName { get; set; }
        [DataMember]
        public string EventName { get; set; }
        [DataMember]
        public List<GenreItem> Genre { get; set; }

        [DataMember]
        public string ActualDstPath { get; set; }
        [DataMember]
        public int ConsoleId { get; set; }

        [DataMember]
        public DateTime EncodeStart { get; set; }
        [DataMember]
        public TimeSpan EncodeTime { get; set; }

        [DataMember]
        public List<string> Tags { get; set; }

        // 内部処理順決定用パラメータ
        public int Order { get; set; }

        public DateTime EncodeFinish
        {
            get
            {
                if(EncodeStart == DateTime.MinValue || EncodeTime == TimeSpan.Zero)
                {
                    return DateTime.MinValue;
                }
                return EncodeStart + EncodeTime;
            }
        }

        public bool IsBatch { get { return Mode == ProcMode.Batch || Mode == ProcMode.AutoBatch; } }
        public bool IsCheck { get { return Mode == ProcMode.DrcsCheck || Mode == ProcMode.CMCheck; } }
        public bool IsTest { get { return Mode == ProcMode.Test; } }

        public string DirName { get { return Path.GetDirectoryName(SrcPath); } }
        public string FileName { get { return Path.GetFileName(SrcPath); } }

        public bool IsActive {
            get {
                return State == QueueState.Encoding ||
                    State == QueueState.LogoPending ||
                    State == QueueState.Queue;
            }
        }

        public bool IsOneSeg {
            get {
                return ImageWidth <= 320 || ImageHeight <= 260;
            }
        }

        // プロファイルが決定してる時だけ有効
        public bool IsSeparateHashRequired
        {
            get
            {
                return Mode == ProcMode.Batch &&
                    Profile.DisableHashCheck == false &&
                    SrcPath.StartsWith("\\\\");
            }
        }

        public void Reset()
        {
            State = QueueState.LogoPending;
            EncodeStart = new DateTime();
            EncodeTime = new TimeSpan();
        }
    }

    [DataContract]
    public class QueueData
    {
        [DataMember]
        public List<QueueItem> Items { get; set; }
    }

    public enum UpdateType
    {
        Add, Remove, Update, Clear, Move
    }

    [DataContract]
    public class QueueUpdate
    {
        [DataMember]
        public UpdateType Type { get; set; }
        [DataMember]
        public QueueItem Item { get; set; }
        [DataMember]
        public int Position { get; set; }
    }

    public enum ChangeItemType
    {
        ResetState,      // リトライ
        UpdateProfile,// プロファイルを更新してリトライ
        Duplicate,      // 同じタスクを投入
        Cancel,     // キャンセル
        Priority,   // 優先度変更
        Profile,    // プロファイル変更
        RemoveItem, // アイテム削除
        RemoveCompleted,  // 完了項目を削除
        ForceStart, // アイテムを強制的に開始
        RemoveSourceFile, // ソースTSを削除（通常or自動追加の完了した項目のみ有効）
        Move,       // リスト上で移動
    }

    [DataContract]
    public class ChangeItemData
    {
        [DataMember]
        public int ItemId { get; set; }
        [DataMember]
        public ChangeItemType ChangeType { get; set; }
        [DataMember]
        public int Priority { get; set; }
        [DataMember]
        public string Profile { get; set; }
        [DataMember]
        public int Position { get; set; }
    }

    [DataContract]
    public class AudioDiff
    {
        [DataMember]
        public int TotalSrcFrames { get; set; }
        [DataMember]
        public int TotalOutFrames { get; set; }
        [DataMember]
        public int TotalOutUniqueFrames { get; set; }
        [DataMember]
        public double NotIncludedPer { get; set; }
        [DataMember]
        public double AvgDiff { get; set; }
        [DataMember]
        public double MaxDiff { get; set; }
        [DataMember]
        public double MaxDiffPos { get; set; }
    }

    [DataContract]
    public class ErrorCount : IExtensibleDataObject
    {
        [DataMember]
        public string Name;
        [DataMember]
        public int Count;

        public ExtensionDataObject ExtensionData { get; set; }
    }

    public enum CheckType
    {
        DRCS, // DRCSサーチ
        CM    // CM解析
    }

    [DataContract]
    public class CheckLogItem : IExtensibleDataObject
    {
        [DataMember]
        public CheckType Type { get; set; }
        [DataMember]
        public string SrcPath { get; set; }
        [DataMember]
        public bool Success { get; set; }
        [DataMember]
        public DateTime CheckStartDate { get; set; }
        [DataMember]
        public DateTime CheckFinishDate { get; set; }
        [DataMember]
        public string Profile { get; set; }
        [DataMember]
        public string ServiceName { get; set; }
        [DataMember]
        public int ServiceId { get; set; }
        [DataMember]
        public DateTime TsTime { get; set; }
        [DataMember]
        public string Reason { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }

        public string DisplayType {
            get {
                if (Type == CheckType.CM) return "CM解析";
                else if (Type == CheckType.DRCS) return "DRCSチェック";
                else return "不明";
            }
        }
        public TimeSpan EncodeDuration { get { return CheckFinishDate - CheckStartDate; } }
        public string DisplayResult { get { return Success ? "〇" : "×"; } }
        public string DisplaySrcDirectory { get { return Path.GetDirectoryName(SrcPath); } }
        public string DisplaySrcFileName { get { return Path.GetFileName(SrcPath); } }
        public string DisplayEncodeStart { get { return CheckStartDate.ToGUIString(); } }
        public string DisplayEncodeFinish { get { return CheckFinishDate.ToGUIString(); } }
        public string DisplayEncodeDuration { get { return EncodeDuration.ToGUIString(); } }
        public string DisplayReason { get { return Reason; } }
        public string DisplayService { get { return ServiceName + "(" + ServiceId + ")"; } }
        public string DisplayTsTime { get { return (TsTime == DateTime.MinValue) ? "不明" : TsTime.ToString("yyyy年M月d日"); } }
    }

    [DataContract]
    public class CheckLogData : IExtensibleDataObject
    {
        [DataMember]
        public List<CheckLogItem> Items { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }
    }

    [DataContract]
    public class LogItem : IExtensibleDataObject
    {
        [DataMember]
        public string SrcPath { get; set; }
        [DataMember]
        public bool Success { get; set; }
        [DataMember]
        public string DstPath { get; set; } // 拡張子を含まないメイン出力先パス
        [DataMember]
        public List<string> OutPath { get; set; } // 実際に出力されたファイル
        [DataMember]
        public DateTime EncodeStartDate { get; set; }
        [DataMember]
        public DateTime EncodeFinishDate { get; set; }
        [DataMember]
        public TimeSpan SrcVideoDuration { get; set; }
        [DataMember]
        public TimeSpan OutVideoDuration { get; set; }
        [DataMember]
        public long SrcFileSize { get; set; }
        [DataMember]
        public long IntVideoFileSize { get; set; }
        [DataMember]
        public long OutFileSize { get; set; }
        [DataMember]
        public AudioDiff AudioDiff { get; set; }
        [DataMember]
        public string Reason { get; set; }
        [DataMember]
        public string MachineName { get; set; }
        [DataMember]
        public List<string> LogoFiles { get; set; }

        [DataMember]
        public bool Chapter { get; set; }
        [DataMember]
        public bool NicoJK { get; set; }
        [DataMember]
        public bool TrimAVS { get; set; }
        [DataMember]
        public int OutputMask { get; set; }
        [DataMember]
        public string ServiceName { get; set; }
        [DataMember]
        public int ServiceId { get; set; }
        [DataMember]
        public DateTime TsTime { get; set; }

        [DataMember]
        public int Incident { get; set; }
        [DataMember]
        public List<ErrorCount> Error { get; set; }

        [DataMember]
        public string Profile { get; set; }

        [DataMember]
        public List<string> Tags { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }

        // アクセサ
        public TimeSpan EncodeDuration { get { return EncodeFinishDate - EncodeStartDate; } }
        public string DisplayResult { get { return Success ? ((Incident > 0) ? "△" : "〇") : "×"; } }
        public string DisplaySrcDirectory { get { return Path.GetDirectoryName(SrcPath); } }
        public string DisplaySrcFileName { get { return Path.GetFileName(SrcPath); } }
        public string DisplayOutDirectory { get { return (OutPath != null && OutPath.Count > 0) ? Path.GetDirectoryName(OutPath[0]) : "-"; } }
        public IEnumerable<string> DisplayOutFile { get { return (OutPath == null) ? null : OutPath.Select(s => Path.GetFileName(s)); } }
        public string DisplayOutNum { get { return (OutPath == null) ? "-" : OutPath.Count.ToString(); } }
        public string DisplayNumIncident { get { return Incident.ToString(); } }
        public string DisplayEncodeStart { get { return EncodeStartDate.ToGUIString(); } }
        public string DisplayEncodeFinish { get { return EncodeFinishDate.ToGUIString(); } }
        public string DisplayEncodeDuration { get { return EncodeDuration.ToGUIString(); } }
        public string DisplaySrcDurationo { get { return (SrcVideoDuration != null) ? SrcVideoDuration.ToGUIString() : null; } }
        public string DisplayOutDuration { get { return (OutVideoDuration != null) ? OutVideoDuration.ToGUIString() : null; } }
        public string DisplayVideoNotIncluded
        {
            get
            {
                if (SrcVideoDuration == null) return null;
                var s = SrcVideoDuration.TotalMilliseconds;
                var o = OutVideoDuration.TotalMilliseconds;
                return ((double)(s - o) / (double)s * 100.0).ToString("F2");
            }
        }
        public string DisplaySrcFileSize { get { return ((double)SrcFileSize / (1024 * 1024)).ToString("F2"); } }
        public string DisplayIntFileSize { get { return ((double)IntVideoFileSize / (1024 * 1024)).ToString("F2"); } }
        public string DisplayOutFileSize { get { return ((double)OutFileSize / (1024 * 1024)).ToString("F2"); } }
        public string DisplayIntVideoRate { get { return ((double)IntVideoFileSize / (double)SrcFileSize * 100).ToString("F2"); } }
        public string DisplayCompressionRate { get { return ((double)OutFileSize / (double)SrcFileSize * 100).ToString("F2"); } }
        public string DisplaySrcAudioFrames { get { return (AudioDiff != null) ? AudioDiff.TotalSrcFrames.ToString() : null; } }
        public string DisplayOutAudioFrames { get { return (AudioDiff != null) ? AudioDiff.TotalOutFrames.ToString() : null; } }
        public string DisplayAudioNotIncluded { get { return (AudioDiff != null) ? AudioDiff.NotIncludedPer.ToString("F3") : null; } }
        public string DisplayAvgAudioDiff { get { return (AudioDiff != null) ? AudioDiff.AvgDiff.ToString("F2") : null; } }
        public string DisplayReason { get { return Reason; } }
        public string DisplayAudioMaxDiff { get { return (AudioDiff != null) ? AudioDiff.MaxDiff.ToString("F2") : null; } }
        public string DisplayAudioMaxDiffPos { get { return (AudioDiff != null) ? AudioDiff.MaxDiffPos.ToString("F2") : null; } }
        public string DisplayEncodeSpeed
        {
            get
            {
                return (SrcVideoDuration != null)
? (SrcVideoDuration.TotalSeconds / EncodeDuration.TotalSeconds).ToString("F2")
: null;
            }
        }
        public string DisplaySrcBitrate
        {
            get
            {
                return (SrcVideoDuration != null)
? ((double)SrcFileSize / (SrcVideoDuration.TotalSeconds * 128.0 * 1024)).ToString("F3")
: null;
            }
        }
        public string DisplayOutBitrate
        {
            get
            {
                return (SrcVideoDuration != null)
? ((double)OutFileSize / (OutVideoDuration.TotalSeconds * 128.0 * 1024)).ToString("F3")
: null;
            }
        }
        public string DisplayLogo { get { return LogoFiles?.FirstOrDefault() ?? "なし"; } }
        public string DisplayChapter { get { return Chapter ? "○" : "☓"; } }
        public string DisplayNicoJK { get { return NicoJK ? "○" : "☓"; } }
        public string DisplayTrimAVS { get { return TrimAVS ? "○" : "☓"; } }
        public string DisplayOutputMask {
            get {
                switch(OutputMask)
                {
                    case 1: return "通常";
                    case 2: return "CMをカット";
                    case 3: return "通常+CMカット";
                    case 4: return "CMのみ";
                    case 5: return "通常+CM";
                    case 6: return "本編とCMを分離";
                    case 7: return "通常+本編+CM";
                }
                return "通常";
            }
        }
        public string DisplayService { get { return ServiceName + "(" + ServiceId + ")"; } }
        public string DisplayTsTime { get { return (TsTime == DateTime.MinValue) ? "不明" : TsTime.ToString("yyyy年M月d日"); } }
        public string DisplayTags { get { return (Tags != null) ? string.Join(" ", Tags) : ""; } }
    }

    [DataContract]
    public class LogData : IExtensibleDataObject
    {
        [DataMember]
        public List<LogItem> Items { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }
    }

    [DataContract]
    public class LogFile
    {
        [DataMember]
        public long Id { get; set; }
        [DataMember]
        public string Content { get; set; }
    }

    [DataContract]
    public class ConsoleData
    {
        [DataMember]
        public int index { get; set; }
        [DataMember]
        public List<string> text { get; set; }
    }

    [DataContract]
    public class ConsoleUpdate
    {
        [DataMember]
        public int index { get; set; }
        [DataMember]
        public byte[] data { get; set; }
    }

    [DataContract]
    public class FinishSetting
    {
        [DataMember]
        public FinishAction Action { get; set; }
        [DataMember]
        public int Seconds { get; set; }
    }

    public enum StateChangeEvent
    {
        WorkersStarted,
        WorkersFinished,
        EncodeStarted,
        EncodeSucceeded,
        EncodeFailed,
        EncodeCanceled,
    }

    [DataContract]
    public class UIData
    {
        [DataMember]
        public State State { get; set; }
        [DataMember]
        public QueueData QueueData { get; set; }
        [DataMember]
        public QueueUpdate QueueUpdate { get; set; }
        [DataMember]
        public LogData LogData { get; set; }
        [DataMember]
        public LogItem LogItem { get; set; }
        [DataMember]
        public CheckLogData CheckLogData { get; set; }
        [DataMember]
        public CheckLogItem CheckLogItem { get; set; }
        [DataMember]
        public ConsoleData ConsoleData { get; set; }
        [DataMember]
        public EncodeState EncodeState { get; set; }
        [DataMember]
        public FinishSetting SleepCancel { get; set; }
        [DataMember]
        public StateChangeEvent? StateChangeEvent { get; set; }
    }

    [DataContract]
    public class EncodeState
    {
        [DataMember]
        public int ConsoleId { get; set; }
        [DataMember]
        public ResourcePhase Phase { get; set; }
        [DataMember]
        public Resource Resource { get; set; }
    }

    [DataContract]
    public class LogFileRequest
    {
        [DataMember]
        public LogItem LogItem { get; set; }
        [DataMember]
        public CheckLogItem CheckLogItem { get; set; }
    }

        [DataContract]
    public class DiskItem
    {
        [DataMember]
        public string Path { get; set; }
        [DataMember]
        public long Capacity { get; set; }
        [DataMember]
        public long Free { get; set; }
    }

    [DataContract]
    public class LogoData
    {
        [DataMember]
        public int ServiceId { get; set; }
        [DataMember]
        public string FileName { get; set; }
        [DataMember]
        public int ImageWith { get; set; }
        [DataMember]
        public int ImageHeight { get; set; }

        // これはシリアライズできないので、別処理で送信する
        public BitmapFrame Image { get; set; }
    }

    public enum ServiceSettingUpdateType
    {
        Update,
        AddNoLogo,
        Remove,
        RemoveLogo,
        Clear,
    }

    [DataContract]
    public class ServiceSettingUpdate
    {
        [DataMember]
        public ServiceSettingUpdateType Type { get; set; }

        [DataMember]
        public int ServiceId { get; set; }

        [DataMember]
        public ServiceSettingElement Data { get; set; }

        [DataMember]
        public int RemoveLogoIndex { get; set; }
    }

    public enum DrcsUpdateType
    {
        Remove, Update
    }

    [DataContract]
    public class DrcsSourceItem
    {
        // 当該TSファイル名
        [DataMember]
        public string FileName { get; set; }

        // DRCS文字を発見した日時（DRCSチェックやエンコードを走らせた時刻）
        [DataMember]
        public DateTime FoundTime { get; set; }

        // 当該DRCS文字出現の映像開始からの時間
        [DataMember]
        public TimeSpan Elapsed { get; set; }

        public override string ToString()
        {
            return Elapsed.ToString(@"h\:mm\:ss") + "@" + FileName;
        }
    }

    [DataContract]
    public class DrcsImage
    {
        [DataMember]
        public string MD5 { get; set; }
        [DataMember]
        public string MapStr { get; set; }

        // 出現リスト
        [DataMember]
        public DrcsSourceItem[] SourceList { get; set; }

        // これはシリアライズできないので、別処理で送信する
        public BitmapFrame Image { get; set; }
    }

    [DataContract]
    public class DrcsImageUpdate
    {
        [DataMember]
        public DrcsUpdateType Type { get; set; }

        [DataMember]
        public DrcsImage Image;

        [DataMember]
        public List<DrcsImage> ImageList;
    }

    [DataContract]
    public class ServerInfo
    {
        [DataMember]
        public string HostName { get; set; }
        [DataMember]
        public byte[] MacAddress { get; set; }
        [DataMember]
        public string Version { get; set; }
    }

    [DataContract]
    public class State
    {
        // キュー停止
        [DataMember]
        public bool Pause { get; set; }

        // エンコードサスペンド
        [DataMember]
        public bool Suspend { get; set; }

        // エンコードサスペンド
        [DataMember]
        public bool[] EncoderSuspended { get; set; }

        // エンコード実行中
        [DataMember]
        public bool Running { get; set; }

        // スケジューリングにより一時停止状態
        [DataMember]
        public bool ScheduledPause { get; set; }

        // スケジューリングにより一時停止状態
        [DataMember]
        public bool ScheduledSuspend { get; set; }

        // 進捗
        [DataMember]
        public double Progress { get; set; }
    }

    [DataContract]
    public class PauseRequest
    {
        // true: キュー停止, false: エンコーダ停止
        [DataMember]
        public bool IsQueue { get; set; }
        // 停止するエンコーダ -1の場合は全エンコーダ
        [DataMember]
        public int Index { get; set; }
        // 停止か稼働か
        [DataMember]
        public bool Pause { get; set; }
    }

    [DataContract]
    public class MakeScriptData
    {
        [DataMember]
        public string Profile { get; set; }
        [DataMember]
        public string OutDir { get; set; }
        [DataMember]
        public string NasDir { get; set; }
        [DataMember]
        public bool IsNasEnabled { get; set; }
        [DataMember]
        public bool IsWakeOnLan { get; set; }
        [DataMember]
        public bool MoveAfter { get; set; }
        [DataMember]
        public bool ClearEncoded { get; set; }
        [DataMember]
        public bool WithRelated { get; set; }
        [DataMember]
        public bool IsDirect { get; set; }
        [DataMember]
        public int Priority { get; set; }
        [DataMember]
        public string AddQueueBat { get; set; }
    }

    [DataContract]
    public class CommonData
    {
        [DataMember]
        public UIState UIState { get; set; }
        [DataMember]
        public Setting Setting { get; set; }
        [DataMember]
        public MakeScriptData MakeScriptData { get; set; }
        [DataMember]
        public List<string> JlsCommandFiles { get; set; }
        [DataMember]
        public List<string> MainScriptFiles { get; set; }
        [DataMember]
        public List<string> PostScriptFiles { get; set; }
        [DataMember]
        public List<DiskItem> Disks { get; set; }
        [DataMember]
        public List<int> CpuClusters { get; set; }
        [DataMember]
        public ServerInfo ServerInfo { get; set; }
        [DataMember]
        public List<string> AddQueueBatFiles { get; set; }
        [DataMember]
        public List<string> PreBatFiles { get; set; }
        [DataMember]
        public List<string> PostBatFiles { get; set; }
        [DataMember]
        public FinishSetting FinishSetting { get; set; }
    }

    [DataContract]
    public class OperationResult
    {
        [DataMember]
        public bool IsFailed { get; set; }
        [DataMember]
        public string Message { get; set; }
        [DataMember]
        public string StackTrace { get; set; }
    }

    public enum VideoSizeCondition
    {
        FullHD, HD1440, SD, OneSeg
    }

    [DataContract]
    public class AutoSelectCondition
    {
        [DataMember]
        public bool FileNameEnabled { get; set; }
        [DataMember]
        public string FileName { get; set; }
        [DataMember]
        public bool ContentConditionEnabled { get; set; }
        [DataMember]
        public List<GenreItem> ContentConditions { get; set; }
        [DataMember]
        public bool ServiceIdEnabled { get; set; }
        [DataMember]
        public List<int> ServiceIds { get; set; }
        [DataMember]
        public bool VideoSizeEnabled { get; set; }
        [DataMember]
        public List<VideoSizeCondition> VideoSizes { get; set; }
        [DataMember]
        public bool TagEnabled { get; set; }
        [DataMember]
        public string Tag { get; set; }
        [DataMember]
        public string Profile { get; set; }
        [DataMember]
        public int Priority { get; set; }
        [DataMember]
        public string Description { get; set; }
    }

    [DataContract]
    public class AutoSelectProfile
    {
        [DataMember]
        public string Name { get; set; }
        [DataMember]
        public List<AutoSelectCondition> Conditions { get; set; }
    }

    [DataContract]
    public class AutoSelectUpdate
    {
        [DataMember]
        public UpdateType Type { get; set; }
        [DataMember]
        public AutoSelectProfile Profile { get; set; }
        [DataMember]
        public string NewName { get; set; }
    }
}
