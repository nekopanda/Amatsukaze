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
using System.IO;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using Amatsukaze.Server;
using Microsoft.Win32;
using System.Windows;

namespace Amatsukaze.ViewModels
{
    public class MakeScriptViewModel : NamedViewModel
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
        public ClientModel Model { get; set; }

        public void Initialize()
        {
        }

        public bool IsRemoteClient {
            get {
                return App.Option.LaunchType == Server.LaunchType.Client;
            }
        }

        #region Description変更通知プロパティ
        private string _Description;

        public string Description {
            get { return _Description; }
            set { 
                if (_Description == value)
                    return;
                _Description = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region NasDir変更通知プロパティ
        private string _NasDir;

        public string NasDir {
            get { return _NasDir; }
            set { 
                if (_NasDir == value)
                    return;
                _NasDir = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsNasEnabled変更通知プロパティ
        private bool _IsNasEnabled;

        public bool IsNasEnabled {
            get { return _IsNasEnabled; }
            set { 
                if (_IsNasEnabled == value)
                    return;
                _IsNasEnabled = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsWakeOnLan変更通知プロパティ
        private bool _IsWakeOnLan;

        public bool IsWakeOnLan {
            get { return _IsWakeOnLan; }
            set { 
                if (_IsWakeOnLan == value)
                    return;
                _IsWakeOnLan = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MakeBatchFileCommand
        private ViewModelCommand _MakeBatchFileCommand;

        public ViewModelCommand MakeBatchFileCommand {
            get {
                if (_MakeBatchFileCommand == null)
                {
                    _MakeBatchFileCommand = new ViewModelCommand(MakeBatchFile);
                }
                return _MakeBatchFileCommand;
            }
        }

        public async void MakeBatchFile()
        {
            string cur = Directory.GetCurrentDirectory();
            string exe = Path.GetDirectoryName(GetType().Assembly.Location);
            string nas = null;
            string ip = "localhost";
            int port = Model.ServerPort;
            string subnet = null;
            string mac = null;

            if (IsRemoteClient)
            {
                ip = Model.ServerIP;
                if (IsNasEnabled)
                {
                    if(string.IsNullOrEmpty(_NasDir))
                    {
                        Description = "NAS保存先を指定してください。";
                        return;
                    }
                    nas = _NasDir;
                }
                if(IsWakeOnLan)
                {
                    var localIP = Model.LocalIP;
                    if(localIP == null)
                    {
                        Description = "IPアドレス取得に失敗";
                        return;
                    }
                    if(localIP.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork)
                    {
                        Description = "IPv4以外の接続には対応していません";
                        return;
                    }
                    var subnetaddr = ServerSupport.GetSubnetMask(((IPEndPoint)localIP).Address);
                    if(subnetaddr == null)
                    {
                        Description = "サブネットマスク取得に失敗";
                        return;
                    }
                    subnet = subnetaddr.ToString();
                    var macbytes = Model.MacAddress;
                    if(macbytes == null)
                    {
                        Description = "MACアドレス取得に失敗";
                        return;
                    }
                    mac = string.Join(":", macbytes);
                }
            }

            Description = "";

            var sb = new StringBuilder();
            sb.Append("cd \"")
                .Append(cur)
                .Append("\"\n")
                .Append(" -f \"$FilePath$\" -ip \"")
                .Append(ip)
                .Append("\" -p ")
                .Append(port);
            if(nas != null)
            {
                sb.Append(" -d \"")
                    .Append(nas)
                    .Append("\"");
            }
            if(mac != null)
            {
                sb.Append(" --subnet \"")
                    .Append(subnet)
                    .Append("\" --mac \"")
                    .Append(mac)
                    .Append("\"");
            }

            var saveFileDialog = new SaveFileDialog();
            saveFileDialog.FilterIndex = 1;
            saveFileDialog.Filter = "バッチファイル(.bat)|*.bat|All Files (*.*)|*.*";
            bool? result = saveFileDialog.ShowDialog();
            if (result == true)
            {
                try
                {
                    File.WriteAllText(saveFileDialog.SafeFileName, sb.ToString());
                }
                catch(Exception e)
                {
                    Description = "バッチファイル作成に失敗: " + e.Message;
                    return;
                }
            }

            var resvm = new MakeBatchResultViewModel() { Path = saveFileDialog.SafeFileName };

            await Messenger.RaiseAsync(new TransitionMessage(
                typeof(Views.MakeBatchResultWindow), resvm, TransitionMode.Modal, "Key"));
        }
        #endregion

        #region StopServerCommand
        private ViewModelCommand _StopServerCommand;

        public ViewModelCommand StopServerCommand {
            get {
                if (_StopServerCommand == null)
                {
                    _StopServerCommand = new ViewModelCommand(StopServer);
                }
                return _StopServerCommand;
            }
        }

        public async void StopServer()
        {
            if (ServerSupport.IsLocalIP(Model.ServerIP))
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
                    await Model.Server.EndServer();
                }
            }
            else
            {
                //
            }
        }
        #endregion

    }
}
