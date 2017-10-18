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

        private Task currentTask;
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

            // DPIを反映
            Point scale = GetDpiScaleFactor(ViewVisual);
            Point pt = new Point(RectPosition.X * scale.X, RectPosition.Y * scale.Y);
            Size sz = new Size(RectSize.Width * scale.X, RectSize.Height * scale.Y);

            try
            {
                currentTask = Model.Analyze(App.Option.FilePath, App.Option.WorkPath, pt, sz, Threshold, MaxFrames);
                await currentTask;
            }
            catch(IOException exception)
            {
                MessageBox.Show(exception.Message);
            }
            finally
            {
                currentTask = null;
            }
        }
        #endregion

        public Visual ViewVisual;

        public LogoAnalyzeViewModel()
        {
            Model = new LogoAnalyzeModel();
        }

        private bool Prepare()
        {
            if (string.IsNullOrWhiteSpace(App.Option.WorkPath))
            {
                MessageBox.Show("テンポラリフォルダが有効なパスではありません");
                return false;
            }
            try
            {
                Directory.CreateDirectory(App.Option.WorkPath);
                Model.Load(App.Option.FilePath);
            }
            catch (IOException exception)
            {
                MessageBox.Show(exception.Message);
                return false;
            }
            return true;
        }

        public void Initialize()
        {
            if(Prepare() == false)
            {
                // アプリ終了
                Application.Current.Shutdown(1);
            }
        }

        public async void WindowClosed()
        {
            // ウィンドウが閉じられた

            if (currentTask != null)
            {
                // 実行中ならキャンセル
                Model.CancelScanning = true;

                // 3秒待つ
                await Task.Delay(3000);

                if (currentTask != null)
                {
                    // まだ実行中なら強制終了
                    Environment.Exit(1);
                }
            }
        }

        public static Point GetDpiScaleFactor(Visual visual)
        {
            var source = PresentationSource.FromVisual(visual);
            if (source != null && source.CompositionTarget != null)
            {
                return new Point(
                    source.CompositionTarget.TransformToDevice.M11,
                    source.CompositionTarget.TransformToDevice.M22);
            }

            return new Point(1.0, 1.0);
        }
    }
}
