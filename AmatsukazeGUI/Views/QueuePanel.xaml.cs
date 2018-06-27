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

            // コンテキストメニューの外の名前空間を見えるようにする
            NameScope.SetNameScope(queueMenu, NameScope.GetNameScope(this));
            NameScope.SetNameScope(buttonProfileMenu, NameScope.GetNameScope(this));
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
                    vm.FileDropped(e.Data.GetData(DataFormats.FileDrop) as string[], false);
                }
            }
        }

        private void UserControl_Loaded(object sender, RoutedEventArgs e)
        {
            var window = Window.GetWindow(this);
            window.KeyDown += HandleKeyEvent;
            window.KeyUp += HandleKeyEvent;
            queueMenu.KeyDown += HandleKeyEvent;
            queueMenu.KeyUp += HandleKeyEvent;
        }

        private void HandleKeyEvent(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.LeftShift || e.Key == Key.RightShift)
            {
                // Shiftキーだった
                if (e.IsDown)
                {
                    RemoveSelectedItemsButton.Content = "TSファイルも同時に削除";
                    RemoveCompletedButton.Content = "完了したアイテム+TSファイルを削除";
                    RemoveSelectedItemsMenuItem.Header = "TSファイルも同時に削除";
                    RemoveCompletedMenuItem.Header = "完了したアイテム+TSファイルを削除";
                }
                else
                {
                    RemoveSelectedItemsButton.Content = "削除";
                    RemoveCompletedButton.Content = "完了したアイテムを削除";
                    RemoveSelectedItemsMenuItem.Header = "削除";
                    RemoveCompletedMenuItem.Header = "完了したアイテムを削除";
                }
            }
        }

        private void queueMenu_ContextMenuClosing(object sender, ContextMenuEventArgs e)
        {
            RemoveSelectedItemsButton.Content = "削除";
            RemoveCompletedButton.Content = "完了したアイテムを削除";
            RemoveSelectedItemsMenuItem.Header = "削除";
            RemoveCompletedMenuItem.Header = "完了したアイテムを削除";
        }

        private void DockPanel_PreviewMouseDown(object sender, MouseButtonEventArgs e)
        {
            RemoveSelectedItemsButton.Content = "削除";
            RemoveCompletedButton.Content = "完了したアイテムを削除";
            RemoveSelectedItemsMenuItem.Header = "削除";
            RemoveCompletedMenuItem.Header = "完了したアイテムを削除";
        }
    }
}
