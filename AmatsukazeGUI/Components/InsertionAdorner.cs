using System.Windows;

namespace Amatsukaze.Components
{
    public class InsertionAdorner : ControlHostAdornerBase
    {
        private readonly InsertionCursor _insertionCursor;

        public InsertionAdorner(UIElement adornedElement, bool showInRightSide = false)
            : base(adornedElement)
        {
            _insertionCursor = new InsertionCursor();

            Host.Children.Add(_insertionCursor);
            _insertionCursor.SetValue(VerticalAlignmentProperty,
                showInRightSide? VerticalAlignment.Bottom : VerticalAlignment.Top);
            _insertionCursor.SetValue(HorizontalAlignmentProperty, HorizontalAlignment.Stretch);
        }
    }
}
