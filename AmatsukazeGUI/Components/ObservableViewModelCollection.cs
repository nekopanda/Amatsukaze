using Livet.EventListeners;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics.Contracts;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Data;

namespace Amatsukaze.Components
{
    public class ObservableViewModelCollection<TViewModel, TModel> : ObservableCollection<TViewModel>
    {
        private readonly INotifyCollectionChanged _source;
        private readonly Func<TModel, TViewModel> _viewModelFactory;
        private CollectionChangedEventListener _listener;

        public ObservableViewModelCollection(CollectionView source, Func<TModel, TViewModel> viewModelFactory)
            : base(source.Cast<TModel>().Select(model => viewModelFactory(model)))
        {
            _source = source;
            _viewModelFactory = viewModelFactory;
            _listener = new CollectionChangedEventListener(_source, OnSourceCollectionChanged);
        }

        public ObservableViewModelCollection(ObservableCollection<TModel> source, Func<TModel, TViewModel> viewModelFactory)
            : base(source.Select(model => viewModelFactory(model)))
        {
            _source = source;
            _viewModelFactory = viewModelFactory;
            _listener = new CollectionChangedEventListener(_source, OnSourceCollectionChanged);
        }

        protected virtual TViewModel CreateViewModel(TModel model)
        {
            return _viewModelFactory(model);
        }

        private void OnSourceCollectionChanged(object sender, NotifyCollectionChangedEventArgs e)
        {
            switch (e.Action)
            {
                case NotifyCollectionChangedAction.Add:
                    for (int i = 0; i < e.NewItems.Count; i++)
                    {
                        this.Insert(e.NewStartingIndex + i, CreateViewModel((TModel)e.NewItems[i]));
                    }
                    break;

                case NotifyCollectionChangedAction.Move:
                    if (e.OldItems.Count == 1)
                    {
                        this.Move(e.OldStartingIndex, e.NewStartingIndex);
                    }
                    else
                    {
                        List<TViewModel> items = this.Skip(e.OldStartingIndex).Take(e.OldItems.Count).ToList();
                        for (int i = 0; i < e.OldItems.Count; i++)
                        {
                            this.RemoveAt(e.OldStartingIndex);
                        }
                        for (int i = 0; i < items.Count; i++)
                        {
                            this.Insert(e.NewStartingIndex + i, items[i]);
                        }
                    }
                    break;

                case NotifyCollectionChangedAction.Remove:
                    for (int i = 0; i < e.OldItems.Count; i++)
                    {
                        this.RemoveAt(e.OldStartingIndex);
                    }
                    break;

                case NotifyCollectionChangedAction.Replace:
                    // remove
                    for (int i = 0; i < e.OldItems.Count; i++)
                    {
                        this.RemoveAt(e.OldStartingIndex);
                    }
                    // add
                    goto case NotifyCollectionChangedAction.Add;

                case NotifyCollectionChangedAction.Reset:
                    Clear();
                    for (int i = 0; i < (e.NewItems?.Count ?? 0); i++)
                    {
                        this.Add(CreateViewModel((TModel)e.NewItems[i]));
                    }
                    break;

                default:
                    break;
            }
        }
    }
}
