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
    /// FilterAutoVfrPanel.xaml の相互作用ロジック
    /// </summary>
    public partial class FilterAutoVfrPanel : UserControl
    {
        public FilterAutoVfrPanel()
        {
            InitializeComponent();
        }

        private void Hyperlink_RequestNavigate(object sender, RequestNavigateEventArgs e)
        {
            System.Diagnostics.Process.Start(e.Uri.ToString());
        }

        private void Hyperlink_PluginFolder(object sender, RequestNavigateEventArgs e)
        {
            var path = System.IO.Path.GetDirectoryName(typeof(FilterYadifPanel).Assembly.Location) + "\\plugins64";
            System.Diagnostics.Process.Start(path);
        }

        private void Hyperlink_Click(object sender, RoutedEventArgs e)
        {
            var link = (Hyperlink)sender;
            var text = string.Join("\r\n", link.Inlines.Where(r => r is Run).Cast<Run>().Select(r => r.Text));
            App.SetClipboardText(text);
        }
    }
}
