using Amatsukaze.ViewModels;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
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
    /// <summary>
    /// DiskFreeSpacePanel.xaml の相互作用ロジック
    /// </summary>
    public partial class DiskFreeSpacePanel : UserControl
    {
        public DiskFreeSpacePanel()
        {
            InitializeComponent();
        }

        private void Rectangle_SizeChanged(object sender, SizeChangedEventArgs e)
        {
            var rect = sender as Rectangle;
            var dc = rect.DataContext as DiskItemViewModel;
            dc.TotalWidth = rect.ActualWidth;
        }
    }
}
