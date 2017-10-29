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
using System.Windows;
using System.Windows.Media;
using System.Threading.Tasks;
using Microsoft.Win32;

namespace Amatsukaze.ViewModels
{
    public class LogoAnalyzeViewModel : ViewModel
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

        public LogoAnalyzeModel Model { private set; get; }

        #region RectVisible変更通知プロパティ
        private bool _RectVisible;

        public bool RectVisible {
            get { return _RectVisible; }
            set {
                if (_RectVisible == value)
                    return;
                _RectVisible = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region RectPosition変更通知プロパティ
        private Point _RectPosition;

        public Point RectPosition {
            get { return _RectPosition; }
            set { 
                if (_RectPosition == value)
                    return;
                _RectPosition = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region RectSize変更通知プロパティ
        private Size _RectSize;

        public Size RectSize {
            get { return _RectSize; }
            set { 
                if (_RectSize == value)
                    return;
                _RectSize = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ThresholdList変更通知プロパティ
        private int[] _ThresholdList = new int[] { 8, 10, 12, 15 };

        public int[] ThresholdList {
            get { return _ThresholdList; }
            set { 
                if (_ThresholdList == value)
                    return;
                _ThresholdList = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Threshold変更通知プロパティ
        private int _Threshold = 12;

        public int Threshold {
            get { return _Threshold; }
            set { 
                if (_Threshold == value)
                    return;
                _Threshold = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MaxFramesList変更通知プロパティ
        private int[] _MaxFramesList = new int[] { 3000, 5000, 10000, 20000, 50000 };

        public int[] MaxFramesList {
            get { return _MaxFramesList; }
            set { 
                if (_MaxFramesList == value)
                    return;
                _MaxFramesList = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region MaxFrames変更通知プロパティ
        private int _MaxFrames = 20000;

        public int MaxFrames {
            get { return _MaxFrames; }
            set { 
                if (_MaxFrames == value)
                    return;
                _MaxFrames = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region StartScanCommand
        private ViewModelCommand _StartScanCommand;

        public ViewModelCommand StartScanCommand {
            get {
                if (_StartScanCommand == null)
                {
                    _StartScanCommand = new ViewModelCommand(StartScan);
                }
                return _StartScanCommand;
            }
        }

        public async void StartScan()
        {
            if(Model.NowScanning)
            {
                Model.CancelScanning = true;
                return;
            }

            if(_RectSize.Height == 0 || _RectSize.Width == 0)
            {
                MessageBox.Show("ロゴ範囲を選択してください");
                return;
            }

            try
            {
                var srcpath = App.Option.SlimTs ? tmpTs : App.Option.FilePath;
                currentTask = Model.Analyze(srcpath, App.Option.WorkPath, RectPosition, RectSize, Threshold, MaxFrames);
                await currentTask;

                var vm = new LogoImageViewModel();
                vm.Model = Model;
                await Messenger.RaiseAsync(new TransitionMessage(vm, "ScanComplete"));
            }
            catch(IOException exception)
            {
                if (Model.CancelScanning == false)
                {
                    MessageBox.Show(exception.Message);
                }
            }
            finally
            {
                currentTask = null;
            }
        }
        #endregion

        #region CanClose変更通知プロパティ
        private bool _CanClose = false;

        public bool CanClose {
            get { return _CanClose; }
            set {
                if (_CanClose == value)
                    return;
                _CanClose = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        private Task currentTask;
        private string tmpTs;

        public LogoAnalyzeViewModel()
        {
            Model = new LogoAnalyzeModel();
        }

        private async Task<bool> Prepare()
        {
            if (string.IsNullOrWhiteSpace(App.Option.WorkPath))
            {
                MessageBox.Show("テンポラリフォルダが有効なパスではありません");
                return false;
            }
            try
            {
                Directory.CreateDirectory(App.Option.WorkPath);

                if(string.IsNullOrWhiteSpace(App.Option.FilePath))
                {
                    OpenFileDialog openFileDialog = new OpenFileDialog();
                    openFileDialog.FilterIndex = 1;
                    openFileDialog.Filter = "MPEG2-TS(.ts)|*.ts;*.m2ts|All Files (*.*)|*.*";
                    bool? result = openFileDialog.ShowDialog();
                    if (result != true)
                    {
                        return false;
                    }
                    App.Option.FilePath = openFileDialog.FileName;
                }

                if(App.Option.SlimTs)
                {
                    int pid = System.Diagnostics.Process.GetCurrentProcess().Id;
                    tmpTs = App.Option.WorkPath + "\\slimts-" + pid + ".ts";
                    currentTask = Model.MakeSlimFile(App.Option.FilePath, tmpTs);
                    await currentTask;
                    Model.Load(tmpTs);
                }
                else
                {
                    Model.Load(App.Option.FilePath);
                }
            }
            catch (IOException exception)
            {
                if (Model.CancelScanning)
                {
                    return true;
                }
                MessageBox.Show(exception.Message);
                return false;
            }
            finally
            {
                currentTask = null;
            }
            return true;
        }

        public async void Initialize()
        {
            if(await Prepare() == false)
            {
                // アプリ終了
                Messenger.Raise(new WindowActionMessage(WindowAction.Close, "WindowAction"));
            }
        }

        public async void CloseCanceledCallback()
        {
            if (currentTask != null)
            {
                // 実行中ならキャンセル
                Model.CancelScanning = true;

                // 3秒待つ
                for(int i = 0; i < 12 && currentTask != null; ++i)
                {
                    await Task.Delay(250);
                }

                if (currentTask != null)
                {
                    // まだ実行中なら強制終了
                    Environment.Exit(1);
                }
            }

            CanClose = true;

            Model.Close();
            if(string.IsNullOrEmpty(tmpTs) == false && File.Exists(tmpTs))
            {
                try
                {
                    File.Delete(tmpTs);
                }
                catch(Exception) { }
            }

            await DispatcherHelper.UIDispatcher.BeginInvoke((Action)(() => {
                Messenger.Raise(new WindowActionMessage(WindowAction.Close, "WindowAction"));
            }));
        }
    }
}
