using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Threading;
using System.Xml;

namespace NicoJK18Client
{
    class Program
    {
        static int Main(string[] args)
        {
            try
            {
                var client = new NicoJK18Client(args);
                client.Exec();
                return 0;
            }
            catch (Exception e)
            {
                Console.Error.WriteLine(e.Message);
                return 1;
            }
        }

        static void TooEarlyError()
        {
            string exepath = Environment.GetCommandLineArgs()[0];
            string[] arg1 = new string[] { exepath, "jk1", "2014", "2014" };
            new NicoJK18Client(arg1).Exec();
        }

        static void Test()
        {
            TooEarlyError();
        }
    }

    public class NicoJK18Client
    {
        private static readonly int SLOT_DURATION = 5 * 60;
        private static readonly int MAX_SLOT_REQ = 8;
        private static readonly DateTime UNIX_EPOCH = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);

        private static HttpClient client = new HttpClient();

        private string dstPath, jknum;
        private long startTime = 0, endTime = 0;
        private int retryNum = 6;
        private bool isXml = false;

        public static List<string> ReadData(Stream stream, int num)
        {
            var ret = new List<string>();
            for (int i = 0; i < num; ++i)
            {
                var buf = new byte[4];
                if (stream.Read(buf, 0, buf.Length) != buf.Length)
                {
                    throw new IOException("受信エラー");
                }
                int len = BitConverter.ToInt32(buf, 0);
                buf = new byte[len];
                if (stream.Read(buf, 0, buf.Length) != buf.Length)
                {
                    throw new IOException("受信エラー");
                }
                // DeflateStreamは生のDeflateストリームを扱うので
                // zlibヘッダとフッダは取り除く
                var reader = new StreamReader(new DeflateStream(
                    new MemoryStream(buf, 2, buf.Length - 2), CompressionMode.Decompress), Encoding.UTF8);
                ret.Add(reader.ReadToEnd());
            }
            return ret;
        }

        private static long ParseTime(string s)
        {
            if(s.Length == 14)
            {
                var dt = DateTime.ParseExact(s, "yyyyMMddHHmmss", null);
                return (long)(dt.ToUniversalTime() - UNIX_EPOCH).TotalSeconds;
            }
            return long.Parse(s);
        }

        private static string GetHelp()
        {
            var sb = new StringBuilder();
            sb.Append("Usage: ")
                .Append(Environment.GetCommandLineArgs()[0])
                .Append(" チャンネル 取得時間範囲のはじめ 取得時間範囲のおわり [option...]")
                .AppendLine()
                .AppendLine("Options:")
                .AppendLine("  -f [filename]  --file [filename]    出力するファイル名を指定します")
                .AppendLine("  -r num  --retry num                 取得エラーが発生した際に再取得へ行く回数")
                .AppendLine("  -x  --xml                           出力フォーマットをXMLにします");
            return sb.ToString();
        }

        public NicoJK18Client(string[] args)
        {
            try
            {
                var nonamed = new List<string>();
                for (int i = 0; i < args.Length; ++i)
                {
                    string arg = args[i];
                    if (arg == "-f" || arg == "--file")
                    {
                        dstPath = args[i + 1];
                        ++i;
                    }
                    else if (arg == "-r" || arg == "--retry")
                    {
                        retryNum = int.Parse(args[i + 1]);
                        ++i;
                    }
                    else if (arg == "-x" || arg == "--xml")
                    {
                        isXml = true;
                    }
                    else if (arg[0] != '-')
                    {
                        nonamed.Add(args[i]);
                    }
                }
                jknum = nonamed[0];
                startTime = ParseTime(nonamed[1]);
                endTime = ParseTime(nonamed[2]);
            }
            catch
            {
                throw new Exception(GetHelp());
            }

            if (startTime > endTime || startTime + 3600 * 24 < endTime)
            {
                throw new Exception("入力された時刻が不正です");
            }

            // タイムアウトは3分に設定
            client.Timeout = TimeSpan.FromMilliseconds(180*1000);
        }

        private List<string> GetData(int slot, int num)
        {
            var url = "http://nicojk18.sakura.ne.jp/api/v1/getcomment?jknum=" + jknum + "&slot=" + slot + "&num=" + num;
            var res = client.GetAsync(url).Result;
            if(res.StatusCode == System.Net.HttpStatusCode.BadRequest)
            {
                // これはパラメータがおかしいのでリトライしないで終了させる
                throw new Exception("パラメータ不正");
            }
            else if(res.StatusCode != System.Net.HttpStatusCode.OK)
            {
                throw new IOException("Return code: " + res.StatusCode.ToString());
            }
            var stream = res.Content.ReadAsStreamAsync().Result;
            return ReadData(stream, num);
        }

        private string WrapXml(IEnumerable<string> list)
        {
            var sb = new StringBuilder();
            sb.AppendLine("<?xml version='1.0' encoding='UTF-8'?>");
            sb.AppendLine("<packet>");
            foreach (var s in list)
            {
                sb.AppendLine(s);
            }
            sb.Append("</packet>");
            return sb.ToString();
        }

        private string NicoJKFormat(IEnumerable<string> list)
        {
            var sb = new StringBuilder();
            foreach (var s in list)
            {
                sb.AppendLine(s.Replace("\r", "&#13;").Replace("\n", "&#10;"));
            }
            return sb.ToString();
        }

        private class ChatElement
        {
            public long Date;
            public long Thread;
            public int No;
            public string Xml;
        }

        public void Exec()
        {
            var recvData = new List<string>();

            var startDate = UNIX_EPOCH.AddSeconds(startTime).ToLocalTime();
            var duration = UNIX_EPOCH.AddSeconds(endTime).ToLocalTime() - startDate;
            Console.WriteLine(startDate.ToString("yyyy年MM月dd日 HH:mm:ss") + "から" + duration.ToString() + "取得します");

            // データ取得
            var startSlot = (int)(startTime / SLOT_DURATION);
            var endSlot = (int)((endTime + SLOT_DURATION - 1) / SLOT_DURATION);
            for (int i = startSlot; i < endSlot; i += MAX_SLOT_REQ)
            {
                var nslot = Math.Min(endSlot - i, MAX_SLOT_REQ);
                Console.WriteLine("" + i + "から" + nslot + "スロット取得します");
                for (int retry = 0; retry < retryNum; ++retry)
                {
                    if (retry > 0)
                    {
                        int waitsec = retry * retry * 2;
                        Console.WriteLine("" + waitsec + "秒後に再試行します ...");
                        Thread.Sleep(waitsec * 1000);
                    }
                    try
                    {
                        recvData.AddRange(GetData(i, nslot));
                        break;
                    }
                    catch (IOException e)
                    {
                        Console.WriteLine("失敗: " + e.Message);
                        continue;
                    }
                }
            }

            // パース
            var doc = new XmlDocument();
            doc.LoadXml(WrapXml(recvData));
            var chats = new List<ChatElement>();
            foreach (XmlElement el in doc.DocumentElement)
            {
                chats.Add(new ChatElement()
                {
                    Date = long.Parse(el.Attributes["date"].Value),
                    Thread = long.Parse(el.Attributes["thread"].Value),
                    No = int.Parse(el.Attributes["no"].Value),
                    Xml = el.OuterXml
                });
            }

            Console.WriteLine("" + chats.Count + "コメント取得しました");

            // 並べ替え
            var ordered = chats.
                Where(c => c.Date >= startTime && c.Date < endTime).
                OrderBy(c => c.Date).
                ThenBy(c => c.Thread).
                ThenBy(c => c.No).
                Select(c => c.Xml);

            var result = isXml ? WrapXml(ordered) : NicoJKFormat(ordered);

            if (dstPath == null)
            {
                Console.WriteLine(result);
            }
            else
            {
                File.WriteAllText(dstPath, result, Encoding.UTF8);
            }
        }
    }
}
