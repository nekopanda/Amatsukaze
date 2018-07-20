using Amatsukaze.Models;
using Amatsukaze.Server;
using Livet.Commands;
using Livet.EventListeners;
using Livet.Messaging;
using Microsoft.Win32;
using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;

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

        public void Initialize() {
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
            string dst = Model.MakeScriptData.OutDir.TrimEnd(Path.DirectorySeparatorChar);
            string prof = DisplayProfile.GetProfileName(Model.MakeScriptData.SelectedProfile);
            string nas = null;
            string ip = "localhost";
            int port = Model.ServerPort;
            string subnet = null;
            string mac = null;
            bool direct = Model.MakeScriptData.IsDirect;

            if (prof == null)
            {
                Description = "プロファイルを選択してください";
                return;
            }

            if (string.IsNullOrEmpty(dst))
            {
                Description = "出力先が設定されていません";
                return;
            }
            if (Directory.Exists(dst) == false)
            {
                Description = "出力先ディレクトリにアクセスできません";
                return;
            }

            if (Model.MakeScriptData.IsNasEnabled)
            {
                if (string.IsNullOrEmpty(Model.MakeScriptData.NasDir))
                {
                    Description = "NAS保存先を指定してください。";
                    return;
                }
                nas = Model.MakeScriptData.NasDir.TrimEnd(Path.DirectorySeparatorChar); ;
            }

            if (IsRemoteClient)
            {
                ip = Model.ServerIP;
                if (Model.MakeScriptData.IsWakeOnLan)
                {
                    var localIP = Model.LocalIP;
                    if (localIP == null)
                    {
                        Description = "IPアドレス取得に失敗";
                        return;
                    }
                    if (localIP.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork)
                    {
                        Description = "IPv4以外の接続には対応していません";
                        return;
                    }
                    var subnetaddr = ServerSupport.GetSubnetMask(((IPEndPoint)localIP).Address);
                    if (subnetaddr == null)
                    {
                        Description = "サブネットマスク取得に失敗";
                        return;
                    }
                    subnet = subnetaddr.ToString();
                    var macbytes = Model.MacAddress;
                    if (macbytes == null)
                    {
                        Description = "MACアドレス取得に失敗";
                        return;
                    }
                    mac = string.Join(":", macbytes.Select(s => s.ToString("X")));
                }
            }

            Description = "";

            var sb = new StringBuilder();
            if(direct)
            {
                sb.Append("rem _EDCBX_DIRECT_\r\n");
            }
            sb.AppendFormat("\"{0}\\AmatsukazeAddTask.exe\"", exe)
                .AppendFormat(" -r \"{0}\"", cur)
                .AppendFormat(" -f \"{0}FilePath{0}\" -ip \"{1}\"", direct ? "%" : "$", ip)
                .AppendFormat(" -p {0}", port)
                .AppendFormat(" -o \"{0}\"", dst)
                .AppendFormat(" -s \"{0}\"", prof)
                .AppendFormat(" --priority {0}", Model.MakeScriptData.Priority);
            if (nas != null)
            {
                sb.AppendFormat(" -d \"{0}\"", nas);
            }
            if (mac != null)
            {
                sb.AppendFormat(" --subnet \"{0}\"", subnet)
                    .AppendFormat(" --mac \"{0}\"", mac);
            }
            if (Model.MakeScriptData.MoveAfter == false)
            {
                sb.Append(" --no-move");
            }
            if (Model.MakeScriptData.ClearSucceeded)
            {
                sb.Append(" --clear-succeeded");
            }
            if (Model.MakeScriptData.WithRelated)
            {
                sb.Append(" --with-related");
            }

            var saveFileDialog = new SaveFileDialog();
            saveFileDialog.FilterIndex = 1;
            saveFileDialog.Filter = "バッチファイル(.bat)|*.bat|All Files (*.*)|*.*";
            bool? result = saveFileDialog.ShowDialog();
            if (result != true)
            {
                return;
            }

            try
            {
                File.WriteAllText(saveFileDialog.FileName, sb.ToString(), Encoding.Default);
            }
            catch (Exception e)
            {
                Description = "バッチファイル作成に失敗: " + e.Message;
                return;
            }

            var resvm = new MakeBatchResultViewModel() { Path = saveFileDialog.FileName };

            await Messenger.RaiseAsync(new TransitionMessage(
                typeof(Views.MakeBatchResultWindow), resvm, TransitionMode.Modal, "Key"));

            await Model.SendMakeScriptData();
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
                    await Model.Server?.EndServer();
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
