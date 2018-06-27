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
using Amatsukaze.Components;
using System.Windows.Data;
using System.Collections;

namespace Amatsukaze.ViewModels
{
    public class QueueMenuProfileViewModel : ViewModel
    {
        public QueueViewModel QueueVM { get; set; }
        public object Item { get; set; }

        #region SelectedCommand
        private ListenerCommand<IEnumerable> _SelectedCommand;

        public ListenerCommand<IEnumerable> SelectedCommand
        {
            get
            {
                if (_SelectedCommand == null)
                {
                    _SelectedCommand = new ListenerCommand<IEnumerable>(Selected);
                }
                return _SelectedCommand;
            }
        }

        public void Selected(IEnumerable selectedItems)
        {
            QueueVM.ChangeProfile(selectedItems, Item);
        }
        #endregion
    }

    public class PriorityItemViewModel : ViewModel
    {
        public QueueViewModel QueueVM { get; set; }
        public int Priority { get; set; }

        #region SelectedCommand
        private ListenerCommand<IEnumerable> _SelectedCommand;

        public ListenerCommand<IEnumerable> SelectedCommand
        {
            get
            {
                if (_SelectedCommand == null)
                {
                    _SelectedCommand = new ListenerCommand<IEnumerable>(Selected);
                }
                return _SelectedCommand;
            }
        }

        public void Selected(IEnumerable selectedItems)
        {
            QueueVM.ChangePriority(selectedItems, Priority);
        }
        #endregion
    }

    public class SingleValueViewModel<T> : ViewModel where T : IComparable
    {
        private T _Value;

        public T Value
        {
            get { return _Value; }
            set
            {
                if (_Value.CompareTo(value) == 0)
                    return;
                _Value = value;
                RaisePropertyChanged();
            }
        }
    }

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
        public MainWindowViewModel MainPanel { get; set; }

        private CollectionItemListener<DisplayQueueItem> queueListener;
        private CollectionChangedEventListener queueListener2;
        private ICollectionView itemsView;
        private PropertyChangedEventListener higeOneSegListener;

        public QueueViewModel()
        {
            SearchChecks = Enumerable.Range(0, 6).Select(i =>
            {
                var vm = new SingleValueViewModel<bool>() { Value = true };
                CompositeDisposable.Add(new PropertyChangedEventListener(vm, SearchCheckChanged));
                return vm;
            }).ToArray();

            StateChecks = Enumerable.Range(0, 6).Select(i =>
            {
                var vm = new SingleValueViewModel<bool>() { Value = true };
                CompositeDisposable.Add(new PropertyChangedEventListener(vm, StateCheckChanged));
                return vm;
            }).ToArray();

            PriorityChecks = Enumerable.Range(0, 5).Select(i =>
            {
                var vm = new SingleValueViewModel<bool>() { Value = true };
                CompositeDisposable.Add(new PropertyChangedEventListener(vm, PriorityCheckChanged));
                return vm;
            }).ToArray();
        }

        public void Initialize()
        {
            ProfileList = new ObservableViewModelCollection<QueueMenuProfileViewModel, DisplayProfile>(
                Model.ProfileList, s => new QueueMenuProfileViewModel()
                {
                    QueueVM = this,
                    Item = s
                });

            AutoSelectList = new ObservableViewModelCollection<QueueMenuProfileViewModel, DisplayAutoSelect>(
                Model.AutoSelectList, s => new QueueMenuProfileViewModel()
                {
                    QueueVM = this,
                    Item = s
                });

            PriorityList = new List<PriorityItemViewModel>(
                Model.PriorityList.Select(p => new PriorityItemViewModel() { QueueVM = this, Priority = p }));

            queueListener = new CollectionItemListener<DisplayQueueItem>(Model.QueueItems,
                item => item.PropertyChanged += QueueItemPropertyChanged,
                item => item.PropertyChanged -= QueueItemPropertyChanged);

            queueListener2 = new CollectionChangedEventListener(Model.QueueItems,
                (sender, e) => ItemStateUpdated());

            itemsView = CollectionViewSource.GetDefaultView(Model.QueueItems);
            itemsView.Filter = ItemsFilter;

            higeOneSegListener = new PropertyChangedEventListener(Model, (sender, e) =>
            {
                if (e.PropertyName == "Setting" || e.PropertyName == "Setting.HideOneSeg")
                {
                    itemsView.Refresh();
                }
            });
        }

        private int StateIndex(QueueState state)
        {
            switch (state)
            {
                case QueueState.Canceled:
                    return 3;
                case QueueState.Complete:
                    return 4;
                case QueueState.Encoding:
                    return 2;
                case QueueState.Failed:
                    return 5;
                case QueueState.LogoPending:
                    return 1;
                case QueueState.PreFailed:
                    return 5;
                case QueueState.Queue:
                    return 0;
            }
            return 0;
        }

        private bool ItemsFilter(object obj)
        {
            var item = obj as DisplayQueueItem;
            if (Model.Setting.HideOneSeg && item.IsTooSmall) return false;
            if (PriorityChecks[item.Priority - 1].Value == false) return false;
            if (StateChecks[StateIndex(item.Model.State)].Value == false) return false;
            if (string.IsNullOrEmpty(_SearchWord)) return true;
            return (SearchChecks[1].Value && item.Model.ServiceName.IndexOf(_SearchWord) != -1) ||
                (SearchChecks[0].Value && item.Model.FileName.IndexOf(_SearchWord) != -1) ||
                (SearchChecks[3].Value && item.GenreString.IndexOf(_SearchWord) != -1) ||
                (SearchChecks[4].Value && item.StateString.IndexOf(_SearchWord) != -1) ||
                (SearchChecks[5].Value && item.ModeString.IndexOf(_SearchWord) != -1) ||
                (SearchChecks[2].Value && item.Model.Profile.Name.IndexOf(_SearchWord) != -1);
        }

        private void QueueItemPropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            if (string.IsNullOrEmpty(e.PropertyName))
            {
                ItemStateUpdated();
            }
        }

        public async void FileDropped(IEnumerable<string> list, bool isText)
        {
            var targets = list.SelectMany(path =>
            {
                if (Directory.Exists(path))
                {
                    return Directory.EnumerateFiles(path).Where(s => s.EndsWith("ts"));
                }
                else
                {
                    return new string[] { path };
                }
            }).Select(s => new AddQueueItem() { Path = s }).ToList();

            if (targets.Count == 0)
            {
                return;
            }

            var req = new AddQueueRequest()
            {
                DirPath = Path.GetDirectoryName(targets[0].Path),
                Targets = targets,
                Outputs = new List<OutputInfo>() {
                    new OutputInfo()
                    {
                        DstPath = Model.UIState.Model?.LastOutputPath,
                        Profile = Model.UIState.Model?.LastUsedProfile,
                        // デフォルト優先順は3
                        Priority = 3
                    }
                }
            };

            // 出力先フォルダ選択
            var selectPathVM = new SelectOutPathViewModel()
            {
                Model = Model,
                Item = req
            };
            await Messenger.RaiseAsync(new TransitionMessage(
                typeof(Views.SelectOutPath), selectPathVM, TransitionMode.Modal, "FromMain"));

            if (selectPathVM.Succeeded)
            {
                if (selectPathVM.PauseStart)
                {
                    await Model.Server?.PauseEncode(true);
                }
                Model.Server?.AddQueue(req).AttachHandler();
            }
        }

        private void LaunchLogoAnalyze(IEnumerable selectedItems, bool slimts)
        {
            var item = selectedItems.OfType<DisplayQueueItem>().FirstOrDefault();
            if (item == null)
            {
                return;
            }
            var file = item.Model;
            var workpath = Model.Setting.WorkPath;
            if (Directory.Exists(workpath) == false)
            {
                MessageBox.Show("一時ファイルフォルダがアクセスできる場所に設定されていないため起動できません");
                return;
            }
            string filepath = file.SrcPath;
            if (File.Exists(filepath) == false)
            {
                // failedに入っているかもしれないのでそっちも見る
                filepath = Path.GetDirectoryName(file.SrcPath) + "\\failed\\" + Path.GetFileName(file.SrcPath);
                if (File.Exists(filepath) == false)
                {
                    MessageBox.Show("ファイルが見つかりません");
                    return;
                }
            }
            var apppath = System.Reflection.Assembly.GetExecutingAssembly().Location;
            var args = "-l logo --file \"" + filepath + "\" --work \"" + workpath + "\" --serviceid " + file.ServiceId;
            if (slimts)
            {
                args += " --slimts";
            }
            System.Diagnostics.Process.Start(apppath, args);
        }

        private async Task<bool> ConfirmRetryCompleted(IEnumerable<DisplayQueueItem> items, string retry)
        {
            var completed = items.Where(s => s.Model.State == QueueState.Complete).ToArray();
            if (completed.Length > 0)
            {
                var top = completed.Take(10);
                var numOthers = completed.Count() - top.Count();
                var message = new ConfirmationMessage(
                    "以下のアイテムは既に完了しています。本当に" + retry + "しますか？\r\n\r\n" +
                    string.Join("\r\n", top.Select(s => s.Model.FileName)) +
                    ((numOthers > 0) ? "\r\n\r\n他" + numOthers + "個" : ""),
                    "Amatsukaze " + retry,
                    MessageBoxImage.Question,
                    MessageBoxButton.OKCancel,
                    "Confirm");

                await Messenger.RaiseAsync(message);

                return message.Response == true;
            }
            return true;
        }

        public ObservableViewModelCollection<QueueMenuProfileViewModel, DisplayProfile> ProfileList { get; private set; }
        public ObservableViewModelCollection<QueueMenuProfileViewModel, DisplayAutoSelect> AutoSelectList { get; private set; }
        public List<PriorityItemViewModel> PriorityList { get; private set; }

        #region SearchWord変更通知プロパティ
        private string _SearchWord;

        public string SearchWord {
            get { return _SearchWord; }
            set {
                if (_SearchWord == value)
                    return;
                _SearchWord = value;
                RaisePropertyChanged();
                if(SuppressUpdateCount == 0)
                    itemsView.Refresh();
            }
        }
        #endregion

        #region ClearSearchWordCommand
        private ViewModelCommand _ClearSearchWordCommand;

        public ViewModelCommand ClearSearchWordCommand {
            get {
                if (_ClearSearchWordCommand == null)
                {
                    _ClearSearchWordCommand = new ViewModelCommand(ClearSearchWord);
                }
                return _ClearSearchWordCommand;
            }
        }

        public void ClearSearchWord()
        {
            SuppressUpdateCount++;
            SearchWord = null;
            StateCheckAll = true;
            PriorityCheckAll = true;
            SuppressUpdateCount--;
            itemsView.Refresh();

        }
        #endregion

        #region ApplyFilterCommand
        private ViewModelCommand _ApplyFilterCommand;

        public ViewModelCommand ApplyFilterCommand
        {
            get
            {
                if (_ApplyFilterCommand == null)
                {
                    _ApplyFilterCommand = new ViewModelCommand(ApplyFilter);
                }
                return _ApplyFilterCommand;
            }
        }

        public void ApplyFilter()
        {
            itemsView.Refresh();
        }
        #endregion

        #region DeleteQueueItemCommand
        private ListenerCommand<IEnumerable> _DeleteQueueItemCommand;

        public ListenerCommand<IEnumerable> DeleteQueueItemCommand {
            get {
                if (_DeleteQueueItemCommand == null)
                {
                    _DeleteQueueItemCommand = new ListenerCommand<IEnumerable>(DeleteQueueItem);
                }
                return _DeleteQueueItemCommand;
            }
        }

        public async void DeleteQueueItem(IEnumerable selectedItems)
        {
            foreach (var item in selectedItems.OfType<DisplayQueueItem>().ToArray())
            {
                var file = item.Model;
                await Model.Server?.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.RemoveItem
                });
            }
        }
        #endregion

        #region RemoveCompletedAllCommand
        private ListenerCommand<IEnumerable> _RemoveCompletedAllCommand;

        public ListenerCommand<IEnumerable> RemoveCompletedAllCommand {
            get {
                if (_RemoveCompletedAllCommand == null)
                {
                    _RemoveCompletedAllCommand = new ListenerCommand<IEnumerable>(RemoveCompletedAll);
                }
                return _RemoveCompletedAllCommand;
            }
        }

        public async void RemoveCompletedAll(IEnumerable selectedItems)
        {
            var candidates = Model.QueueItems
                .Select(item => item.Model)
                .Where(s => s.State == QueueState.Complete || s.State == QueueState.PreFailed).ToArray();
            if(candidates.Length > 0)
            {
                var message = new ConfirmationMessage(
                    candidates.Length + "個のアイテムを削除します。",
                    "Amatsukaze アイテム削除",
                    MessageBoxImage.Question,
                    MessageBoxButton.OKCancel,
                    "Confirm");

                await Messenger.RaiseAsync(message);

                if (message.Response == true)
                {
                    Model.Server?.ChangeItem(new ChangeItemData()
                    {
                        ChangeType = ChangeItemType.RemoveCompleted
                    });
                }
            }
        }
        #endregion

        #region IsFilterPanelOpen変更通知プロパティ
        private bool _IsFilterPanelOpen;

        public bool IsFilterPanelOpen
        {
            get { return _IsFilterPanelOpen; }
            set {
                if (_IsFilterPanelOpen == value)
                    return;
                _IsFilterPanelOpen = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        private async Task RemoveTS(DisplayQueueItem[] items)
        {
            if (items.Length == 0)
            {
                var message = new ConfirmationMessage(
                    "対象アイテムがありません",
                    "Amatsukaze TSファイル削除",
                    MessageBoxImage.Information,
                    MessageBoxButton.OK,
                    "Confirm");

                await Messenger.RaiseAsync(message);
            }
            else
            {
                var top = items.Take(10);
                var numOthers = items.Length - top.Count();
                var message = new ConfirmationMessage(
                    "以下のアイテムのソースTSファイルを削除します。本当によろしいですか？\r\n" +
                    "（TSファイルを削除したアイテムはキューからも削除されます。）\r\n\r\n" +
                    string.Join("\r\n", top.Select(s => s.Model.FileName)) +
                    ((numOthers > 0) ? "\r\n\r\n他" + numOthers + "個" : ""),
                    "Amatsukaze TSファイル削除",
                    MessageBoxImage.Question,
                    MessageBoxButton.OKCancel,
                    "Confirm");

                await Messenger.RaiseAsync(message);

                if (message.Response == true)
                {
                    foreach (var item in items)
                    {
                        await Model.Server?.ChangeItem(new ChangeItemData()
                        {
                            ItemId = item.Model.Id,
                            ChangeType = ChangeItemType.RemoveSourceFile
                        });
                    }
                }
            }
        }

        #region RemoveTSQueueItemCommand
        private ListenerCommand<IEnumerable> _RemoveTSQueueItemCommand;

        public ListenerCommand<IEnumerable> RemoveTSQueueItemCommand
        {
            get
            {
                if (_RemoveTSQueueItemCommand == null)
                {
                    _RemoveTSQueueItemCommand = new ListenerCommand<IEnumerable>(RemoveTSQueueItem);
                }
                return _RemoveTSQueueItemCommand;
            }
        }

        public async void RemoveTSQueueItem(IEnumerable selectedItems)
        {
            var selected = selectedItems.OfType<DisplayQueueItem>().ToArray();
            var items = selected.Where(item => item.Model.State == QueueState.Complete && item.Model.IsBatch).ToArray();
            if (selected.Length - items.Length > 0)
            {
                var message = new ConfirmationMessage(
                    "TSファイルを削除できるのは、通常または自動追加のアイテムのうち\r\n" +
                    "完了した項目だけです。" + (selected.Length - items.Length) + "件のアイテムがこれに該当しないため除外\r\nされます",
                    "Amatsukaze TSファイル削除除外",
                    MessageBoxImage.Information,
                    MessageBoxButton.OK,
                    "Confirm");

                await Messenger.RaiseAsync(message);
            }
            await RemoveTS(items);
        }
        #endregion

        #region RemoveCompletedTSCommand
        private ListenerCommand<IEnumerable> _RemoveCompletedTSCommand;

        public ListenerCommand<IEnumerable> RemoveCompletedTSCommand
        {
            get
            {
                if (_RemoveCompletedTSCommand == null)
                {
                    _RemoveCompletedTSCommand = new ListenerCommand<IEnumerable>(RemoveCompletedTS);
                }
                return _RemoveCompletedTSCommand;
            }
        }

        public async void RemoveCompletedTS(IEnumerable selectedItems)
        {
            var items = Model.QueueItems
                .Where(item => item.Model.State == QueueState.Complete && item.Model.IsBatch).ToArray();
            await RemoveTS(items);
        }
        #endregion

        #region IsControlPanelOpen変更通知プロパティ
        private bool _IsControlPanelOpen;

        public bool IsControlPanelOpen
        {
            get { return _IsControlPanelOpen; }
            set
            {
                if (_IsControlPanelOpen == value)
                    return;
                _IsControlPanelOpen = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OpenLogoAnalyzeCommand
        private ListenerCommand<IEnumerable> _OpenLogoAnalyzeCommand;

        public ListenerCommand<IEnumerable> OpenLogoAnalyzeCommand {
            get {
                if (_OpenLogoAnalyzeCommand == null)
                {
                    _OpenLogoAnalyzeCommand = new ListenerCommand<IEnumerable>(OpenLogoAnalyze);
                }
                return _OpenLogoAnalyzeCommand;
            }
        }

        public void OpenLogoAnalyze(IEnumerable selectedItems)
        {
            LaunchLogoAnalyze(selectedItems, false);
        }
        #endregion

        #region OpenLogoAnalyzeSlimTsCommand
        private ListenerCommand<IEnumerable> _OpenLogoAnalyzeSlimTsCommand;

        public ListenerCommand<IEnumerable> OpenLogoAnalyzeSlimTsCommand {
            get {
                if (_OpenLogoAnalyzeSlimTsCommand == null)
                {
                    _OpenLogoAnalyzeSlimTsCommand = new ListenerCommand<IEnumerable>(OpenLogoAnalyzeSlimTs);
                }
                return _OpenLogoAnalyzeSlimTsCommand;
            }
        }

        public void OpenLogoAnalyzeSlimTs(IEnumerable selectedItems)
        {
            LaunchLogoAnalyze(selectedItems, true);
        }
        #endregion

        #region RetryCommand
        private ListenerCommand<IEnumerable> _RetryCommand;

        public ListenerCommand<IEnumerable> RetryCommand {
            get {
                if (_RetryCommand == null)
                {
                    _RetryCommand = new ListenerCommand<IEnumerable>(Retry);
                }
                return _RetryCommand;
            }
        }

        public async void Retry(IEnumerable selectedItems)
        {
            var items = selectedItems.OfType<DisplayQueueItem>().ToArray();
            if (await ConfirmRetryCompleted(items, "リトライ"))
            {
                foreach (var item in items)
                {
                    var file = item.Model;
                    await Model.Server?.ChangeItem(new ChangeItemData()
                    {
                        ItemId = file.Id,
                        ChangeType = ChangeItemType.ResetState
                    });
                }
            }
        }
        #endregion

        #region RetryUpdateCommand
        private ListenerCommand<IEnumerable> _RetryUpdateCommand;

        public ListenerCommand<IEnumerable> RetryUpdateCommand {
            get {
                if (_RetryUpdateCommand == null)
                {
                    _RetryUpdateCommand = new ListenerCommand<IEnumerable>(RetryUpdate);
                }
                return _RetryUpdateCommand;
            }
        }

        public async void RetryUpdate(IEnumerable selectedItems)
        {
            var items = selectedItems.OfType<DisplayQueueItem>().ToArray();
            foreach (var item in items)
            {
                var file = item.Model;
                await Model.Server?.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.UpdateProfile
                });
            }
        }
        #endregion

        #region ReAddCommand
        private ListenerCommand<IEnumerable> _ReAddCommand;

        public ListenerCommand<IEnumerable> ReAddCommand {
            get {
                if (_ReAddCommand == null)
                {
                    _ReAddCommand = new ListenerCommand<IEnumerable>(ReAdd);
                }
                return _ReAddCommand;
            }
        }

        public async void ReAdd(IEnumerable selectedItems)
        {
            var items = selectedItems.OfType<DisplayQueueItem>().ToArray();
            foreach (var item in items)
            {
                var file = item.Model;
                await Model.Server?.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.Duplicate
                });
            }
        }
        #endregion

        #region CancelCommand
        private ListenerCommand<IEnumerable> _CancelCommand;

        public ListenerCommand<IEnumerable> CancelCommand {
            get {
                if (_CancelCommand == null)
                {
                    _CancelCommand = new ListenerCommand<IEnumerable>(Cancel);
                }
                return _CancelCommand;
            }
        }

        public void Cancel(IEnumerable selectedItems)
        {
            foreach (var item in selectedItems.OfType<DisplayQueueItem>().ToArray())
            {
                var file = item.Model;
                Model.Server?.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.Cancel
                });
            }
        }
        #endregion

        #region RemoveCommand
        private ListenerCommand<IEnumerable> _RemoveCommand;

        public ListenerCommand<IEnumerable> RemoveCommand {
            get {
                if (_RemoveCommand == null)
                {
                    _RemoveCommand = new ListenerCommand<IEnumerable>(Remove);
                }
                return _RemoveCommand;
            }
        }

        public void Remove(IEnumerable selectedItems)
        {
            foreach (var item in selectedItems.OfType<DisplayQueueItem>().ToArray())
            {
                var file = item.Model;
                Model.Server?.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.RemoveItem
                });
            }
        }
        #endregion

        #region ForceStartCommand
        private ListenerCommand<IEnumerable> _ForceStartCommand;

        public ListenerCommand<IEnumerable> ForceStartCommand {
            get {
                if (_ForceStartCommand == null)
                {
                    _ForceStartCommand = new ListenerCommand<IEnumerable>(ForceStart);
                }
                return _ForceStartCommand;
            }
        }

        public void ForceStart(IEnumerable selectedItems)
        {
            foreach (var item in selectedItems.OfType<DisplayQueueItem>().ToArray())
            {
                var file = item.Model;
                Model.Server?.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.ForceStart
                });
            }
        }
        #endregion

        #region OpenFileInExplorerCommand
        private ListenerCommand<IEnumerable> _OpenFileInExplorerCommand;

        public ListenerCommand<IEnumerable> OpenFileInExplorerCommand {
            get {
                if (_OpenFileInExplorerCommand == null)
                {
                    _OpenFileInExplorerCommand = new ListenerCommand<IEnumerable>(OpenFileInExplorer);
                }
                return _OpenFileInExplorerCommand;
            }
        }

        public void OpenFileInExplorer(IEnumerable selectedItems)
        {
            var item = selectedItems.OfType<DisplayQueueItem>().FirstOrDefault();
            if (item == null)
            {
                return;
            }
            var file = item.Model;
            if (File.Exists(file.SrcPath) == false)
            {
                MessageBox.Show("ファイルが見つかりません");
                return;
            }
            System.Diagnostics.Process.Start("EXPLORER.EXE", "/select, \"" + file.SrcPath + "\"");
        }
        #endregion

        #region TogglePauseCommand
        private ViewModelCommand _TogglePauseCommand;

        public ViewModelCommand TogglePauseCommand {
            get {
                if (_TogglePauseCommand == null)
                {
                    _TogglePauseCommand = new ViewModelCommand(TogglePause);
                }
                return _TogglePauseCommand;
            }
        }

        public void TogglePause()
        {
            Model.Server?.PauseEncode(!Model.IsPaused);
        }
        #endregion

        #region ShowItemDetailCommand
        private ListenerCommand<DisplayQueueItem> _ShowItemDetailCommand;

        public ListenerCommand<DisplayQueueItem> ShowItemDetailCommand {
            get {
                if (_ShowItemDetailCommand == null)
                {
                    _ShowItemDetailCommand = new ListenerCommand<DisplayQueueItem>(ShowItemDetail);
                }
                return _ShowItemDetailCommand;
            }
        }

        public void ShowItemDetail(DisplayQueueItem item)
        {
            if (item.Model.State == QueueState.Encoding)
            {
                if (item.Model.ConsoleId < Model.ConsoleList.Count)
                {
                    MainPanel.ConsolePanelSelectedIndex = item.Model.ConsoleId;
                }
            }
        }
        #endregion

        #region ListStyle変更通知プロパティ
        private int _ListStyle;

        public int ListStyle {
            get { return _ListStyle; }
            set {
                if (_ListStyle == value)
                    return;
                _ListStyle = value;
                RaisePropertyChanged();
                itemsView.Refresh();
            }
        }
        #endregion

        public SingleValueViewModel<bool>[] SearchChecks { get; set; }
        public SingleValueViewModel<bool>[] StateChecks { get; set; }
        public SingleValueViewModel<bool>[] PriorityChecks { get; set; }

        private int SuppressUpdateCount = 0;

        private void SearchCheckChanged(object sender, PropertyChangedEventArgs e)
        {
            if (SuppressUpdateCount == 0)
            {
                UpdateSearchAll();
                itemsView.Refresh();
            }
        }

        private void StateCheckChanged(object sender, PropertyChangedEventArgs e)
        {
            if (SuppressUpdateCount == 0)
            {
                UpdateStateAll();
                itemsView.Refresh();
            }
        }

        private void PriorityCheckChanged(object sender, PropertyChangedEventArgs e)
        {
            if (SuppressUpdateCount == 0)
            {
                UpdatePriorityAll();
                itemsView.Refresh();
            }
        }

        #region SearchCheckAll変更通知プロパティ
        private bool? _SearchCheckAll = true;

        public bool? SearchCheckAll {
            get { return _SearchCheckAll; }
            set { 
                if (_SearchCheckAll == value)
                    return;
                _SearchCheckAll = value;
                RaisePropertyChanged();
                if (value != null)
                {
                    bool val = (bool)value;
                    SuppressUpdateCount++;
                    foreach(var c in SearchChecks)
                    {
                        c.Value = val;
                    }
                    SuppressUpdateCount--;
                    itemsView.Refresh();
                }
            }
        }
        #endregion

        #region StateCheckAll変更通知プロパティ
        private bool? _StateCheckAll = true;

        public bool? StateCheckAll
        {
            get { return _StateCheckAll; }
            set
            {
                if (_StateCheckAll == value)
                    return;
                _StateCheckAll = value;
                RaisePropertyChanged();
                if (value != null)
                {
                    bool val = (bool)value;
                    SuppressUpdateCount++;
                    foreach (var c in StateChecks)
                    {
                        c.Value = val;
                    }
                    SuppressUpdateCount--;
                    itemsView.Refresh();
                }
            }
        }
        #endregion

        #region PriorityCheckAll変更通知プロパティ
        private bool? _PriorityCheckAll = true;

        public bool? PriorityCheckAll
        {
            get { return _PriorityCheckAll; }
            set
            {
                if (_PriorityCheckAll == value)
                    return;
                _PriorityCheckAll = value;
                RaisePropertyChanged();
                if (value != null)
                {
                    bool val = (bool)value;
                    SuppressUpdateCount++;
                    foreach (var c in PriorityChecks)
                    {
                        c.Value = val;
                    }
                    SuppressUpdateCount--;
                    itemsView.Refresh();
                }
            }
        }
        #endregion

        public int Active { get { return Model.QueueItems.Count(s => s.Model.IsActive); } }
        public int Encoding { get { return Model.QueueItems.Count(s => s.Model.State == QueueState.Encoding); } }
        public int Complete { get { return Model.QueueItems.Count(s => s.Model.State == QueueState.Complete); } }
        public int Pending { get { return Model.QueueItems.Count(s => s.Model.State == QueueState.LogoPending); } }
        public int Fail { get { return Model.QueueItems.Count(s => s.Model.State == QueueState.Failed); } }
        public int Canceled { get { return Model.QueueItems.Count(s => s.Model.State == QueueState.Canceled); } }

        private void ItemStateUpdated()
        {
            RaisePropertyChanged("Active");
            RaisePropertyChanged("Encoding");
            RaisePropertyChanged("Complete");
            RaisePropertyChanged("Pending");
            RaisePropertyChanged("Fail");
            RaisePropertyChanged("Canceled");
        }

        private void UpdateSearchAll()
        {
            SearchCheckAll = SearchChecks.All(s => s.Value)
                ? (bool?)true : SearchChecks.All(s => !s.Value)
                ? (bool?)false : null;
        }

        private void UpdateStateAll()
        {
            StateCheckAll = StateChecks.All(s => s.Value)
                ? (bool?)true : StateChecks.All(s => !s.Value)
                ? (bool?)false : null;
        }

        private void UpdatePriorityAll()
        {
            PriorityCheckAll = PriorityChecks.All(s => s.Value)
                ? (bool?)true : PriorityChecks.All(s => !s.Value)
                ? (bool?)false : null;
        }

        public void ChangeProfile(IEnumerable selectedItems, object profile)
        {
            var profileName = DisplayProfile.GetProfileName(profile);
            foreach (var item in selectedItems.OfType<DisplayQueueItem>().ToArray())
            {
                var file = item.Model;
                Model.Server?.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.Profile,
                    Profile = profileName
                });
            }
        }

        public void ChangePriority(IEnumerable selectedItems, int priority)
        {
            foreach (var item in selectedItems.OfType<DisplayQueueItem>().ToArray())
            {
                var file = item.Model;
                Model.Server?.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.Priority,
                    Priority = priority
                });
            }
        }
    }
}
