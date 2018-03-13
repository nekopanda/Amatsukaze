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

        private PropertyChangedEventListener modelListener;
        private CollectionChangedEventListener consoleListListener;

        private bool disposed = false;


        #region MainPanelMenu変更通知プロパティ
        private ObservableCollection<ViewModel> _MainPanelMenu = new ObservableCollection<ViewModel>();

        public ObservableCollection<ViewModel> MainPanelMenu {
            get { return _MainPanelMenu; }
            set {
                if (_MainPanelMenu == value)
                    return;
                _MainPanelMenu = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ConsolePanelMenu変更通知プロパティ
        private ObservableCollection<ViewModel> _ConsolePanelMenu = new ObservableCollection<ViewModel>();

        public ObservableCollection<ViewModel> ConsolePanelMenu {
            get { return _ConsolePanelMenu; }
            set {
                if (_ConsolePanelMenu == value)
                    return;
                _ConsolePanelMenu = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region InfoPanelMenu変更通知プロパティ
        private ObservableCollection<ViewModel> _InfoPanelMenu = new ObservableCollection<ViewModel>();

        public ObservableCollection<ViewModel> InfoPanelMenu {
            get { return _InfoPanelMenu; }
            set {
                if (_InfoPanelMenu == value)
                    return;
                _InfoPanelMenu = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MainPanelSelectedIndex変更通知プロパティ
        private int _MainPanelSelectedIndex;

        public int MainPanelSelectedIndex {
            get { return _MainPanelSelectedIndex; }
            set {
                if (_MainPanelSelectedIndex == value)
                    return;
                _MainPanelSelectedIndex = value;
                RaisePropertyChanged();
                RaisePropertyChanged("MainPanel");
            }
        }

        public ViewModel MainPanel {
            get {
                if (_MainPanelSelectedIndex >= 0 && _MainPanelSelectedIndex < _MainPanelMenu.Count)
                {
                    return _MainPanelMenu[_MainPanelSelectedIndex];
                }
                return null;
            }
        }
        #endregion

        #region ConsolePanelSelectedIndex変更通知プロパティ
        private int _ConsolePanelSelectedIndex;

        public int ConsolePanelSelectedIndex {
            get { return _ConsolePanelSelectedIndex; }
            set {
                if (_ConsolePanelSelectedIndex == value)
                    return;
                _ConsolePanelSelectedIndex = value;
                RaisePropertyChanged();
                RaisePropertyChanged("ConsolePanel");
            }
        }

        public ViewModel ConsolePanel {
            get {
                if (ConsolePanelSelectedIndex >= 0 && ConsolePanelSelectedIndex < _ConsolePanelMenu.Count)
                {
                    return _ConsolePanelMenu[ConsolePanelSelectedIndex];
                }
                return null;
            }
        }
        #endregion

        #region InfoPanelSelectedIndex変更通知プロパティ
        private int _InfoPanelSelectedIndex;

        public int InfoPanelSelectedIndex {
            get { return _InfoPanelSelectedIndex; }
            set {
                if (_InfoPanelSelectedIndex == value)
                    return;
                _InfoPanelSelectedIndex = value;
                RaisePropertyChanged();
                RaisePropertyChanged("InfoPanel");
            }
        }

        public ViewModel InfoPanel {
            get {
                if (InfoPanelSelectedIndex >= 0 && InfoPanelSelectedIndex < _InfoPanelMenu.Count)
                {
                    return _InfoPanelMenu[InfoPanelSelectedIndex];
                }
                return null;
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
                return "AmatsukazeClient@" + hostName;
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

        public string RunningState { get { return Model.IsRunning ? "エンコード中" : "停止"; } }

        public MainWindowViewModel()
        {
            Model = new ClientModel();
            MainPanelMenu.Add(new QueueViewModel() { Name = "キュー", Model = Model });
            MainPanelMenu.Add(new LogViewModel() { Name = "ログ", Model = Model });
            MainPanelMenu.Add(new ProfileSettingViewModel() { Name = "エンコード設定", Model = Model });
            MainPanelMenu.Add(new ServiceSettingViewModel() { Name = "チャンネル設定", Model = Model });
            MainPanelMenu.Add(new SettingViewModel() { Name = "基本設定", Model = Model });
            ConsolePanelMenu.Add(new LogFileViewModel() { Name = "ログファイル", Model = Model });
            InfoPanelMenu.Add(new DrcsImageListViewModel() { Name = "DRCS外字", Model = Model });
            InfoPanelMenu.Add(new DiskFreeSpaceViewModel() { Name = "ディスク空き", Model = Model });
            InfoPanelMenu.Add(new SummaryViewModel() { Name = "サマリー", Model = Model });
            InfoPanelMenu.Add(new MakeScriptViewModel() { Name = "その他", Model = Model });
            InfoPanelMenu.Add(new ClientLogViewModel() { Name = "クライアントログ", Model = Model });
        }

        private static void InitializeVM(dynamic vm)
        {
            vm.Initialize();
        }

        public async void Initialize()
        {
            modelListener = new PropertyChangedEventListener(Model);
            modelListener.Add(() => Model.IsRunning, (_, __) => RaisePropertyChanged(() => RunningState));
            modelListener.Add(() => Model.ServerHostName, (_, __) => RaisePropertyChanged(() => WindowCaption));
            modelListener.Add(() => Model.IsCurrentResultFail, (_, __) =>
            {
                RaisePropertyChanged("StatusBackColor");
                RaisePropertyChanged("StatusForeColor");
            });

            consoleListListener = new CollectionChangedEventListener(Model.ConsoleList, (_, __) => UpdateNumConsole());

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

            Model.ServerAddressRequired = ServerAddressRequired;

            try
            {
                Model.Start();
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

        private void UpdateNumConsole()
        {
            while (ConsolePanelMenu.Count < Model.ConsoleList.Count + 1)
            {
                int index = ConsolePanelMenu.Count - 1;
                ConsolePanelMenu.Insert(index,
                    new ConsoleViewModel() { Name = "コンソール" + (index + 1), Model = Model.ConsoleList[index] });
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

        #region TogglePauseCommand
        private ViewModelCommand _TogglePauseCommand;

        public ViewModelCommand TogglePauseCommand
        {
            get
            {
                if (_TogglePauseCommand == null)
                {
                    _TogglePauseCommand = new ViewModelCommand(TogglePause);
                }
                return _TogglePauseCommand;
            }
        }

        public void TogglePause()
        {
            Model.Server.PauseEncode(!Model.IsPaused);
        }
        #endregion

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
            Model.Server.RefreshRequest();
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
