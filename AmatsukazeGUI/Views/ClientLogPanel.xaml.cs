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
    /// ClientLogPanel.xaml の相互作用ロジック
    /// </summary>
    public partial class ClientLogPanel : UserControl
    {
        public ClientLogPanel()
        {
            InitializeComponent();
        }

        private void MenuItem_Click(object sender, RoutedEventArgs e)
        {
           App.SetClipboardText(string.Join("\r\n",
                lst.SelectedItems.Cast<object>().Select(item => item.ToString())));
        }
    }
}
