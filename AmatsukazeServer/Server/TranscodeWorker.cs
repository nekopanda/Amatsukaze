using Amatsukaze.Lib;
using Codeplex.Data;
using log4net;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Net;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    internal class TranscodeWorker : ConsoleTextBase, IScheduleWorker
    {
        public int Id { get; private set; }
        
        private EncodeServer server;
        private RollingTextLines logText;
        private RollingTextLines consoleText = new RollingTextLines(500);

        private ILog preScriptLog = LogManager.GetLogger("UserScript.Pre");
        private ILog postScriptLog = LogManager.GetLogger("UserScript.Post");
        private ILog currentScriptLog;

        private QueueItem item;
        private LogWriter logWriter;
        private CancellationTokenSource resourceCancel;
        private IProcessExecuter process;

        public List<string> TextLines { get { return consoleText.TextLines; } }
        public EncodeState State { get; private set; }

        public bool ScheduledSuspended { get; private set; }
        public bool UserSuspended { get; private set; }
        public bool Suspended { get { return ScheduledSuspended || UserSuspended; } }

        private List<Task> waitList;

        public TranscodeWorker(int id, EncodeServer server)
        {
            this.Id = id;
            this.server = server;
        }

        public override void OnAddLine(string text)
        {
            logText?.AddLine(text);
            consoleText.AddLine(text);
            currentScriptLog?.Info(text);
        }

        public override void OnReplaceLine(string text)
        {
            logText?.ReplaceLine(text);
            consoleText.ReplaceLine(text);
            currentScriptLog?.Info(text);
        }

        public void CancelCurrentItem()
        {
            if (item != null)
            {
                // キャンセル状態にする
                item.State = QueueState.Canceled;

                // ここでは早く終わる（呼び出しから戻ってくる）ようにするだけで、リソースの解放等は行わない
                // リソースの解放は確保した人の仕事。ここでやってしまうと確保した人が
                // いつ解放されるか分からないリソースを相手にしなければならなくなるので大変

                // プロセスが残っていたら終了
                if (process != null)
                {
                    try
                    {
                        process.Canel();
                    }
                    catch (InvalidOperationException)
                    {
                        // プロセスが既に終了していた場合
                    }
                }

                // リソース待ちだったらキャンセル
                if(resourceCancel != null)
                {
                    resourceCancel.Cancel();
                }
            }
        }

        public bool CancelItem(QueueItem item)
        {
            if(item == this.item)
            {
                CancelCurrentItem();
                return true;
            }
            return false;
        }

        public void SetSuspend(bool suspend, bool scheduled)
        {
            bool current = Suspended;
            if(scheduled)
            {
                ScheduledSuspended = suspend;
            }
            else
            {
                UserSuspended = suspend;
            }
            if(Suspended != current)
            {
                if(Suspended)
                {
                    process?.Suspend();
                }
                else
                {
                    process?.Resume();
                }
            }
        }

        private Task WriteTextBytes(byte[] buffer, int offset, int length)
        {
            if (logWriter != null)
            {
                logWriter.Write(buffer, offset, length);
            }
            AddBytes(buffer, offset, length);

            byte[] newbuf = new byte[length];
            Array.Copy(buffer, newbuf, length);
            return server.Client.OnConsoleUpdate(new ConsoleUpdate() { index = Id, data = newbuf });
        }

        private Task WriteTextBytes(byte[] buffer)
        {
            return WriteTextBytes(buffer, 0, buffer.Length);
        }

        private async Task RedirectOut(StreamReader stream)
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
                    await WriteTextBytes(Encoding.Default.GetBytes(line + "\n"));
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

        private async Task GetRenamed(RenamedResult result, StreamReader stream)
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
        private async Task<string> SCRename(string screnamepath, string tmppath, string format, QueueItem item)
        {
            var filename = item.FileName;
            var time = item.TsTime;
            var eventName = item.EventName;
            var serviceName = item.ServiceName;

            var ext = ".ts";

            // 情報がある時はその情報を元にファイル名を作成
            // ないときはファイル名をそのまま使う
            string srcname = filename;
            if (time > new DateTime(2000, 1, 1) &&
                string.IsNullOrEmpty(eventName) == false &&
                string.IsNullOrEmpty(serviceName) == false)
            {
                srcname = time.ToString("yyyyMMddHHmm") + "_" +
                    Util.EscapeFileName(eventName, true) + " _" +
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

                // キャンセルチェック
                if(item.State == QueueState.Canceled)
                {
                    return null;
                }

                var result = new RenamedResult();
                using (var p = new NormalProcess(psi))
                {
                    process = p;

                    // 起動コマンドをログ出力
                    await WriteTextBytes(Encoding.Default.GetBytes(exename + " " + args + "\n"));

                    await Task.WhenAll(
                        GetRenamed(result, p.Process.StandardOutput),
                        RedirectOut(p.Process.StandardError),
                        Task.Run(() => p.Process.WaitForExit()));
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
            }
        }

        private LogItem FailLogItem(QueueItem item, string profile, string reason, DateTime start, DateTime finish)
        {
            return new LogItem()
            {
                Success = false,
                Reason = reason,
                SrcPath = item.SrcPath,
                MachineName = Dns.GetHostName(),
                EncodeStartDate = start,
                EncodeFinishDate = finish,
                Profile = profile,
                ServiceName = item.ServiceName,
                ServiceId = item.ServiceId,
                TsTime = item.TsTime,
            };
        }

        private LogItem LogFromJson(bool isGeneric, string profile, string jsonpath, DateTime start, DateTime finish, QueueItem item, int outputMask)
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
                TrimAVS = json.trimavs,
                OutputMask = outputMask,
                ServiceName = item.ServiceName,
                ServiceId = item.ServiceId,
                TsTime = item.TsTime,
                LogoFiles = logofiles,
                Incident = error.Sum(s => s.Count),
                Error = error,
                Profile = profile,
            };
        }

        private CheckLogItem MakeCheckLogItem(ProcMode mode, bool success,
            QueueItem item, string profile, string reason, DateTime start, DateTime finish)
        {
            return new CheckLogItem()
            {
                Type = (mode == ProcMode.DrcsCheck) ? CheckType.DRCS : CheckType.CM,
                Success = success,
                Reason = reason,
                SrcPath = item.SrcPath,
                CheckStartDate = start,
                CheckFinishDate = finish,
                Profile = profile,
                ServiceName = item.ServiceName,
                ServiceId = item.ServiceId,
                TsTime = item.TsTime,
            };
        }

        private static string ModeToString(ProcMode mode)
        {
            switch(mode)
            {
                case ProcMode.AutoBatch:
                case ProcMode.Batch:
                    return "エンコード";
                case ProcMode.Test:
                    return "エンコード（テスト）";
                case ProcMode.DrcsCheck:
                    return "DRCSチェック";
                case ProcMode.CMCheck:
                    return "CM解析";
                default:
                    return "不明な処理";
            }
        }

        private async Task ReadBytes(PipeStream readPipe, byte[] buf)
        {
            int readBytes = 0;
            while (readBytes < buf.Length)
            {
                readBytes += await readPipe.ReadAsync(buf, readBytes, buf.Length - readBytes);
            }
        }

        private async Task<ResourcePhase> ReadCommand(PipeStream readPipe)
        {
            byte[] buf = new byte[4];
            await ReadBytes(readPipe, buf);
            return (ResourcePhase)BitConverter.ToInt32(buf, 0);
        }

        private Task WriteBytes(PipeStream writePipe, byte[] buf)
        {
            return writePipe.WriteAsync(buf, 0, buf.Length);
        }

        private static byte[] Combine(params byte[][] arrays)
        {
            byte[] rv = new byte[arrays.Sum(a => a.Length)];
            int offset = 0;
            foreach (byte[] array in arrays)
            {
                System.Buffer.BlockCopy(array, 0, rv, offset, array.Length);
                offset += array.Length;
            }
            return rv;
        }

        private Task WriteCommand(PipeStream writePipe, ResourcePhase phase, int gpuIndex, int group, ulong mask)
        {
            return WriteBytes(writePipe, Combine(
                BitConverter.GetBytes((int)phase),
                BitConverter.GetBytes(gpuIndex),
                BitConverter.GetBytes(group),
                BitConverter.GetBytes(mask)));
        }

        private async Task HostThread(PipeCommunicator pipes, bool ignoreResource)
        {
            if (!server.AppData_.setting.SchedulingEnabled)
            {
                return;
            }

            var ress = item.Profile.ReqResources;
            var ignoreAffinity = item.Profile.IgnoreEncodeAffinity;
            
            // 現在専有中のリソース
            Resource resource = null;

            //Util.AddLog("ホストスレッド開始@" + id);

            try
            {
                // 子プロセスが終了するまでループ
                while (true)
                {
                    var cmd = await ReadCommand(pipes.ReadPipe);

                    if (resource != null)
                    {
                        server.ResourceManager.ReleaseResource(resource);
                        resource = null;
                    }

                    var nowait = (cmd & ResourcePhase.NoWait) != 0;
                    cmd &= ~ResourcePhase.NoWait;
                    var reqEncoderIndex = (!ignoreAffinity) && (cmd == ResourcePhase.Encode);

                    // リソース確保
                    if (ignoreResource)
                    {
                        // リソース上限無視なのでNoWaitは関係ない
                        //Util.AddLog("フェーズ移行リクエスト（上限無視）: " + cmd + "@" + id);
                        resource = server.ResourceManager.ForceGetResource(ress[(int)cmd], reqEncoderIndex);
                    }
                    else if (nowait)
                    {
                        // NoWait指定の場合は待たない
                        //Util.AddLog("フェーズ移行NoWaitリクエスト: " + cmd + "@" + id);
                        resource = server.ResourceManager.TryGetResource(ress[(int)cmd], reqEncoderIndex);
                    }
                    else
                    {
                        try
                        {
                            resourceCancel = new CancellationTokenSource();
                            //Util.AddLog("フェーズ移行リクエスト: " + cmd + "@" + id);
                            resource = await server.ResourceManager.GetResource(ress[(int)cmd], resourceCancel.Token, reqEncoderIndex);
                        }
                        finally
                        {
                            // GetResourceを抜けてるならもう必要ない
                            resourceCancel = null;
                        }
                    }

                    // 確保したリソースを通知
                    // 確保に失敗したら-1
                    //Util.AddLog("フェーズ移行" + ((resource != null) ? "成功" : "失敗") + "通知: " + cmd + "@" + id);
                    int gpuIndex = -1;
                    int group = -1;
                    ulong mask = 0;
                    if(resource != null)
                    {
                        gpuIndex = resource.GpuIndex;
                        var setting = server.AppData_.setting.AffinitySetting;
                        if (resource.EncoderIndex != -1 &&
                            setting != (int)ProcessGroupKind.None)
                        {
                            var s = server.affinityCreator.GetMask(
                                (ProcessGroupKind)setting, resource.EncoderIndex);
                            group = s.Group;
                            mask = s.Mask;
                        }
                    }
                    await WriteCommand(pipes.WritePipe, cmd, gpuIndex, group, mask);

                    // UIクライアントに通知
                    State = new EncodeState()
                    {
                        ConsoleId = Id,
                        Phase = (resource != null) ? cmd : ResourcePhase.Max,
                        Resource = resource
                    };
                    await server.Client.OnEncodeState(State);
                }
            }
            catch(Exception)
            {
                // 子プロセスが終了すると例外を吐く
            }
            finally
            {
                //Util.AddLog("ホストスレッド終了@" + id);
                // 専有中のリソースがあったら解放
                if (resource != null)
                {
                    server.ResourceManager.ReleaseResource(resource);
                    resource = null;
                }

                // UIクライアントに通知
                State = new EncodeState()
                {
                    ConsoleId = Id,
                    Phase = ResourcePhase.Max,
                    Resource = null
                };
                await server.Client.OnEncodeState(State);
            }
        }

        private object GetCancelLog(DateTime start, DateTime finish)
        {
            if (item.IsCheck)
            {
                return MakeCheckLogItem(item.Mode, false, item, item.Profile.Name, "キャンセルされました", start, finish);
            }
            else
            {
                return FailLogItem(item, item.Profile.Name, "キャンセルされました", start, finish);
            }
        }

        private static async Task CopyFileAsync(string sourcePath, string destinationPath)
        {
            using (Stream source = File.Open(sourcePath, FileMode.Open))
            {
                using (Stream destination = File.Create(destinationPath))
                {
                    await source.CopyToAsync(destination);
                }
            }
        }

        private static Encoding defaultEncoding = Encoding.GetEncoding(
            Encoding.Default.CodePage, new EncoderExceptionFallback(), new DecoderExceptionFallback());

        // システムデフォルトエンコーディングで変換可能な文字列か？
        private static bool IsEncodableString(string str)
        {
            try
            {
                defaultEncoding.GetBytes(str);
            }
            catch(Exception)
            {
                return false;
            }
            return true;
        }

        private async Task<object> ProcessItem(bool ignoreResource)
        {
            DateTime now = item.EncodeStart;

            if (File.Exists(item.SrcPath) == false)
            {
                if(item.IsCheck)
                {
                    return MakeCheckLogItem(item.Mode, false, item, item.Profile.Name, "入力ファイルが見つかりません", now, now);
                }
                else
                {
                    return FailLogItem(item, item.Profile.Name, "入力ファイルが見つかりません", now, now);
                }
            }

            if (item.Mode != ProcMode.DrcsCheck && server.AppData_.services.ServiceMap.ContainsKey(item.ServiceId) == false)
            {
                if (item.IsCheck)
                {
                    return MakeCheckLogItem(item.Mode, false, item, item.Profile.Name, "サービス設定がありません", now, now);
                }
                else
                {
                    return FailLogItem(item, item.Profile.Name, "サービス設定がありません", now, now);
                }
            }

            ProfileSetting profile = item.Profile;
            ServiceSettingElement serviceSetting =
                (item.Mode != ProcMode.DrcsCheck) ?
                server.AppData_.services.ServiceMap[item.ServiceId] :
                null;

            // 実行前バッチ
            if(!string.IsNullOrEmpty(profile.PreBatchFile))
            {
                using (var scriptExecuter = new UserScriptExecuter()
                {
                    Server = server,
                    Phase = ScriptPhase.PreEncode,
                    ScriptPath = server.GetBatDirectoryPath() + "\\" + profile.PreBatchFile,
                    Item = item,
                    OnOutput = WriteTextBytes
                })
                {
                    try
                    {
                        preScriptLog.Info("実行前バッチ起動: " + item.SrcPath);
                        process = scriptExecuter;
                        currentScriptLog = preScriptLog;
                        await scriptExecuter.Execute();
                    }
                    finally
                    {
                        process = null;
                        currentScriptLog = null;
                    }
                }
            }

            // キャンセルチェック
            if (item.State == QueueState.Canceled)
            {
                return GetCancelLog(now, now);
            }

            bool ignoreNoLogo = true;
            string[] logopaths = null;
            if (item.Mode != ProcMode.DrcsCheck && profile.DisableChapter == false)
            {
                var logofiles = serviceSetting.LogoSettings
                    .Where(s => s.CanUse(item.TsTime))
                    .Select(s => s.FileName)
                    .ToArray();
                if (logofiles.Length == 0)
                {
                    // これは必要ないはず
                    item.FailReason = "ロゴ設定がありません";
                    return null;
                }
                ignoreNoLogo = !logofiles.All(path => path != LogoSetting.NO_LOGO);
                logopaths = logofiles.Where(path => path != LogoSetting.NO_LOGO).ToArray();
            }
            ignoreNoLogo |= profile.IgnoreNoLogo;

            // 出力パス生成
            // datpathは拡張子を含まないこと
            //（拡張子があるのかないのか分からないと.tsで終わる名前とかが使えなくなるので）
            var ext = ServerSupport.GetFileExtension(profile.OutputFormat);
            string dstpath = item.DstPath;
            bool renamed = false;

            if (item.IsCheck == false && profile.EnableRename)
            {
                // SCRenameによるリネーム
                string newName = null;
                try
                {
                    newName = await SCRename(
                        server.AppData_.setting.SCRenamePath, 
                        server.AppData_.setting.WorkPath,
                        profile.RenameFormat, item);
                }
                catch (Exception)
                {
                    return FailLogItem(item, item.Profile.Name, "SCRenameに失敗", now, now);
                }

                if (newName != null)
                {
                    var newPath = Path.Combine(
                        Path.GetDirectoryName(dstpath), newName);
                    if (File.Exists(newPath + ext))
                    {
                        // 同名ファイルが存在する場合は、ファイル名は変えずに別のフォルダに移動する
                        dstpath = Path.Combine(
                            Path.GetDirectoryName(dstpath),
                            "_重複",
                            Path.GetFileName(dstpath));
                    }
                    else
                    {
                        dstpath = newPath;
                    }
                    renamed = true;
                }
            }

            // キャンセルチェック
            if (item.State == QueueState.Canceled)
            {
                return GetCancelLog(now, now);
            }

            if (item.IsCheck == false && profile.EnableGunreFolder && renamed == false)
            {
                // ジャンルフォルダに入れる
                string genreName = null;
                if (item.Genre.Count > 0)
                {
                    genreName = SubGenre.GetDisplayGenre(item.Genre[0])?.Main?.Name;
                }
                if (string.IsNullOrEmpty(genreName))
                {
                    // ジャンルがない
                    dstpath = Path.Combine(
                        Path.GetDirectoryName(dstpath),
                        "_ジャンル情報なし",
                        Path.GetFileName(dstpath));
                }
                else {
                    dstpath = Path.Combine(
                        Path.GetDirectoryName(dstpath),
                        Util.EscapeFileName(genreName, true),
                        Path.GetFileName(dstpath));
                }
                renamed = true;
            }

            if(renamed)
            {
                // EDCB関連ファイルの移動に使う
                item.ActualDstPath = dstpath;
                // ディレクトリは作っておく
                Directory.CreateDirectory(Path.GetDirectoryName(dstpath));
            }

            if (item.IsCheck == false && item.IsTest)
            {
                // 同じ名前のファイルがある場合はサフィックス(-A...)を付ける
                var baseName = Path.Combine(
                    Path.GetDirectoryName(dstpath),
                    Path.GetFileName(dstpath));
                dstpath = Util.CreateDstFile(baseName, ext);
            }

            bool isMp4 = item.SrcPath.ToLower().EndsWith(".mp4");
            string srcpath = item.SrcPath;
            string localsrc = null;
            string localdst = dstpath;
            string tmpBase = null;

            // Trim指定ファイル
            string trimavs = srcpath + ".trim.avs";
            if(!File.Exists(trimavs))
            {
                trimavs = null;
            }

            try
            {
                bool hashEnabled = (item.IsCheck == false && item.Hash != null && profile.DisableHashCheck == false);
                // システムデフォルトエンコーディングで変換不可なファイル名の場合もコピー
                bool needCopy = hashEnabled || !IsEncodableString(srcpath + ";" + dstpath);
                if (needCopy)
                {
                    // ハッシュがある（ネットワーク経由）の場合はローカルにコピー
                    // NASとエンコードPCが同じ場合はローカルでのコピーとなってしまうが
                    // そこだけ特別処理するのは大変なので、全部同じようにコピーする

                    tmpBase = Util.CreateTmpFile(server.AppData_.setting.WorkPath);
                    localsrc = tmpBase + "-in" + Path.GetExtension(srcpath);
                    string name = Path.GetFileName(srcpath);

                    if(hashEnabled)
                    {
                        byte[] hash = await HashUtil.CopyWithHash(srcpath, localsrc);
                        if (hash.SequenceEqual(item.Hash) == false)
                        {
                            File.Delete(localsrc);
                            return FailLogItem(item, item.Profile.Name, "コピーしたファイルのハッシュが一致しません", now, now);
                        }
                    }
                    else
                    {
                        await CopyFileAsync(srcpath, localsrc);
                    }

                    srcpath = localsrc;
                    localdst = tmpBase + "-out";
                }

                // リソース管理用
                PipeCommunicator pipes = null;
                if (server.AppData_.setting.SchedulingEnabled)
                {
                    pipes = new PipeCommunicator();
                }

                string json = Path.Combine(
                    Path.GetDirectoryName(localdst),
                    Path.GetFileName(localdst)) + "-enc.json";
                string logpath = Path.Combine(
                    Path.GetDirectoryName(dstpath),
                    Path.GetFileName(dstpath)) + "-enc.log";
                string jlscmd = (serviceSetting?.DisableCMCheck ?? true) ?
                    null :
                    (!string.IsNullOrEmpty(profile.JLSCommandFile) ? profile.JLSCommandFile
                    : !string.IsNullOrEmpty(serviceSetting?.JLSCommand ?? null) ? serviceSetting.JLSCommand
                    : "JL_標準.txt");
                string jlsopt = (serviceSetting?.DisableCMCheck ?? true) ? null
                    : profile.EnableJLSOption ? profile.JLSOption
                    : serviceSetting.JLSOption;
                string ceopt = (serviceSetting?.DisableCMCheck ?? true) ? null : profile.ChapterExeOption;

                string args = server.MakeAmatsukazeArgs(
                    item.Mode, profile,
                    server.AppData_.setting,
                    isMp4,
                    srcpath, localdst + ext, json,
                    item.ServiceId, logopaths, ignoreNoLogo, jlscmd, jlsopt, ceopt, trimavs,
                    pipes?.InHandle, pipes?.OutHandle, Id);
                string exename = server.AppData_.setting.AmatsukazePath;

                int outputMask = profile.OutputMask;

                Util.AddLog(Id, ModeToString(item.Mode) + "開始: " + item.SrcPath, null);
                Util.AddLog(Id, "Args: " + exename + " " + args, null);

                // キャンセルチェック
                if (item.State == QueueState.Canceled)
                {
                    return GetCancelLog(now, now);
                }

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
                logText = new RollingTextLines(1 * 1024 * 1024);

                try
                {
                    using (var p = new NormalProcess(psi)
                    {
                        OnOutput = WriteTextBytes
                    })
                    {
                        process = p;

                        try
                        {
                            if (item.IsCheck == false)
                            {
                                // 優先度を設定
                                p.Process.PriorityClass = server.AppData_.setting.ProcessPriorityClass;
                            }
                        }
                        catch (InvalidOperationException)
                        {
                            // 既にプロセスが終了していると例外が出るが無視する
                        }

                        // これをやらないと子プロセスが終了してもreadが帰って来ないので注意
                        pipes?.DisposeLocalCopyOfClientHandle();

                        try
                        {
                            if (item.IsCheck == false && profile.DisableLogFile == false)
                            {
                                logWriter = new LogWriter(logpath);
                            }

                            // 起動コマンドをログ出力
                            await WriteTextBytes(Encoding.Default.GetBytes(exename + " " + args + "\n"));

                            // サスペンドチェック
                            if (Suspended)
                            {
                                process.Suspend();
                            }

                            // キャンセルチェック
                            if (item.State == QueueState.Canceled)
                            {
                                CancelCurrentItem();
                            }
                            else
                            {
                                await Task.WhenAll(
                                    p.WaitForExitAsync(),
                                    HostThread(pipes, ignoreResource));
                            }

                        }
                        finally
                        {
                            logWriter?.Close();
                            logWriter = null;
                        }

                        exitCode = p.Process.ExitCode;
                    }
                }
                catch (Win32Exception w32e)
                {
                    Util.AddLog(Id, "Amatsukazeプロセス起動に失敗", w32e);
                    if (item.IsCheck)
                    {
                        return MakeCheckLogItem(item.Mode, false, item, item.Profile.Name, "Amatsukazeプロセス起動に失敗", now, now);
                    }
                    else
                    {
                        return FailLogItem(item, item.Profile.Name, "Amatsukazeプロセス起動に失敗", now, now);
                    }
                }
                catch (IOException ioe)
                {
                    Util.AddLog(Id, "ログファイル生成に失敗", ioe);
                    if (item.IsCheck)
                    {
                        return MakeCheckLogItem(item.Mode, false, item, item.Profile.Name, "ログファイル生成に失敗", now, now);
                    }
                    else
                    {
                        return FailLogItem(item, item.Profile.Name, "ログファイル生成に失敗", now, now);
                    }
                }

                DateTime start = item.EncodeStart;
                DateTime finish = DateTime.Now;
                item.EncodeTime = finish - start;

                if (needCopy)
                {
                    File.Delete(localsrc);
                }

                // ログを整形したテキストに置き換える
                if (item.IsCheck == false && profile.DisableLogFile == false)
                {
                    using (var fs = new StreamWriter(File.Create(logpath), Encoding.Default))
                    {
                        foreach (var str in logText.TextLines)
                        {
                            fs.WriteLine(str);
                        }
                    }
                }

                // 専用フォルダにログを出力
                string logbase = item.IsCheck
                    ? server.GetCheckLogFileBase(start)
                    : server.GetLogFileBase(start);
                Directory.CreateDirectory(Path.GetDirectoryName(logbase));
                string dstlog = logbase + ".txt";
                using (var fs = new StreamWriter(File.Create(dstlog), Encoding.Default))
                {
                    foreach (var str in logText.TextLines)
                    {
                        fs.WriteLine(str);
                    }
                }

                logText = null;

                Util.AddLog(Id, ModeToString(item.Mode) + "終了: " + item.SrcPath, null);

                if (item.IsCheck)
                {
                    if (item.State == QueueState.Canceled)
                    {
                        return GetCancelLog(start, finish);
                    }
                    else if (exitCode == 0)
                    {
                        return MakeCheckLogItem(item.Mode, true, item, item.Profile.Name, "", start, finish);
                    }
                    else
                    {
                        // その他
                        return MakeCheckLogItem(item.Mode, false, item, item.Profile.Name,
                            "Amatsukaze.exeはコード" +
                            ServerSupport.ExitCodeString(exitCode) + "で終了しました。", start, finish);
                    }
                }
                else
                {
                    // 出力Jsonを専用フォルダにコピー
                    if (File.Exists(json))
                    {
                        string dstjson = logbase + ".json";
                        File.Move(json, dstjson);
                        json = dstjson;
                    }

                    if (exitCode == 0 && item.State != QueueState.Canceled)
                    {
                        // 成功
                        var log = LogFromJson(isMp4, item.Profile.Name, json, start, finish, item, outputMask);
                        var dstFullPath = dstpath + ext;
                        log.DstPath = dstpath;

                        if(File.Exists(dstFullPath) && log.OutPath.IndexOf(dstFullPath) == -1)
                        {
                            // 出力ファイル名が変わっている可能性があるのでゴミファイルが残らないように消しておく
                            if(new System.IO.FileInfo(dstFullPath).Length == 0)
                            {
                                File.Delete(dstFullPath);
                            }
                        }

                        // ハッシュがある（ネットワーク経由）の場合はリモートにコピー
                        if (needCopy)
                        {
                            log.SrcPath = item.SrcPath;
                            for (int i = 0; i < log.OutPath.Count; ++i)
                            {
                                string outpath = dstpath + log.OutPath[i].Substring(localdst.Length);
                                if (hashEnabled)
                                {
                                    var hash = await HashUtil.CopyWithHash(log.OutPath[i], outpath);
                                    string name = Path.GetFileName(outpath);
                                    HashUtil.AppendHash(Path.Combine(Path.GetDirectoryName(item.DstPath), "_encoded.hash"), name, hash);
                                }
                                else
                                {
                                    await CopyFileAsync(log.OutPath[i], outpath);
                                }
                                File.Delete(log.OutPath[i]);
                                log.OutPath[i] = outpath;
                            }
                        }

                        return log;
                    }

                    // 失敗 //

                    if (item.IsTest)
                    {
                        // 出力ファイルを削除
                        for(int retry = 0; ; ++retry)
                        {
                            // 終了直後は消せないことがあるので、リトライする
                            try
                            {
                                File.Delete(dstpath + ext);
                                break;
                            }
                            catch(IOException)
                            {
                                if (retry > 10) throw;
                                await Task.Delay(3000);
                            }
                        }
                    }

                    if (item.State == QueueState.Canceled)
                    {
                        return GetCancelLog(start, finish);
                    }
                    else if (exitCode == 100)
                    {
                        // マッチするロゴがなかった
                        return FailLogItem(item, item.Profile.Name, "マッチするロゴがありませんでした", start, finish);
                    }
                    else if (exitCode == 101)
                    {
                        // DRCSマッピングがなかった
                        return FailLogItem(item, item.Profile.Name, "DRCS外字のマッピングがありませんでした", start, finish);
                    }
                    else
                    {
                        // その他
                        return FailLogItem(item, item.Profile.Name,
                            "Amatsukaze.exeはコード" + 
                            ServerSupport.ExitCodeString(exitCode) + "で終了しました。", start, finish);
                    }
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

        private async Task MoveWithRetry(ServerSupport.MoveFileItem item)
        {
            Func<string, Task> Print = s => WriteTextBytes(Encoding.Default.GetBytes(s));

            int MAX_RETRY = 10 * 60;
            int retry = 0;

            while (true)
            {
                try
                {
                    if (File.Exists(item.SrcPath))
                    {
                        ServerSupport.MoveFile(item.SrcPath, item.DstPath);
                    }
                    if(retry > 0)
                    {
                        await Print(string.Format("ファイル「{0}」の移動に成功しました\n", item.SrcPath));
                    }
                    return;
                }
                catch (Exception e)
                {
                    if(retry++ < MAX_RETRY)
                    {
                        await Print(string.Format(
                            "ファイル「{0}」の移動に失敗しました。1秒後にリトライします({1}/{2})\r", 
                            item.SrcPath, retry, MAX_RETRY));
                        await Task.Delay(1000);
                        continue;
                    }
                    throw e;
                }
            }
        }

        private async Task MoveTSFileWithRetry(string file, string dstDir, bool withEDCB)
        {
            foreach (var item in ServerSupport.GetMoveList(file, dstDir, withEDCB))
            {
                await MoveWithRetry(item);
            }
        }

        private StateChangeEvent? EventFromItem(QueueItem item)
        {
            switch(item.State)
            {
                case QueueState.Failed:
                    return StateChangeEvent.EncodeFailed;
                case QueueState.Canceled:
                    return StateChangeEvent.EncodeCanceled;
                case QueueState.Complete:
                    return StateChangeEvent.EncodeSucceeded;
            }
            return null;
        }

        public async Task<bool> RunItem(QueueItem workerItem, bool forceStart)
        {
            try
            {
                item = workerItem;

                // キューじゃなかったらダメ
                // 同じアイテムが複数回スケジューラに登録される事があるのでここで弾く
                if (item.State != QueueState.Queue)
                {
                    return true;
                }

                var srcDir = Path.GetDirectoryName(item.SrcPath);
                var succeededDir = srcDir + "\\" + ServerSupport.SUCCESS_DIR;
                var failedDir = srcDir + "\\" + ServerSupport.FAIL_DIR;

                if (item.IsBatch)
                {
                    Directory.CreateDirectory(succeededDir);
                    Directory.CreateDirectory(failedDir);
                }

                if (item.IsCheck == false)
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(item.DstPath));
                }

                // 待たなくてもいいタスクリスト
                waitList = new List<Task>();

                // 互換性の問題からLogItemとCheckLogItemに
                // 基底クラスを追加することはできないのでdynamicにする
                dynamic logItem = null;
                bool result = true;

                server.UpdateQueueItem(item, waitList);
                if (item.State == QueueState.Queue)
                {
                    item.State = QueueState.Encoding;
                    item.EncodeStart = DateTime.Now;
                    item.ConsoleId = Id;
                    waitList.Add(server.NotifyQueueItemUpdate(item));
                    waitList.Add(server.RequestState(StateChangeEvent.EncodeStarted));
                    logItem = await ProcessItem(forceStart);
                }

                if (logItem == null)
                {
                    // ペンディング
                    item.State = QueueState.LogoPending;
                    // 他の項目も更新しておく
                    server.UpdateQueueItems(waitList);
                }
                else
                {
                    if (logItem.Success)
                    {
                        item.State = QueueState.Complete;
                    }
                    else
                    {
                        if (item.State != QueueState.Canceled)
                        {
                            item.State = QueueState.Failed;
                        }
                        item.FailReason = logItem.Reason;
                        result = false;
                    }

                    UserScriptExecuter scriptExecuter = null;
                    if(!string.IsNullOrEmpty(item.Profile.PostBatchFile))
                    {
                        scriptExecuter = new UserScriptExecuter()
                        {
                            Server = server,
                            Phase = ScriptPhase.PostEncode,
                            ScriptPath = server.GetBatDirectoryPath() + "\\" + item.Profile.PostBatchFile,
                            Item = item,
                            Log = logItem,
                            RelatedFiles = new List<string>(),
                            OnOutput = WriteTextBytes
                        };
                    }

                    if (item.IsBatch)
                    {
                        var sameItems = server.GetQueueItems(item.SrcPath);
                        if (sameItems.Any(s => s.IsActive) == false)
                        {
                            // もうこのファイルでアクティブなアイテムはない

                            if (sameItems.Any(s => s.State == QueueState.Complete))
                            {
                                // リネームしてる場合は、そのパスを使う
                                var dstpath = sameItems.FirstOrDefault(s => s.ActualDstPath != null)?.ActualDstPath ?? item.DstPath;

                                // 成功が1つでもあれば関連ファイルをコピー
                                if (item.Profile.MoveEDCBFiles)
                                {
                                    try
                                    {
                                        // ソースパスは拡張子を含むがdstは含まない
                                        var srcBody = Path.GetDirectoryName(item.SrcPath) + "\\" + Path.GetFileNameWithoutExtension(item.SrcPath);
                                        var dstBody = Path.GetDirectoryName(dstpath) + "\\" + Path.GetFileName(dstpath);
                                        foreach (var ext in ServerSupport
                                            .GetFileExtentions(null, item.Profile.MoveEDCBFiles))
                                        {
                                            var srcPath = srcBody + ext;
                                            var dstPath = dstBody + ext;
                                            if (File.Exists(srcPath) && !File.Exists(dstPath))
                                            {
                                                File.Copy(srcPath, dstPath);

                                                if (scriptExecuter != null)
                                                {
                                                    scriptExecuter.RelatedFiles.Add(dstPath);
                                                }
                                            }
                                        }
                                    }
                                    catch (Exception e)
                                    {
                                        Util.AddLog(Id, "関連ファイルコピーでエラー", e);

                                        // エンコードには成功しているので、ここでエラーが出てもこのまま進む
                                    }
                                }
                            }

                            // キャンセルの場合、削除されている可能性がある
                            // 自分がキャンセルされている場合は、移動しない
                            if (item.State != QueueState.Canceled && sameItems.All(s => s.State != QueueState.Canceled))
                            {
                                try
                                {
                                    // キャンセルが1つもない場合のみ
                                    if (sameItems.Any(s => s.State == QueueState.Failed))
                                    {
                                        // 失敗がある
                                        await MoveTSFileWithRetry(item.SrcPath, failedDir, item.Profile.MoveEDCBFiles);

                                        if (scriptExecuter != null)
                                        {
                                            scriptExecuter.MovedSrcPath = failedDir + "\\" + Path.GetFileName(item.SrcPath);
                                        }
                                    }
                                    else
                                    {
                                        // 全て成功
                                        await MoveTSFileWithRetry(item.SrcPath, succeededDir, item.Profile.MoveEDCBFiles);

                                        if (scriptExecuter != null)
                                        {
                                            scriptExecuter.MovedSrcPath = succeededDir + "\\" + Path.GetFileName(item.SrcPath);
                                        }
                                    }
                                }
                                catch (Exception e)
                                {
                                    Util.AddLog(Id, "TSファイル移動でエラー", e);

                                    // エンコードには成功しているので、ここでエラーが出てもこのまま進む
                                }
                            }
                        }
                    }
                    
                    // 実行後バッチ
                    if(scriptExecuter != null)
                    {
                        try
                        {
                            postScriptLog.Info("実行後バッチ起動: " + item.SrcPath);
                            process = scriptExecuter;
                            currentScriptLog = postScriptLog;
                            await scriptExecuter.Execute();
                        }
                        finally
                        {
                            process = null;
                            scriptExecuter.Dispose();
                            currentScriptLog = null;
                        }
                    }

                    if (item.IsCheck)
                    {
                        waitList.Add(server.AddCheckLog(logItem));
                    }
                    else
                    {
                        // 最終状態のタグをログに記録
                        logItem.Tags = item.Tags;
                        waitList.Add(server.AddEncodeLog(logItem));
                    }
                }

                waitList.Add(server.NotifyQueueItemUpdate(item));
                waitList.Add(server.RequestState(EventFromItem(item)));
                waitList.Add(server.RequestFreeSpace());

                await Task.WhenAll(waitList);

                return result;

            }
            catch (Exception e)
            {
                await server.FatalError(Id, "エンコード中にエラー", e);
                if(item != null)
                {
                    item.State = QueueState.Failed;
                    await server.NotifyQueueItemUpdate(item);
                    await server.RequestState(StateChangeEvent.EncodeFailed);
                }
                return false;
            }
            finally
            {
                item = null;
            }
        }
    }
}
