using Codeplex.Data;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    internal class ConsoleText : ConsoleTextBase
    {
        public List<string> TextLines = new List<string>();

        private int maxLines;

        public ConsoleText(int maxLines)
        {
            this.maxLines = maxLines;
        }

        public override void Clear()
        {
            base.Clear();
            TextLines.Clear();
        }

        public override void OnAddLine(string text)
        {
            if (TextLines.Count > maxLines)
            {
                TextLines.RemoveRange(0, 100);
            }
            TextLines.Add(text);
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
        }
    }

    internal class TranscodeTask
    {
        public TranscodeWorker thread;
        public QueueItem src;
        public FileStream logWriter;
        public Process process;
    }

    internal class TranscodeWorker : IScheduleWorker<QueueItem>
    {
        public int id;
        public EncodeServer server;
        public ConsoleText logText;
        public ConsoleText consoleText;

        public TranscodeTask current { get; private set; }

        private List<Task> waitList;

        public void KillProcess()
        {
            if (current != null)
            {
                if (current.process != null)
                {
                    try
                    {
                        current.process.Kill();
                    }
                    catch (InvalidOperationException)
                    {
                        // プロセスが既に終了していた場合
                    }
                }
            }
        }

        private LogItem FailLogItem(string srcpath, string profile, string reason, DateTime start, DateTime finish)
        {
            return new LogItem()
            {
                Success = false,
                Reason = reason,
                SrcPath = srcpath,
                MachineName = Dns.GetHostName(),
                EncodeStartDate = start,
                EncodeFinishDate = finish,
                Profile = profile,
            };
        }

        private Task WriteTextBytes(EncodeServer server, TranscodeTask transcode, byte[] buffer, int offset, int length)
        {
            if (transcode.logWriter != null)
            {
                transcode.logWriter.Write(buffer, offset, length);
            }
            logText.AddBytes(buffer, offset, length);
            consoleText.AddBytes(buffer, offset, length);

            byte[] newbuf = new byte[length];
            Array.Copy(buffer, newbuf, length);
            return server.Client.OnConsoleUpdate(new ConsoleUpdate() { index = id, data = newbuf });
        }

        private Task WriteTextBytes(EncodeServer server, TranscodeTask transcode, byte[] buffer)
        {
            return WriteTextBytes(server, transcode, buffer, 0, buffer.Length);
        }

        private async Task RedirectOut(EncodeServer server, TranscodeTask transcode, Stream stream)
        {
            try
            {
                byte[] buffer = new byte[1024];
                while (true)
                {
                    var readBytes = await stream.ReadAsync(buffer, 0, buffer.Length);
                    if (readBytes == 0)
                    {
                        // 終了
                        return;
                    }
                    await WriteTextBytes(server, transcode, buffer, 0, readBytes);
                }
            }
            catch (Exception e)
            {
                Debug.Print("RedirectOut exception " + e.Message);
            }
        }

        private async Task RedirectOut(EncodeServer server, TranscodeTask transcode, StreamReader stream)
        {
            try
            {
                while (true)
                {
                    var line = await stream.ReadLineAsync();
                    if (line == null)
                    {
                        // 終了
                        return;
                    }
                    await WriteTextBytes(server, transcode, Encoding.Default.GetBytes(line + "\n"));
                }
            }
            catch (Exception e)
            {
                Debug.Print("RedirectOut exception " + e.Message);
            }
        }

        private class RenamedResult
        {
            public string renamed;
        }

        private async Task GetRenamed(EncodeServer server, RenamedResult result, StreamReader stream)
        {
            try
            {
                result.renamed = null;
                while (true)
                {
                    var line = await stream.ReadLineAsync();
                    if (line == null)
                    {
                        // 終了
                        return;
                    }
                    else if(string.IsNullOrWhiteSpace(line) ==false)
                    {
                        result.renamed = line;
                    }
                }
            }
            catch (Exception e)
            {
                Debug.Print("RedirectOut exception " + e.Message);
            }
        }

        private static string MakeSCRenameArgs(string screnamepath, string format, string filepath)
        {
            var sb = new StringBuilder();
            sb.Append("//nologo //U") // 文字化けを防ぐためUnicodeで出力させる
                .Append(" \"")
                .Append(screnamepath)
                .Append("\" \"")
                .Append(filepath)
                .Append("\" \"")
                .Append(format)
                .Append("\"");
            return sb.ToString();
        }

        private string SearchRenamedFile(string path, string ext)
        {
            return Directory.EnumerateFiles(path).Where(s => s.EndsWith(ext)).FirstOrDefault() ??
                Directory.EnumerateDirectories(path)
                .Select(s => SearchRenamedFile(s, ext))
                .Where(s => s != null).FirstOrDefault();
        }

        // 拡張子なしの相対パスを返す
        private async Task<string> SCRename(string screnamepath, string tmppath, string format, QueueItem src)
        {
            var filename = src.FileName;
            var time = src.TsTime;
            var eventName = src.EventName;
            var serviceName = src.ServiceName;

            var ext = ".ts";

            // 情報がある時はその情報を元にファイル名を作成
            // ないときはファイル名をそのまま使う
            string srcname = filename;
            if (time > new DateTime(2000, 1, 1) &&
                string.IsNullOrEmpty(eventName) == false &&
                string.IsNullOrEmpty(serviceName) == false)
            {
                srcname = time.ToString("yyyyMMddHHmm") + "_" +
                    Util.EscapeFileName(eventName, true) + "_ " +
                    Util.EscapeFileName(serviceName, true) + ext;
            }

            // 作業用フォルダを作る
            string baseTmp = Util.CreateTmpFile(tmppath);
            string basePath = baseTmp + "-rename";

            try
            {
                Directory.CreateDirectory(basePath);

                string srcpath = basePath + "\\" + srcname;
                using (File.Create(srcpath)) { }

                string exename = "cscript.exe";
                string args = MakeSCRenameArgs(screnamepath, format, srcpath);

                var psi = new ProcessStartInfo(exename, args)
                {
                    UseShellExecute = false,
                    WorkingDirectory = Directory.GetCurrentDirectory(),
                    RedirectStandardError = true,
                    RedirectStandardOutput = true,
                    RedirectStandardInput = false,
                    StandardErrorEncoding = Encoding.Unicode,
                    StandardOutputEncoding = Encoding.Unicode,
                    CreateNoWindow = true
                };

                var result = new RenamedResult();
                using (var p = Process.Start(psi))
                {
                    current = new TranscodeTask()
                    {
                        thread = this,
                        src = src,
                        process = p,
                    };

                    // 起動コマンドをログ出力
                    await WriteTextBytes(server, current, Encoding.Default.GetBytes(exename + " " + args + "\n"));

                    await Task.WhenAll(
                        GetRenamed(server, result, p.StandardOutput),
                        RedirectOut(server, current, p.StandardError),
                        Task.Run(() =>
                        {
                            while (p.WaitForExit(1000) == false)
                            {
                                if (src.State == QueueState.Canceled)
                                {
                                    // キャンセルされた
                                    p.Kill();
                                    break;
                                }
                            }
                        }));
                }

                // なぜか標準出力だと取得できないのでリネームされたファイルを探す
                if (result.renamed == null)
                {
                    result.renamed = SearchRenamedFile(basePath, ext);
                }

                if (string.IsNullOrWhiteSpace(result.renamed) || result.renamed == srcpath)
                {
                    // 名前が変わっていないのは失敗とみなす
                    return null;
                }

                // ベースパス部分と拡張子を取り除く
                var renamed = result.renamed.Substring(basePath.Length + 1);
                return renamed.Substring(0, renamed.Length - ext.Length);
            }
            finally
            {
                // 作業用フォルダを削除
                if (Directory.Exists(basePath))
                {
                    Directory.Delete(basePath, true);
                }
                File.Delete(baseTmp);

                if (current != null)
                {
                    current.logWriter = null;
                    current = null;
                }
            }
        }

        private LogItem LogFromJson(bool isGeneric, string profile, string jsonpath, DateTime start, DateTime finish, QueueItem src, int outputMask)
        {
            var json = DynamicJson.Parse(File.ReadAllText(jsonpath));
            if (isGeneric)
            {
                return new LogItem()
                {
                    Success = true,
                    SrcPath = json.srcpath,
                    OutPath = json.outpath,
                    SrcFileSize = (long)json.srcfilesize,
                    OutFileSize = (long)json.outfilesize,
                    MachineName = Dns.GetHostName(),
                    EncodeStartDate = start,
                    EncodeFinishDate = finish,
                    Profile = profile,
                };
            }
            var outpath = new List<string>();
            foreach (var file in json.outfiles)
            {
                outpath.Add(file.path);
                foreach (var sub in file.subs)
                {
                    outpath.Add(sub);
                }
            }
            var logofiles = new List<string>();
            foreach (var logo in json.logofiles)
            {
                if (string.IsNullOrEmpty(logo) == false)
                {
                    logofiles.Add(Path.GetFileName(logo));
                }
            }
            // dynamicオブジェクトは拡張メソッドをサポートしていないので一旦型を確定させる
            IEnumerable<string> counters = json.error.GetDynamicMemberNames();
            List<ErrorCount> error = counters.Select((Func<string, ErrorCount>)(name => new ErrorCount()
            {
                Name = name,
                Count = (int)json.error[name]
            })).ToList();

            return new LogItem()
            {
                Success = true,
                Reason = ServerSupport.ErrorCountToString(error),
                SrcPath = json.srcpath,
                OutPath = outpath,
                SrcFileSize = (long)json.srcfilesize,
                IntVideoFileSize = (long)json.intvideofilesize,
                OutFileSize = (long)json.outfilesize,
                SrcVideoDuration = TimeSpan.FromSeconds(json.srcduration),
                OutVideoDuration = TimeSpan.FromSeconds(json.outduration),
                EncodeStartDate = start,
                EncodeFinishDate = finish,
                MachineName = Dns.GetHostName(),
                AudioDiff = new AudioDiff()
                {
                    TotalSrcFrames = (int)json.audiodiff.totalsrcframes,
                    TotalOutFrames = (int)json.audiodiff.totaloutframes,
                    TotalOutUniqueFrames = (int)json.audiodiff.totaloutuniqueframes,
                    NotIncludedPer = json.audiodiff.notincludedper,
                    AvgDiff = json.audiodiff.avgdiff,
                    MaxDiff = json.audiodiff.maxdiff,
                    MaxDiffPos = json.audiodiff.maxdiffpos
                },
                Chapter = json.cmanalyze,
                NicoJK = json.nicojk,
                OutputMask = outputMask,
                ServiceName = src.ServiceName,
                ServiceId = src.ServiceId,
                TsTime = src.TsTime,
                LogoFiles = logofiles,
                Incident = error.Sum(s => s.Count),
                Error = error,
                Profile = profile,
            };
        }

        private async Task<LogItem> ProcessItem(EncodeServer server, QueueItem src, QueueDirectory dir)
        {
            DateTime now = DateTime.Now;

            if (File.Exists(src.SrcPath) == false)
            {
                return FailLogItem(src.SrcPath, dir.Profile.Name, "入力ファイルが見つかりません", now, now);
            }

            ProfileSetting profile = dir.Profile;
            ServiceSettingElement serviceSetting =
                server.AppData_.services.ServiceMap[src.ServiceId];

            bool ignoreNoLogo = true;
            string[] logopaths = null;
            if (profile.DisableChapter == false)
            {
                var logofiles = serviceSetting.LogoSettings
                    .Where(s => s.CanUse(src.TsTime))
                    .Select(s => s.FileName)
                    .ToArray();
                if (logofiles.Length == 0)
                {
                    // これは必要ないはず
                    src.FailReason = "ロゴ設定がありません";
                    return null;
                }
                ignoreNoLogo = !logofiles.All(path => path != LogoSetting.NO_LOGO);
                logopaths = logofiles.Where(path => path != LogoSetting.NO_LOGO).ToArray();
            }


            // 出力パス生成
            // datpathは拡張子を含んでなくても含んでても可
            // エンコード後、CLIから取得したファイル名の拡張子を
            // 使うことになるので拡張子を含んでても無視される
            string dstpath = src.DstPath;
            bool renamed = false;

            if (profile.EnableRename)
            {
                // SCRenameによるリネーム
                string newName = null;
                try
                {
                    newName = await SCRename(
                        server.AppData_.setting.SCRenamePath, 
                        server.AppData_.setting.WorkPath,
                        profile.RenameFormat, src);
                }
                catch (Exception)
                {
                    return FailLogItem(src.SrcPath, dir.Profile.Name, "SCRenameに失敗", now, now);
                }

                if (newName != null)
                {
                    var ext = ServerSupport.GetFileExtension(profile.OutputFormat);
                    var newPath = Path.Combine(
                        Path.GetDirectoryName(dstpath), newName);
                    if (File.Exists(newPath + ext))
                    {
                        // 同名ファイルが存在する場合は、ファイル名は変えずに別のフォルダに移動する
                        dstpath = Path.Combine(
                            Path.GetDirectoryName(dstpath),
                            "_重複",
                            Path.GetFileNameWithoutExtension(dstpath));
                    }
                    else
                    {
                        dstpath = newPath;
                    }
                    renamed = true;
                }
            }

            if(profile.EnableGunreFolder && renamed == false)
            {
                // ジャンルフォルダに入れる
                string genreName = null;
                if (src.Genre.Count > 0)
                {
                    genreName = SubGenre.GetDisplayGenre(src.Genre[0])?.Main?.Name;
                }
                if (string.IsNullOrEmpty(genreName))
                {
                    // ジャンルがない
                    dstpath = Path.Combine(
                        Path.GetDirectoryName(dstpath),
                        "_ジャンル情報なし",
                        Path.GetFileNameWithoutExtension(dstpath));
                }
                else {
                    dstpath = Path.Combine(
                        Path.GetDirectoryName(dstpath),
                        Util.EscapeFileName(genreName, true),
                        Path.GetFileNameWithoutExtension(dstpath));
                }
                renamed = true;
            }

            if(renamed)
            {
                // EDCB関連ファイルの移動に使う
                src.ActualDstPath = dstpath;
                // ディレクトリは作っておく
                Directory.CreateDirectory(Path.GetDirectoryName(dstpath));
            }

            if (dir.IsTest)
            {
                // 同じ名前のファイルがある場合はサフィックス(-A...)を付ける
                var ext = ServerSupport.GetFileExtension(profile.OutputFormat);
                var baseName = Path.Combine(
                    Path.GetDirectoryName(dstpath),
                    Path.GetFileNameWithoutExtension(dstpath));
                dstpath = Util.CreateDstFile(baseName, ext);
            }

            bool isMp4 = src.SrcPath.ToLower().EndsWith(".mp4");
            string srcpath = src.SrcPath;
            string localsrc = null;
            string localdst = dstpath;
            string tmpBase = null;

            try
            {
                if (dir.HashList != null || src.Hash != null)
                {
                    // ハッシュがある（ネットワーク経由）の場合はローカルにコピー
                    // NASとエンコードPCが同じ場合はローカルでのコピーとなってしまうが
                    // そこだけ特別処理するのは大変なので、全部同じようにコピーする

                    tmpBase = Util.CreateTmpFile(server.AppData_.setting.WorkPath);
                    localsrc = tmpBase + "-in" + Path.GetExtension(srcpath);
                    string name = Path.GetFileName(srcpath);
                    if (dir.HashList != null && dir.HashList.ContainsKey(name) == false)
                    {
                        return FailLogItem(src.SrcPath, dir.Profile.Name, "入力ファイルのハッシュがありません", now, now);
                    }

                    byte[] hash = await HashUtil.CopyWithHash(srcpath, localsrc);
                    var refhash = (dir.HashList != null) ? dir.HashList[name] : src.Hash;
                    if (hash.SequenceEqual(refhash) == false)
                    {
                        File.Delete(localsrc);
                        return FailLogItem(src.SrcPath, dir.Profile.Name, "コピーしたファイルのハッシュが一致しません", now, now);
                    }

                    srcpath = localsrc;
                    localdst = tmpBase + "-out.mp4";
                }

                string json = Path.Combine(
                    Path.GetDirectoryName(localdst),
                    Path.GetFileNameWithoutExtension(localdst)) + "-enc.json";
                string logpath = Path.Combine(
                    Path.GetDirectoryName(dstpath),
                    Path.GetFileNameWithoutExtension(dstpath)) + "-enc.log";
                string jlscmd = serviceSetting.DisableCMCheck ?
                    null :
                    (string.IsNullOrEmpty(serviceSetting.JLSCommand) ?
                    profile.DefaultJLSCommand :
                    serviceSetting.JLSCommand);
                string jlsopt = serviceSetting.DisableCMCheck ?
                    null : serviceSetting.JLSOption;

                string args = server.MakeAmatsukazeArgs(
                    profile,
                    server.AppData_.setting,
                    isMp4,
                    srcpath, localdst, json,
                    src.ServiceId, logopaths, ignoreNoLogo, jlscmd, jlsopt);
                string exename = server.AppData_.setting.AmatsukazePath;

                int outputMask = profile.OutputMask;

                Util.AddLog(id, "エンコード開始: " + src.SrcPath);
                Util.AddLog(id, "Args: " + exename + " " + args);

                DateTime start = DateTime.Now;

                var psi = new ProcessStartInfo(exename, args)
                {
                    UseShellExecute = false,
                    WorkingDirectory = Directory.GetCurrentDirectory(),
                    RedirectStandardError = true,
                    RedirectStandardOutput = true,
                    RedirectStandardInput = false,
                    CreateNoWindow = true
                };

                int exitCode = -1;
                bool isCanceled = false;
                logText.Clear();

                try
                {
                    using (var p = Process.Start(psi))
                    {
                        try
                        {
                            // アフィニティを設定
                            IntPtr affinityMask = new IntPtr((long)server.affinityCreator.GetMask(id));
                            Util.AddLog(id, "AffinityMask: " + affinityMask.ToInt64());
                            p.ProcessorAffinity = affinityMask;
                            p.PriorityClass = ProcessPriorityClass.BelowNormal;
                        }
                        catch (InvalidOperationException)
                        {
                            // 既にプロセスが終了していると例外が出るが無視する
                        }

                        current = new TranscodeTask()
                        {
                            thread = this,
                            src = src,
                            process = p,
                        };

                        using (current.logWriter = File.Create(logpath))
                        {
                            // 起動コマンドをログ出力
                            await WriteTextBytes(server, current, Encoding.Default.GetBytes(exename + " " + args + "\n"));

                            await Task.WhenAll(
                                RedirectOut(server, current, p.StandardOutput.BaseStream),
                                RedirectOut(server, current, p.StandardError.BaseStream),
                                Task.Run(() =>
                                {
                                    while (p.WaitForExit(1000) == false)
                                    {
                                        if (src.State == QueueState.Canceled)
                                        {
                                            // キャンセルされた
                                            p.Kill();
                                            isCanceled = true;
                                            break;
                                        }
                                    }
                                }));
                        }

                        exitCode = p.ExitCode;
                    }
                }
                catch (Win32Exception w32e)
                {
                    Util.AddLog(id, "Amatsukazeプロセス起動に失敗");
                    throw w32e;
                }
                catch (IOException ioe)
                {
                    Util.AddLog(id, "ログファイル生成に失敗");
                    throw ioe;
                }
                finally
                {
                    if (current != null)
                    {
                        current.logWriter = null;
                        current = null;
                    }
                }

                DateTime finish = DateTime.Now;

                if (dir.HashList != null || src.Hash != null)
                {
                    File.Delete(localsrc);
                }

                // ログを整形したテキストに置き換える
                using (var fs = new StreamWriter(File.Create(logpath), Encoding.Default))
                {
                    foreach (var str in logText.TextLines)
                    {
                        fs.WriteLine(str);
                    }
                }

                // ログファイルを専用フォルダにコピー
                if (File.Exists(logpath))
                {
                    string logbase = server.GetLogFileBase(start);
                    Directory.CreateDirectory(Path.GetDirectoryName(logbase));
                    string dstlog = logbase + ".txt";
                    if (profile.MoveLogFile)
                    {
                        File.Move(logpath, dstlog);
                    }
                    else
                    {
                        File.Copy(logpath, dstlog);
                    }

                    if (File.Exists(json))
                    {
                        string dstjson = logbase + ".json";
                        File.Move(json, dstjson);
                        json = dstjson;
                    }
                }

                if (exitCode == 0 && isCanceled == false)
                {
                    // 成功
                    var log = LogFromJson(isMp4, dir.Profile.Name, json, start, finish, src, outputMask);

                    // ハッシュがある（ネットワーク経由）の場合はリモートにコピー
                    if (dir.HashList != null || src.Hash != null)
                    {
                        log.SrcPath = src.SrcPath;
                        string localbase = Path.GetDirectoryName(localdst) + "\\" + Path.GetFileNameWithoutExtension(localdst);
                        string outbase = Path.GetDirectoryName(dstpath) + "\\" + Path.GetFileNameWithoutExtension(dstpath);
                        for (int i = 0; i < log.OutPath.Count; ++i)
                        {
                            string outpath = outbase + log.OutPath[i].Substring(localbase.Length);
                            var hash = await HashUtil.CopyWithHash(log.OutPath[i], outpath);
                            string name = Path.GetFileName(outpath);
                            HashUtil.AppendHash(Path.Combine(Path.GetDirectoryName(src.DstPath), "_encoded.hash"), name, hash);
                            File.Delete(log.OutPath[i]);
                            log.OutPath[i] = outpath;
                        }
                    }

                    return log;
                }

                // 失敗 //

                if (dir.IsTest)
                {
                    // 出力ファイルを削除
                    File.Delete(dstpath);
                }

                if (isCanceled)
                {
                    // キャンセルされた
                    return FailLogItem(src.SrcPath, dir.Profile.Name, "キャンセルされました", start, finish);
                }
                else if (exitCode == 100)
                {
                    // マッチするロゴがなかった
                    return FailLogItem(src.SrcPath, dir.Profile.Name, "マッチするロゴがありませんでした", start, finish);
                }
                else if (exitCode == 101)
                {
                    // DRCSマッピングがなかった
                    return FailLogItem(src.SrcPath, dir.Profile.Name, "DRCS外字のマッピングがありませんでした", start, finish);
                }
                else
                {
                    // その他
                    return FailLogItem(src.SrcPath, dir.Profile.Name,
                        "Amatsukaze.exeはコード" + exitCode + "で終了しました。", start, finish);
                }

            }
            finally
            {
                if (tmpBase != null)
                {
                    File.Delete(tmpBase);
                }
            }
        }

        private async Task<bool> RunEncodeItem(QueueItem workerItem)
        {
            try
            {
                var dir = workerItem.Dir;
                var src = workerItem;

                // キューじゃなかったらダメ
                if (src.State != QueueState.Queue)
                {
                    return true;
                }

                if (dir.IsBatch)
                {
                    Directory.CreateDirectory(dir.Succeeded);
                    Directory.CreateDirectory(dir.Failed);
                }

                Directory.CreateDirectory(Path.GetDirectoryName(src.DstPath));

                // 待たなくてもいいタスクリスト
                waitList = new List<Task>();

                LogItem logItem = null;
                bool result = true;

                server.UpdateQueueItem(src, waitList, true);
                if (src.State == QueueState.Queue)
                {
                    src.State = QueueState.Encoding;
                    waitList.Add(server.NotifyQueueItemUpdate(src));
                    logItem = await ProcessItem(server, src, dir);
                }

                if (logItem == null)
                {
                    // ペンディング
                    src.State = QueueState.LogoPending;
                    // 他の項目も更新しておく
                    server.UpdateQueueItems(waitList);
                }
                else
                {
                    if (logItem.Success)
                    {
                        src.State = QueueState.Complete;
                    }
                    else
                    {
                        if (src.State != QueueState.Canceled)
                        {
                            src.State = QueueState.Failed;
                        }
                        src.FailReason = logItem.Reason;
                        result = false;
                    }

                    if (dir.IsBatch)
                    {
                        var sameItems = dir.Items.Where(s => s.SrcPath == src.SrcPath);
                        if (sameItems.Any(s => s.IsActive) == false)
                        {
                            // もうこのファイルでアクティブなアイテムはない

                            if (sameItems.Any(s => s.State == QueueState.Complete))
                            {
                                // リネームしてる場合は、そのパスを使う
                                var dstpath = sameItems.FirstOrDefault(s => s.ActualDstPath != null)?.ActualDstPath ?? src.DstPath;

                                // 成功が1つでもあれば関連ファイルをコピー
                                if (dir.Profile.MoveEDCBFiles)
                                {
                                    var srcBody = Path.GetDirectoryName(src.SrcPath) + "\\" + Path.GetFileNameWithoutExtension(src.SrcPath);
                                    var dstBody = Path.GetDirectoryName(dstpath) + "\\" + Path.GetFileNameWithoutExtension(dstpath);
                                    foreach (var ext in ServerSupport
                                        .GetFileExtentions(null, dir.Profile.MoveEDCBFiles))
                                    {
                                        var srcPath = srcBody + ext;
                                        var dstPath = dstBody + ext;
                                        if (File.Exists(srcPath) && !File.Exists(dstPath))
                                        {
                                            File.Copy(srcPath, dstPath);
                                        }
                                    }
                                }
                            }

                            // キャンセルの場合、削除されている可能性がある
                            // 自分がキャンセルされている場合は、移動しない
                            if (src.State != QueueState.Canceled && sameItems.All(s => s.State != QueueState.Canceled))
                            {
                                // キャンセルが1つもない場合のみ
                                if (sameItems.Any(s => s.State == QueueState.Failed))
                                {
                                    // 失敗がある
                                    EncodeServer.MoveTSFile(src.SrcPath, dir.Failed, dir.Profile.MoveEDCBFiles);
                                }
                                else
                                {
                                    // 全て成功
                                    EncodeServer.MoveTSFile(src.SrcPath, dir.Succeeded, dir.Profile.MoveEDCBFiles);
                                }
                            }
                        }
                    }

                    server.Log.Items.Add(logItem);
                    server.WriteLog();
                    waitList.Add(server.Client.OnLogUpdate(server.Log.Items.Last()));
                }

                waitList.Add(server.NotifyQueueItemUpdate(src));
                waitList.Add(server.RequestFreeSpace());

                await Task.WhenAll(waitList);

                return result;

            }
            catch (Exception e)
            {
                await server.Client.OnOperationResult(new OperationResult()
                {
                    IsFailed = true,
                    Message = "予期せぬエラー: " + e.Message
                });
                return false;
            }
        }

        private async Task<bool> ProcessSearchItem(EncodeServer server, QueueItem src)
        {
            if (File.Exists(src.SrcPath) == false)
            {
                return false;
            }

            string args = server.MakeAmatsukazeSearchArgs(
                src.SrcPath, src.ServiceId);
            string exename = server.AppData_.setting.AmatsukazePath;

            Util.AddLog(id, "サーチ開始: " + src.SrcPath);
            Util.AddLog(id, "Args: " + exename + " " + args);

            var psi = new ProcessStartInfo(exename, args)
            {
                UseShellExecute = false,
                WorkingDirectory = Directory.GetCurrentDirectory(),
                RedirectStandardError = true,
                RedirectStandardOutput = true,
                RedirectStandardInput = false,
                CreateNoWindow = true
            };

            int exitCode = -1;
            logText.Clear();

            try
            {
                using (var p = Process.Start(psi))
                {
                    try
                    {
                        p.PriorityClass = ProcessPriorityClass.BelowNormal;
                    }
                    catch (InvalidOperationException)
                    {
                        // 既にプロセスが終了していると例外が出るが無視する
                    }

                    current = new TranscodeTask()
                    {
                        thread = this,
                        src = src,
                        process = p,
                    };

                    // 起動コマンドをログ出力
                    await WriteTextBytes(server, current, Encoding.Default.GetBytes(exename + " " + args + "\n"));

                    await Task.WhenAll(
                        RedirectOut(server, current, p.StandardOutput.BaseStream),
                        RedirectOut(server, current, p.StandardError.BaseStream),
                        Task.Run(() => p.WaitForExit()));

                    exitCode = p.ExitCode;
                }

                if (exitCode == 0)
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
            catch (Win32Exception w32e)
            {
                Util.AddLog(id, "Amatsukazeプロセス起動に失敗");
                throw w32e;
            }
            finally
            {
                current = null;
            }
        }

        private async Task<bool> RunSearchItem(QueueItem workerItem)
        {
            try
            {
                var dir = workerItem.Dir;
                var src = workerItem;

                // キューじゃなかったらダメ
                if (src.State != QueueState.Queue)
                {
                    return true;
                }

                // 待たなくてもいいタスクリスト
                waitList = new List<Task>();

                src.State = QueueState.Encoding;
                waitList.Add(server.NotifyQueueItemUpdate(src));
                bool result = await ProcessSearchItem(server, src);
                src.State = result ? QueueState.Complete : QueueState.Failed;
                waitList.Add(server.NotifyQueueItemUpdate(src));

                await Task.WhenAll(waitList);

                return result;

            }
            catch (Exception e)
            {
                await server.Client.OnOperationResult(new OperationResult()
                {
                    IsFailed = true,
                    Message = "予期せぬエラー: " + e.Message
                });
                return false;
            }
        }

        public Task<bool> RunItem(QueueItem workerItem)
        {
            if (workerItem.Dir.Mode == ProcMode.DrcsSearch)
            {
                return RunSearchItem(workerItem);
            }
            else
            {
                return RunEncodeItem(workerItem);
            }
        }
    }
}
