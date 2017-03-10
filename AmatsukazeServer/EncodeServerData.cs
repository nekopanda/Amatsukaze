using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EncodeServer
{
    public class Setting
    {
        public string X264Path;
        public string X265Path;
        public string QSVEncPath;
        public string EncoderName;

        public string MuxerPath;
        public string TimelineEditorPath;

        public string WorkPath;
    }

    public class QueueItem
    {
        public string Path;
        public List<string> MediaFiles;
    }

    public class QueueData
    {
        public List<QueueItem> Items;
    }

    public class QueueUpdate
    {
        public bool AddOrRemove;
        public string ItemPath;
        public string MediaPath;
    }

    public class LogItem
    {
        public long Id;
        public string SrcPath;
        public bool Success;
        public List<string> OutPath;
        public DateTime EncodeStartDate;
        public DateTime EncodeFinishDate;
        public TimeSpan VideoDuration;
        public long SrcFileSize;
        public long IntVideoFileSize;
        public long OutFileSize;
    }

    public class LogData
    {
        public List<LogItem> Items;
    }

    public class LogFile
    {
        public long Id;
        public string Content;
    }
}
