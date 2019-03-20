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
using System.Threading.Tasks;
using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Media;
using Amatsukaze.Components;

namespace Amatsukaze.ViewModels
{
    public class MainWindowViewModel : ViewModel
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

        public ClientModel Model { get; private set; }

        private bool disposed = false;

        public ObservableCollection<ViewModel> MainPanelMenu { get; } = new ObservableCollection<ViewModel>();
        public ObservableViewModelCollection<ConsoleViewModel, DisplayConsole> ConsoleList { get; private set; }
        public ObservableCollection<ViewModel> ConsolePanelMenu { get; } = new ObservableCollection<ViewModel>();
        public ObservableCollection<ViewModel> InfoPanelMenu { get; } = new ObservableCollection<ViewModel>();

        private SleepCancelViewModel SleepCancelVM;

        #region SelectedMainPanel変更通知プロパティ
        private ViewModel _SelectedMainPanel;

        public ViewModel SelectedMainPanel {
            get { return _SelectedMainPanel; }
            set {
                var value_ = value ?? MainPanelMenu.FirstOrDefault();
                if (_SelectedMainPanel == value_)
                    return;
                _SelectedMainPanel = value_;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SelectedConsolePanel変更通知プロパティ
        private ViewModel _SelectedConsolePanel;

        public ViewModel SelectedConsolePanel {
            get { return _SelectedConsolePanel; }
            set {
                var value_ = value ?? ConsolePanelMenu.FirstOrDefault();
                if (_SelectedConsolePanel == value_)
                    return;
                _SelectedConsolePanel = value_;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region SelectedInfoPanel変更通知プロパティ
        private ViewModel _SelectedInfoPanel;

        public ViewModel SelectedInfoPanel {
            get { return _SelectedInfoPanel; }
            set {
                var value_ = value ?? InfoPanelMenu.FirstOrDefault();
                if (_SelectedInfoPanel == value_)
                    return;
                _SelectedInfoPanel = value_;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region WindowCaption変更通知プロパティ
        public string WindowCaption {
            get {
                var hostName = Model.ServerHostName;
                if (hostName == null)
                {
                    return "AmatsukazeGUI(未接続)";
                }
                if (Model.ServerVersion == null)
                {
                    return "AmatsukazeClient@" + hostName;
                }
                return "Amatsukaze" + Model.ServerVersion + "@" + hostName;
            }
        }
        #endregion

        #region StatucColor変更通知プロパティ
        public Brush StatusBackColor {
            get { return Model.IsCurrentResultFail ? Brushes.DarkRed : Brushes.White; }
        }
        public Brush StatusForeColor {
            get { return Model.IsCurrentResultFail ? Brushes.White : Brushes.Black; }
        }
        #endregion

        public bool IsManyConsole {
            get { return (ConsoleList?.Count ?? 0) > 8; }
        }

        public string RunningState { get { return Model.IsRunning ? "エンコード中" : "停止"; } }

        public QueueViewModel QueueVM { get; private set; }

        public MainWindowViewModel()
        {
            Model = new ClientModel();
            ConsoleList = new ObservableViewModelCollection<ConsoleViewModel, DisplayConsole>(
                Model.ConsoleList, console => new ConsoleViewModel()
                {
                    Name = "コンソール" + (console.Id),
                    ShortName = (console.Id).ToString(),
                    Model = console
                });
            QueueVM = new QueueViewModel() { Name = "キュー", Model = Model, MainPanel = this };
            MainPanelMenu.Add(QueueVM);
            MainPanelMenu.Add(new LogViewModel() { Name = "ログ", Model = Model });
            MainPanelMenu.Add(new ProfileSettingViewModel() { Name = "プロファイル", Model = Model });
            MainPanelMenu.Add(new AutoSelectSettingViewModel() { Name = "自動選択", Model = Model });
            MainPanelMenu.Add(new ServiceSettingViewModel() { Name = "チャンネル設定", Model = Model });
            MainPanelMenu.Add(new SettingViewModel() { Name = "基本設定", Model = Model });
            ConsolePanelMenu.Add(new LogFileViewModel() { Name = "ログファイル", Model = Model });
            InfoPanelMenu.Add(new SummaryViewModel() { Name = "サマリー", Model = Model, MainPanel = this });
            InfoPanelMenu.Add(new DrcsImageListViewModel() { Name = "DRCS外字", Model = Model });
            InfoPanelMenu.Add(new AddQueueConsoleViewModel() { Name = "追加コンソール", Model = Model });
            InfoPanelMenu.Add(new DiskFreeSpaceViewModel() { Name = "ディスク空き", Model = Model });
            InfoPanelMenu.Add(new MakeScriptViewModel() { Name = "その他", Model = Model });
            InfoPanelMenu.Add(new ClientLogViewModel() { Name = "クライアントログ", Model = Model });

            SleepCancelVM = new SleepCancelViewModel() { Model = Model };
        }

        private static void InitializeVM(dynamic vm)
        {
            vm.Initialize();
        }

        public async void Initialize()
        {
            var modelListener = new PropertyChangedEventListener(Model);
            modelListener.Add(() => Model.IsRunning, (_, __) => RaisePropertyChanged(() => RunningState));
            modelListener.Add(() => Model.ServerHostName, (_, __) => RaisePropertyChanged(() => WindowCaption));
            modelListener.Add(() => Model.ServerVersion, (_, __) => RaisePropertyChanged(() => WindowCaption));
            modelListener.Add(() => Model.IsCurrentResultFail, (_, __) =>
            {
                RaisePropertyChanged("StatusBackColor");
                RaisePropertyChanged("StatusForeColor");
            });
            modelListener.Add(() => Model.SleepCancel, (_, __) => ShowOrCloseSleepCancel());
            CompositeDisposable.Add(modelListener);

            // 他のVMのInitializeを読んでやる
            foreach (var vm in MainPanelMenu)
            {
                InitializeVM(vm);
            }
            foreach (var vm in ConsolePanelMenu)
            {
                InitializeVM(vm);
            }
            foreach (var vm in InfoPanelMenu)
            {
                InitializeVM(vm);
            }

            // 初期パネル表示
            SelectedMainPanel = null;
            SelectedConsolePanel = null;
            SelectedInfoPanel = null;

            Model.ServerAddressRequired = ServerAddressRequired;
            Model.FinishRequested = () =>
                Messenger.Raise(new WindowActionMessage(WindowAction.Close, "MainWindowAction"));

            try
            {
                await Model.Start();
            }
            catch(MultipleInstanceException)
            {
                var message = new InformationMessage(
                    "多重起動を検知しました。\r\n"+
                    "サーバが起動している場合はAmatsukazeClientで接続してみてください。",
                    "AmatsukazeServer",
                    "Message");

                await Messenger.RaiseAsync(message);

                Messenger.Raise(new WindowActionMessage(WindowAction.Close, "MainWindowAction"));
            }
            catch(Exception e)
            {
                var message = new InformationMessage(
                    "起動処理でエラーが発生しました\r\n" +
                    e.Message + "\r\n" + e.StackTrace,
                    "AmatsukazeServer",
                    "Message");

                await Messenger.RaiseAsync(message);

                Messenger.Raise(new WindowActionMessage(WindowAction.Close, "MainWindowAction"));
            }
        }

        protected override void Dispose(bool disposing)
        {
            base.Dispose(disposing);
            if (!disposed)
            {
                if (disposing)
                {
                    Model.Dispose();
                }
                disposed = true;
            }
        }

        private async Task ServerAddressRequired(object sender, string reason)
        {
            // サーバIP・ポートを入力させる
            var config = new ConfigWindowViewModel(Model, Model.ServerIP, Model.ServerPort);
            config.Description = reason;
            await Messenger.RaiseAsync(new TransitionMessage(
                typeof(Views.ConfigWindow), config, TransitionMode.Modal, "FromMain"));

            if (config.Succeeded == false)
            {
                // キャンセルされたら継続できないので終了
                Messenger.Raise(new WindowActionMessage(WindowAction.Close, "MainWindowAction"));
                return;
            }
        }

        private Task ShowOrCloseSleepCancel()
        {
            if(Model.SleepCancel == null ||
                Model.SleepCancel.Action == FinishAction.None)
            {
                return SleepCancelVM.Close();
            }
            else
            {
                return Messenger.RaiseAsync(new TransitionMessage(
                    typeof(Views.SleepCancelWindow), SleepCancelVM, TransitionMode.Modal, "FromMain"));
            }
        }

        #region RefreshCommand
        private ViewModelCommand _RefreshCommand;

        public ViewModelCommand RefreshCommand
        {
            get
            {
                if (_RefreshCommand == null)
                {
                    _RefreshCommand = new ViewModelCommand(Refresh);
                }
                return _RefreshCommand;
            }
        }

        public void Refresh()
        {
            Model.Server?.RefreshRequest();
        }
        #endregion

        #region ChangeServerCommand
        private ViewModelCommand _ChangeServerCommand;

        public ViewModelCommand ChangeServerCommand
        {
            get
            {
                if (_ChangeServerCommand == null)
                {
                    _ChangeServerCommand = new ViewModelCommand(ChangeServer);
                }
                return _ChangeServerCommand;
            }
        }

        public async void ChangeServer()
        {
            // サーバIP・ポートを入力させる
            var config = new ConfigWindowViewModel(Model, Model.ServerIP, Model.ServerPort);
            await Messenger.RaiseAsync(new TransitionMessage(
                typeof(Views.ConfigWindow), config, TransitionMode.Modal, "FromMain"));

            if (config.Succeeded)
            {
                Model.Reconnect();
            }
        }
        #endregion

    }
}
