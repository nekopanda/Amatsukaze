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
using System.Collections.Specialized;
using System.Collections.ObjectModel;
using Amatsukaze.Server;

namespace Amatsukaze.ViewModels
{
    public class DrcsImageListViewModel : NamedViewModel
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

        #region ImageList変更通知プロパティ
        private ObservableCollection<DrcsImageViewModel> _ImageList = new ObservableCollection<DrcsImageViewModel>();

        public ObservableCollection<DrcsImageViewModel> ImageList {
            get { return _ImageList; }
            set { 
                if (_ImageList == value)
                    return;
                _ImageList = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ImageItemSelectedIndex変更通知プロパティ
        private int _ImageItemSelectedIndex;

        public int ImageItemSelectedIndex {
            get { return _ImageItemSelectedIndex; }
            set { 
                if (_ImageItemSelectedIndex == value)
                    return;
                _ImageItemSelectedIndex = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region IsNoMapOnly変更通知プロパティ
        private bool _IsNoMapOnly = true;

        public bool IsNoMapOnly {
            get { return _IsNoMapOnly; }
            set { 
                if (_IsNoMapOnly == value)
                    return;
                _IsNoMapOnly = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ToggleNoMapOnlyCommand
        private ViewModelCommand _ToggleNoMapOnlyCommand;

        public ViewModelCommand ToggleNoMapOnlyCommand {
            get {
                if (_ToggleNoMapOnlyCommand == null)
                {
                    _ToggleNoMapOnlyCommand = new ViewModelCommand(ToggleNoMapOnly);
                }
                return _ToggleNoMapOnlyCommand;
            }
        }

        public void ToggleNoMapOnly()
        {
            IsNoMapOnly = !IsNoMapOnly;
            imagesView.Refresh();
        }
        #endregion

        private List<CollectionChangedEventListener> imagesListener = new List<CollectionChangedEventListener>();
        private ICollectionView imagesView;

        public void Initialize()
        {
            imagesView = System.Windows.Data.CollectionViewSource.GetDefaultView(_ImageList);
            imagesView.Filter = x => IsNoMapOnly == false || string.IsNullOrEmpty(((DrcsImageViewModel)x).MapStr);

            imagesListener.Add(new CollectionChangedEventListener(Model.DrcsImageList, (o, e) => {
                switch (e.Action)
                {
                    case NotifyCollectionChangedAction.Add:
                        foreach(var item in e.NewItems)
                        {
                            _ImageList.Add(new DrcsImageViewModel(Model, item as DrcsImage));
                        }
                        break;
                    case NotifyCollectionChangedAction.Remove:
                        foreach (var item in e.OldItems)
                        {
                            var tgt = _ImageList.First(s => s.Image == item);
                            _ImageList.Remove(tgt);
                        }
                        break;
                    case NotifyCollectionChangedAction.Reset:
                        _ImageList.Clear();
                        break;
                    case NotifyCollectionChangedAction.Replace:
                        for(int i = 0; i < e.NewItems.Count; ++i)
                        {
                            _ImageList[e.NewStartingIndex + i].Image = e.NewItems[i] as DrcsImage;
                        }
                        break;
                }
                imagesView.Refresh();
            }));
        }
    }
}
