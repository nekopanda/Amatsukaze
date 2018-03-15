

using System;
using System.Collections;
using System.Diagnostics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interactivity;

namespace Amatsukaze.Components
{
    class DragBehavior : Behavior<ListBox>
    {
        private object _draggedData;
        private int? _draggedItemIndex;
        private Point? _initialPosition;
        private InsertionAdorner _insertionAdorner;
        private DragContentAdorner _dragContentAdorner;
        private Point _mouseOffsetFromItem;

        private static bool MovedEnoughForDrag(Vector delta)
        {
            return Math.Abs(delta.X) > SystemParameters.MinimumHorizontalDragDistance
                   || Math.Abs(delta.Y) > SystemParameters.MinimumVerticalDragDistance;
        }

        protected override void OnAttached()
        {
            base.OnAttached();

            AssociatedObject.PreviewMouseLeftButtonDown += PreviewMouseLeftButtonDown;
            AssociatedObject.PreviewMouseMove += PreviewMouseMove;
            AssociatedObject.PreviewMouseUp += PreviewMouseUp;
            AssociatedObject.PreviewDrop += PreviewDrop;
            AssociatedObject.PreviewDragEnter += PreviewDragEnter;
            AssociatedObject.PreviewDragLeave += PreviewDragLeave;
            AssociatedObject.PreviewDragOver += PreviewDragOver;
        }

        protected override void OnDetaching()
        {
            base.OnDetaching();

            AssociatedObject.PreviewMouseLeftButtonDown -= PreviewMouseLeftButtonDown;
            AssociatedObject.PreviewMouseMove -= PreviewMouseMove;
            AssociatedObject.PreviewMouseUp -= PreviewMouseUp;
            AssociatedObject.PreviewDrop -= PreviewDrop;
            AssociatedObject.PreviewDragEnter -= PreviewDragEnter;
            AssociatedObject.PreviewDragLeave -= PreviewDragLeave;
            AssociatedObject.PreviewDragOver -= PreviewDragOver;
        }

        private void CleanUpData()
        {
            _initialPosition = null;
            _draggedData = null;
            _insertionAdorner?.Detach();
            _dragContentAdorner?.Detach();
            _insertionAdorner = null;
            _draggedItemIndex = null;
        }

        private void CreateInsertionAdorner(DependencyObject draggedItem, ItemsControl itemsControl)
        {
            var draggedOveredContainer = itemsControl.GetItemContainer(draggedItem);
            bool showInRight = false;
            if (draggedOveredContainer == null)
            {
                draggedOveredContainer = itemsControl.GetLastContainer();
                showInRight = true;
            }

            _insertionAdorner?.Detach();
            _insertionAdorner = new InsertionAdorner(draggedOveredContainer, showInRight);
        }

        private void DropItemAt(int? droppedItemIndex, ItemsControl itemsControl)
        {
            itemsControl.RemoveItem(_draggedData);

            if (droppedItemIndex != null)
            {
                droppedItemIndex -= droppedItemIndex > _draggedItemIndex ? 1 : 0;
                itemsControl.InsertItemAt((int)droppedItemIndex, _draggedData);
            }
            else
            {
                itemsControl.AddItem(_draggedData);
            }
        }

        private void CreanUpInsertionAdorner()
        {
            _insertionAdorner.Detach();
            _insertionAdorner = null;
        }

        private void PreviewMouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            var itemsControl = sender as ItemsControl;
            var draggedItem = e.OriginalSource as DependencyObject;

            if (itemsControl == null || draggedItem == null) {
                return;
            }

            _draggedData = (itemsControl.ContainerFromElement(draggedItem) as FrameworkElement)
                ?.DataContext;
            if (_draggedData == null) {
                return;
            }

            _initialPosition = AssociatedObject.PointToScreen(e.GetPosition(AssociatedObject));
            _mouseOffsetFromItem = itemsControl.PointToItem(draggedItem, _initialPosition.Value);
            _draggedItemIndex = ((itemsControl.ItemsSource as IList) ?? itemsControl.Items).IndexOf(_draggedData);
        }

        private void PreviewMouseMove(object sender, System.Windows.Input.MouseEventArgs e)
        {
            var itemsControl = sender as ItemsControl;

            if (_draggedData == null || _initialPosition == null || itemsControl == null)
            {
                return;
            }

            var currentPos = AssociatedObject.PointToScreen(e.GetPosition(AssociatedObject));
            if (!MovedEnoughForDrag((_initialPosition - currentPos).Value))
            {
                return;
            }

            _dragContentAdorner?.Detach();
            _dragContentAdorner = new DragContentAdorner(
                itemsControl, _draggedData, itemsControl.ItemTemplate, _mouseOffsetFromItem);
            _dragContentAdorner.SetScreenPosition(currentPos);

            DragDrop.DoDragDrop(itemsControl, _draggedData, DragDropEffects.Move);
            CleanUpData();
        }

        private void PreviewMouseUp(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            CleanUpData();
        }

        private void PreviewDrop(object sender, System.Windows.DragEventArgs e)
        {
            var itemsControl = sender as ItemsControl;

            if (_draggedData == null || _initialPosition == null || itemsControl == null)
            {
                return;
            }

            var dropTargetData = itemsControl.GetItemData(e.OriginalSource as DependencyObject);
            DropItemAt(itemsControl.GetItemIndex(dropTargetData), itemsControl);
        }

        private void PreviewDragEnter(object sender, System.Windows.DragEventArgs e)
        {
            var itemsControl = sender as ItemsControl;

            if (_draggedData == null || _initialPosition == null || itemsControl == null)
            {
                return;
            }

            CreateInsertionAdorner(e.OriginalSource as DependencyObject, itemsControl);
        }

        private void PreviewDragLeave(object sender, System.Windows.DragEventArgs e)
        {
            if (_insertionAdorner != null)
            {
                CreanUpInsertionAdorner();
            }
        }

        private void PreviewDragOver(object sender, System.Windows.DragEventArgs e)
        {
            var currentPos = AssociatedObject.PointToScreen(e.GetPosition(AssociatedObject));
            _dragContentAdorner?.SetScreenPosition(currentPos);
        }
    }
}
