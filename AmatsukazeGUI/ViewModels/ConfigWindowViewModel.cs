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
using Amatsukaze;

namespace Amatsukaze.ViewModels
{
    public class ConfigWindowViewModel : ViewModel
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

        private ClientModel model;

        public ConfigWindowViewModel()
        { }

        public ConfigWindowViewModel(ClientModel model, string serverIp, int port)
        {
            this.model = model;
            this.ServerIP = serverIp;
            this.ServerPort = port.ToString();
        }

        public void Initialize() { }

        public bool Succeeded { get; private set; }

        #region OkCommand
        private ViewModelCommand _OkCommand;

        public ViewModelCommand OkCommand
        {
            get
            {
                if (_OkCommand == null)
                {
                    _OkCommand = new ViewModelCommand(Ok);
                }
                return _OkCommand;
            }
        }

        public async void Ok()
        {
            int port;
            if(int.TryParse(_ServerPort, out port) == false) {
                Description = "ポート番号は数値でなければなりません";
                return;
            }
            model.SetServerAddress(_ServerIP, port);
            Succeeded = true;
            await Messenger.RaiseAsync(new WindowActionMessage(WindowAction.Close, "Close"));
        }
        #endregion

        #region ServerIP変更通知プロパティ
        private string _ServerIP;

        public string ServerIP
        {
            get
            { return _ServerIP; }
            set
            { 
                if (_ServerIP == value)
                    return;
                _ServerIP = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ServerPort変更通知プロパティ
        private string _ServerPort;

        public string ServerPort
        {
            get
            { return _ServerPort; }
            set
            { 
                if (_ServerPort == value)
                    return;
                _ServerPort = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Description変更通知プロパティ
        private string _Description;

        public string Description
        {
            get
            { return _Description; }
            set
            { 
                if (_Description == value)
                    return;
                _Description = value;
                RaisePropertyChanged();
            }
        }
        #endregion

    }
}
