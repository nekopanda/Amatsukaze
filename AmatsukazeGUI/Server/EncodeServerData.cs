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

    [DataContract]
    public class Setting : IExtensibleDataObject
    {
        [DataMember]
        public string AmatsukazePath { get; set; }
        [DataMember]
        public EncoderType EncoderType { get; set; }

        [DataMember]
        public string X264Path { get; set; }
        [DataMember]
        public string X265Path { get; set; }
        [DataMember]
        public string QSVEncPath { get; set; }
        [DataMember]
        public string NVEncPath { get; set; }
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

        // depricated
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

        [DataMember]
        public bool TwoPass { get; set; }
        [DataMember]
        public bool AutoBuffer { get; set; }
        [DataMember]
        public BitrateSetting Bitrate { get; set; }
        [DataMember]
        public bool Pulldown { get; set; }

        [DataMember]
        public int NumParallel { get; set; }
        [DataMember]
        public bool EnableFilterTmp { get; set; }
        [DataMember]
        public int MaxTmpGB { get; set; }

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
    public class AddQueueDirectory
    {
        [DataMember]
        public string DirPath { get; set; }
        [DataMember]
        public List<string> Targets { get; set; }
        [DataMember]
        public string DstPath { get; set; }
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
        [DataMember]
        public string DstPath;
        [DataMember]
        public int CurrentHead { get; set; }
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

        [DataMember]
        public bool Pulldown { get; set; }
        [DataMember]
        public bool Timecode { get; set; }
        [DataMember]
        public int Incident { get; set; }

        public ExtensionDataObject ExtensionData { get; set; }

        // アクセサ
        public TimeSpan EncodeDuration { get { return EncodeFinishDate - EncodeStartDate; } }
        public string DisplayResult { get { return Success ? ((Incident > 0) ? "△" : "〇") : "×"; } }
        public string DisplaySrcDirectory { get { return Path.GetDirectoryName(SrcPath); } }
        public string DisplaySrcFileName { get { return Path.GetFileName(SrcPath); } }
        public string DisplayOutNum { get { return OutPath.Count.ToString(); } }
        public string DisplayNumIncident { get { return Incident.ToString(); } }
        public string DisplayEncodeStart { get { return EncodeStartDate.ToGUIString(); } }
        public string DisplayEncodeFinish { get { return EncodeFinishDate.ToGUIString(); } }
        public string DisplayEncodeDuration { get { return EncodeDuration.ToGUIString(); } }
        public string DisplaySrcDurationo { get { return (SrcVideoDuration != null) ? SrcVideoDuration.ToGUIString() : null; } }
        public string DisplayOutDuration { get { return (OutVideoDuration != null) ? OutVideoDuration.ToGUIString() : null; } }
        public string DisplayVideoNotIncluded { get {
            if (SrcVideoDuration == null) return null;
            var s = SrcVideoDuration.TotalMilliseconds;
            var o = OutVideoDuration.TotalMilliseconds;
            return ((double)(s - o) / (double)s * 100.0).ToString("F2");
        } }
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
        public string DisplayEncodeSpeed { get { return (SrcVideoDuration != null)
                    ? (SrcVideoDuration.TotalSeconds / EncodeDuration.TotalSeconds).ToString("F2")
                    : null; } }
        public string DisplaySrcBitrate { get { return (SrcVideoDuration != null)
                    ? ((double)SrcFileSize / (SrcVideoDuration.TotalSeconds * 128.0 * 1024)).ToString("F3")
                    : null; } }
        public string DisplayOutBitrate { get { return (SrcVideoDuration != null)
                    ? ((double)OutFileSize / (OutVideoDuration.TotalSeconds * 128.0 * 1024)).ToString("F3")
                    : null; } }

        public string DisplayPulldown { get { return Pulldown ? "Y" : ""; } }
        public string DisplayTimecode { get { return Timecode ? "Y" : ""; } }
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
}
