#pragma once

#include "b_plus_tree_page.h"
#include <algorithm>
#include <utility>

/**
 * BPlusTreeLeafPage stores ordered (KeyType, ValueType) pairs on a 4KB disk
 * page.
 *
 * Physical Layout on 4096-byte Page:
 * ---------------------------------------------------------------------------------------
 * | Base Header (24B) | next_page_id_ (4B) | prev_page_id_ (4B) | Array of
 * (Key, Value) |
 * ---------------------------------------------------------------------------------------
 * Total Header Size = 32 bytes.
 * Remaining space (4064 bytes) is dedicated to storing contiguous key/value
 * pairs.
 */
template <typename KeyType, typename ValueType, typename KeyComparator>
class BPlusTreeLeafPage : public BPlusTreePage {
public:
  using MappingType = std::pair<KeyType, ValueType>;

  static constexpr size_t LEAF_HEADER_SIZE =
      sizeof(BPlusTreePage) + sizeof(page_id_t) * 2;
  static constexpr size_t LEAF_PAGE_SLOT_CNT =
      (PAGE_SIZE - LEAF_HEADER_SIZE) / sizeof(MappingType);

  void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID,
            int max_size = LEAF_PAGE_SLOT_CNT) {
    SetPageType(BPlusTreePageType::LEAF_PAGE);
    SetSize(0);
    SetMaxSize(max_size);
    SetParentPageId(parent_id);
    SetPageId(page_id);
    next_page_id_ = INVALID_PAGE_ID;
    prev_page_id_ = INVALID_PAGE_ID;
  }

  inline page_id_t GetNextPageId() const { return next_page_id_; }
  inline void SetNextPageId(page_id_t next_page_id) {
    next_page_id_ = next_page_id;
  }

  inline page_id_t GetPrevPageId() const { return prev_page_id_; }
  inline void SetPrevPageId(page_id_t prev_page_id) {
    prev_page_id_ = prev_page_id;
  }

  inline KeyType KeyAt(int index) const { return array_[index].first; }
  inline ValueType ValueAt(int index) const { return array_[index].second; }
  inline const MappingType &GetItem(int index) const { return array_[index]; }

  inline void SetKeyAt(int index, const KeyType &key) {
    array_[index].first = key;
  }
  inline void SetValueAt(int index, const ValueType &value) {
    array_[index].second = value;
  }

  /**
   * TODO(student): Binary search to find the index of the first key >= search
   * key.
   *
   * Invariants:
   * - Returns an integer in range [0, GetSize()].
   * - If all existing keys are smaller than search key, return GetSize().
   * - Use comparator(a, b) which returns true if a < b.
   */
  int KeyIndex(const KeyType &key, const KeyComparator &comparator) const {
    int low = 0;
    int high = GetSize() - 1;
    int ans = GetSize();

    while (low <= high) {
      int mid = low + (high - low) / 2;
      // If array_[mid].first >= key (i.e. not array_[mid].first < key)
      if (!comparator(array_[mid].first, key)) {
        ans = mid;
        high = mid - 1; // Narrow search to the left half
      } else {
        low = mid + 1; // Key is strictly greater, search right half
      }
    }
    return ans;
  }

  /**
   * TODO(student): Point lookup: Find value associated with key.
   *
   * Steps:
   * 1. Use KeyIndex to locate the target index.
   * 2. If index < GetSize() and array_[idx].first == key
   *    (check strict weak ordering equivalence: !comparator(key,
   * arr[idx].first) && !comparator(arr[idx].first, key)), populate *result and
   * return true.
   * 3. Otherwise return false.
   */
  bool Lookup(const KeyType &key, ValueType *result,
              const KeyComparator &comparator) const {
    int idx = KeyIndex(key, comparator);
    if (idx < GetSize() && !comparator(key, array_[idx].first)) {
      if (result != nullptr) {
        *result = array_[idx].second;
      }
      return true;
    }
    return false;
  }

  /**
   * TODO(student): Insert a (key, value) pair in sorted order.
   *
   * Steps:
   * 1. Find insertion index via KeyIndex.
   * 2. Check for duplicates: if key exists, return -1 (unique index invariant).
   * 3. Shift elements to the right to make room at idx: array_[i] = array_[i -
   * 1].
   * 4. Set array_[idx] = {key, value} and IncreaseSize(1).
   * 5. Return updated GetSize().
   */
  int Insert(const KeyType &key, const ValueType &value,
             const KeyComparator &comparator) {
    int insertion_idx = KeyIndex(key, comparator);
    if (insertion_idx >= 0 && insertion_idx < GetSize() &&
        !comparator(key, array_[insertion_idx].first)) {
      return -1;
    }
    // shift operation o(n)
    for (int i = GetSize(); i > insertion_idx; i--) {
      array_[i] = array_[i - 1];
    }
    array_[insertion_idx] = {key, value};
    IncreaseSize(1);
    return GetSize();
  }

  /**
   * TODO(student): Remove the entry corresponding to key.
   *
   * Steps:
   * 1. Find key index.
   * 2. If key doesn't exist, return -1.
   * 3. Shift elements to the left to close the gap: array_[i] = array_[i + 1].
   * 4. IncreaseSize(-1).
   * 5. Return updated GetSize().
   */
  int RemoveAndDeleteRecord(const KeyType &key,
                            const KeyComparator &comparator) {
    int rmi = KeyIndex(key, comparator);
    if (rmi >= GetSize() || comparator(key, array_[rmi].first)) {
      return -1;
    }
    for (int i = rmi; i < GetSize(); i++) {
      array_[i] = array_[i + 1];
    }
    IncreaseSize(-1);
    return GetSize();
  }

  /**
   * TODO(student): Split this leaf page:
   *
   * Steps:
   * 1. recipient->Init(recipient_page_id, GetParentPageId(), GetMaxSize()).
   * 2. Calculate move_count = GetSize() / 2, start_idx = GetSize() -
   * move_count.
   * 3. Copy array_[start_idx ... ] into recipient->array_[0 ... ].
   * 4. Update sizes for both nodes:
   *    recipient->SetSize(move_count), SetSize(start_idx).
   * 5. Splice sibling pointers:
   *    - recipient->next = this->next
   *    - recipient->prev = this->page_id
   *    - this->next = recipient->page_id
   */
  void Split(page_id_t recipient_page_id, BPlusTreeLeafPage *recipient) {
    recipient->Init(recipient_page_id, GetParentPageId(), GetMaxSize());
    int move_count = GetSize() / 2;
    int start_idx = GetSize() - move_count;
    int j = 0;
    for (int i = start_idx; i < GetSize(); i++) {
      recipient->array_[j] = array_[i];
      j++;
    }
    recipient->SetSize(move_count);
    SetSize(start_idx);
    recipient->SetNextPageId(GetNextPageId());
    recipient->SetPrevPageId(GetPageId());
    SetNextPageId(recipient_page_id);
  }

  /**
   * Merge this right node entirely into left_sibling.
   *
   * Directional Invariant:
   * - `left_sibling` MUST be the LEFT sibling (recipient).
   * - `this` MUST be the RIGHT sibling (donor).
   * - Appends all items from `this` to the end of `left_sibling` (preserving ascending key order).
   * - Caller should delete `this` page after merging.
   */
  void MergeInto(BPlusTreeLeafPage *left_sibling) {
    int start_idx = left_sibling->GetSize();
    for (int i = 0; i < GetSize(); i++) {
      left_sibling->array_[start_idx + i] = array_[i];
    }
    left_sibling->IncreaseSize(GetSize());
    left_sibling->SetNextPageId(GetNextPageId());

    // Clear dead node state
    SetSize(0);
    SetNextPageId(INVALID_PAGE_ID);
    SetPrevPageId(INVALID_PAGE_ID);
  }

  /**
   * Lend this right node's first/smallest item to left_sibling (redistribution).
   *
   * Directional Invariant:
   * - `this` MUST be the RIGHT sibling (donor).
   * - `left_sibling` MUST be the LEFT sibling (receiver).
   */
  void LendFirstTo(BPlusTreeLeafPage *left_sibling) {
    left_sibling->array_[left_sibling->GetSize()] = array_[0];
    left_sibling->IncreaseSize(1);
    for (int i = 0; i < GetSize() - 1; i++) {
      array_[i] = array_[i + 1];
    }
    IncreaseSize(-1);
  }

  /**
   * Lend this left node's last/largest item to right_sibling (redistribution).
   *
   * Directional Invariant:
   * - `this` MUST be the LEFT sibling (donor).
   * - `right_sibling` MUST be the RIGHT sibling (receiver).
   */
  void LendLastTo(BPlusTreeLeafPage *right_sibling) {
    std::pair<KeyType, ValueType> last = array_[GetSize() - 1];
    for (int i = right_sibling->GetSize(); i > 0; i--) {
      right_sibling->array_[i] = right_sibling->array_[i - 1];
    }
    right_sibling->array_[0] = last;
    right_sibling->IncreaseSize(1);
    IncreaseSize(-1);
  }

private:
  page_id_t next_page_id_{INVALID_PAGE_ID}; // Pointer to right sibling leaf
  page_id_t prev_page_id_{INVALID_PAGE_ID}; // Pointer to left sibling leaf
  MappingType
      array_[LEAF_PAGE_SLOT_CNT]; // Slotted contiguous array of key-value pairs
};
