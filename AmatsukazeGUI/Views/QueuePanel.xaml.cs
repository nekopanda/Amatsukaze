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

        private void AddQueue(string dirPath)
        {
            var vm = DataContext as QueueViewModel;
            if (vm != null)
            {
                vm.Model.Server.AddQueue(dirPath);
            }
        }

        private void ListBox_Drop(object sender, DragEventArgs e)
        {
            if (e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                string[] files = e.Data.GetData(DataFormats.FileDrop) as string[];
                if (files != null)
                {
                    foreach (var path in files)
                    {
                        if (Directory.Exists(path))
                        {
                            AddQueue(path);
                        }
                    }
                }
            }
            else if(e.Data.GetDataPresent(DataFormats.Text))
            {
                var str = (string)e.Data.GetData(DataFormats.Text);
                AddQueue(str);
            }
        }
    }
}
