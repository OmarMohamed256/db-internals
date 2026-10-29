#pragma once

#include "b_plus_tree_page.h"
#include <algorithm>
#include <cstddef>
#include <utility>

/**
 * BPlusTreeInternalPage stores routing keys and page_id pointers to child
 * pages.
 *
 * Physical Layout on 4096-byte Page:
 * -------------------------------------------------------------------------------------------------
 * | Base Header (24B) | (DummyKey, Child0) | (Key1, Child1) | (Key2, Child2) |
 * ... | (KeyN, ChildN) |
 * -------------------------------------------------------------------------------------------------
 * Size invariant:
 * - size_ represents the number of valid child pointers (ValueAt(0) ...
 * ValueAt(size_ - 1)).
 * - KeyAt(0) is invalid/dummy.
 * - KeyAt(i) is the separator key between subtree ValueAt(i - 1) and subtree
 * ValueAt(i).
 */
template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTreeInternalPage : public BPlusTreePage {
public:
  using MappingType = std::pair<KeyType, ValueType>;

  static constexpr size_t INTERNAL_HEADER_SIZE = sizeof(BPlusTreePage);
  static constexpr size_t INTERNAL_PAGE_SLOT_CNT =
      (PAGE_SIZE - INTERNAL_HEADER_SIZE) / sizeof(MappingType);

  void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID,
            int max_size = INTERNAL_PAGE_SLOT_CNT) {
    SetPageType(BPlusTreePageType::INTERNAL_PAGE);
    SetSize(0);
    SetMaxSize(max_size);
    SetParentPageId(parent_id);
    SetPageId(page_id);
  }

  inline KeyType KeyAt(int index) const { return array_[index].first; }
  inline void SetKeyAt(int index, const KeyType &key) {
    array_[index].first = key;
  }

  inline ValueType ValueAt(int index) const { return array_[index].second; }
  inline void SetValueAt(int index, const ValueType &value) {
    array_[index].second = value;
  }

  /**
   * Find index of a given child pointer value.
   */
  int ValueIndex(const ValueType &value) const {
    for (int i = 0; i < GetSize(); ++i) {
      if (array_[i].second == value) {
        return i;
      }
    }
    return -1;
  }

  /**
   * Binary search to find which child page pointer to descend
   * into.
   *
   * Invariants:
   * - Keys are stored in array_[1 ... GetSize() - 1].
   * - Look for the largest key <= search key.
   * - If search key is strictly smaller than KeyAt(1), descend into child 0:
   * ValueAt(0).
   * - Return the selected child ValueType (page_id_t).
   */
  ValueType Lookup(const KeyType &key, const KeyComparator &comparator) const {
    int low = 1;
    int high = GetSize();
    while (low < high) {
      int mid = low + (high - low) / 2;
      if (comparator(key, KeyAt(mid))) {
        high = mid;
      } else {
        low = mid + 1;
      }
    }
    return ValueAt(low - 1);
  }

  /**
   * Populate a newly created root node with two children
   * separated by key.
   *
   * Invariants:
   * - array_[0].second = value0
   * - array_[1].first = key
   * - array_[1].second = value1
   * - SetSize(2)
   */
  void Populate(const ValueType &value0, const KeyType &key,
                const ValueType &value1) {
    array_[0].second = value0;
    array_[1].first = key;
    array_[1].second = value1;
    SetSize(2);
  }

  /**
   * Insert a new child pointer and separator key after an
   * existing child.
   */
  int InsertNodeAfter(const ValueType &old_value, const KeyType &new_key,
                      const ValueType &new_value) {
    int old_idx = ValueIndex(old_value);
    for (int i = GetSize(); i > old_idx; i--) {
      array_[i] = array_[i - 1];
    }
    array_[old_idx + 1] = {new_key, new_value};
    IncreaseSize(1);
    return GetSize();
  }

  /**
   * Split internal page, pushing the middle key up to the parent.
   */
  void Split(page_id_t recipient_page_id, BPlusTreeInternalPage *recipient,
             KeyType *split_key) {
    recipient->Init(recipient_page_id, GetParentPageId(), GetMaxSize());
    int split_idx = GetSize() / 2;
    *split_key = KeyAt(split_idx); // to be pushed into the parent
    int new_size = GetSize() - split_idx;

    for (int i = split_idx; i < GetSize(); i++) {
      int recipient_idx = i - split_idx;
      if (recipient_idx == 0) {
        recipient->array_[0].second = array_[i].second;
      } else {
        recipient->array_[recipient_idx] = array_[i];
      }
    }

    recipient->SetSize(new_size);
    SetSize(split_idx);
  }

  /**
   * Merge this right node entirely into left_sibling.
   *
   * Directional Invariant:
   * - `left_sibling` MUST be the LEFT sibling (recipient).
   * - `this` MUST be the RIGHT sibling (donor).
   * - `parent_separator` is the separator key from the parent separating left_sibling and this.
   * - Appends `parent_separator` + all items from `this` onto the end of `left_sibling`.
   * - Note: Caller (BPlusTree) is responsible for reparenting adopted children.
   * - Caller should delete `this` page after merging.
   */
  void MergeInto(BPlusTreeInternalPage *left_sibling, const KeyType &parent_separator) {
    int start_idx = left_sibling->GetSize();

    for (int i = 0; i < GetSize(); i++) {
      if (i == 0) {
        left_sibling->array_[start_idx] = {parent_separator, array_[0].second};
      } else {
        left_sibling->array_[start_idx + i] = array_[i];
      }
    }

    left_sibling->IncreaseSize(GetSize());
    SetSize(0);
  }

  /**
   * Lend this right node's first/smallest child to left_sibling (redistribution).
   *
   * Directional Invariant:
   * - `this` MUST be the RIGHT sibling (donor).
   * - `left_sibling` MUST be the LEFT sibling (receiver).
   * - Appends `parent_separator` + first child from `this` to the end of `left_sibling`.
   * - Returns the page_id of the moved child so caller (BPlusTree) can reparent it.
   */
  page_id_t LendFirstTo(BPlusTreeInternalPage *left_sibling,
                        const KeyType &parent_separator) {
    page_id_t moved_child = array_[0].second; // ch (3)
    left_sibling->array_[left_sibling->GetSize()] = {parent_separator, moved_child}; //
    left_sibling->IncreaseSize(1);

    for (int i = 0; i < GetSize() - 1; i++) {
      array_[i] = array_[i + 1];
    }
    IncreaseSize(-1);
    return moved_child;
  }

  /**
   * Lend this left node's last/largest child to right_sibling (redistribution).
   *
   * Directional Invariant:
   * - `this` MUST be the LEFT sibling (donor).
   * - `right_sibling` MUST be the RIGHT sibling (receiver).
   * - Prepends last child from `this` to right_sibling, using `parent_separator` as its separator.
   * - Returns the page_id of the moved child so caller (BPlusTree) can reparent it.
   */
  page_id_t LendLastTo(BPlusTreeInternalPage *right_sibling,
                       const KeyType &parent_separator) {
    page_id_t moved_child = array_[GetSize() - 1].second; // ch2

    for (int i = right_sibling->GetSize(); i > 0; i--) {
      right_sibling->array_[i] = right_sibling->array_[i - 1];
    }
    right_sibling->array_[1].first = parent_separator; // set receipeient key to parent seprator = 2
    right_sibling->array_[0] = {KeyType{}, moved_child}; // arr[0] = {Dummy, ch2}
    right_sibling->IncreaseSize(1);
    IncreaseSize(-1);
    return moved_child;
  }

  /**
   * Remove entry at index.
   *
   * Shift entries from index + 1 down to index. IncreaseSize(-1).
   */
  void Remove(int index) {
    if (index < 0 || index >= GetSize()) {
      return;
    }
    for (int i = index; i < GetSize() - 1; ++i) {
      array_[i] = array_[i + 1];
    }
    IncreaseSize(-1);
  }

private:
  MappingType array_[INTERNAL_PAGE_SLOT_CNT];
};
