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
    /// FilterKFMPanel.xaml の相互作用ロジック
    /// </summary>
    public partial class FilterKFMPanel : UserControl
    {
        public FilterKFMPanel()
        {
            InitializeComponent();
        }

        private void Hyperlink_RequestNavigate(object sender, RequestNavigateEventArgs e)
        {
            System.Diagnostics.Process.Start(e.Uri.ToString());
        }

        private void Hyperlink_PluginFolder(object sender, RequestNavigateEventArgs e)
        {
            var path = System.IO.Path.GetDirectoryName(typeof(FilterKFMPanel).Assembly.Location) + "\\plugins64";
            System.Diagnostics.Process.Start(path);
        }
    }
}
