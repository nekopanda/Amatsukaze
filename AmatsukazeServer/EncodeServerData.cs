using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace AmatsukazeServer
{
    [DataContract]
    public class Setting : IExtensibleDataObject
    {
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

        public ExtensionDataObject ExtensionData { get; set; }
    }

    [DataContract]
    public class State
    {
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
        public List<string> MediaFiles { get; set; }
    }

    [DataContract]
    public class QueueData
    {
        [DataMember]
        public List<QueueItem> Items { get; set; }
    }

    [DataContract]
    public class QueueUpdate
    {
        [DataMember]
        public bool AddOrRemove { get; set; }
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

        public ExtensionDataObject ExtensionData { get; set; }
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
}
