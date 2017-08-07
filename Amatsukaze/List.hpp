#pragma once

// コンテナじゃないただの双方向リスト

namespace av {

template <typename T>
struct ListNode {
  T value;
  ListNode* left;
  ListNode* right;
};

template <typename T>
class List
{
  template< class Ty >
  class itr : public std::iterator< std::bidirectional_iterator_tag,
    Ty, std::ptrdiff_t, Ty*, Ty& >
  {
    using base = std::iterator< std::bidirectional_iterator_tag,
      Ty, std::ptrdiff_t, Ty*, Ty& >;
    Ty* cur;
  public:
    using iterator_category = typename base::iterator_category;
    using value_type = typename base::value_type;
    using difference_type = typename base::difference_type;
    using pointer = typename base::pointer;
    using reference = typename base::reference;

    itr() : cur(nullptr) { }
    itr(Ty* cur) : cur(cur) { }
    itr(const itr& it) : cur(it.cur) { }
    itr& operator=(const itr& it) {
      cur = it.cur;
      return *this;
    }
    ~itr() noexcept { }

    reference operator*() const {
      return *cur;
    }
    pointer operator->() const {
      return cur;
    }
    itr& operator++() {
      cur = cur->right;
      return *this;
    }
    itr operator++(int) {
      itr tmp = *this;
      ++(*this);
      return tmp;
    }
    itr& operator--() {
      cur = cur->left;
      return *this;
    }
    itr operator--(int) {
      itr tmp = *this;
      --(*this);
      return tmp;
    }

    friend bool operator==(const itr& a, const itr& b) {
      return a.cur == b.cur;
    }
    friend bool operator!=(const itr& a, const itr& b) {
      return a.cur != b.cur;
    }
  };

  ListNode<T> root;
  size_t size_;

  void InsertBefore(ListNode<T>* pos, ListNode<T>* node) {
    node->left = pos->left;
    node->right = pos;
    pos->left->right = node;
    pos->left = node;
    ++size_;
  }

  void InsertAfter(ListNode<T>* pos, ListNode<T>* node) {
    node->right = pos->right;
    node->left = pos;
    pos->right->left = node;
    pos->right = node;
    ++size_;
  }

  void Remove(ListNode<T>* node) {
    node->left->right = node->right;
    node->right->left = node->left;
    node->right = node->left = nullptr;
    --size_;
  }

public:
  typedef ListNode<T> value_type;
  typedef ListNode<T>& reference_type;
  typedef const ListNode<T>& const_reference;
  typedef itr<ListNode<T>> iterator;
  typedef itr<const ListNode<T>> const_iterator;
  typedef size_t size_type;
  typedef std::ptrdiff_t difference_type;
  typedef std::reverse_iterator<iterator> reverse_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

  List() : root(), size_()
  {
    root.left = root.right = &root;
  }

  iterator begin() { return iterator(root.right); }
  const_iterator cbegin() const { return const_iterator(root.right); }
  iterator end() { return iterator(&root); }
  const_iterator cend() const { return const_iterator(&root); }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  const_reverse_iterator crbegin() const { return const_reverse_iterator(cend()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator crend() const { return const_reverse_iterator(cbegin()); }

  iterator it(ListNode<T>* node) { return iterator(node); }
  const_iterator cit(const ListNode<T>* node) const { return const_iterator(node); }

  bool empty() const { return size_ == 0; }

  size_t size() const { return size_; }

  void clear() {
    root.left = root.right = &root;
    size_ = 0;
  }

  void insert(iterator pos, ListNode<T>* node) {
    InsertBefore(pos->cur, node);
  }

  void push_back(ListNode<T>* node) {
    InsertBefore(&root, node);
  }

  void push_front(ListNode<T>* node) {
    InsertAfter(&root, node);
  }

  void pop_front() {
    assert(size_ > 0);
    Remove(root->right);
  }

  void pop_back() {
    assert(size_ > 0);
    Remove(root->left);
  }

  iterator erase(iterator pos) {
    assert(pos != end());
    value_type* node = &*pos;
    iterator it = ++pos;
    Remove(node);
    return it;
  }

  value_type& front() {
    assert(size_ > 0);
    return *root->right;
  }

  value_type& back() {
    assert(size_ > 0);
    return *root->left;
  }
};

} // namepsace av
