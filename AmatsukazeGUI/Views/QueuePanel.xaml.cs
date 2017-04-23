using Amatsukaze.Server;
using Amatsukaze.ViewModels;
using System;
using System.Collections.Generic;
using System.IO;
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
    /// QueuePanel.xaml の相互作用ロジック
    /// </summary>
    public partial class QueuePanel : UserControl
    {
        public QueuePanel()
        {
            InitializeComponent();
        }

        private void ListBox_PreviewDragOver(object sender, DragEventArgs e)
        {
            e.Handled = e.Data.GetDataPresent(DataFormats.FileDrop) ||
                        e.Data.GetDataPresent(DataFormats.Text);
        }

        private void ListBox_Drop(object sender, DragEventArgs e)
        {
            var vm = DataContext as QueueViewModel;
            if (vm != null)
            {
                if (e.Data.GetDataPresent(DataFormats.FileDrop))
                {
                    vm.FileDropped(e.Data.GetData(DataFormats.FileDrop) as string[]);
                }
                else if (e.Data.GetDataPresent(DataFormats.Text))
                {
                    vm.FileDropped(new string[] { (string)e.Data.GetData(DataFormats.Text) });
                }
            }
        }
    }
}
