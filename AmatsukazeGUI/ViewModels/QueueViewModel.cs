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

        public async void FileDropped(IEnumerable<string> list, bool isText)
        {
            Dictionary<string, AddQueueDirectory> dirList = new Dictionary<string, AddQueueDirectory>();
            if (isText)
            {
                // テキストパスの場合は存在チェックしない
                foreach (var path in list)
                {
                    if (string.IsNullOrWhiteSpace(path) == false)
                    {
                        dirList.Add(path, new AddQueueDirectory()
                        {
                            DirPath = path
                        });
                    }
                }
            }
            else
            {
                foreach (var path in list)
                {
                    if (Directory.Exists(path))
                    {
                        dirList.Add(path, new AddQueueDirectory()
                        {
                            DirPath = path
                        });
                    }
                    else if (File.Exists(path))
                    {
                        var dirPath = Path.GetDirectoryName(path);
                        var fileitem = new AddQueueItem() { Path = path };
                        AddQueueDirectory item;
                        if (dirList.TryGetValue(dirPath, out item))
                        {
                            if (item.Targets != null)
                            {
                                item.Targets.Add(fileitem);
                            }
                        }
                        else
                        {
                            dirList.Add(dirPath, new AddQueueDirectory()
                            {
                                DirPath = dirPath,
                                Targets = new List<AddQueueItem>() { fileitem }
                            });
                        }
                    }
                }
            }

            foreach (var item in dirList.Values)
            {
                item.Outputs = new List<OutputInfo>() { new OutputInfo()
                {
                // デフォルト優先順は3
                    Priority = 3
                } };

                // 出力先フォルダ選択
                var selectPathVM = new SelectOutPathViewModel()
                {
                    Model = Model,
                    Item = item
                };
                await Messenger.RaiseAsync(new TransitionMessage(
                    typeof(Views.SelectOutPath), selectPathVM, TransitionMode.Modal, "FromMain"));

                if (selectPathVM.Succeeded)
                {
                    Model.Server.AddQueue(item).AttachHandler();
                }
            }
        }

        private IEnumerable<DisplayQueueItem> SelectedQueueFiles
        {
            get
            {
                var dir = SetectedQueueDir;
                if (dir == null)
                {
                    return Enumerable.Empty<DisplayQueueItem>();
                }
                return dir.Items.Where(s => s.IsSelected);
            }
        }

        private void LaunchLogoAnalyze(bool slimts)
        {
            var item = SelectedQueueFiles.FirstOrDefault();
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

        private async Task<bool> ConfirmRetryCompleted(DisplayQueueItem[] items, string retry)
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

        #region SetectedQueueDir変更通知プロパティ
        private DisplayQueueDirectory _SetectedQueueDir;

        public DisplayQueueDirectory SetectedQueueDir {
            get { return _SetectedQueueDir; }
            set { 
                if (_SetectedQueueDir == value)
                    return;
                _SetectedQueueDir = value;
                RaisePropertyChanged();
            }
        }

        public bool IsQueueDirSelected {
            get {
                return SetectedQueueDir != null;
            }
        }
        #endregion

        #region DeleteQueueDirCommand
        private ViewModelCommand _DeleteQueueDirCommand;

        public ViewModelCommand DeleteQueueDirCommand
        {
            get
            {
                if (_DeleteQueueDirCommand == null)
                {
                    _DeleteQueueDirCommand = new ViewModelCommand(DeleteQueueDir);
                }
                return _DeleteQueueDirCommand;
            }
        }

        public void DeleteQueueDir()
        {
            var item = SetectedQueueDir;
            if (item != null)
            {
                Model.Server.ChangeItem(new ChangeItemData()
                {
                    ChangeType = ChangeItemType.RemoveDir,
                    ItemId = item.Id
                });
            }
        }
        #endregion

        public bool IsQueueFileSelected
        {
            get { return SelectedQueueFiles.Any(); }
        }

        #region DeleteQueueItemCommand
        private ViewModelCommand _DeleteQueueItemCommand;

        public ViewModelCommand DeleteQueueItemCommand {
            get {
                if (_DeleteQueueItemCommand == null)
                {
                    _DeleteQueueItemCommand = new ViewModelCommand(DeleteQueueItem);
                }
                return _DeleteQueueItemCommand;
            }
        }

        public async void DeleteQueueItem()
        {
            var items = SelectedQueueFiles.ToArray();
            foreach (var item in items)
            {
                var file = item.Model;
                await Model.Server.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.RemoveItem
                });
            }
        }
        #endregion

        #region RemoveCompletedItemsCommand
        private ViewModelCommand _RemoveCompletedItemsCommand;

        public ViewModelCommand RemoveCompletedItemsCommand {
            get {
                if (_RemoveCompletedItemsCommand == null)
                {
                    _RemoveCompletedItemsCommand = new ViewModelCommand(RemoveCompletedItems);
                }
                return _RemoveCompletedItemsCommand;
            }
        }

        public void RemoveCompletedItems()
        {
            var dir = SetectedQueueDir;
            if (dir != null)
            {
                Model.Server.ChangeItem(new ChangeItemData()
                {
                    ChangeType = ChangeItemType.RemoveCompletedItem,
                    ItemId = dir.Id
                });
            }
        }
        #endregion

        #region RemoveCompletedAllCommand
        private ViewModelCommand _RemoveCompletedAllCommand;

        public ViewModelCommand RemoveCompletedAllCommand {
            get {
                if (_RemoveCompletedAllCommand == null)
                {
                    _RemoveCompletedAllCommand = new ViewModelCommand(RemoveCompletedAll);
                }
                return _RemoveCompletedAllCommand;
            }
        }

        public void RemoveCompletedAll()
        {
            Model.Server.ChangeItem(new ChangeItemData()
            {
                ChangeType = ChangeItemType.RemoveCompletedAll
            });
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

        #region OpenLogoAnalyzeCommand
        private ViewModelCommand _OpenLogoAnalyzeCommand;

        public ViewModelCommand OpenLogoAnalyzeCommand
        {
            get
            {
                if (_OpenLogoAnalyzeCommand == null)
                {
                    _OpenLogoAnalyzeCommand = new ViewModelCommand(OpenLogoAnalyze);
                }
                return _OpenLogoAnalyzeCommand;
            }
        }

        public void OpenLogoAnalyze()
        {
            LaunchLogoAnalyze(false);
        }
        #endregion

        #region OpenLogoAnalyzeSlimTsCommand
        private ViewModelCommand _OpenLogoAnalyzeSlimTsCommand;

        public ViewModelCommand OpenLogoAnalyzeSlimTsCommand
        {
            get
            {
                if (_OpenLogoAnalyzeSlimTsCommand == null)
                {
                    _OpenLogoAnalyzeSlimTsCommand = new ViewModelCommand(OpenLogoAnalyzeSlimTs);
                }
                return _OpenLogoAnalyzeSlimTsCommand;
            }
        }

        public void OpenLogoAnalyzeSlimTs()
        {
            LaunchLogoAnalyze(true);
        }
        #endregion

        #region RetryCommand
        private ViewModelCommand _RetryCommand;

        public ViewModelCommand RetryCommand
        {
            get
            {
                if (_RetryCommand == null)
                {
                    _RetryCommand = new ViewModelCommand(Retry);
                }
                return _RetryCommand;
            }
        }

        public async void Retry()
        {
            var items = SelectedQueueFiles.ToArray();
            if (await ConfirmRetryCompleted(items, "リトライ"))
            {
                foreach (var item in items)
                {
                    var file = item.Model;
                    await Model.Server.ChangeItem(new ChangeItemData()
                    {
                        ItemId = file.Id,
                        ChangeType = ChangeItemType.Retry
                    });
                }
            }
        }
        #endregion

        #region ReAddCommand
        private ViewModelCommand _ReAddCommand;

        public ViewModelCommand ReAddCommand
        {
            get
            {
                if (_ReAddCommand == null)
                {
                    _ReAddCommand = new ViewModelCommand(ReAdd);
                }
                return _ReAddCommand;
            }
        }

        public async void ReAdd()
        {
            var items = SelectedQueueFiles.ToArray();
            if (await ConfirmRetryCompleted(items, "再投入"))
            {
                foreach (var item in items)
                {
                    var file = item.Model;
                    await Model.Server.ChangeItem(new ChangeItemData()
                    {
                        ItemId = file.Id,
                        ChangeType = ChangeItemType.ReAdd
                    });
                }
            }
        }
        #endregion

        #region CancelCommand
        private ViewModelCommand _CancelCommand;

        public ViewModelCommand CancelCommand
        {
            get
            {
                if (_CancelCommand == null)
                {
                    _CancelCommand = new ViewModelCommand(Cancel);
                }
                return _CancelCommand;
            }
        }

        public void Cancel()
        {
            foreach (var item in SelectedQueueFiles.ToArray())
            {
                var file = item.Model;
                Model.Server.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.Cancel
                });
            }
        }
        #endregion

        #region RemoveCommand
        private ViewModelCommand _RemoveCommand;

        public ViewModelCommand RemoveCommand {
            get {
                if (_RemoveCommand == null)
                {
                    _RemoveCommand = new ViewModelCommand(Remove);
                }
                return _RemoveCommand;
            }
        }

        public void Remove()
        {
            foreach (var item in SelectedQueueFiles.ToArray())
            {
                var file = item.Model;
                Model.Server.ChangeItem(new ChangeItemData()
                {
                    ItemId = file.Id,
                    ChangeType = ChangeItemType.RemoveItem
                });
            }
        }
        #endregion

        #region OpenFileInExplorerCommand
        private ViewModelCommand _OpenFileInExplorerCommand;

        public ViewModelCommand OpenFileInExplorerCommand
        {
            get
            {
                if (_OpenFileInExplorerCommand == null)
                {
                    _OpenFileInExplorerCommand = new ViewModelCommand(OpenFileInExplorer);
                }
                return _OpenFileInExplorerCommand;
            }
        }

        public void OpenFileInExplorer()
        {
            var item = SelectedQueueFiles.FirstOrDefault();
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

    }
}
