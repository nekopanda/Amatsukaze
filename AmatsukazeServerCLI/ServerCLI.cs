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
                TaskSupport.SetSynchronizationContext();
                GUIOPtion option = new GUIOPtion(args);
                using (var lockFile = ServerSupport.GetLock())
                {
                    var logpath = ServerSupport.GetServerLogPath();
                    var file = new StreamWriter(new FileStream(logpath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite));
                    Util.LogHandlers.Add(text =>
                    {
                        var formatted = DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss") + " " + text;
                        file.WriteLine(formatted);
                        file.Flush();
                    });
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
