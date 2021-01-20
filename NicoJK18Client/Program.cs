using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
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
                ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls
                    | SecurityProtocolType.Tls11 | SecurityProtocolType.Tls12;
                var client = new NicoJK18Client(args);
                client.Exec();
                return 0;
            }
            catch (NoThreadException e)
            {
                Console.Error.WriteLine(e.Message);
                return 100;
            }
            catch (AggregateException es)
            {
                foreach (var e in es.InnerExceptions)
                {
                    Console.Error.WriteLine(e.Message);
                }
                return 1;
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

    public class NoThreadException : Exception
    {
        public NoThreadException(string message) : base(message) { }
    }

    public class NeedWaitException : Exception { }

    public class NicoJK18Client
    {
        private static readonly DateTime UNIX_EPOCH = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);

        private string serverUrl = "https://nicojk18.nekopanda.net/api/kakolog";
        private string dstPath, jknum;
        private long startTime = 0, endTime = 0;
        private int retryNum = 360;
        private bool isXml = false;
        private bool verbose = false;

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
                .AppendLine("  -f [filename]  --file [filename]  出力するファイル名を指定します")
                .AppendLine("  -r num  --retry num               取得エラーが発生した際に再取得へ行く回数")
                .AppendLine("  -x  --xml                         出力フォーマットをXMLにします")
                .AppendLine("  --server <serverURL>              取得するサーバのURL デフォルト: https://nicojk18.nekopanda.net/api/kakolog")
                .AppendLine("  -v                                Verboseモード");
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
                    else if (arg == "-v")
                    {
                        verbose = true;
                    }
                    else if (arg == "--server")
                    {
                        serverUrl = args[i + 1];
                        ++i;
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

            if (startTime > endTime || startTime + 3600 * 24 * 180 < endTime)
            {
                throw new Exception("入力された時刻が不正です");
            }

        }

        private string GetData(string jknum, long starttime, long endtime)
        {
            var url = serverUrl + "/" + jknum + "?starttime=" + starttime + "&endtime=" + endtime + "&format=xml&emotion=false";
            if(verbose)
            {
                Console.WriteLine("URL: " + url);
            }
            // 本当はHttpClientは使いまわすべきだが、
            // AutomaticDecompressionが有効だと、3回目のリクエストでデッドロックする不具合があるので、
            // 毎回作り直す
            using (var client = new HttpClient(new HttpClientHandler()
            {
                // 圧縮サポート
                AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate
            }))
            {
                // タイムアウトは1分に設定
                client.Timeout = TimeSpan.FromMilliseconds(60 * 1000);
                client.DefaultRequestHeaders.Add("User-Agent", "NicoJK18Client");

                // ResponseHeadersReadがないと、全てダウンロードするまでの時間でタイムアウト処理されてしまう
                // データが大きいとダウンロードに時間がかかるのは当然なので、ヘッダーの受信までをタイムアウト対象期間とする
                var res = client.GetAsync(url, HttpCompletionOption.ResponseHeadersRead).Result;
                if (res.StatusCode == System.Net.HttpStatusCode.BadRequest)
                {
                    // これはパラメータがおかしいのでリトライしないで終了させる
                    throw new Exception("パラメータ不正");
                }
                else if (res.StatusCode != System.Net.HttpStatusCode.OK)
                {
                    throw new IOException("Return code: " + res.StatusCode.ToString());
                }
                var stream = res.Content.ReadAsStreamAsync().Result;
                return new StreamReader(stream).ReadToEnd();
            }
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
            public int No;
            public string Xml;
        }

        private List<ChatElement> GetChatElements(string jknum, long starttime, long endtime)
        {
            var doc = new XmlDocument();
            var recvData = GetData(jknum, starttime, endtime);
            doc.LoadXml(recvData);
            if (doc.DocumentElement.Name == "error")
            {
                var message = doc.DocumentElement.InnerText;
                if (message.Contains("チャンネルがない") || message.Contains("存在しないチャンネル"))
                {
                    // エラーだった
                    throw new NoThreadException(doc.DocumentElement.InnerText);
                }
                else if (message.StartsWith("データ量"))
                {
                    // データ量が多すぎる場合は分割して取得する
                    if (endtime - starttime < 2)
                    {
                        // これ以上分割できないのでエラーとする
                        throw new Exception("データが取得できませんでした");
                    }
                    var mid = (starttime + endtime) / 2;
                    return GetChatElements(jknum, starttime, mid)
                        .Concat(GetChatElements(jknum, mid, endtime)).ToList();
                }
                else if (message.StartsWith("まだ取得していない"))
                {
                    throw new NeedWaitException();
                }
                throw new Exception(doc.DocumentElement.InnerText);
            }
            // パース
            var chats = new List<ChatElement>();
            foreach (XmlElement el in doc.DocumentElement)
            {
                chats.Add(new ChatElement()
                {
                    Date = long.Parse(el.Attributes["date"].Value),
                    No = int.Parse(el.Attributes["no"].Value),
                    Xml = el.OuterXml
                });
            }
            return chats;
        }

        private List<ChatElement> GetWithRetry(string jknum, long starttime, long endtime)
        {
            // データ取得
            for (int failCount = 0, waitCount = 0; failCount < retryNum && waitCount < 6; )
            {
                if (failCount > 0)
                {
                    int waitsec = failCount * failCount * 60;
                    Console.WriteLine("" + waitsec + "秒後に再試行します ...");
                    Thread.Sleep(waitsec * 1000);
                }
                try
                {
                    return GetChatElements(jknum, starttime, endtime);
                }
                catch (NeedWaitException)
                {
                    failCount = 0;
                    waitCount++;
                    Console.WriteLine("まだサーバのデータが更新されていません");
                    int waitsec = 10 * 60; // 10分待つ
                    Console.WriteLine("" + waitsec + "秒後に再試行します ...");
                    Thread.Sleep(waitsec * 1000);
                    continue;
                }
                catch (IOException e)
                {
                    failCount++;
                    Console.WriteLine("失敗: " + e.Message);
                    continue;
                }
                catch (AggregateException e)
                {
                    failCount++;
                    Console.WriteLine("失敗: " + e.Message);
                    continue;
                }
            }
            throw new Exception("データが取得できませんでした");
        }

        public void Exec()
        {
            var startDate = UNIX_EPOCH.AddSeconds(startTime).ToLocalTime();
            var duration = UNIX_EPOCH.AddSeconds(endTime).ToLocalTime() - startDate;
            Console.WriteLine(startDate.ToString("yyyy年MM月dd日 HH:mm:ss") + "から" + duration.ToString() + "取得します");

            List<ChatElement> chats = GetWithRetry(jknum, startTime, endTime);

            Console.WriteLine("" + chats.Count + "コメント取得しました");

            // 並べ替え
            var ordered = chats.
                Where(c => c.Date >= startTime && c.Date < endTime).
                OrderBy(c => c.Date).
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
