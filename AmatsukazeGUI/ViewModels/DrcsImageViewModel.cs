using Amatsukaze.Models;
using Amatsukaze.Server;
using Livet;
using Livet.Commands;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;

namespace Amatsukaze.ViewModels
{
    public class DrcsImageViewModel : NamedViewModel
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

        #region Image変更通知プロパティ
        private DrcsImage _Image;

        public DrcsImage Image {
            get { return _Image; }
            set { 
                if (_Image == value)
                    return;
                _Image = value;
                MapStr = value.MapStr;
                RaisePropertyChanged();
                RaisePropertyChanged("IsModified");
            }
        }
        #endregion

        #region MapStr変更通知プロパティ
        private string _MapStr;

        public string MapStr {
            get { return _MapStr; }
            set { 
                if (_MapStr == value)
                    return;
                _MapStr = value;
                RaisePropertyChanged();
                RaisePropertyChanged("IsModified");
            }
        }

        public bool IsModified {
            get { return !string.IsNullOrEmpty(_MapStr) && _MapStr != Image.MapStr; }
        }
        #endregion

        #region SetMapStrCommand
        private ViewModelCommand _SetMapStrCommand;

        public ViewModelCommand SetMapStrCommand {
            get {
                if (_SetMapStrCommand == null)
                {
                    _SetMapStrCommand = new ViewModelCommand(SetMapStr);
                }
                return _SetMapStrCommand;
            }
        }

        public void SetMapStr()
        {
            Image.MapStr = _MapStr;
            Model.Server?.AddDrcsMap(Image);
        }
        #endregion

        #region DeleteMapStrCommand
        private ViewModelCommand _DeleteMapStrCommand;

        public ViewModelCommand DeleteMapStrCommand {
            get {
                if (_DeleteMapStrCommand == null)
                {
                    _DeleteMapStrCommand = new ViewModelCommand(DeleteMapStr);
                }
                return _DeleteMapStrCommand;
            }
        }

        public void DeleteMapStr()
        {
            if (MessageBox.Show("「" + Image.MapStr + "」を削除します。画像ファイルも削除します。\r\nよろしいですか？",
                "AmatsukazeGUI", MessageBoxButton.OKCancel) == MessageBoxResult.OK)
            {
                Image.MapStr = null;
                Model.Server?.AddDrcsMap(Image);
            }
        }
        #endregion

        public DrcsImageViewModel(ClientModel model, DrcsImage image)
        {
            Model = model;
            Image = image;
        }
    }
}
