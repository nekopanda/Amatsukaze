using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Amatsukaze
{
    public enum LaunchType {
        Standalone,
        Server,
        Client
    };

    public class GUIOPtion
    {
        public LaunchType LaunchType = LaunchType.Standalone;
        public int ServerPort = 32768;

        public GUIOPtion(string[] args)
        {
            for (int i = 1; i < args.Length; ++i)
            {
                string arg = args[i];
                if (arg == "-p" || arg == "--port")
                {
                    ServerPort = int.Parse(args[i + 1]);
                    ++i;
                }
                else if(arg == "-l" || arg == "--launch")
                {
                    string opt = args[i + 1];
                    if (opt == "standalone")
                    {
                        LaunchType = LaunchType.Standalone;
                    }
                    else if (opt == "server")
                    {
                        LaunchType = LaunchType.Server;
                    }
                    else
                    {
                        LaunchType = LaunchType.Client;
                    }
                }
            }
        }
    }

    public class Debug
    {
        [Conditional("DEBUG")]
        public static void Print(string str)
        {
            Console.WriteLine(str);
        }
    }

    public class ConsoleText
    {
        public IList<string> TextLines { get; private set; }
        private int maxlines;

        private List<byte> rawtext = new List<byte>();
        private bool isCR = false;

        public ConsoleText(IList<string> textlines, int maxlines)
        {
            this.TextLines = textlines;
            this.maxlines = maxlines;
        }

        public void Clear()
        {
            TextLines.Clear();
            rawtext.Clear();
            isCR = false;
        }

        public void AddBytes(byte[] buf, int offset, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                if (buf[i] == '\n' || buf[i] == '\r')
                {
                    if (rawtext.Count > 0)
                    {
                        string text = Encoding.UTF8.GetString(rawtext.ToArray());
                        if (isCR)
                        {
                            TextLines[TextLines.Count - 1] = text;
                        }
                        else
                        {
                            if (TextLines.Count > maxlines)
                            {
                                TextLines.RemoveAt(0);
                            }
                            TextLines.Add(text);
                        }
                        rawtext.Clear();
                    }
                    isCR = (buf[i] == '\r');
                }
                else
                {
                    rawtext.Add(buf[i]);
                }
            }
        }
    }
}
