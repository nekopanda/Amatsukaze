using Amatsukaze.Lib;
using log4net;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Server
{
    public static class UserScriptExecuter
    {
        private static readonly ILog LOG = LogManager.GetLogger(
            System.Reflection.MethodBase.GetCurrentMethod().DeclaringType);

        private static async Task RedirectLog(StreamReader sr)
        {
            while (true)
            {
                var line = await sr.ReadLineAsync();
                if (line == null) break;
                LOG.Info(line);
            }
        }

        private static async Task RunCommandHost(QueueItem item, PipeCommunicator pipes)
        {
            try
            {
                // 子プロセスが終了するまでループ
                while (true)
                {
                    var rpc = await RPCTypes.Deserialize(pipes.ReadPipe);
                    switch (rpc.id)
                    {
                        case RPCMethodId.AddTag:
                            var tag = (string)rpc.arg;
                            if (!string.IsNullOrEmpty(tag))
                            {
                                item.Tags.Add(tag);
                            }
                            var bytes = RPCTypes.Serialize(rpc.id, string.Join(";", item.Tags));
                            await pipes.WritePipe.WriteAsync(bytes, 0, bytes.Length);
                            break;
                        case RPCMethodId.OutInfo:
                            // TODO:
                            break;
                    }
                }
            }
            catch (Exception)
            {
                // 子プロセスが終了すると例外を吐く
            }
        }

        public static async Task ExecuteOnAdd(string scriptPath, QueueItem item, Program prog)
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
            // Specialized.StringDictionaryのkeyはcase insensitiveであることに注意
            psi.EnvironmentVariables["path"] = 
                Path.GetDirectoryName(typeof(UserScriptExecuter).Assembly.Location)
                + ";" + psi.EnvironmentVariables["path"];

            // パラメータを環境変数に追加
            psi.EnvironmentVariables.Add("ITEM_ID", item.Id.ToString());
            psi.EnvironmentVariables.Add("IN_PATH", item.SrcPath.ToString());
            psi.EnvironmentVariables.Add("OUT_PATH", item.DstPath.ToString());
            psi.EnvironmentVariables.Add("SERVICE_ID", item.ServiceId.ToString());
            psi.EnvironmentVariables.Add("SERVICE_NAME", item.ServiceName.ToString());
            psi.EnvironmentVariables.Add("TS_TIME", item.TsTime.ToString());
            psi.EnvironmentVariables.Add("ITEM_MODE", item.Mode.ToString());
            psi.EnvironmentVariables.Add("ITEM_PRIORITY", item.Priority.ToString());
            psi.EnvironmentVariables.Add("EVENT_GENRE", item.Genre.FirstOrDefault()?.ToString() ?? "-");
            psi.EnvironmentVariables.Add("IMAGE_WIDTH", prog.Width.ToString());
            psi.EnvironmentVariables.Add("IMAGE_HEIGHT", prog.Height.ToString());
            psi.EnvironmentVariables.Add("EVENT_NAME", prog.EventName.ToString());

            // パイプ通信用
            psi.EnvironmentVariables.Add("IN_PIPE_HANDLE", pipes.InHandle);
            psi.EnvironmentVariables.Add("OUT_PIPE_HANDLE", pipes.OutHandle);

            LOG.Info("追加時バッチ起動: " + item.SrcPath);

            using (var p = Process.Start(psi))
            {
                pipes.DisposeLocalCopyOfClientHandle();

                await Task.WhenAll(
                    RedirectLog(p.StandardOutput),
                    RedirectLog(p.StandardError),
                    RunCommandHost(item, pipes),
                    Task.Run(() => p.WaitForExit()));
            }
        }
    }
}
