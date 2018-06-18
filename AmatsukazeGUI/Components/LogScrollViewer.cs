using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;

namespace Amatsukaze.Components
{
    /// <summary>
    /// 以下の拡張をしたScrollViewer
    /// - 下端までスクロールすると自動で下端に張り付く
    /// - VerticalOffsetを保持
    /// 2つの機能を有効にするにはAutoScrollとBindableVerticalOffsetをViewModelにTwoWayバインドする必要がある
    /// </summary>
    class LogScrollViewer : ScrollViewer
    {
        public bool AutoScroll {
            get { return (bool)GetValue(AutoScrollProperty); }
            set { SetValue(AutoScrollProperty, value); }
        }

        // Using a DependencyProperty as the backing store for AutoScroll.  This enables animation, styling, binding, etc...
        public static readonly DependencyProperty AutoScrollProperty =
            DependencyProperty.Register("AutoScroll", typeof(bool), typeof(LogScrollViewer), 
                new PropertyMetadata(false, AutoScrollChanged));

        private void autoScrollChanged(bool newValue)
        {
            if(newValue)
            {
                ScrollToVerticalOffset(ScrollableHeight);
            }
        }

        private static void AutoScrollChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            (d as LogScrollViewer).autoScrollChanged((bool)e.NewValue);
        }

        public double BindableVerticalOffset {
            get { return (double)GetValue(BindableVerticalOffsetProperty); }
            set { SetValue(BindableVerticalOffsetProperty, value); }
        }

        // Using a DependencyProperty as the backing store for BindableVerticalOffset.  This enables animation, styling, binding, etc...
        public static readonly DependencyProperty BindableVerticalOffsetProperty =
            DependencyProperty.Register("BindableVerticalOffset", typeof(double), typeof(LogScrollViewer),
                new PropertyMetadata(0.0, BindableVerticalOffsetChanged));

        private void bindableVerticalOffsetChanged(double newValue)
        {
            if(VerticalOffset != newValue)
            {
                ScrollToVerticalOffset(newValue);
            }
        }

        private static void BindableVerticalOffsetChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            (d as LogScrollViewer).bindableVerticalOffsetChanged((double)e.NewValue);
        }

        protected override void OnScrollChanged(ScrollChangedEventArgs e)
        {
            base.OnScrollChanged(e);

            // スクロールできる高さがあるときだけ
            if (ScrollableHeight > 0)
            {
                if (e.ExtentHeightChange == 0)
                {
                    if (VerticalOffset == ScrollableHeight)
                    {
                        AutoScroll = true;
                    }
                    else
                    {
                        AutoScroll = false;
                    }
                    BindableVerticalOffset = VerticalOffset;
                }

                if (AutoScroll && e.ExtentHeightChange != 0)
                {
                    ScrollToVerticalOffset(ScrollableHeight);
                }
                else if (VerticalOffset != BindableVerticalOffset)
                {
                    ScrollToVerticalOffset(BindableVerticalOffset);
                }
            }
        }
    }
}
