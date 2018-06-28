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
using System.Collections.ObjectModel;
using System.Threading.Tasks;
using Amatsukaze.Components;

namespace Amatsukaze.ViewModels
{
    public class SummaryViewModel : NamedViewModel
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
        public MainWindowViewModel MainPanel { get; set; }

        public ObservableViewModelCollection<SummaryItemViewModel, DisplayConsole> ConsoleList { get; private set; }

        public void Initialize()
        {
            ConsoleList = new ObservableViewModelCollection<SummaryItemViewModel, DisplayConsole>(Model.ConsoleList, s =>
            {
                var vm = new SummaryItemViewModel() { Data = s };
                vm.Initialize();
                return vm;
            });

            UpdateThread();
        }

        private async void UpdateThread()
        {
            while(true)
            {
                await Task.Delay(1 * 1000);

                foreach(var item in ConsoleList)
                {
                    item.Update();
                }
            }
        }

        #region ShowItemDetailCommand
        private ListenerCommand<SummaryItemViewModel> _ShowItemDetailCommand;

        public ListenerCommand<SummaryItemViewModel> ShowItemDetailCommand {
            get {
                if (_ShowItemDetailCommand == null)
                {
                    _ShowItemDetailCommand = new ListenerCommand<SummaryItemViewModel>(ShowItemDetail);
                }
                return _ShowItemDetailCommand;
            }
        }

        public void ShowItemDetail(SummaryItemViewModel item)
        {
            if (item.Data.Id - 1 < MainPanel.ConsoleList.Count)
            {
                var vm = MainPanel.ConsoleList[item.Data.Id - 1];
                MainPanel.SelectedConsolePanel = vm;
                vm.AutoScroll = true;
            }
        }
        #endregion

    }
}
