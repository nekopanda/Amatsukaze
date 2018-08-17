using Amatsukaze.Lib;
using log4net;
using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    public static class UserScriptExecuter
    {
        private static readonly ILog LOG = LogManager.GetLogger(
            System.Reflection.MethodBase.GetCurrentMethod().DeclaringType);

        private enum Phase
        {
            OnAdd, PreEncode, PostEncode
        }

        private static async Task RedirectLog(StreamReader sr)
        {
            while (true)
            {
                var line = await sr.ReadLineAsync();
                if (line == null) break;
                LOG.Info(line);
            }
        }

        private static string GetOutFiles(QueueItem item, LogItem log, string fmt)
        {
            int MAIN = 1;        // v
            int MAIN_CM = 2;     // c
            int MAIN_SUBS = 4;   // s
            int OTHER = 8;       // w
            int OTHER_CM = 16;   // d
            int OTHER_SUBS = 32; // t
            int LOG = 64;        // l

            int mask = 255;

            if(string.IsNullOrEmpty(fmt) == false && fmt != "all") {
                mask = 0;
                foreach(char c in fmt)
                {
                    switch(c)
                    {
                        case 'v':
                            mask |= MAIN;
                            break;
                        case 'c':
                            mask |= MAIN_CM;
                            break;
                        case 's':
                            mask |= MAIN_SUBS;
                            break;
                        case 'w':
                            mask |= OTHER;
                            break;
                        case 'd':
                            mask |= OTHER_CM;
                            break;
                        case 't':
                            mask |= OTHER_SUBS;
                            break;
                        case 'l':
                            mask |= LOG;
                            break;
                    }
                }
            }

            var tailParser = new Regex("(-(\\d+))?(-cm)?(\\..+)");
            Func<string, int> fileType = tail =>
            {
                var m = tailParser.Match(tail);
                if (m.Success == false)
                {
                    throw new FormatException("出力ファイルのフォーマットがパースできません");
                }
                var numStr = m.Groups[2].Value;
                var cmStr = m.Groups[3].Value;
                var ext = m.Groups[4].Value;
                bool isSubs = (ext == ".ass") || (ext == ".srt");
                int number = (numStr.Length == 0) ? 0 : int.Parse(numStr);
                bool isCM = (cmStr.Length > 0);
                if (number > 0)
                {
                    if (isSubs)
                    {
                        return MAIN_SUBS;
                    }
                    else if (isCM)
                    {
                        return MAIN_CM;
                    }
                    else
                    {
                        return MAIN;
                    }
                }
                else
                {
                    if (isSubs)
                    {
                        return OTHER_SUBS;
                    }
                    else if (isCM)
                    {
                        return OTHER_CM;
                    }
                    else
                    {
                        return OTHER;
                    }
                }
            };

            try
            {
                var mainPath = log.OutPath[0];
                var mainNoExt = Path.GetFileNameWithoutExtension(mainPath);
                var list = log.OutPath.Where(path =>
                {
                    var type = fileType(Path.GetFileName(path).Substring(mainNoExt.Length));
                    return (mask & type) != 0;
                }).ToList();

                if ((mask & LOG) != 0)
                {
                    if (item.Profile.DisableLogFile == false)
                    {
                        var prefix = Path.GetDirectoryName(mainPath) + "\\" + mainNoExt;
                        list.Add(prefix + ".log");
                    }
                }

                return string.Join(";", list);
            }
            catch(FormatException e)
            {
                return e.Message;
            }
        }

        private static async Task RunCommandHost(Phase phase, PipeCommunicator pipes, QueueItem item, LogItem log)
        {
            try
            {
                // 子プロセスが終了するまでループ
                while (true)
                {
                    var rpc = await RPCTypes.Deserialize(pipes.ReadPipe);
                    string ret = "";
                    switch (rpc.id)
                    {
                        case RPCMethodId.AddTag:
                            var tag = (string)rpc.arg;
                            if (!string.IsNullOrEmpty(tag))
                            {
                                item.Tags.Add(tag);
                            }
                            ret = string.Join(";", item.Tags);
                            break;
                        case RPCMethodId.SetOutDir:
                            if(phase == Phase.PostEncode)
                            {
                                ret = "エンコードが完了したアイテムの出力先は変更できません";
                            }
                            else
                            {
                                var outdir = (rpc.arg as string).TrimEnd(Path.DirectorySeparatorChar);
                                item.DstPath = outdir + "\\" + Path.GetFileName(item.DstPath);
                                ret = "成功";
                            }
                            break;
                        case RPCMethodId.SetPriority:
                            if (phase != Phase.OnAdd)
                            {
                                ret = "追加時以外の優先度操作はできません";
                            }
                            else
                            {
                                var priority = int.Parse(rpc.arg as string);
                                if (priority < 1 && priority > 5)
                                {
                                    ret = "優先度が範囲外です";
                                }
                                else {
                                    item.Priority = priority;
                                    ret = "成功";
                                }
                            }
                            break;
                        case RPCMethodId.GetOutFiles:
                            ret = GetOutFiles(item, log, (string)rpc.arg);
                            break;
                        case RPCMethodId.CancelItem:
                            if(phase == Phase.PostEncode)
                            {
                                ret = "エンコードが完了したアイテムはキャンセルできません";
                            }
                            else
                            {
                                item.State = QueueState.Canceled;
                                ret = "成功";
                            }
                            break;
                    }
                    var bytes = RPCTypes.Serialize(rpc.id, ret);
                    await pipes.WritePipe.WriteAsync(bytes, 0, bytes.Length);
                }
            }
            catch (Exception)
            {
                // 子プロセスが終了すると例外を吐く
            }
        }

        private static void SetupEnv(EncodeServer server, Phase phase, StringDictionary env,
            PipeCommunicator pipes, QueueItem item, LogItem log)
        {
            env.Add("ITEM_ID", item.Id.ToString());
            env.Add("IN_PATH", item.SrcPath.ToString());
            env.Add("OUT_PATH", item.DstPath.ToString());
            env.Add("SERVICE_ID", item.ServiceId.ToString());
            env.Add("SERVICE_NAME", item.ServiceName.ToString());
            env.Add("TS_TIME", item.TsTime.ToString());
            env.Add("ITEM_MODE", item.Mode.ToString());
            env.Add("ITEM_PRIORITY", item.Priority.ToString());
            env.Add("EVENT_GENRE", item.Genre.FirstOrDefault()?.ToString() ?? "-");
            env.Add("IMAGE_WIDTH", item.ImageWidth.ToString());
            env.Add("IMAGE_HEIGHT", item.ImageHeight.ToString());
            env.Add("EVENT_NAME", item.EventName.ToString());
            env.Add("TAG", string.Join(";", item.Tags));

            if(phase != Phase.OnAdd)
            {
                env.Add("PROFILE_NAME", item.Profile.Name);
            }
            if(phase == Phase.PostEncode)
            {
                env.Add("SUCCESS", log.Success ? "1" : "0");
                env.Add("ERROR_MESSAGE", log.Reason ?? "");
                env.Add("IN_DURATION", log.SrcVideoDuration.TotalSeconds.ToString());
                env.Add("OUT_DURATION", log.OutVideoDuration.TotalSeconds.ToString());
                env.Add("IN_SIZE", log.SrcFileSize.ToString());
                env.Add("OUT_SIZE", log.OutFileSize.ToString());
                env.Add("LOGO_FILE", string.Join(";", log.LogoFiles));
                env.Add("NUM_INCIDENT", log.Incident.ToString());
                env.Add("JSON_PATH", server.GetLogFileBase(log.EncodeStartDate) + ".json");
                env.Add("LOG_PATH", server.GetLogFileBase(log.EncodeStartDate) + ".log");
            }

            // パイプ通信用
            env.Add("IN_PIPE_HANDLE", pipes.InHandle);
            env.Add("OUT_PIPE_HANDLE", pipes.OutHandle);
        }

        public static async Task ExecuteOnAdd(EncodeServer server, string scriptPath, QueueItem item, Program prog)
        {
            PipeCommunicator pipes = new PipeCommunicator();

            var psi = new ProcessStartInfo("cmd.exe", "/C " + scriptPath)
            {
                UseShellExecute = false,
                WorkingDirectory = Directory.GetCurrentDirectory(),
                RedirectStandardError = true,
                RedirectStandardOutput = true,
                RedirectStandardInput = false,
                CreateNoWindow = true
            };

            // exe_filesをパスに追加
            var exeDir = Path.GetDirectoryName(typeof(UserScriptExecuter).Assembly.Location);
            // Specialized.StringDictionaryのkeyはcase insensitiveであることに注意
            psi.EnvironmentVariables["path"] =
                exeDir + ";" + exeDir + "\\cmd" + ";" + psi.EnvironmentVariables["path"];

            // パラメータを環境変数に追加
            SetupEnv(server, Phase.OnAdd, psi.EnvironmentVariables, pipes, item, null);

            LOG.Info("追加時バッチ起動: " + item.SrcPath);

            using (var p = Process.Start(psi))
            {
                pipes.DisposeLocalCopyOfClientHandle();

                await Task.WhenAll(
                    RedirectLog(p.StandardOutput),
                    RedirectLog(p.StandardError),
                    RunCommandHost(Phase.OnAdd, pipes, item, null),
                    Task.Run(() => p.WaitForExit()));
            }
        }
    }
}
