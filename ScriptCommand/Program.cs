using Amatsukaze.Server;
using Microsoft.Win32.SafeHandles;
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Command
{
    class ScriptCommand
    {
        static void DoCommand(RPCMethodId id, string[] args)
        {
            var inPipe = new AnonymousPipeClientStream(PipeDirection.In,
                Environment.GetEnvironmentVariable("IN_PIPE_HANDLE"));
            var outPipe = new AnonymousPipeClientStream(PipeDirection.Out,
                Environment.GetEnvironmentVariable("OUT_PIPE_HANDLE"));

            var tag = (args.Length >= 1) ? args[0] : "";
            var bytes = RPCTypes.Serialize(id, tag);
            outPipe.Write(bytes, 0, bytes.Length);
            var ret = RPCTypes.Deserialize(inPipe).Result;
            Console.WriteLine((string)ret.arg);
        }

        static void Main(string[] args)
        {
            // 自分のexe名がコマンドになる
            var exeName = Path.GetFileNameWithoutExtension(Environment.GetCommandLineArgs()[0]);
            if(exeName == "AddTag")
            {
                DoCommand(RPCMethodId.AddTag, args);
            }
            else if (exeName == "SetOutDir")
            {
                DoCommand(RPCMethodId.SetOutDir, args);
            }
            else if (exeName == "SetPriority")
            {
                DoCommand(RPCMethodId.SetPriority, args);
            }
            else if(exeName == "GetOutFiles")
            {
                DoCommand(RPCMethodId.GetOutFiles, args);
            }
            else
            {
                Console.WriteLine("不明なコマンドです");
            }
        }
    }
}
