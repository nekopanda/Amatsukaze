using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interactivity;

namespace Amatsukaze.Components
{
    public class ListBoxAutoScrollBehavior : Behavior<ListBox>
    {
        public bool AutoScroll {
            get { return (bool)GetValue(AutoScrollProperty); }
            set { SetValue(AutoScrollProperty, value); }
        }

        // Using a DependencyProperty as the backing store for AutoScroll.  This enables animation, styling, binding, etc...
        public static readonly DependencyProperty AutoScrollProperty =
            DependencyProperty.Register("AutoScroll", typeof(bool), typeof(ListBoxAutoScrollBehavior), new PropertyMetadata(false));

        private INotifyCollectionChanged incc;

        protected override void OnAttached()
        {
            base.OnAttached();

            incc = AssociatedObject.ItemsSource as INotifyCollectionChanged;
            if(incc != null)
            {
                incc.CollectionChanged += Incc_CollectionChanged;
            }
            var itemsSourcePropertyDescriptor = TypeDescriptor.GetProperties(AssociatedObject)["ItemsSource"];
            itemsSourcePropertyDescriptor.AddValueChanged(AssociatedObject, ListBox_ItemsSourceChanged);
        }

        protected override void OnDetaching()
        {
            base.OnDetaching();

            if (incc != null)
            {
                incc.CollectionChanged -= Incc_CollectionChanged;
            }
            var itemsSourcePropertyDescriptor = TypeDescriptor.GetProperties(AssociatedObject)["ItemsSource"];
            itemsSourcePropertyDescriptor.RemoveValueChanged(AssociatedObject, ListBox_ItemsSourceChanged);
        }

        private void ListBox_ItemsSourceChanged(object sender, EventArgs e)
        {
            if (incc != null)
            {
                incc.CollectionChanged -= Incc_CollectionChanged;
            }
            incc = AssociatedObject.ItemsSource as INotifyCollectionChanged;
            if (incc != null)
            {
                incc.CollectionChanged += Incc_CollectionChanged;
            }
        }

        private void Incc_CollectionChanged(object sender, NotifyCollectionChangedEventArgs e)
        {
            if(e.Action == NotifyCollectionChangedAction.Add || e.Action == NotifyCollectionChangedAction.Replace)
            {
                if (AutoScroll)
                {
                    AssociatedObject.ScrollIntoView(e.NewItems[0]);
                    //AssociatedObject.SelectedItem = e.NewItems[0];
                }
            }
        }
    }
}
