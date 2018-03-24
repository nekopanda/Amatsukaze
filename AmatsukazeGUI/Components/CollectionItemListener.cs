using Livet.EventListeners;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Amatsukaze.Components
{
    public class CollectionItemListener<T>
    {
        private Action<T> _attaching;
        private Action<T> _detaching;
        private CollectionChangedEventListener _listener;

        public CollectionItemListener(ObservableCollection<T> source, Action<T> attaching, Action<T> detaching)
        {
            _attaching = attaching;
            _detaching = detaching;
            _listener = new CollectionChangedEventListener(source, OnSourceCollectionChanged);
        }

        private void OnSourceCollectionChanged(object sender, NotifyCollectionChangedEventArgs e)
        {
            switch (e.Action)
            {
                case NotifyCollectionChangedAction.Add:
                    for (int i = 0; i < e.NewItems.Count; i++)
                    {
                        _attaching((T)e.NewItems[i]);
                    }
                    break;

                case NotifyCollectionChangedAction.Remove:
                    for (int i = 0; i < e.OldItems.Count; i++)
                    {
                        _detaching((T)e.OldItems[i]);
                    }
                    break;

                case NotifyCollectionChangedAction.Replace:
                    // remove
                    for (int i = 0; i < e.OldItems.Count; i++)
                    {
                        _detaching((T)e.OldItems[i]);
                    }
                    // add
                    goto case NotifyCollectionChangedAction.Add;

                case NotifyCollectionChangedAction.Reset:
                    for (int i = 0; i < (e.OldItems?.Count ?? 0); i++)
                    {
                        _detaching((T)e.OldItems[i]);
                    }
                    for (int i = 0; i < (e.NewItems?.Count ?? 0); i++)
                    {
                        _attaching((T)e.NewItems[i]);
                    }
                    break;

                default:
                    break;
            }
        }
    }
}
