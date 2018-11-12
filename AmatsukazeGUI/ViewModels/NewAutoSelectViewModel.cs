using Amatsukaze.Models;
using Livet;
using Livet.Commands;
using Livet.Messaging.Windows;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.ViewModels
{
    public class NewAutoSelectViewModel : ViewModel
    {
        public ClientModel Model { get; set; }

        public bool Success;

        public void Initialize()
        {
        }

        private bool IsDuplicate()
        {
            return Model.AutoSelectList.Any(s => s.Model.Name.Equals(_Name, StringComparison.OrdinalIgnoreCase));
        }

        private bool IsInvalid()
        {
            var invalidChars = System.IO.Path.GetInvalidFileNameChars();
            return _Name.Any(c => Array.IndexOf(invalidChars, c) != -1);
        }

        #region OkCommand
        private ViewModelCommand _OkCommand;

        public ViewModelCommand OkCommand
        {
            get
            {
                if (_OkCommand == null)
                {
                    _OkCommand = new ViewModelCommand(Ok);
                }
                return _OkCommand;
            }
        }

        public async void Ok()
        {
            if (!IsDuplicate() && !IsInvalid())
            {
                Success = true;
                await Messenger.RaiseAsync(new WindowActionMessage(WindowAction.Close, "Close"));
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

        public async void Cancel()
        {
            Success = false;
            await Messenger.RaiseAsync(new WindowActionMessage(WindowAction.Close, "Close"));
        }
        #endregion

        #region Name変更通知プロパティ
        private string _Name;

        public string Name
        {
            get { return _Name; }
            set
            {
                if (_Name == value)
                    return;
                _Name = value;
                Description = IsInvalid() ? "無効な文字が含まれています。" : IsDuplicate() ? "名前が重複しています。" : "";
                RaisePropertyChanged();
            }
        }
        #endregion

        #region Description変更通知プロパティ
        private string _Description;

        public string Description
        {
            get { return _Description; }
            set
            {
                if (_Description == value)
                    return;
                _Description = value;
                RaisePropertyChanged();
            }
        }
        #endregion
    }
}
