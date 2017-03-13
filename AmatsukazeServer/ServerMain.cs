using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace AmatsukazeServer
{
    public static class ServerMain
    {
        private static int mainThreadId;
        private static SingleThreadSynchronizationContext syncCtx;

        public static void Start(string[] args)
        {
            mainThreadId = Thread.CurrentThread.ManagedThreadId;
            syncCtx = new SingleThreadSynchronizationContext();
            SynchronizationContext.SetSynchronizationContext(syncCtx);

            EncodeServer server = new EncodeServer();

            // 終わったらRunOnCurrentThread()から抜けるようにしておく
            server.ServerTask.ContinueWith(t =>
            {
                FinishMain();
            });

            syncCtx.RunOnCurrentThread();

            Console.WriteLine("Finished");
        }

        public static void CheckThread()
        {
            if (Thread.CurrentThread.ManagedThreadId != mainThreadId)
            {
                Console.WriteLine(
                    "Error: ThreadId=" + Thread.CurrentThread.ManagedThreadId +
                    ", MainThreadId=" + mainThreadId);
            }
        }

        public static void FinishMain()
        {
            syncCtx.Complete();
        }

        private sealed class SingleThreadSynchronizationContext :
            SynchronizationContext
        {
            private readonly
                BlockingCollection<KeyValuePair<SendOrPostCallback, object>>
                m_queue =
                new BlockingCollection<KeyValuePair<SendOrPostCallback, object>>();

            public override void Post(SendOrPostCallback d, object state)
            {
                m_queue.Add(
                    new KeyValuePair<SendOrPostCallback, object>(d, state));
            }

            public void RunOnCurrentThread()
            {
                KeyValuePair<SendOrPostCallback, object> workItem;
                while (m_queue.TryTake(out workItem, Timeout.Infinite))
                    workItem.Key(workItem.Value);
            }

            public void Complete() { m_queue.CompleteAdding(); }
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
            TextLines.Clear():
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
