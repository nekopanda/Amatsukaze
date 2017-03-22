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

namespace Amatsukaze.ViewModels
{
    public class SettingViewModel : NamedViewModel
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

        private PropertyChangedEventListener listener;

        public void Initialize()
        {
            listener = new PropertyChangedEventListener(Model);

            listener.Add(() => Model.EncoderName, (_, __) =>
            {
                EncoderSelectedIndex = _EncoderList.IndexOf(Model.EncoderName);
            });

            listener.Add(() => Model.BitrateA, UpdateBitrate);
            listener.Add(() => Model.BitrateB, UpdateBitrate);
            listener.Add(() => Model.BitrateH264, UpdateBitrate);
        }

        #region SendSettingCommand
        private ViewModelCommand _SendSettingCommand;

        public ViewModelCommand SendSettingCommand
        {
            get
            {
                if (_SendSettingCommand == null)
                {
                    _SendSettingCommand = new ViewModelCommand(SendSetting);
                }
                return _SendSettingCommand;
            }
        }

        public void SendSetting()
        {
            Model.SendSetting();
        }
        #endregion

        #region EncoderList変更通知プロパティ
        private List<string> _EncoderList = new List<string>() {
                "x264", "x265", "QSVEnc"
            };

        public List<string> EncoderList
        {
            get
            { return _EncoderList; }
            set
            { 
                if (_EncoderList == value)
                    return;
                _EncoderList = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region EncoderSelectedIndex変更通知プロパティ
        private int _EncoderSelectedIndex;

        public int EncoderSelectedIndex
        {
            get
            { return _EncoderSelectedIndex; }
            set
            { 
                if (_EncoderSelectedIndex == value)
                    return;
                _EncoderSelectedIndex = value;

                if(_EncoderSelectedIndex >= 0 && _EncoderSelectedIndex < _EncoderList.Count) {
                    Model.EncoderName = _EncoderList[_EncoderSelectedIndex];
                }

                RaisePropertyChanged();
            }
        }
        #endregion

        private void UpdateBitrate(object sender, PropertyChangedEventArgs args)
        {
            RaisePropertyChanged("Bitrate18MPEG2");
            RaisePropertyChanged("Bitrate12MPEG2");
            RaisePropertyChanged("Bitrate7MPEG2");
            RaisePropertyChanged("Bitrate18H264");
            RaisePropertyChanged("Bitrate12H264");
            RaisePropertyChanged("Bitrate7H264");
        }

        private double CalcBitrate(double src, int encoder)
        {
            double baseBitRate = Model.BitrateA * src + Model.BitrateB;
            switch (encoder)
            {
                case 0:
                    return baseBitRate;
                case 1:
                    return baseBitRate * Model.BitrateH264;
            }
            return 0;
        }

        private string BitrateString(double src, int encoder)
        {
            return ((int)CalcBitrate(src, encoder)).ToString() + "Kbps";
        }

        public string Bitrate18MPEG2 { get { return BitrateString(18000, 0); } }
        public string Bitrate12MPEG2 { get { return BitrateString(12000, 0); } }
        public string Bitrate7MPEG2 { get { return BitrateString(7000, 0); } }
        public string Bitrate18H264 { get { return BitrateString(18000, 1); } }
        public string Bitrate12H264 { get { return BitrateString(12000, 1); } }
        public string Bitrate7H264 { get { return BitrateString(7000, 1); } }

    }
}
