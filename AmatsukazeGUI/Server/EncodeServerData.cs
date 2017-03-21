using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
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
        public string EncoderName { get; set; }
        [DataMember]
        public string EncoderOption { get; set; }

        [DataMember]
        public string MuxerPath { get; set; }
        [DataMember]
        public string TimelineEditorPath { get; set; }

        [DataMember]
        public string WorkPath { get; set; }

        [DataMember]
        public string AlwaysShowDisk { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }
    }

    [DataContract]
    public class State
    {
        [DataMember]
        public string HostName { get; set; }
        [DataMember]
        public bool Pause { get; set; }
        [DataMember]
        public bool Running { get; set; }
    }

    [DataContract]
    public class QueueItem
    {
        [DataMember]
        public string Path { get; set; }
        [DataMember]
        public bool IsComplete { get; set; }
        [DataMember]
        public bool IsEncoding { get; set; }
    }

    [DataContract]
    public class QueueDirectory
    {
        [DataMember]
        public string Path { get; set; }
        [DataMember]
        public List<QueueItem> Items { get; set; }
    }

    [DataContract]
    public class QueueData
    {
        [DataMember]
        public List<QueueDirectory> Items { get; set; }
    }

    public enum UpdateType
    {
        Add, Remove, Update
    }

    [DataContract]
    public class QueueUpdate
    {
        [DataMember]
        public UpdateType Type { get; set; }
        [DataMember]
        public QueueDirectory Directory { get; set; }
        [DataMember]
        public string DirPath { get; set; }
        [DataMember]
        public QueueItem Item { get; set; }
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
    public class LogItem : IExtensibleDataObject
    {
        [DataMember]
        public string SrcPath { get; set; }
        [DataMember]
        public bool Success { get; set; }
        [DataMember]
        public List<string> OutPath { get; set; }
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

        public ExtensionDataObject ExtensionData { get; set; }

        // アクセサ
        public TimeSpan EncodeDuration { get { return EncodeFinishDate - EncodeStartDate; } }
        public string DisplayResult { get { return Success ? "〇" : "×"; } }
        public string DisplaySrcDirectory { get { return Path.GetDirectoryName(SrcPath); } }
        public string DisplaySrcFileName { get { return Path.GetFileName(SrcPath); } }
        public string DisplayEncodeStart { get { return EncodeStartDate.ToGUIString(); } }
        public string DisplayEncodeFinish { get { return EncodeFinishDate.ToGUIString(); } }
        public string DisplayEncodeDuration { get { return EncodeDuration.ToGUIString(); } }
        public string DisplaySrcDurationo { get { return SrcVideoDuration.ToGUIString(); } }
        public string DisplayOutDuration { get { return OutVideoDuration.ToGUIString(); } }
        public string DisplayVideoNotIncluded { get {
            var s = SrcVideoDuration.TotalMilliseconds;
            var o = OutVideoDuration.TotalMilliseconds;
            return ((double)(s - o) / (double)s * 100.0).ToString("F2");
        } }
        public string DisplaySrcFileSize { get { return ((double)SrcFileSize / (1024 * 1024)).ToString("F2"); } }
        public string DisplayIntFileSize { get { return ((double)IntVideoFileSize / (1024 * 1024)).ToString("F2"); } }
        public string DisplayOutFileSize { get { return ((double)OutFileSize / (1024 * 1024)).ToString("F2"); } }
        public string DisplayIntVideoRate { get { return ((double)IntVideoFileSize / (double)SrcFileSize * 100).ToString("F2"); } }
        public string DisplayCompressionRate { get { return ((double)OutFileSize / (double)SrcFileSize * 100).ToString("F2"); } }
        public string DisplaySrcAudioFrames { get { return AudioDiff.TotalSrcFrames.ToString(); } }
        public string DisplayOutAudioFrames { get { return AudioDiff.TotalOutFrames.ToString(); } }
        public string DisplayAudioNotIncluded { get { return AudioDiff.NotIncludedPer.ToString("F3"); } }
        public string DisplayAvgAudioDiff { get { return AudioDiff.AvgDiff.ToString("F2"); } }
        public string DisplayReason { get { return Reason; } }
        public string DisplayAudioMaxDiff { get { return AudioDiff.MaxDiff.ToString("F2"); } }
        public string DisplayAudioMaxDiffPos { get { return AudioDiff.MaxDiffPos.ToString("F2"); } }
        public string DisplayEncodeSpeed { get { return (SrcVideoDuration.TotalSeconds / EncodeDuration.TotalSeconds).ToString("F2"); } }
        public string DisplaySrcBitrate { get { return ((double)SrcFileSize / (SrcVideoDuration.TotalSeconds * 128.0 * 1024)).ToString("F3"); } }
        public string DisplayOutBitrate { get { return ((double)OutFileSize / (OutVideoDuration.TotalSeconds * 128.0 * 1024)).ToString("F3"); } }
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
    public class DiskFreeSpace
    {
        [DataMember]
        public List<DiskItem> Disks { get; set; }
    }
}
