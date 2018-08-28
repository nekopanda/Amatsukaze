using System;
using System.IO;

namespace Amatsukaze.Server
{
    class ServerCLI
    {
        static void Main(string[] args)
        {
            try
            {
                // ログパスを設定
                log4net.GlobalContext.Properties["Root"] = Directory.GetCurrentDirectory();
                log4net.Config.XmlConfigurator.Configure(new FileInfo(
                    Path.GetDirectoryName(typeof(ServerCLI).Assembly.Location) + "\\Log4net.Config.xml"));

                TaskSupport.SetSynchronizationContext();
                GUIOPtion option = new GUIOPtion(args);
                using (var lockFile = ServerSupport.GetLock())
                {
                    log4net.ILog LOG = log4net.LogManager.GetLogger("Server");
                    Util.LogHandlers.Add(text => LOG.Info(text));
                    using (var server = new EncodeServer(option.ServerPort, null, () =>
                     {
                         TaskSupport.Finish();
                     }))
                    {
                        var task = server.Init();

                        // この時点でtaskが完了していなくてもEnterMessageLoop()で続きが処理される

                        TaskSupport.EnterMessageLoop();

                        // この時点では"継続"を処理する人がいないので、
                        // task.Wait()はデッドロックするので呼べないことに注意
                        // あとはプログラムが終了するだけなのでWait()しても意味がない
                    }
                }
            }
            catch(MultipleInstanceException)
            {
                Console.WriteLine("多重起動を検知しました");
                return;
            }
            catch (Exception e)
            {
                Console.WriteLine(e.Message);
                return;
            }
        }
    }
}
