using Amatsukaze.Models;
using Amatsukaze.Server;
using Livet;
using Livet.Commands;
using Livet.Messaging;
using Livet.Messaging.Windows;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;

namespace Amatsukaze.ViewModels
{
    public class AutoSelectSettingViewModel : NamedViewModel
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


        #region ApplyProfileCommand
        private ViewModelCommand _ApplyProfileCommand;

        public ViewModelCommand ApplyProfileCommand
        {
            get
            {
                if (_ApplyProfileCommand == null)
                {
                    _ApplyProfileCommand = new ViewModelCommand(ApplyProfile);
                }
                return _ApplyProfileCommand;
            }
        }

        public void ApplyProfile()
        {
            if (Model.SelectedAutoSelect != null)
            {
                Model.SelectedAutoSelect.Model.Conditions = 
                    Model.SelectedAutoSelect.Conditions.Select(s =>
                    {
                        s.UpdateItem();
                        return s.Item;
                    }).ToList();
                Model.UpdateAutoSelect(Model.SelectedAutoSelect.Model);
            }
        }
        #endregion

        #region NewProfileCommand
        private ViewModelCommand _NewProfileCommand;

        public ViewModelCommand NewProfileCommand
        {
            get
            {
                if (_NewProfileCommand == null)
                {
                    _NewProfileCommand = new ViewModelCommand(NewProfile);
                }
                return _NewProfileCommand;
            }
        }

        public async void NewProfile()
        {
            if (Model.SelectedAutoSelect != null)
            {
                var profile = Model.SelectedAutoSelect;
                var newp = new NewProfileViewModel()
                {
                    Model = Model,
                    Title = "自動選択プロファイル",
                    Operation = "追加",
                    IsDuplicate = Name => Model.AutoSelectList.Any(
                        s => s.Model.Name.Equals(Name, StringComparison.OrdinalIgnoreCase)),
                    Name = profile.Model.Name + "のコピー"
                };

                await Messenger.RaiseAsync(new TransitionMessage(
                    typeof(Views.NewProfileWindow), newp, TransitionMode.Modal, "FromProfile"));

                if (newp.Success)
                {
                    var newprofile = ServerSupport.DeepCopy(profile.Model);
                    newprofile.Name = newp.Name;
                    await Model.AddAutoSelect(newprofile);
                }
            }
        }
        #endregion

        #region RenameProfileCommand
        private ViewModelCommand _RenameProfileCommand;

        public ViewModelCommand RenameProfileCommand {
            get {
                if (_RenameProfileCommand == null)
                {
                    _RenameProfileCommand = new ViewModelCommand(RenameProfile);
                }
                return _RenameProfileCommand;
            }
        }

        public async void RenameProfile()
        {
            if (Model.SelectedAutoSelect != null)
            {
                var profile = Model.SelectedAutoSelect;
                var newp = new NewProfileViewModel()
                {
                    Model = Model,
                    Title = "プロファイル自動選択",
                    Operation = "リネーム",
                    IsDuplicate = Name => Name != profile.Model.Name && Model.AutoSelectList.Any(
                        s => s.Model.Name.Equals(Name, StringComparison.OrdinalIgnoreCase)),
                    Name = profile.Model.Name
                };

                await Messenger.RaiseAsync(new TransitionMessage(
                    typeof(Views.NewProfileWindow), newp, TransitionMode.Modal, "FromProfile"));

                if (newp.Success)
                {
                    await Model.Server.SetAutoSelect(new AutoSelectUpdate()
                    {
                        Type = UpdateType.Update,
                        Profile = profile.Model,
                        NewName = newp.Name
                    });
                }
            }
        }
        #endregion

        #region DeleteProfileCommand
        private ViewModelCommand _DeleteProfileCommand;

        public ViewModelCommand DeleteProfileCommand
        {
            get
            {
                if (_DeleteProfileCommand == null)
                {
                    _DeleteProfileCommand = new ViewModelCommand(DeleteProfile);
                }
                return _DeleteProfileCommand;
            }
        }

        public async void DeleteProfile()
        {
            if (Model.SelectedAutoSelect != null)
            {
                var profile = Model.SelectedAutoSelect.Model;

                var message = new ConfirmationMessage(
                    "プロファイル自動選択「" + profile.Name + "」を削除しますか？",
                    "Amatsukaze",
                    System.Windows.MessageBoxImage.Information,
                    System.Windows.MessageBoxButton.OKCancel,
                    "Confirm");

                await Messenger.RaiseAsync(message);

                if (message.Response == true)
                {
                    await Model.RemoveAutoSelect(profile);
                }
            }
        }
        #endregion

        #region AddConditionCommand
        private ViewModelCommand _AddConditionCommand;

        public ViewModelCommand AddConditionCommand
        {
            get
            {
                if (_AddConditionCommand == null)
                {
                    _AddConditionCommand = new ViewModelCommand(AddCondition);
                }
                return _AddConditionCommand;
            }
        }

        public void AddCondition()
        {
            if(Model.SelectedAutoSelect != null)
            {
                var cond = new AutoSelectCondition()
                {
                    ContentConditions = new List<GenreItem>(),
                    ServiceIds = new List<int>(),
                    VideoSizes = new List<VideoSizeCondition>()
                };
                var disp = new DisplayCondition()
                {
                    Model = Model,
                    Item = cond
                };
                disp.Initialize();
                Model.SelectedAutoSelect.Model.Conditions.Add(cond);
                Model.SelectedAutoSelect.Conditions.Add(disp);
            }
        }
        #endregion

        #region RemoveConditionCommand
        private ViewModelCommand _RemoveConditionCommand;

        public ViewModelCommand RemoveConditionCommand
        {
            get
            {
                if (_RemoveConditionCommand == null)
                {
                    _RemoveConditionCommand = new ViewModelCommand(RemoveCondition);
                }
                return _RemoveConditionCommand;
            }
        }

        public void RemoveCondition()
        {
            if (Model.SelectedAutoSelect?.SelectedCondition != null)
            {
                var cond = Model.SelectedAutoSelect.SelectedCondition;
                Model.SelectedAutoSelect.Model.Conditions.Remove(cond.Item);
                Model.SelectedAutoSelect.Conditions.Remove(cond);
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
