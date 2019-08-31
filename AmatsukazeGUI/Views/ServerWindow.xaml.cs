using Amatsukaze.Components;
using Amatsukaze.ViewModels;
using Livet;
using Livet.EventListeners;
using System;
using System.Linq;
using System.Windows;

namespace Amatsukaze.Views
{
    /* 
	 * ViewModelからの変更通知などの各種イベントを受け取る場合は、PropertyChangedWeakEventListenerや
     * CollectionChangedWeakEventListenerを使うと便利です。独自イベントの場合はLivetWeakEventListenerが使用できます。
     * クローズ時などに、LivetCompositeDisposableに格納した各種イベントリスナをDisposeする事でイベントハンドラの開放が容易に行えます。
     *
     * WeakEventListenerなので明示的に開放せずともメモリリークは起こしませんが、できる限り明示的に開放するようにしましょう。
     */

    /// <summary>
    /// ServerWindow.xaml の相互作用ロジック
    /// </summary>
    public partial class ServerWindow : Window
    {
        private NotifyIconWrapper NotifyIcon;

        private LivetCompositeDisposable CompositeDisposable;

        public ServerWindow()
        {
            InitializeComponent();
            Utils.SetWindowProperties(this);
            NotifyIcon = new NotifyIconWrapper() { Window = this };
            CompositeDisposable = new LivetCompositeDisposable();

            var serverVM = DataContext as ServerViewModel;
            if(serverVM != null)
            {
                var modelListener = new PropertyChangedEventListener(serverVM);
                modelListener.Add(() => serverVM.WindowCaption, (_, __) => NotifyIcon.Text = serverVM.WindowCaption);
                CompositeDisposable.Add(modelListener);
            }
        }

        protected override void OnClosed(EventArgs e)
        {
            NotifyIcon?.Dispose();
            NotifyIcon = null;
            CompositeDisposable.Dispose();
            base.OnClosed(e);
        }

        private void MenuItem_Click(object sender, RoutedEventArgs e)
        {
            App.SetClipboardText(string.Join("\r\n",
                lst.SelectedItems.Cast<object>().Select(item => item.ToString())));
        }

        private void Window_StateChanged(object sender, EventArgs e)
        {
            switch (this.WindowState)
            {
                case WindowState.Maximized:
                    ShowInTaskbar = true;
                    break;
                case WindowState.Minimized:
                    ShowInTaskbar = false;
                    break;
                case WindowState.Normal:
                    ShowInTaskbar = true;
                    break;
            }
        }
    }
}

