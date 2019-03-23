using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using Amatsukaze.Components;

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
    /// MainWindow.xaml の相互作用ロジック
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            Utils.SetWindowProperties(this);
        }

        private void Window_SourceInitialized(object sender, EventArgs e)
        {
            (DataContext as ViewModels.MainWindowViewModel)?.Model?.RestoreWindowPlacement(this);
        }

        private void Window_Closing(object sender, System.ComponentModel.CancelEventArgs e)
        {
            var vm = (DataContext as ViewModels.MainWindowViewModel);
            if(vm != null && vm.Model.IsStandalone && vm.Model.IsRunning)
            {
                MessageBoxResult result = MessageBox.Show("エンコード中です。" +
                    "\r\n終了するとエンコード中の項目はすべてキャンセルされます。" +
                    "\r\n本当に終了しますか？", "Amatsukaze終了警告", MessageBoxButton.YesNo);
                if (result != MessageBoxResult.Yes)
                {
                    e.Cancel = true;
                }
            }

            if (e.Cancel == false)
            {
                (DataContext as ViewModels.MainWindowViewModel)?.Model?.SaveWindowPlacement(this);
            }
        }
    }
}
