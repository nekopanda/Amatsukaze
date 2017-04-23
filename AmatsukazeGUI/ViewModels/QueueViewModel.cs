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
using System.Windows;
using System.IO;

namespace Amatsukaze.ViewModels
{
    public class QueueViewModel : NamedViewModel
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

        public void Initialize()
        {
        }

        public async void FileDropped(IEnumerable<string> list)
        {
            Dictionary<string, AddQueueDirectory> dirList = new Dictionary<string, AddQueueDirectory>();
            foreach (var path in list)
            {
                if (Directory.Exists(path))
                {
                    dirList.Add(path, new AddQueueDirectory() {
                        DirPath = path
                    });
                }
                else if (File.Exists(path))
                {
                    var dirPath = System.IO.Path.GetDirectoryName(path);
                    AddQueueDirectory item;
                    if (dirList.TryGetValue(dirPath, out item))
                    {
                        if (item.Targets != null)
                        {
                            item.Targets.Add(path);
                        }
                    }
                    else
                    {
                        dirList.Add(dirPath, new AddQueueDirectory() {
                            DirPath = dirPath,
                            Targets = new List<string>() { path }
                        });
                    }
                }
            }

            foreach (var item in dirList.Values)
            {
                // 出力先フォルダ選択
                var selectPathVM = new SelectOutPathViewModel() { Item = item };
                await Messenger.RaiseAsync(new TransitionMessage(
                    typeof(Views.SelectOutPath), selectPathVM, TransitionMode.Modal, "FromMain"));

                if(selectPathVM.Succeeded)
                {
                    Model.Server.AddQueue(item).AttachHandler();
                }
            }
        }

        #region QueueItemSelectedIndex変更通知プロパティ
        private int _QueueItemSelectedIndex = -1;

        public int QueueItemSelectedIndex
        {
            get { return _QueueItemSelectedIndex; }
            set
            {
                if (_QueueItemSelectedIndex == value)
                    return;
                _QueueItemSelectedIndex = value;
                RaisePropertyChanged();
                RaisePropertyChanged("SetectedQueueItem");
                RaisePropertyChanged("IsQueueItemSelected");
            }
        }

        public DisplayQueueDirectory SetectedQueueItem
        {
            get {
                if (_QueueItemSelectedIndex >= 0 && _QueueItemSelectedIndex < Model.QueueItems.Count)
                {
                    return Model.QueueItems[_QueueItemSelectedIndex];
                }
                return null;
            }
        }

        public bool IsQueueItemSelected
        {
            get
            {
                return SetectedQueueItem != null;
            }
        }
        #endregion

        #region DeleteQueueItemCommand
        private ViewModelCommand _DeleteQueueItemCommand;

        public ViewModelCommand DeleteQueueItemCommand
        {
            get
            {
                if (_DeleteQueueItemCommand == null)
                {
                    _DeleteQueueItemCommand = new ViewModelCommand(DeleteQueueItem);
                }
                return _DeleteQueueItemCommand;
            }
        }

        public void DeleteQueueItem()
        {
            var item = SetectedQueueItem;
            if (item != null)
            {
                Model.Server.RemoveQueue(item.Path);
            }
        }
        #endregion

        #region UpperRowLength変更通知プロパティ
        private GridLength _UpperRowLength = new GridLength(1, GridUnitType.Star);

        public GridLength UpperRowLength
        {
            get
            { return _UpperRowLength; }
            set
            { 
                if (_UpperRowLength == value)
                    return;
                _UpperRowLength = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region LowerRowLength変更通知プロパティ
        private GridLength _LowerRowLength = new GridLength(1, GridUnitType.Star);

        public GridLength LowerRowLength
        {
            get
            { return _LowerRowLength; }
            set
            { 
                if (_LowerRowLength == value)
                    return;
                _LowerRowLength = value;
                RaisePropertyChanged();
            }
        }
        #endregion

    }
}
