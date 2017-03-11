using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace EncodeServer
{
    [DataContract]
    public class Setting : IExtensibleDataObject
    {
        [DataMember]
        public string X264Path;
        [DataMember]
        public string X265Path;
        [DataMember]
        public string QSVEncPath;
        [DataMember]
        public string EncoderName;

        [DataMember]
        public string MuxerPath;
        [DataMember]
        public string TimelineEditorPath;

        [DataMember]
        public string WorkPath;

        public ExtensionDataObject ExtensionData { get; set; }
    }

    [DataContract]
    public class State
    {
        [DataMember]
        public bool pause;
    }

    [DataContract]
    public class QueueItem
    {
        [DataMember]
        public string Path;
        [DataMember]
        public List<string> MediaFiles;
    }

    [DataContract]
    public class QueueData
    {
        [DataMember]
        public List<QueueItem> Items;
    }

    [DataContract]
    public class QueueUpdate
    {
        [DataMember]
        public bool AddOrRemove;
        [DataMember]
        public string ItemPath;
        [DataMember]
        public string MediaPath;
    }

    [DataContract]
    public class LogItem : IExtensibleDataObject
    {
        [DataMember]
        public long Id;
        [DataMember]
        public string SrcPath;
        [DataMember]
        public bool Success;
        [DataMember]
        public List<string> OutPath;
        [DataMember]
        public DateTime EncodeStartDate;
        [DataMember]
        public DateTime EncodeFinishDate;
        [DataMember]
        public TimeSpan SrcVideoDuration;
        [DataMember]
        public TimeSpan OutVideoDuration;
        [DataMember]
        public long SrcFileSize;
        [DataMember]
        public long IntVideoFileSize;
        [DataMember]
        public long OutFileSize;

        public ExtensionDataObject ExtensionData { get; set; }
    }

    [DataContract]
    public class LogData : IExtensibleDataObject
    {
        [DataMember]
        public List<LogItem> Items;

        public ExtensionDataObject ExtensionData { get; set; }
    }

    [DataContract]
    public class LogFile
    {
        [DataMember]
        public long Id;
        [DataMember]
        public string Content;
    }
}
