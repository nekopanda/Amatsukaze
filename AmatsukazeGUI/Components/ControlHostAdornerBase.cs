using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;

namespace Amatsukaze.Components
{
    public class ControlHostAdornerBase : Adorner, IDisposable
    {
        private AdornerLayer _adornerLayer;
        protected Grid Host { get; set; }

        protected ControlHostAdornerBase(UIElement adornedElement)
            : base(adornedElement)
        {
            _adornerLayer = AdornerLayer.GetAdornerLayer(adornedElement);
            Host = new Grid();
            AdornerLayer.Add(this);
        }

        public void Detach()
        {
            AdornerLayer.Remove(this);
        }

        protected AdornerLayer AdornerLayer
        {
            get { return _adornerLayer; }
        }

        /// <summary>
        /// Override of VisualChildrenCount.
        /// Always return 1
        /// </summary>
        protected override int VisualChildrenCount
        {
            get { return 1; }
        }

        protected override Size MeasureOverride(Size constraint)
        {
            Host.Measure(constraint);
            return base.MeasureOverride(constraint);
        }

        protected override Size ArrangeOverride(Size finalSize)
        {
            Host.Arrange(new Rect(finalSize));
            return base.ArrangeOverride(finalSize);
        }

        protected override Visual GetVisualChild(int index)
        {
            if (VisualChildrenCount <= index)
            {
                throw new IndexOutOfRangeException();
            }
            return Host;
        }

        #region Dispose Pattern

        private bool _disposed;

        public void Dispose()
        {
            GC.SuppressFinalize(this);
            this.Dispose(true);
        }

        ~ControlHostAdornerBase()
        {
            this.Dispose(false);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (_disposed)
            {
                return;
            }
            _disposed = true;
            if (disposing)
            {
                Detach();
            }
        }

        #endregion

    }
}