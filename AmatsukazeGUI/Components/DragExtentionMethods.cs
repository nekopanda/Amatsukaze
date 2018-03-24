using System;
using System.Collections;
using System.Windows;
using System.Windows.Controls;
using Microsoft.CSharp.RuntimeBinder;

namespace Amatsukaze.Components.DragExtention
{
    public static class DragExtentionMethods
    {
        public static void AddItem(this ItemsControl itemsControl, object dataToBeAdded)
        {
            itemsControl.Operate(
                dataToBeAdded,
                (o, list) => list.Add(o));
        }

        public static FrameworkElement GetItemContainer(this ItemsControl itemsControl, DependencyObject item)
        {
            if (itemsControl == null || item == null)
            {
                return null;
            }
            return itemsControl.ContainerFromElement(item) as FrameworkElement;
        }

        public static object GetItemData(this ItemsControl itemsControl, DependencyObject item)
        {
            var data = itemsControl.GetItemContainer(item);
            return data == null ? null : data.DataContext;
        }

        public static int? GetItemIndex(this ItemsControl itemsControl, object item)
        {
            return itemsControl.Operate(
                item,
                (o, list) => list.Contains(o) ? new int?(list.IndexOf(o)) : null);
        }

        public static FrameworkElement GetLastContainer(this ItemsControl itemsControl)
        {
            return itemsControl.ItemContainerGenerator.ContainerFromIndex
                       (itemsControl.Items.Count - 1) as FrameworkElement;
        }

        public static Point PointToItem(this ItemsControl itemsControl, DependencyObject item, Point screenPos)
        {
            var itemContainer = itemsControl.GetItemContainer(item);
            return itemContainer == null ? new Point() : itemContainer.PointFromScreen(screenPos);
        }

        public static void InsertItemAt(this ItemsControl itemsControl, int droppedItemIndex, object dataToBeInserted)
        {
            itemsControl.Operate(
                dataToBeInserted,
                (o, list) => list.Insert(droppedItemIndex, o));
        }

        public static void RemoveItem(this ItemsControl itemsControl, object dataToBeRemoved)
        {
            itemsControl.Operate(
                dataToBeRemoved,
                (o, list) => list.Remove(o));
        }

        private static void Operate(this ItemsControl itemsControl, object operatedData,
                                    Action<object, IList> operationForItemsSource)
        {
            if (itemsControl.ItemsSource != null)
            {
                var itemsSourceList = itemsControl.ItemsSource as IList;
                if (itemsSourceList != null)
                {
                    operationForItemsSource(operatedData, itemsSourceList);
                }
                else
                {
                    try
                    {
                        operationForItemsSource((dynamic)operatedData, (dynamic)itemsControl.ItemsSource);
                    }
                    catch (MissingMethodException) { return; }
                    catch (RuntimeBinderException) { return; }
                }
            }
            else
            {
                operationForItemsSource(operatedData, itemsControl.Items);
            }
        }


        private static T Operate<T>(this ItemsControl itemsControl, object operatedData,
                                    Func<object, IList, T> operationForItemsSource)
        {
            if (itemsControl.ItemsSource != null)
            {
                var itemsSourceList = itemsControl.ItemsSource as IList;
                if (itemsSourceList != null)
                {
                    return operationForItemsSource(operatedData, itemsSourceList);
                }
                else
                {
                    try
                    {
                        return operationForItemsSource((dynamic) operatedData, (dynamic) itemsControl.ItemsSource);
                    }
                    catch (MissingMethodException)
                    {
                        return default(T);
                    }
                    catch (RuntimeBinderException)
                    {
                        return default(T);
                    }
                }
            }
            else
            {
                return operationForItemsSource(operatedData, itemsControl.Items);
            }
        }
    }
}