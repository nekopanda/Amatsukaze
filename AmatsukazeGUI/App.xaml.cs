using System;
using System.Collections.Generic;
using System.Configuration;
using System.Data;
using System.Linq;
using System.Windows;

using Livet;
using System.Threading;
using System.Collections.Concurrent;
using Amatsukaze.Server;

namespace Amatsukaze
{
    /// <summary>
    /// App.xaml の相互作用ロジック
    /// </summary>
    public partial class App : Application
    {
        public static GUIOPtion Option;

        /// <summary>
        /// Application Entry Point.
        /// </summary>
        [System.STAThreadAttribute()]
        public static void Main(string[] args)
        {
            Option = new GUIOPtion(args);
            if (Option.LaunchType == LaunchType.Server)
            {
                var syncCtx = new SingleThreadSynchronizationContext();
                SynchronizationContext.SetSynchronizationContext(syncCtx);
                EncodeServer server = new EncodeServer(Option.ServerPort, null);

                // 終わったらRunOnCurrentThread()から抜けるようにしておく
                server.ServerTask.ContinueWith(t =>
                {
                    syncCtx.Complete();
                });
                syncCtx.RunOnCurrentThread();
            }
            else
            {
                Amatsukaze.App app = new Amatsukaze.App();
                app.InitializeComponent();
                app.Run();
            }
        }

        private void Application_Startup(object sender, StartupEventArgs e)
        {
            DispatcherHelper.UIDispatcher = Dispatcher;
            //AppDomain.CurrentDomain.UnhandledException += new UnhandledExceptionEventHandler(CurrentDomain_UnhandledException);
        }

        //集約エラーハンドラ
        //private void CurrentDomain_UnhandledException(object sender, UnhandledExceptionEventArgs e)
        //{
        //    //TODO:ロギング処理など
        //    MessageBox.Show(
        //        "不明なエラーが発生しました。アプリケーションを終了します。",
        //        "エラー",
        //        MessageBoxButton.OK,
        //        MessageBoxImage.Error);
        //
        //    Environment.Exit(1);
        //}
    }

    public sealed class SingleThreadSynchronizationContext :
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
