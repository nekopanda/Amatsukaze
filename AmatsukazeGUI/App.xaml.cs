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
using System.Runtime.InteropServices;

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

            Amatsukaze.App app = new Amatsukaze.App();

            if (Option.LaunchType == LaunchType.Server || Option.LaunchType == LaunchType.Debug)
            {
                app.StartupUri = new Uri("Views\\ServerWindow.xaml", UriKind.Relative);
            }
            else
            {
                app.StartupUri = new Uri("Views\\MainWindow.xaml", UriKind.Relative);
            }

            app.InitializeComponent();
            app.Run();
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

        public static void SetClipboardText(string str)
        {
            // RealVNCクライアント使ってると失敗する？ので
            try
            {
                Clipboard.SetText(str);
            }
            catch { }
        }
    }
}
