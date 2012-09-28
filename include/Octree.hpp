#pragma once

#include "BoundingBox.hpp"
#include "MortonCoder.hpp"

#include <iostream>
#include <iomanip>

// Automatically derive !=, <=, >, and >= from a class's == and <
using namespace std::rel_ops;

//! Class for tree structure
template <typename Point>
class Octree
{
 public:
  // Type declarations
  typedef Point point_type;
  static_assert(point_type::dimension == 3, "Only 3D at the moment");

  //! The type of this tree
  typedef Octree<point_type> tree_type;

 private:
  // The Coder this tree is based on
  typedef MortonCoder<point_type> Coder;
  // Morton Coder
  Coder coder_;
  // Code type
  typedef typename Coder::code_type code_type;
  typedef unsigned uint;

  // Morton coded objects this Tree holds.
  std::vector<point_type> point_;
  std::vector<code_type> mc_;
  std::vector<unsigned> permute_;
  std::vector<unsigned> level_offset_;

  struct box_data {
    static constexpr unsigned leaf_bit = (1<<31);
    static constexpr unsigned max_marker_bit = (1<<30);

    //! key_ = leaf_bit 0* marker_bit morton_code
    unsigned key_;   // TODO: use level + leaf_bit instead of key
    unsigned parent_;
    // These can be either point offsets or box offsets depending on is_leaf
    unsigned child_begin_;
    unsigned child_end_;
    // TODO: body_begin_ and body_end_?

    box_data(unsigned key, unsigned parent=0,
             unsigned child_begin=0, unsigned child_end=0)
        : key_(key), parent_(parent),
          child_begin_(child_begin), child_end_(child_end) {
    }

    unsigned num_children() const {
      return child_end_ - child_begin_;
    }

    /** Gets the level from the key
     * TODO: optimize
     */
    unsigned level() const {
      constexpr static uint lookup[] = {0, 3, 0, 3, 4, 7, 0, 9,
                                        3, 4, 5, 6, 7, 8, 1, 10,
                                        2, 4, 6, 9, 5, 5, 8, 2,
                                        6, 9, 7, 2, 8, 1, 1, 10};
      unsigned v = key_ & ~leaf_bit;
      v |= v >> 1;
      v |= v >> 2;
      v |= v >> 4;
      v |= v >> 8;
      v |= v >> 16;
      return lookup[(v * 0X07C4ACDD) >> 27];
    }

    /** Returns the minimum possible Morton code in this box
     * TODO: this is a stupid way of doing this
     */
    code_type get_mc_lower_bound() const {
      code_type mc_mask = key_;
      while (!(mc_mask & max_marker_bit))
        mc_mask = mc_mask << 3;
      return mc_mask & ~max_marker_bit;
    }
    /** Returns the maximum possible Morton code in this box
     * TODO: this is a stupid way of doing this
     */
    code_type get_mc_upper_bound() const {
      code_type mc_mask = key_;
      while (!(mc_mask & max_marker_bit))
        mc_mask = (mc_mask << 3) | 7;
      return mc_mask & ~max_marker_bit;
    }

    void set_leaf(bool b) {
      if (b)
        key_ |= leaf_bit;
      else
        key_ &= ~leaf_bit;
    }

    bool is_leaf() const {
      return key_ & leaf_bit;
    }
  };

  std::vector<box_data> box_data_;

 public:
  // Predeclarations
  struct Body;
  struct Box;
  struct body_iterator;
  struct box_iterator;

  struct Body {
    /** Construct an invalid Body */
    Body()
        : tree_(NULL) {
    }

    Point point() const {
      return tree_->point_[idx_];
    }
    uint index() const {
      return idx_;
    }
    code_type morton_index() const {
      return tree_->mc_[idx_];
    }

   private:
    uint idx_;
    tree_type* tree_;
    Body(uint idx, tree_type* tree)
        : idx_(idx), tree_(tree) {
      assert(idx_ < tree_->size());
    }
    friend class Octree;
  };

  // A tree-aligned box
  struct Box {
    /** Construct an invalid Box */
    Box()
        : tree_(NULL) {
    }

    uint index() const {
      return idx_;
    }
    code_type morton_index() const {
      return data().key_;
    }
    uint level() const {
      return data().level();
    }
    uint num_children() const {
      return data().num_children();
    }
    bool is_leaf() const {
      return data().is_leaf();
    }
    // TODO: optimize
    point_type center() const {
      BoundingBox<point_type> bb = tree_->coder_.cell(data().get_mc_lower_bound());
      point_type p = bb.min();
      p += bb.dimensions() * (1 << (10-data().level()-1));
      return p;
    }

    /** The parent box of this box */
    Box parent() const {
      return Box(data().parent_, tree_);
    }

    /** The begin iterator to the Points contained in this box */
    body_iterator body_begin() const {
      if (is_leaf()) {
        return body_iterator(data().child_begin_, tree_);
      } else {
        unsigned body_begin_idx = data().child_begin_;
        while (!tree_->box_data_[body_begin_idx].is_leaf()) {
          body_begin_idx = tree_->box_data_[body_begin_idx].child_begin_;
        }
        return body_iterator(tree_->box_data_[body_begin_idx].child_begin_, tree_);
      }
    }
    /** The end iterator to the Points contained in this box */
    body_iterator body_end() const {
      if (is_leaf()) {
        return body_iterator(data().child_end_, tree_);
      } else {
        unsigned body_end_idx = data().child_end_ - 1;
        while (!tree_->box_data_[body_end_idx].is_leaf())
          body_end_idx = tree_->box_data_[body_end_idx].child_end_ - 1;
        return body_iterator(tree_->box_data_[body_end_idx].child_end_, tree_);
      }
    }

    /** The begin iterator to the child Boxes contained in this box */
    box_iterator child_begin() const {
      assert(!is_leaf());
      return box_iterator(data().child_begin_, tree_);
    }
    /** The end iterator to the child Boxes contained in this box */
    box_iterator child_end() const {
      assert(!is_leaf());
      return box_iterator(data().child_end_, tree_);
    }

   private:
    uint idx_;
    tree_type* tree_;
    Box(uint idx, tree_type* tree)
        : idx_(idx), tree_(tree) {
    }
    inline box_data& data() const {
      return tree_->box_data_[idx_];
    }
    friend class Octree;
  };

  /** @struct Tree::box_iterator
   * @brief Iterator class for Boxes in the tree
   * TODO: Use a Mutator to condense/clarify code
   */
  struct box_iterator {
    // These type definitions help us use STL's iterator_traits.
    /** Element type. */
    typedef Box value_type;
    /** Type of pointers to elements. */
    typedef Box* pointer;
    /** Type of references to elements. */
    typedef Box& reference;
    /** Iterator category. */
    typedef std::input_iterator_tag iterator_category;
    /** Iterator difference */
    typedef std::ptrdiff_t difference_type;
    /** Construct an invalid box_iterator */
    box_iterator() {
    }

    box_iterator& operator++() {
      ++idx_;
      return *this;
    }
    box_iterator& operator+(int n) {
      idx_ += n;
      return *this;
    }
    box_iterator& operator-(int n) {
      idx_ -= n;
      return *this;
    }
    Box operator*() const {
      return Box(idx_, tree_);
    }
    Box* operator->() const {
      placeholder_ = operator*();
      return &placeholder_;
    }
    bool operator==(const box_iterator& it) const {
      return tree_ == it.tree_ && idx_ == it.idx_;
    }

   private:
    uint idx_;
    tree_type* tree_;
    mutable Box placeholder_;
    box_iterator(uint idx, tree_type* tree)
        : idx_(idx), tree_(tree) {
    }
    box_iterator(Box b)
        : idx_(b.idx_), tree_(b.tree_) {
    }
    friend class Octree;
  };

  /** @struct Tree::body_iterator
   * @brief Iterator class for Bodies in the tree
   * TODO: Use a Mutator to condense/clarify code
   */
  struct body_iterator {
    // These type definitions help us use STL's iterator_traits.
    /** Element type. */
    typedef Body value_type;
    /** Type of pointers to elements. */
    typedef Body* pointer;
    /** Type of references to elements. */
    typedef Body& reference;
    /** Iterator category. */
    typedef std::input_iterator_tag iterator_category;
    /** Iterator difference */
    typedef std::ptrdiff_t difference_type;
    /** Construct an invalid iterator */
    body_iterator() {
    }

    body_iterator& operator++() {
      ++idx_;
      return *this;
    }
    body_iterator& operator+(int n) {
      idx_ += n;
      return *this;
    }
    body_iterator& operator-(int n) {
      idx_ -= n;
      return *this;
    }
    Body operator*() const {
      return Body(idx_, tree_);
    }
    Body* operator->() const {
      placeholder_ = operator*();
      return &placeholder_;
    }
    bool operator==(const body_iterator& it) const {
      return tree_ == it.tree_ && idx_ == it.idx_;
    }

   private:
    uint idx_;
    tree_type* tree_;
    mutable Body placeholder_;
    body_iterator(uint idx, tree_type* tree)
        :idx_(idx), tree_(tree) {
    }
    friend class Octree;
  };

  //! Construct an octree encompassing a bounding box
  Octree(const BoundingBox<Point>& bb)
      : coder_(bb) {
  }

  /** Return the Bounding Box that this Octree encompasses
   */
  BoundingBox<point_type> bounding_box() const {
    return coder_.bounding_box();
  }

  /** The number of points contained in this tree
   */
  inline uint size() const {
    return point_.size();
  }

  /** The number of points contained in this tree
   */
  inline uint bodies() const {
    return size();
  }

  /** The number of boxes contained in this tree
   */
  inline uint boxes() const {
    return box_data_.size();
  }

  /** The maximum level of any box in this tree
   */
  inline uint levels() const {
    return level_offset_.size() - 1;
  }

  template <typename IT>
  void construct_tree(IT begin, IT end) {
    // Create a code-idx pair vector
    std::vector<point_type> points_tmp;
    std::vector<std::pair<code_type, unsigned>> code_idx;
    unsigned idx = 0;
    for ( ; begin != end; ++begin, ++idx) {
      assert(coder_.bounding_box().contains(*begin));
      points_tmp.push_back(*begin);
      code_idx.push_back(std::make_pair(coder_.code(*begin), idx));
    }

    // TODO: Use radix or bucket sort for efficiency
    // or incrementally sort...
    std::sort(code_idx.begin(), code_idx.end());

    // Extract the code, permutation vector, and sorted point
    for (auto it = code_idx.begin(); it != code_idx.end(); ++it) {
      mc_.push_back(it->first);
      permute_.push_back(it->second);
      point_.push_back(points_tmp[permute_.back()]);
    }

    // Add the boxes (in a pretty dumb way...)
    unsigned NCRIT = 1;

    // Push the root box which contains all points
    box_data_.push_back( box_data(1, 0, 0, point_.size()) );
    level_offset_.push_back(0);

    // For every box that is created
    // TODO: Can do this in one scan through the morton codes...
    for (int k = 0; k != box_data_.size(); ++k) {

      if (box_data_[k].num_children() <= NCRIT) {
        box_data_[k].set_leaf(true);
      } else {
        // Get the key and interval
        code_type key_p = box_data_[k].key_;
        auto mc_begin = mc_.begin() + box_data_[k].child_begin_;
        auto mc_end = mc_.begin() + box_data_[k].child_end_;

        // Split this box - point offsets become box offsets
        box_data_[k].child_begin_ = box_data_.size();
        box_data_[k].child_end_ = box_data_.size();

        // For each octant
        for (int oct = 0; oct < 8; ++oct) {
          // Construct the new box key
          code_type key_c = (key_p << 3) | oct;

          // Construct a temporary child box
          box_data box_c(key_c, k);

          // Find the morton start and end of this child
          // TODO: Can do this MUCH better
          auto begin_c = std::lower_bound(mc_begin, mc_end, box_c.get_mc_lower_bound());
          auto end_c = std::upper_bound(mc_begin, mc_end, box_c.get_mc_upper_bound());

          // If this child contains points, add this child box
          if (end_c - begin_c > 0) {
            // Increment parent child offset
            ++box_data_[k].child_end_;

            // TODO: Optimize on key
            // If this is starting a new level, record it
            if (box_c.level() > box_data_[level_offset_.back()].level()) {
              std::cout << box_c.level() << " > " << box_data_[level_offset_.back()].level() << "\n";
              level_offset_.push_back(box_data_.size());
            }

            // Set the child body offsets
            box_c.child_begin_ = begin_c - mc_.begin();
            box_c.child_end_ = end_c - mc_.begin();

            // Add the child
            box_data_.push_back(box_c);
          }
        }
      }
    }

    level_offset_.push_back(box_data_.size());
  }

  /** Return the root box of this tree
   */
  Box root() {
    return Box(0, const_cast<tree_type*>(this));
  }

  /** Return an iterator to the first body in this tree */
  body_iterator body_begin() {
    return body_iterator(0, const_cast<tree_type*>(this));
  }
  /** Return an iterator one past the last body in this tree */
  body_iterator body_end() {
    return body_iterator(point_.size(), const_cast<tree_type*>(this));
  }
  /** Return an iterator to the first box in this tree */
  box_iterator box_begin() {
    return box_iterator(0, const_cast<tree_type*>(this));
  }
  /** Return an iterator one past the last box in this tree */
  box_iterator box_end() {
    return box_iterator(box_data_.size(), const_cast<tree_type*>(this));
  }
  /** Return an iterator to the first box at level L in this tree
   * @pre L < levels()
   */
  box_iterator box_begin(unsigned L) {
    assert(L < levels());
    return box_iterator(level_offset_[L], const_cast<tree_type*>(this));
  }
  /** Return an iterator one past the last box at level L in this tree
   * @pre L < levels()
   */
  box_iterator box_end(unsigned L) {
    assert(L < levels());
    return box_iterator(level_offset_[L+1], const_cast<tree_type*>(this));
  }
};





