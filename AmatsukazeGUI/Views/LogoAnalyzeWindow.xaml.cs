using Amatsukaze.ViewModels;
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
    /// LogoAnalyzeWindow.xaml の相互作用ロジック
    /// </summary>
    public partial class LogoAnalyzeWindow : Window
    {
        public LogoAnalyzeWindow()
        {
            InitializeComponent();
        }

        private void image_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            var vm = DataContext as LogoAnalyzeViewModel;
            if (vm != null)
            { 
                vm.RectPosition = Mouse.GetPosition(image);
                vm.RectSize = new Size();
            }
        }

        private void image_MouseMove(object sender, MouseEventArgs e)
        {
            if (Mouse.LeftButton == MouseButtonState.Pressed)
            {
                var curPos = Mouse.GetPosition(image);
                var vm = DataContext as LogoAnalyzeViewModel;
                if (vm != null)
                {
                    var diff = curPos - vm.RectPosition;
                    vm.RectSize = new Size(Math.Max(0, diff.X), Math.Max(0, diff.Y));
                }
            }
        }

        private void image_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
        {
        }
    }
}