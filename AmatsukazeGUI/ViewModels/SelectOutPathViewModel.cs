using Amatsukaze.Models;
using Amatsukaze.Server;
using Livet;
using Livet.Commands;
using Livet.Messaging;
using Livet.Messaging.Windows;
using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace Amatsukaze.ViewModels
{
    public class SelectOutPathViewModel : ViewModel
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

        public AddQueueRequest Item { get; set; }

        public void Initialize()
        {
            if(Item.Outputs[0].DstPath == null)
            {
                ResetOutPath();
            }
            if(Item.Outputs[0].Profile != null)
            {
                bool isAuto = false;
                var profileName = ServerSupport.ParseProfileName(Item.Outputs[0].Profile, out isAuto);
                SelectedProfile = isAuto
                    ? (object)Model.AutoSelectList.FirstOrDefault(s => s.Model.Name == profileName)
                    : Model.ProfileList.FirstOrDefault(s => s.Model.Name == profileName);
            }
        }

        public bool Succeeded { get; private set; }

        private async Task<bool> GetOutPath()
        {
            if (System.IO.Directory.Exists(Item.Outputs[0].DstPath) == false)
            {
                var message = new ConfirmationMessage(
                    "出力先フォルダが存在しません。作成しますか？",
                    "Amatsukaze フォルダ作成",
                    System.Windows.MessageBoxImage.Information,
                    System.Windows.MessageBoxButton.OKCancel,
                    "Confirm");

                await Messenger.RaiseAsync(message);

                if (message.Response != true)
                {
                    return false;
                }

                try
                {
                    Directory.CreateDirectory(Item.Outputs[0].DstPath);
                }
                catch (Exception e)
                {
                    Description = "フォルダの作成に失敗しました: " + e.Message;
                    return false;
                }
            }
            return true;
        }

        private bool GetProfileName()
        {
            Item.Outputs[0].Profile = DisplayProfile.GetProfileName(SelectedProfile);
            if(string.IsNullOrEmpty(Item.Outputs[0].Profile))
            {
                Description = "プロファイルを選択してください";
                return false;
            }
            return true;
        }

        #region OkCommand
        private ViewModelCommand _OkCommand;

        public ViewModelCommand OkCommand {
            get {
                if (_OkCommand == null)
                {
                    _OkCommand = new ViewModelCommand(Ok);
                }
                return _OkCommand;
            }
        }

        public async void Ok()
        {
            if (!await GetOutPath()) return;
            if (!GetProfileName()) return;
            Item.Mode = ProcMode.Batch;
            Succeeded = true;
            await Messenger.RaiseAsync(new WindowActionMessage(WindowAction.Close, "Close"));
        }
        #endregion

        #region TestCommand
        private ViewModelCommand _TestCommand;

        public ViewModelCommand TestCommand
        {
            get {
                if (_TestCommand == null)
                {
                    _TestCommand = new ViewModelCommand(Test);
                }
                return _TestCommand;
            }
        }

        public async void Test()
        {
            if (!await GetOutPath()) return;
            if (!GetProfileName()) return;
            Item.Mode = ProcMode.Test;
            Succeeded = true;
            await Messenger.RaiseAsync(new WindowActionMessage(WindowAction.Close, "Close"));
        }
        #endregion

        #region SearchCommand
        private ViewModelCommand _SearchCommand;

        public ViewModelCommand SearchCommand {
            get {
                if (_SearchCommand == null)
                {
                    _SearchCommand = new ViewModelCommand(Search);
                }
                return _SearchCommand;
            }
        }

        public async void Search()
        {
            if (!GetProfileName()) return;
            Item.Mode = ProcMode.DrcsCheck;
            Succeeded = true;
            await Messenger.RaiseAsync(new WindowActionMessage(WindowAction.Close, "Close"));
        }
        #endregion

        #region CMCheckCommand
        private ViewModelCommand _CMCheckCommand;

        public ViewModelCommand CMCheckCommand {
            get {
                if (_CMCheckCommand == null)
                {
                    _CMCheckCommand = new ViewModelCommand(CMCheck);
                }
                return _CMCheckCommand;
            }
        }

        public async void CMCheck()
        {
            if (!GetProfileName()) return;
            Item.Mode = ProcMode.CMCheck;
            Succeeded = true;
            await Messenger.RaiseAsync(new WindowActionMessage(WindowAction.Close, "Close"));
        }
        #endregion

        #region SelectedProfile変更通知プロパティ
        private object _SelectedProfile;

        public object SelectedProfile {
            get { return _SelectedProfile; }
            set { 
                if (_SelectedProfile == value)
                    return;
                _SelectedProfile = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region OutPath変更通知プロパティ
        public string OutPath {
            get { return Item.Outputs[0].DstPath; }
            set { 
                if (Item.Outputs[0].DstPath == value)
                    return;
                Item.Outputs[0].DstPath = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Description変更通知プロパティ
        private string _Description;

        public string Description {
            get { return _Description; }
            set { 
                if (_Description == value)
                    return;
                _Description = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region PauseStart変更通知プロパティ
        private bool _PauseStart;

        public bool PauseStart {
            get { return _PauseStart; }
            set { 
                if (_PauseStart == value)
                    return;
                _PauseStart = value;
                RaisePropertyChanged();
            }
        }
        #endregion

        #region ResetOutPathCommand
        private ViewModelCommand _ResetOutPathCommand;

        public ViewModelCommand ResetOutPathCommand {
            get {
                if (_ResetOutPathCommand == null)
                {
                    _ResetOutPathCommand = new ViewModelCommand(ResetOutPath);
                }
                return _ResetOutPathCommand;
            }
        }

        public void ResetOutPath()
        {
            OutPath = Path.Combine(Item.DirPath, "encoded");
        }
        #endregion

        public string InputInfoText {
            get {
                if(Item == null)
                {
                    return null;
                }
                string text = "入力フォルダ: " + Item.DirPath;
                if(Item.Targets != null)
                {
                    text += "(" + Item.Targets.Count + ")";
                }
                return text;
            }
        }
    }
}
