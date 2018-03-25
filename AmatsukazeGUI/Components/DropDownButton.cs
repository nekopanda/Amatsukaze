using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Data;

namespace Amatsukaze.Components
{
    /// <summary>
    /// ドロップ ダウン メニューを表示する為のボタン コントロール クラスです。
    /// </summary>
    public sealed class DropDownMenuButton : ToggleButton
    {
        /// <summary>
        /// インスタンスを初期化します。
        /// </summary>
        public DropDownMenuButton()
        {
            var binding = new Binding("DropDownContextMenu.IsOpen") { Source = this };
            this.SetBinding(DropDownMenuButton.IsCheckedProperty, binding);
        }

        /// <summary>
        /// ドロップ ダウンとして表示するコンテキスト メニューを取得または設定します。
        /// </summary>
        public ContextMenu DropDownContextMenu {
            get {
                return this.GetValue(DropDownContextMenuProperty) as ContextMenu;
            }
            set {
                this.SetValue(DropDownContextMenuProperty, value);
            }
        }

        /// <summary>
        /// コントロールがクリックされた時のイベントです。
        /// </summary>
        protected override void OnClick()
        {
            if (this.DropDownContextMenu == null) { return; }

            this.DropDownContextMenu.PlacementTarget = this;
            this.DropDownContextMenu.Placement = PlacementMode.Bottom;
            this.DropDownContextMenu.IsOpen = !DropDownContextMenu.IsOpen;
        }

        /// <summary>
        /// ドロップ ダウンとして表示するメニューを表す依存プロパティです。
        /// </summary>
        public static readonly DependencyProperty DropDownContextMenuProperty = DependencyProperty.Register("DropDownContextMenu", typeof(ContextMenu), typeof(DropDownMenuButton), new UIPropertyMetadata(null));
    }
}
