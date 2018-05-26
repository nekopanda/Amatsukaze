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
    /// CheckLogPanel.xaml の相互作用ロジック
    /// </summary>
    public partial class CheckLogPanel : UserControl
    {
        public CheckLogPanel()
        {
            InitializeComponent();
        }

        private void ListView_MouseDoubleClick(object sender, MouseButtonEventArgs e)
        {
            var vm = DataContext as CheckLogViewModel;
            if (vm != null)
            {
                vm.GetLogFileOfCurrentSelectedItem();
            }
        }
    }
}
