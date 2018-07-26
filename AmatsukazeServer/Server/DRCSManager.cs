using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Media.Imaging;

namespace Amatsukaze.Server
{
    class DRCSManager
    {
        private EncodeServer server;

        class DrcsSource
        {
            public int LastUpdate;
            public List<DrcsSourceItem> Items;
        }

        class LogItem
        {
            public string SrcFileName;    // 当該TSファイル名
            public DateTime Time; // DRCS文字を発見した日時
            public string LogPath; // ログファイルパス
        }

        private Dictionary<string, BitmapFrame> drcsImageCache = new Dictionary<string, BitmapFrame>();
        private Dictionary<string, DrcsImage> drcsMap = new Dictionary<string, DrcsImage>();
        private Dictionary<string, DrcsSource> drcsSourceMap = new Dictionary<string, DrcsSource>();
        private List<LogItem> logQueue = new List<LogItem>();

        private int UpdateCount; // 実行開始から追加されたログファイルの数に相当
        private int LastUpdate;  // クライアントに通知済みのログファイル数に相当

        private DateTime drcsDirTime = DateTime.MinValue;
        private DateTime drcsTime = DateTime.MinValue;

        private static Func<char, bool> IsHex = c =>
                 (c >= '0' && c <= '9') ||
                 (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F');

        public DRCSManager(EncodeServer server)
        {
            this.server = server;
        }

        public Task RequestDrcsImages()
        {
            return server.Client.OnDrcsData(new DrcsImageUpdate()
            {
                Type = DrcsUpdateType.Update,
                ImageList = drcsMap.Values.ToList()
            });
        }

        public void AddLogFile(string logPath, string srcfilename, DateTime time)
        {
            logQueue.Add(new LogItem() { SrcFileName = srcfilename, Time = time, LogPath = logPath });
        }

        public void UpdateFromOldVersion()
        {
            // DRCS文字の並びを変更する
            var dirPath = server.GetDRCSDirectoryPath();
            var oldDirPath = dirPath + ".old";
            if (Directory.Exists(dirPath) && !Directory.Exists(oldDirPath))
            {
                // drcs -> drcs.old にディレクトリ名変更
                Directory.Move(dirPath, oldDirPath);

                // drcsディレクトリを改めて作る
                Directory.CreateDirectory(dirPath);

                Func<string, string> RevertHash = s =>
                {
                    var sb = new StringBuilder();
                    for (int i = 0; i < 32; i += 2)
                    {
                        sb.Append(s[i + 1]).Append(s[i]);
                    }
                    return sb.ToString();
                };

                // drcs_map.txtを変換
                using (var sw = new StreamWriter(File.OpenWrite(server.GetDRCSMapPath()), Encoding.UTF8))
                {
                    foreach (var line in File.ReadAllLines(server.GetDRCSMapPath(oldDirPath)))
                    {
                        if (line.Length >= 34 && line.IndexOf('=') == 32)
                        {
                            string md5 = line.Substring(0, 32);
                            string mapStr = line.Substring(33);
                            if (md5.All(IsHex))
                            {
                                sw.WriteLine(RevertHash(md5) + "=" + mapStr);
                            }
                        }
                    }
                }

                // 文字画像ファイルを変換
                foreach (var imgpath in Directory.GetFiles(oldDirPath))
                {
                    var filename = Path.GetFileName(imgpath);
                    if (filename.Length == 36 && Path.GetExtension(filename).ToLower() == ".bmp")
                    {
                        string md5 = filename.Substring(0, 32);
                        File.Copy(imgpath, dirPath + "\\" + RevertHash(md5) + ".bmp");
                    }
                }

            }
        }

        private BitmapFrame LoadImage(string imgpath)
        {
            if (drcsImageCache.ContainsKey(imgpath))
            {
                return drcsImageCache[imgpath];
            }
            try
            {
                var img = BitmapFrame.Create(new MemoryStream(File.ReadAllBytes(imgpath)));
                drcsImageCache.Add(imgpath, img);
                return img;
            }
            catch (Exception) { }

            return null;
        }

        private Dictionary<string, DrcsImage> LoadDrcsImages()
        {
            var ret = new Dictionary<string, DrcsImage>();

            foreach (var imgpath in Directory.GetFiles(server.GetDRCSDirectoryPath()))
            {
                var filename = Path.GetFileName(imgpath);
                if (filename.Length == 36 && Path.GetExtension(filename).ToLower() == ".bmp")
                {
                    string md5 = filename.Substring(0, 32);
                    ret.Add(md5, new DrcsImage()
                    {
                        MD5 = md5,
                        MapStr = null,
                        Image = LoadImage(imgpath)
                    });
                }
            }

            return ret;
        }

        private Dictionary<string, DrcsImage> LoadDrcsMap()
        {
            var ret = new Dictionary<string, DrcsImage>();

            try
            {
                foreach (var line in File.ReadAllLines(server.GetDRCSMapPath()))
                {
                    if (line.Length >= 34 && line.IndexOf('=') == 32)
                    {
                        string md5 = line.Substring(0, 32);
                        string mapStr = line.Substring(33);
                        if (md5.All(IsHex))
                        {
                            ret.Add(md5, new DrcsImage() { MD5 = md5, MapStr = mapStr, Image = null });
                        }
                    }
                }
            }
            catch (Exception)
            {
                // do nothing
            }

            return ret;
        }

        private Regex regex = new Regex("\\[字幕\\] 映像時刻(\\d+)分(\\d+)秒付近にマッピングのないDRCS外字があります。追加してください -> .*\\\\([0-9A-F]+)\\.bmp");

        private bool MatchDRCSInfo(string line, out string md5, out TimeSpan elapsed)
        {
            try
            {
                var match = regex.Match(line);
                if (match.Success)
                {
                    md5 = match.Groups[3].Value;
                    elapsed = new TimeSpan(0, int.Parse(match.Groups[1].Value), int.Parse(match.Groups[2].Value));
                    return true;
                }
            }
            catch (FormatException) {
                // マッチング失敗
            }
            md5 = null;
            elapsed = new TimeSpan();
            return false;
        }

        // ログからDRCS文字に関する情報を抽出
        private void ReadLogFiles(LogItem[] logQueue)
        {
            foreach(var log in logQueue)
            {
                if (!File.Exists(log.LogPath))
                {
                    continue;
                }
                foreach(var line in File.ReadAllLines(log.LogPath, Encoding.Default))
                {
                    string md5;
                    TimeSpan elapsed;
                    if (MatchDRCSInfo(line, out md5, out elapsed))
                    {
                        if(!drcsSourceMap.ContainsKey(md5))
                        {
                            drcsSourceMap.Add(md5, new DrcsSource() {
                                Items = new List<DrcsSourceItem>()
                            });
                        }
                        var item = drcsSourceMap[md5];
                        if(item.Items.Any(s => s.FileName == log.SrcFileName && s.Elapsed == elapsed) == false)
                        {
                            item.Items.Add(new DrcsSourceItem()
                            {
                                FileName = log.SrcFileName,
                                FoundTime = log.Time,
                                Elapsed = elapsed
                            });
                        }
                        item.LastUpdate = UpdateCount;
                    }
                }
                ++UpdateCount;
            }
        }

        public async Task Update()
        {
            string drcspath = server.GetDRCSDirectoryPath();
            string drcsmappath = server.GetDRCSMapPath();
            if (!Directory.Exists(drcspath))
            {
                Directory.CreateDirectory(drcspath);
            }
            if (!File.Exists(drcsmappath))
            {
                using (File.Create(drcsmappath)) { }
            }

            // 全ログファイルを読むのは時間がかかりそうなので、
            // 別スレッドで実行する
            // 大丈夫だとは思うけど、一応 drcsSourceMap と UpdateCount に触る時は
            // drcsSourceMapをロックすること

            if (logQueue.Count > 0)
            {
                // 読んでる途中で追加されるとマズいので
                // コピーを取って、logQueueはクリアしておく
                var input = logQueue.ToArray();
                logQueue.Clear();

                await Task.Run(() =>
                {
                    lock (drcsSourceMap)
                    {
                        ReadLogFiles(input);
                    }
                });
            }

            {
                // 今drcsMapにある分については、drcsSourceMapの更新分をクライアントに通知
                var updateImages = new List<DrcsImage>();

                lock (drcsSourceMap)
                {
                    foreach (var item in drcsMap.Values)
                    {
                        if (drcsSourceMap.ContainsKey(item.MD5))
                        {
                            var srcItem = drcsSourceMap[item.MD5];
                            if (srcItem.LastUpdate >= LastUpdate)
                            {
                                // 更新された
                                item.SourceList = srcItem.Items.ToArray();
                                updateImages.Add(item);
                            }
                        }
                    }
                    LastUpdate = UpdateCount;
                }

                if (updateImages.Count > 0)
                {
                    await server.Client.OnDrcsData(new DrcsImageUpdate()
                    {
                        Type = DrcsUpdateType.Update,
                        ImageList = updateImages.Distinct().ToList()
                    });
                }
            }

            // ファイルの更新を見る
            bool needUpdate = false;
            var lastModified = Directory.GetLastWriteTime(drcspath);
            if (drcsDirTime != lastModified)
            {
                // ファイルが追加された
                needUpdate = true;
                drcsDirTime = lastModified;
            }
            lastModified = File.GetLastWriteTime(drcsmappath);
            if (drcsTime != lastModified)
            {
                // マッピングが更新された
                needUpdate = true;
                drcsTime = lastModified;
            }
            if (needUpdate)
            {
                var newImageMap = LoadDrcsImages();
                var newStrMap = LoadDrcsMap();

                var newDrcsMap = new Dictionary<string, DrcsImage>();
                lock (drcsSourceMap)
                {
                    foreach (var key in newImageMap.Keys.Union(newStrMap.Keys))
                    {
                        var newItem = new DrcsImage() { MD5 = key };
                        if (newImageMap.ContainsKey(key))
                        {
                            newItem.Image = newImageMap[key].Image;
                        }
                        if (newStrMap.ContainsKey(key))
                        {
                            newItem.MapStr = newStrMap[key].MapStr;
                        }
                        if (drcsSourceMap.ContainsKey(key))
                        {
                            newItem.SourceList = drcsSourceMap[key].Items.ToArray();
                        }
                        newDrcsMap.Add(key, newItem);
                    }
                }

                // 更新処理
                var updateImages = new List<DrcsImage>();
                foreach (var key in newDrcsMap.Keys.Union(drcsMap.Keys))
                {
                    if (newDrcsMap.ContainsKey(key) == false)
                    {
                        // 消えた
                        await server.Client.OnDrcsData(new DrcsImageUpdate()
                        {
                            Type = DrcsUpdateType.Remove,
                            Image = drcsMap[key]
                        });
                    }
                    else if (drcsMap.ContainsKey(key) == false)
                    {
                        // 追加された
                        updateImages.Add(newDrcsMap[key]);
                    }
                    else
                    {
                        var oldItem = drcsMap[key];
                        var newItem = newDrcsMap[key];
                        if (oldItem.MapStr != newItem.MapStr || oldItem.Image != newItem.Image)
                        {
                            // 変更された
                            updateImages.Add(newDrcsMap[key]);
                        }
                    }
                }

                if (updateImages.Count > 0)
                {
                    await server.Client.OnDrcsData(new DrcsImageUpdate()
                    {
                        Type = DrcsUpdateType.Update,
                        ImageList = updateImages.Distinct().ToList()
                    });
                }

                drcsMap = newDrcsMap;
            }
        }

        public async Task AddDrcsMap(DrcsImage recvitem)
        {
            if (drcsMap.ContainsKey(recvitem.MD5))
            {
                var item = drcsMap[recvitem.MD5];

                if (item.MapStr != recvitem.MapStr)
                {
                    var filepath = server.GetDRCSMapPath();
                    var updateType = DrcsUpdateType.Update;

                    // データ更新
                    if (item.MapStr == null)
                    {
                        // 既存のマッピングにないので追加
                        item.MapStr = recvitem.MapStr;
                        try
                        {
                            File.AppendAllLines(filepath,
                                new string[] { item.MD5 + "=" + recvitem.MapStr },
                                Encoding.UTF8);
                        }
                        catch (Exception e)
                        {
                            await server.FatalError(
                                "DRCSマッピングファイル書き込みに失敗", e);
                        }
                    }
                    else
                    {
                        // 既にマッピングにある
                        item.MapStr = recvitem.MapStr;
                        if (item.MapStr == null)
                        {
                            // 削除
                            drcsMap.Remove(recvitem.MD5);
                            updateType = DrcsUpdateType.Remove;
                            try
                            {
                                File.Delete(server.GetDRCSImagePath(recvitem.MD5));
                            }
                            catch (Exception e)
                            {
                                await server.FatalError(
                                    "DRCS画像ファイル削除に失敗", e);
                            }
                        }

                        // まず、一時ファイルに書き込む
                        var tmppath = filepath + ".tmp";
                        // BOMありUTF-8
                        try
                        {
                            using (var sw = new StreamWriter(File.OpenWrite(tmppath), Encoding.UTF8))
                            {
                                foreach (var s in drcsMap.Values)
                                {
                                    sw.WriteLine(s.MD5 + "=" + s.MapStr);
                                }
                            }
                        }
                        catch (Exception e)
                        {
                            await server.FatalError(
                                "DRCSマッピングファイル書き込みに失敗", e);
                        }
                        // ファイル置き換え
                        Util.MoveFileEx(tmppath, filepath, 11);
                    }

                    await server.Client.OnDrcsData(new DrcsImageUpdate()
                    {
                        Type = updateType,
                        Image = item
                    });
                }
            }
        }
    }
}
