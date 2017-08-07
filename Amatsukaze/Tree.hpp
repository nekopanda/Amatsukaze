#pragma once

#include <cassert>


// ÉRÉìÉeÉiÇ∂Ç·Ç»Ç¢ÇΩÇæÇÃê‘çïñÿ

namespace av {

template <typename K, typename V>
struct TreeNode {
  K key;
  V value;
  char red; /* if red=0 then the node is black */
  char isnil;
  TreeNode* left;
  TreeNode* right;
  TreeNode* parent;
};

template <typename K, typename V>
class Tree
{
  template< class Ty >
  class itr : public std::iterator< std::bidirectional_iterator_tag,
    Ty, std::ptrdiff_t, Ty*, Ty& >
  {
    using base = std::iterator< std::bidirectional_iterator_tag,
      Ty, std::ptrdiff_t, Ty*, Ty& >;
    const Tree* tree;
    Ty* cur;
  public:
    using iterator_category = typename base::iterator_category;
    using value_type = typename base::value_type;
    using difference_type = typename base::difference_type;
    using pointer = typename base::pointer;
    using reference = typename base::reference;

    itr() : tree(nullptr), cur(nullptr) { }
    itr(const Tree* tree, Ty* cur) : tree(tree), cur(cur) { }
    itr(const itr& it) : tree(it.tree), cur(it.cur) { }
    itr& operator=(const itr& it) {
      tree = it.tree;
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
      cur = tree->TreeSuccessor(cur);
      return *this;
    }
    itr operator++(int) {
      itr tmp = *this;
      ++(*this);
      return tmp;
    }
    itr& operator--() {
      cur = tree->TreePredecessor(cur);
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

  TreeNode<K, V> root;
  TreeNode<K, V> nil;
  size_t size_;

  void LeftRotate(TreeNode<K, V>* x) {
    TreeNode<K, V>* y;

    /*  I originally wrote this function to use the sentinel for */
    /*  nullptr to avoid checking for nullptr.  However this introduces a */
    /*  very subtle bug because sometimes this function modifies */
    /*  the parent pointer of nullptr.  This can be a problem if a */
    /*  function which calls LeftRotate also uses the nullptr sentinel */
    /*  and expects the nullptr sentinel's parent pointer to be unchanged */
    /*  after calling this function.  For example, when RBDeleteFixUP */
    /*  calls LeftRotate it expects the parent pointer of nullptr to be */
    /*  unchanged. */

    y = x->right;
    x->right = y->left;

    if (y->left != &nil) y->left->parent = x; /* used to use sentinel here */
                                              /* and do an unconditional assignment instead of testing for nullptr */

    y->parent = x->parent;

    /* instead of checking if x->parent is the root as in the book, we */
    /* count on the root sentinel to implicitly take care of this case */
    if (x == x->parent->left) {
      x->parent->left = y;
    }
    else {
      x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
  }

  void RightRotate(TreeNode<K, V>* y) {
    TreeNode<K, V>* x;

    /*  I originally wrote this function to use the sentinel for */
    /*  nullptr to avoid checking for nullptr.  However this introduces a */
    /*  very subtle bug because sometimes this function modifies */
    /*  the parent pointer of nullptr.  This can be a problem if a */
    /*  function which calls LeftRotate also uses the nullptr sentinel */
    /*  and expects the nullptr sentinel's parent pointer to be unchanged */
    /*  after calling this function.  For example, when RBDeleteFixUP */
    /*  calls LeftRotate it expects the parent pointer of nullptr to be */
    /*  unchanged. */

    x = y->left;
    y->left = x->right;

    if (&nil != x->right)  x->right->parent = y; /*used to use sentinel here */
                                                 /* and do an unconditional assignment instead of testing for nullptr */

                                                 /* instead of checking if x->parent is the root as in the book, we */
                                                 /* count on the root sentinel to implicitly take care of this case */
    x->parent = y->parent;
    if (y == y->parent->left) {
      y->parent->left = x;
    }
    else {
      y->parent->right = x;
    }
    x->right = y;
    y->parent = x;
  }

  void TreeInsertHelp(TreeNode<K, V>* z) {
    /*  This function should only be called by InsertRBTree (see above) */

    z->left = z->right = &nil;
    TreeNode<K, V>* y = &root;
    TreeNode<K, V>* x = root.left;
    while (x != &nil) {
      y = x;
      if(z->key < x->key) { /* x.key > z.key */
        x = x->left;
      }
      else { /* x,key <= z.key */
        x = x->right;
      }
    }
    z->parent = y;
    if ((y == &root) ||
      (z->key < y->key)) { /* y.key > z.key */
      y->left = z;
    }
    else {
      y->right = z;
    }

    ++size_;
  }

  void RBTreeInsert(TreeNode<K, V>* x)
  {
    TreeInsertHelp(x);
    x->red = 1;
    while (x->parent->red) { /* use sentinel instead of checking for root */
      if (x->parent == x->parent->parent->left) {
        TreeNode<K, V>* y = x->parent->parent->right;
        if (y->red) {
          x->parent->red = 0;
          y->red = 0;
          x->parent->parent->red = 1;
          x = x->parent->parent;
        }
        else {
          if (x == x->parent->right) {
            x = x->parent;
            LeftRotate(x);
          }
          x->parent->red = 0;
          x->parent->parent->red = 1;
          RightRotate(x->parent->parent);
        }
      }
      else { /* case for x->parent == x->parent->parent->right */
        TreeNode<K, V>* y = x->parent->parent->left;
        if (y->red) {
          x->parent->red = 0;
          y->red = 0;
          x->parent->parent->red = 1;
          x = x->parent->parent;
        }
        else {
          if (x == x->parent->left) {
            x = x->parent;
            RightRotate(x);
          }
          x->parent->red = 0;
          x->parent->parent->red = 1;
          LeftRotate(x->parent->parent);
        }
      }
    }
    root.left->red = 0;

    assert(!root.red);
  }

  TreeNode<K, V>* TreeSuccessor(const TreeNode<K, V>* x) const
  {
    if (x == &root) {
      throw std::runtime_error("end iterator cannnot be incremented");
    }

    TreeNode<K, V>* y = (x == &nil) ? &root : x->right;

    if (&nil != y) { /* assignment to y is intentional */
      while (y->left != &nil) { /* returns the minium of the right subtree of x */
        y = y->left;
      }
      return(y);
    }
    else {
      y = x->parent;
      while (x == y->right) { /* sentinel used instead of checking for nullptr */
        x = y;
        y = y->parent;
      }
      return y;
    }
  }

  TreeNode<K, V>* TreePredecessor(const TreeNode<K, V>* x) const
  {
    if (x == &nil) {
      throw std::runtime_error("rend iterator cannnot be decremented");
    }

    TreeNode<K, V>* y = x->left;

    if (&nil != y) { /* assignment to y is intentional */
      while (y->right != &nil) { /* returns the maximum of the left subtree of x */
        y = y->right;
      }
      return y;
    }
    else {
      y = x->parent;
      while (x == y->left) {
        if (y == &root) return &nil;
        x = y;
        y = y->parent;
      }
      return y;
    }
  }

  TreeNode<K, V>* RBLowerBound(const K& q) const {
    TreeNode<K, V>* x = root.left;
    TreeNode<K, V>* y = &root;
    while (x != &nil) {/*assignemnt*/
      if (x->key < q) {
        x = x->right;
      }
      else {
        y = x;
        x = x->left;
      }
    }
    return y;
  }

  TreeNode<K, V>* RBUpperBound(const K& q) const {
    TreeNode<K, V>* x = root.left;
    TreeNode<K, V>* y = &root;
    while (x != &nil) {/*assignemnt*/
      if (q < x->key) {
        y = x;
        x = x->left;
      }
      else {
        x = x->right;
      }
    }
    return y;
  }

  TreeNode<K, V>* RBExactQuery(const K& q) const {
    TreeNode<K, V>* r = RBLowerBound(q);
    if (r->key != q) return &root;
    return r;
  }

  void RBDeleteFixUp(TreeNode<K, V>* x) {
    TreeNode<K, V>* root = this->root.left;
    TreeNode<K, V>* w;

    while ((!x->red) && (root != x)) {
      if (x == x->parent->left) {
        w = x->parent->right;
        if (w->red) {
          w->red = 0;
          x->parent->red = 1;
          LeftRotate(x->parent);
          w = x->parent->right;
        }
        if ((!w->right->red) && (!w->left->red)) {
          w->red = 1;
          x = x->parent;
        }
        else {
          if (!w->right->red) {
            w->left->red = 0;
            w->red = 1;
            RightRotate(w);
            w = x->parent->right;
          }
          w->red = x->parent->red;
          x->parent->red = 0;
          w->right->red = 0;
          LeftRotate(x->parent);
          x = root; /* this is to exit while loop */
        }
      }
      else { /* the code below is has left and right switched from above */
        w = x->parent->left;
        if (w->red) {
          w->red = 0;
          x->parent->red = 1;
          RightRotate(x->parent);
          w = x->parent->left;
        }
        if ((!w->right->red) && (!w->left->red)) {
          w->red = 1;
          x = x->parent;
        }
        else {
          if (!w->left->red) {
            w->right->red = 0;
            w->red = 1;
            LeftRotate(w);
            w = x->parent->left;
          }
          w->red = x->parent->red;
          x->parent->red = 0;
          w->left->red = 0;
          RightRotate(x->parent);
          x = root; /* this is to exit while loop */
        }
      }
    }
    x->red = 0;
  }

  void RBDelete(TreeNode<K, V>* z) {
    TreeNode<K, V>* y;
    TreeNode<K, V>* x;

    y = ((z->left == &nil) || (z->right == &nil)) ? z : TreeSuccessor(z);
    x = (y->left == &nil) ? y->right : y->left;
    if (&root == (x->parent = y->parent)) { /* assignment of y->p to x->p is intentional */
      root.left = x;
    }
    else {
      if (y == y->parent->left) {
        y->parent->left = x;
      }
      else {
        y->parent->right = x;
      }
    }
    if (y != z) { /* y should not be nullptr in this case */

                  /* y is the node to splice out and x is its child */

      if (!(y->red)) RBDeleteFixUp(x);

      y->left = z->left;
      y->right = z->right;
      y->parent = z->parent;
      y->red = z->red;
      z->left->parent = z->right->parent = y;
      if (z == z->parent->left) {
        z->parent->left = y;
      }
      else {
        z->parent->right = y;
      }
    }
    else {
      if (!(y->red)) RBDeleteFixUp(x);
    }

    --size_;
  }

public:
  typedef K key_type;
  typedef TreeNode<K, V> value_type;
  typedef V mapped_type;
  typedef TreeNode<K, V>& reference;
  typedef const TreeNode<K, V>& const_reference;
  typedef itr<TreeNode<K, V>> iterator;
  typedef itr<const TreeNode<K, V>> const_iterator;
  typedef size_t size_type;
  typedef std::ptrdiff_t difference_type;
  typedef TreeNode<K, V>* pointer;
  typedef const TreeNode<K, V> const_pointer;
  typedef std::reverse_iterator<itr<TreeNode<K, V>>> reverse_iterator;
  typedef std::reverse_iterator<itr<const TreeNode<K, V>>> const_reverse_iterator;

  Tree()
    : root()
    , nil()
    , size_(0)
  {
    /*  see the comment in the Tree structure in red_black_tree.h */
    /*  for information on nullptr and root */
    nil.isnil = 1;
    nil.parent = nil.left = nil.right = &nil;
    nil.red = 0;
    root.parent = root.left = root.right = &nil;
    root.red = 0;
  }

  iterator begin() { return iterator(this, TreeSuccessor(&nil)); }
  const_iterator cbegin() const { return const_iterator(this, TreeSuccessor(&nil)); }
  iterator end() { return iterator(this, &root); }
  const_iterator cend() const { return const_iterator(this, &root); }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  const_reverse_iterator crbegin() const { return const_reverse_iterator(cend()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator crend() const { return const_reverse_iterator(cbegin()); }

  iterator it(value_type* node) { return iterator(this, node); }
  const_iterator cit(const value_type* node) const { return const_iterator(this, node); }

  bool empty() const { return size_ == 0; }

  size_t size() const { return size_; }

  void clear() {
    root.left = &nil;
    size_ = 0;
  }

  iterator find(const K& key) {
    return iterator(this, RBExactQuery(key));
  }

  const_iterator find(const K& key) const {
    return const_iterator(this, RBExactQuery(key));
  }

  iterator lower_bound(const K& key) {
    return iterator(this, RBLowerBound(key));
  }

  const_iterator lower_bound(const K& key) const {
    return const_iterator(this, RBLowerBound(key));
  }

  iterator upper_bound(const K& key) {
    return iterator(this, RBUpperBound(key));
  }

  const_iterator upper_bound(const K& key) const {
    return const_iterator(this, RBUpperBound(key));
  }

  void insert(value_type* node) {
    RBTreeInsert(node);
  }

  iterator erase(iterator pos) {
    value_type* node = &*pos;
    iterator it = ++pos;
    RBDelete(node);
    return it;
  }

  mapped_type& operator[](const K& key) {
    value_type* ret = RBExactQuery(key);
    if (ret == &root) {
      throw std::runtime_error("key not found ...");
    }
    return ret->value;
  }

private:
  Tree(const Tree& other) {}
  Tree& operator=(Tree& other) = delete;
};

} // namespace av
