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
                using (var lockFile = ServerSupport.GetLock(option.ServerPort))
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
                        TaskSupport.EnterMessageLoop();
                    }
                }
            }
            catch (Exception e)
            {
                Console.WriteLine(e.Message);
                return;
            }
        }
    }
}
