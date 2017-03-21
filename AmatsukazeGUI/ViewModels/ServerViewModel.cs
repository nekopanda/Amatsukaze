using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.ComponentModel;

using Livet;
using Livet.Commands;
using Livet.Messaging;
using Livet.Messaging.IO;
using Livet.EventListeners;
using Livet.Messaging.Windows;

using Amatsukaze.Models;
using Amatsukaze.Server;
using System.Collections.ObjectModel;
using System.IO;
using System.Diagnostics;
using System.Reflection;
using System.Net;

namespace Amatsukaze.ViewModels
{
    public class ServerViewModel : ViewModel
    {
        /* コマンド、プロパティの定義にはそれぞれ 
         * 
         *  lvcom   : ViewModelCommand
         *  lvcomn  : ViewModelCommand(CanExecute無)
         *  llcom   : ListenerCommand(パラメータ有のコマンド)
         *  llcomn  : ListenerCommand(パラメータ有のコマンド・CanExecute無)
         *  lprop   : 変更通知プロパティ(.NET4.5ではlpropn)
         *  
         * を使用してください。
         * 
         * Modelが十分にリッチであるならコマンドにこだわる必要はありません。
         * View側のコードビハインドを使用しないMVVMパターンの実装を行う場合でも、ViewModelにメソッドを定義し、
         * LivetCallMethodActionなどから直接メソッドを呼び出してください。
         * 
         * ViewModelのコマンドを呼び出せるLivetのすべてのビヘイビア・トリガー・アクションは
         * 同様に直接ViewModelのメソッドを呼び出し可能です。
         */

        /* ViewModelからViewを操作したい場合は、View側のコードビハインド無で処理を行いたい場合は
         * Messengerプロパティからメッセージ(各種InteractionMessage)を発信する事を検討してください。
         */

        /* Modelからの変更通知などの各種イベントを受け取る場合は、PropertyChangedEventListenerや
         * CollectionChangedEventListenerを使うと便利です。各種ListenerはViewModelに定義されている
         * CompositeDisposableプロパティ(LivetCompositeDisposable型)に格納しておく事でイベント解放を容易に行えます。
         * 
         * ReactiveExtensionsなどを併用する場合は、ReactiveExtensionsのCompositeDisposableを
         * ViewModelのCompositeDisposableプロパティに格納しておくのを推奨します。
         * 
         * LivetのWindowテンプレートではViewのウィンドウが閉じる際にDataContextDisposeActionが動作するようになっており、
         * ViewModelのDisposeが呼ばれCompositeDisposableプロパティに格納されたすべてのIDisposable型のインスタンスが解放されます。
         * 
         * ViewModelを使いまわしたい時などは、ViewからDataContextDisposeActionを取り除くか、発動のタイミングをずらす事で対応可能です。
         */

        /* UIDispatcherを操作する場合は、DispatcherHelperのメソッドを操作してください。
         * UIDispatcher自体はApp.xaml.csでインスタンスを確保してあります。
         * 
         * LivetのViewModelではプロパティ変更通知(RaisePropertyChanged)やDispatcherCollectionを使ったコレクション変更通知は
         * 自動的にUIDispatcher上での通知に変換されます。変更通知に際してUIDispatcherを操作する必要はありません。
         */
        public EncodeServer Server { get; set; }

        private StreamWriter file;

        public void Initialize()
        {
            var logpath = GetServerLogPath();
            Directory.CreateDirectory(Path.GetDirectoryName(logpath));
            file = new StreamWriter(new FileStream(logpath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite));
            Util.LogHandlers.Add(AddLog);
            Server = new EncodeServer(App.Option.ServerPort, null);
            RaisePropertyChanged("Server");
            WindowCaption = "AmatsukazeServer@" + Dns.GetHostName() + ":" + App.Option.ServerPort;

            if (App.Option.LaunchType == LaunchType.Debug)
            {
                Process.Start(new ProcessStartInfo()
                {
                    FileName = Assembly.GetEntryAssembly().Location,
                    Arguments = "--launch client",
                });
            }
        }

        private string GetServerLogPath()
        {
            return "data\\Server.log";
        }

        private void AddLog(string text)
        {
            var formatted = DateTime.Now.ToString("yyyy/MM/dd HH:mm:ss") + " " + text;

            _Log.Add(formatted);

            if (_Log.Count > 100)
            {
                var tmp = _Log.Skip(50).ToArray();
                _Log.Clear();
                foreach (var item in tmp)
                {
                    _Log.Add(item);
                }
            }

            file.WriteLine(formatted);
            file.Flush();
        }

        #region Log変更通知プロパティ
        private ObservableCollection<string> _Log = new ObservableCollection<string>();

        public ObservableCollection<string> Log {
            get { return _Log; }
            set { 
                if (_Log == value)
                    return;
                _Log = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region CanCloseWindow変更通知プロパティ
        private bool _CanCloseWindow;

        public bool CanCloseWindow {
            get { return _CanCloseWindow; }
            set { 
                if (_CanCloseWindow == value)
                    return;
                _CanCloseWindow = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        public async void CloseCanceledCallback()
        {
            var message = new ConfirmationMessage(
                "AmatsukazeServerを終了しますか？",
                "AmatsukazeServer",
                System.Windows.MessageBoxImage.Information,
                System.Windows.MessageBoxButton.OKCancel,
                "Confirm");

            await Messenger.RaiseAsync(message);

            if (message.Response == true)
            {
                CanCloseWindow = true;
                await DispatcherHelper.UIDispatcher.BeginInvoke((Action)(() => {
                    Messenger.Raise(new WindowActionMessage(WindowAction.Close, "WindowAction"));
                }));
            }
        }

        #region WindowCaption変更通知プロパティ
        private string _WindowCaption;

        public string WindowCaption
        {
            get
            { return _WindowCaption; }
            set
            { 
                if (_WindowCaption == value)
                    return;
                _WindowCaption = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region AutoScroll変更通知プロパティ
        private bool _AutoScroll = true;

        public bool AutoScroll
        {
            get { return _AutoScroll; }
            set
            {
                if (_AutoScroll == value)
                    return;
                _AutoScroll = value;
                RaisePropertyChanged();
            }
        }
        #endregion

    }
}
