using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Amatsukaze.Components
{
    public class DragContentAdorner : ControlHostAdornerBase
    {
        private readonly ContentPresenter _contentPresenter;
        private TranslateTransform _translate;
        private Point _offset;

        public DragContentAdorner(UIElement adornedElement, object draggedData, DataTemplate dataTemplate, Point offset)
            : base(adornedElement)
        {
            _contentPresenter = new ContentPresenter
                                    {
                                        Content = draggedData,
                                        ContentTemplate = dataTemplate,
                                        Opacity = 0.7
                                    };

            _translate = new TranslateTransform {X = 0, Y = 0};
            _contentPresenter.RenderTransform = _translate;

            _offset = offset;

            Host.Children.Add(_contentPresenter);

           _contentPresenter.SetValue(HorizontalAlignmentProperty, HorizontalAlignment.Left);
           _contentPresenter.SetValue(VerticalAlignmentProperty, VerticalAlignment.Top);
        }

        public void SetScreenPosition(Point screenPosition)
        {
            var positionInControl = base.AdornedElement.PointFromScreen(screenPosition);
            _translate.X = positionInControl.X - _offset.X;
            _translate.Y = positionInControl.Y - _offset.Y;
            base.AdornerLayer.Update();

        }
    }
}
