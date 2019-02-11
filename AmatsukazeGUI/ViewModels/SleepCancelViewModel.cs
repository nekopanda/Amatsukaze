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
using System.Threading.Tasks;
using Amatsukaze.Server;

namespace Amatsukaze.ViewModels
{
    public class SleepCancelViewModel : ViewModel
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

        public ISleepCancel Model;

        private bool Enabled;
        private Task CountDownThread;

        #region Elapsed変更通知プロパティ
        private double _Elapsed;

        public double Elapsed {
            get { return _Elapsed; }
            set {
                if (_Elapsed == value)
                    return;
                _Elapsed = value;
                RaisePropertyChanged();
                RaisePropertyChanged("RemainSeconds");
            }
        }
        #endregion

        #region WaitSeconds変更通知プロパティ
        private double _WaitSeconds;

        public double WaitSeconds {
            get { return _WaitSeconds; }
            set { 
                if (_WaitSeconds == value)
                    return;
                _WaitSeconds = value;
                RaisePropertyChanged();
                RaisePropertyChanged("RemainSeconds");
            }
        }
        #endregion

        public int RemainSeconds {
            get {
                return (int)(_WaitSeconds - _Elapsed);
            }
        }

        public string Action {
            get {
                switch(Model.SleepCancel.Action)
                {
                    case Server.FinishAction.Suspend:
                        return "スリープ";
                    case Server.FinishAction.Hibernate:
                        return "休止状態";
                }
                return "???";
            }
        }

        public string WindowTitle {
            get {
                return "Amatsukaze" + Action + "待機";
            }
        }

        public SleepCancelViewModel()
        { }

        public void Initialize()
        {
            Elapsed = 0;
            WaitSeconds = Model.SleepCancel.Seconds;
            Enabled = true;
            CountDownThread = CountDown();
        }

        public async Task CountDown()
        {
            var Start = DateTime.Now;
            for(; Elapsed < WaitSeconds; Elapsed = (DateTime.Now - Start).TotalSeconds)
            {
                await Task.Delay(250);
                if (!Enabled) return;
            }
            await Close();
        }

        public Task Close()
        {
            if(Enabled)
            {
                Enabled = false;
                CountDownThread = null;
                return Messenger.RaiseAsync(new WindowActionMessage(WindowAction.Close, "Close"));
            }
            return Task.FromResult(0);
        }
        
        #region CancelCommand
        private ViewModelCommand _CancelCommand;

        public ViewModelCommand CancelCommand {
            get {
                if (_CancelCommand == null)
                {
                    _CancelCommand = new ViewModelCommand(Cancel);
                }
                return _CancelCommand;
            }
        }

        public async void Cancel()
        {
            if (!Enabled) return;
            await Model.CancelSleep();
            await Close();
        }
        #endregion
    }
}
