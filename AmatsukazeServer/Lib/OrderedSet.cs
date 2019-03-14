using System;
using System.Collections;
using System.Collections.Generic;

namespace Amatsukaze.Lib
{
    // https://stackoverflow.com/questions/1552225/hashset-that-preserves-ordering から拝借
    public class OrderedSet<T> : ICollection<T> where T : IEquatable<T>
    {
        private readonly IDictionary<T, LinkedListNode<T>> m_Dictionary;
        private readonly LinkedList<T> m_LinkedList;

        public OrderedSet()
            : this(EqualityComparer<T>.Default)
        {
        }

        public OrderedSet(IEqualityComparer<T> comparer)
        {
            m_Dictionary = new Dictionary<T, LinkedListNode<T>>(comparer);
            m_LinkedList = new LinkedList<T>();
        }

        public int Count {
            get { return m_Dictionary.Count; }
        }

        public virtual bool IsReadOnly {
            get { return m_Dictionary.IsReadOnly; }
        }

        void ICollection<T>.Add(T item)
        {
            Add(item);
        }

        public bool Add(T item)
        {
            if (m_Dictionary.ContainsKey(item)) return false;
            LinkedListNode<T> node = m_LinkedList.AddLast(item);
            m_Dictionary.Add(item, node);
            return true;
        }

        public bool AddHistory(T item)
        {
            if (m_Dictionary.Count > 0 && m_LinkedList.First.Value.Equals(item)) return false;
            if (m_Dictionary.ContainsKey(item))
            {
                m_LinkedList.Remove(m_Dictionary[item]);
                m_Dictionary.Remove(item);
            }
            LinkedListNode<T> node = m_LinkedList.AddFirst(item);
            m_Dictionary.Add(item, node);
            return true;
        }

        public void Clear()
        {
            m_LinkedList.Clear();
            m_Dictionary.Clear();
        }

        public bool Remove(T item)
        {
            LinkedListNode<T> node;
            bool found = m_Dictionary.TryGetValue(item, out node);
            if (!found) return false;
            m_Dictionary.Remove(item);
            m_LinkedList.Remove(node);
            return true;
        }

        public IEnumerator<T> GetEnumerator()
        {
            return m_LinkedList.GetEnumerator();
        }

        IEnumerator IEnumerable.GetEnumerator()
        {
            return GetEnumerator();
        }

        public bool Contains(T item)
        {
            return m_Dictionary.ContainsKey(item);
        }

        public void CopyTo(T[] array, int arrayIndex)
        {
            m_LinkedList.CopyTo(array, arrayIndex);
        }
    }
}
