#pragma once

#include <deque>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

#include "../buffer_pool_manager_skeleton/buffer_pool_manager.h"
#include "b_plus_tree_internal_page.h"
#include "b_plus_tree_iterator.h"
#include "b_plus_tree_leaf_page.h"
#include "b_plus_tree_page.h"

enum class OperationType { SEARCH, INSERT, DELETE, OPTIMISTIC_INSERT, OPTIMISTIC_DELETE };

/**
 * Context maintains the set of latched and pinned pages during tree traversal.
 * Essential for Phase 3: Latch Crabbing / Coupling.
 */
struct Context {
  std::deque<Page *> write_set_;
  std::deque<Page *> read_set_;
  bool root_is_latched_{false};

  void Clear(BufferPoolManager *bpm, bool is_dirty = false) {
    for (auto *page : write_set_) {
      page->WUnlatch();
      bpm->UnpinPage(page->GetPageId(), is_dirty);
    }
    write_set_.clear();

    for (auto *page : read_set_) {
      page->RUnlatch();
      bpm->UnpinPage(page->GetPageId(), false);
    }
    read_set_.clear();
  }
};

/**
 * BPlusTree is an on-disk balanced tree index layered on top of
 * BufferPoolManager.
 */
template <typename KeyType, typename ValueType,
          typename KeyComparator = std::less<KeyType>>
class BPlusTree {
public:
  using InternalPage = BPlusTreeInternalPage<KeyType, page_id_t, KeyComparator>;
  using LeafPage = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;
  using Iterator = BPlusTreeIterator<KeyType, ValueType, KeyComparator>;

  explicit BPlusTree(
      std::string name, BufferPoolManager *bpm,
      const KeyComparator &comparator = KeyComparator(),
      int leaf_max_size = LeafPage::LEAF_PAGE_SLOT_CNT,
      int internal_max_size = InternalPage::INTERNAL_PAGE_SLOT_CNT)
      : index_name_(std::move(name)), bpm_(bpm), comparator_(comparator),
        leaf_max_size_(leaf_max_size), internal_max_size_(internal_max_size) {}

  inline bool IsEmpty() const { return root_page_id_ == INVALID_PAGE_ID; }
  inline page_id_t GetRootPageId() const { return root_page_id_; }

  /**
   * TODO(student): Point query: Search for a single key.
   *
   * Steps:
   * 1. If IsEmpty(), return false.
   * 2. Call FindLeafPage(key) to retrieve target leaf page.
   * 3. Cast page data to LeafPage*, call leaf->Lookup(key, &val, comparator_).
   * 4. If found and result != nullptr, append val to result.
   * 5. Unpin leaf page (is_dirty = false).
   * 6. Return true if key found, false otherwise.
   */
  bool GetValue(const KeyType &key, std::vector<ValueType> *result,
                Context *ctx = nullptr) {
    if (IsEmpty()) {
      return false;
    }
    // read latched leaf
    Page *leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
      leaf_page->RUnlatch();
      return false;
    }
    auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());
    ValueType val;
    bool found = leaf->Lookup(key, &val, comparator_);
    if (found && result != nullptr) {
      result->push_back(val);
    }
    leaf_page->RUnlatch();
    bpm_->UnpinPage(leaf->GetPageId(), false);
    return found;
  }

  /**
   * TODO(student): Insert a (key, value) pair into the index.
   *
   * Steps:
   * 1. If IsEmpty(), call StartNewTree(key, value) and return true.
   * 2. Otherwise call InsertIntoLeaf(key, value).
   * 3. Return true on success, false if duplicate key.
   */
  bool Insert(const KeyType &key, const ValueType &value,
              Context *ctx = nullptr) {
    Context local_ctx;
    if (ctx == nullptr) {
      ctx = &local_ctx;
    }

    if (IsEmpty()) {
      return StartNewTree(key, value);
    }

    // -------------------------------------------------------------
    // 1. OPTIMISTIC PASS (Safe insert without ancestor write locks)
    // -------------------------------------------------------------
    Page *leaf_page = FindLeafPage(key, OperationType::OPTIMISTIC_INSERT, ctx);
    if (leaf_page != nullptr) {
      auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());

      // Safe from split: leaf has space
      if (leaf->GetSize() < leaf->GetMaxSize() - 1) {
        int new_size = leaf->Insert(key, value, comparator_);
        bool success = (new_size != -1);
        ctx->Clear(bpm_, success);
        return success;
      }

      // Leaf is full (might split) -> abort optimistic pass
      ctx->Clear(bpm_, false);
    }

    // -------------------------------------------------------------
    // 2. PESSIMISTIC PASS (Fallback for splits)
    // -------------------------------------------------------------
    return InsertIntoLeaf(key, value, ctx);
  }

  /**
   * TODO(student): Remove a key from the index.
   *
   * Steps:
   * 1. If IsEmpty(), return.
   * 2. Find leaf page.
   * 3. Call leaf->RemoveAndDeleteRecord(key, comparator_).
   * 4. If key wasn't found, unpin leaf (false) and return.
   * 5. If leaf is root:
   *    - If size == 0, delete root page from bpm, set root_page_id_ =
   * INVALID_PAGE_ID.
   *    - Unpin leaf (true) and return.
   * 6. If leaf size < leaf->GetMinSize(), call
   * HandleUnderflow(leaf_page).
   * 7. Unpin leaf (true).
   */
  void Remove(const KeyType &key, Context *ctx = nullptr) {
    Context local_ctx;
    if (ctx == nullptr) {
      ctx = &local_ctx;
    }

    if (IsEmpty()) {
      return;
    }

    // -------------------------------------------------------------
    // 1. OPTIMISTIC PASS (Safe delete without ancestor write locks)
    // -------------------------------------------------------------
    Page *leaf_page = FindLeafPage(key, OperationType::OPTIMISTIC_DELETE, ctx);
    if (leaf_page != nullptr) {
      auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());

      // Safe from underflow or root collapse
      if (leaf->GetSize() > leaf->GetMinSize()) {
        int res = leaf->RemoveAndDeleteRecord(key, comparator_);
        bool found = (res != -1);
        ctx->Clear(bpm_, found);
        return;
      }

      // Unsafe (might underflow, merge, or collapse root) -> abort optimistic pass
      ctx->Clear(bpm_, false);
    }

    // -------------------------------------------------------------
    // 2. PESSIMISTIC PASS (Fallback for underflow, merge, and collapse)
    // -------------------------------------------------------------
    leaf_page = FindLeafPage(key, OperationType::DELETE, ctx);
    if (leaf_page == nullptr) {
      if (ctx->root_is_latched_) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
      }
      ctx->Clear(bpm_, false);
      return;
    }

    auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());
    int res = leaf->RemoveAndDeleteRecord(key, comparator_);
    if (res == -1) {
      if (ctx->root_is_latched_) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
      }
      ctx->Clear(bpm_, false);
      return;
    }

    if (leaf->IsRootPage()) {
      if (leaf->GetSize() == 0) {
        page_id_t old_root = root_page_id_;
        root_page_id_ = INVALID_PAGE_ID;
        leaf_page->WUnlatch();
        bpm_->UnpinPage(old_root, false);
        bpm_->DeletePage(old_root);
        ctx->write_set_.clear();
      }
      if (ctx->root_is_latched_) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
      }
      ctx->Clear(bpm_, true);
      return;
    }

    if (leaf->GetSize() < leaf->GetMinSize()) {
      bool deleted = HandleUnderflow(leaf_page);
      if (deleted) {
        // If leaf_page was merged into left sibling and deleted in HandleUnderflow,
        // it is already unpinned and deleted. Unlatch it and remove from write_set_.
        if (!ctx->write_set_.empty() && ctx->write_set_.back() == leaf_page) {
          leaf_page->WUnlatch();
          ctx->write_set_.pop_back();
        }
      }
    }

    if (ctx->root_is_latched_) {
      root_latch_.unlock();
      ctx->root_is_latched_ = false;
    }
    ctx->Clear(bpm_, true);
  }

  /**
   * TODO(student): Return iterator pointing to first item in the leftmost leaf
   * page.
   */
  Iterator Begin() {
    if (IsEmpty()) {
      return End();
    }
    Page *curr_page = bpm_->FetchPage(root_page_id_);
    if (curr_page == nullptr) {
      return End();
    }
    auto *curr_node = reinterpret_cast<BPlusTreePage *>(curr_page->GetData());
    while (!curr_node->IsLeafPage()) {
      auto *internal_node = reinterpret_cast<InternalPage *>(curr_node);
      page_id_t next_page_id = internal_node->ValueAt(0);
      Page *next_page = bpm_->FetchPage(next_page_id);
      bpm_->UnpinPage(curr_page->GetPageId(), false);
      curr_page = next_page;
      if (curr_page == nullptr) {
        return End();
      }
      curr_node = reinterpret_cast<BPlusTreePage *>(curr_page->GetData());
    }
    return Iterator(bpm_, curr_page, 0);
  }

  Iterator Begin(const KeyType &key) {
    if (IsEmpty()) {
      return End();
    }
    Page *leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
      return End();
    }
    auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());
    int index = leaf->KeyIndex(key, comparator_);

    if (index >= leaf->GetSize()) {
      page_id_t next_page_id = leaf->GetNextPageId();
      bpm_->UnpinPage(leaf->GetPageId(), false);
      if (next_page_id == INVALID_PAGE_ID) {
        return End();
      }
      Page *next_page = bpm_->FetchPage(next_page_id);
      if (next_page == nullptr) {
        return End();
      }
      return Iterator(bpm_, next_page, 0);
    }

    return Iterator(bpm_, leaf_page, index);
  }

  Iterator End() { return Iterator(bpm_, INVALID_PAGE_ID, 0); }

  /**
   * Print entire tree structure for visual inspection and debugging.
   */
  void PrintTree() const {
    if (IsEmpty()) {
      std::cout << "[Empty B+ Tree]" << std::endl;
      return;
    }

    std::queue<page_id_t> q;
    q.push(root_page_id_);

    int level = 0;
    while (!q.empty()) {
      size_t sz = q.size();
      std::cout << "Level " << level << ": ";

      for (size_t i = 0; i < sz; ++i) {
        page_id_t pid = q.front();
        q.pop();

        Page *page = bpm_->FetchPage(pid);
        auto *node = reinterpret_cast<BPlusTreePage *>(page->GetData());

        if (node->IsLeafPage()) {
          auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());
          std::cout << "[Leaf " << pid << " (size " << leaf->GetSize() << "): ";
          for (int k = 0; k < leaf->GetSize(); ++k) {
            std::cout << leaf->KeyAt(k) << " ";
          }
          std::cout << "] ";
        } else {
          auto *internal = reinterpret_cast<InternalPage *>(page->GetData());
          std::cout << "[Int " << pid << " (size " << internal->GetSize()
                    << "): ";
          for (int k = 1; k < internal->GetSize(); ++k) {
            std::cout << internal->KeyAt(k) << " ";
          }
          std::cout << "] ";

          for (int c = 0; c < internal->GetSize(); ++c) {
            q.push(internal->ValueAt(c));
          }
        }
        bpm_->UnpinPage(pid, false);
      }
      std::cout << std::endl;
      level++;
    }
  }

private:
  std::string index_name_;
  page_id_t root_page_id_{INVALID_PAGE_ID};
  BufferPoolManager *bpm_;
  KeyComparator comparator_;
  int leaf_max_size_;
  int internal_max_size_;
  std::shared_mutex root_latch_;

  /**
   * TODO(student): Initialize an empty tree by allocating a new root leaf page.
   */
  bool StartNewTree(const KeyType &key, const ValueType &value) {
    // Acquire exclusive lock protecting root_page_id_
    std::unique_lock<std::shared_mutex> root_lock(root_latch_);

    if (!IsEmpty()) {
      return false; // Or redirect to InsertIntoLeaf
    }

    page_id_t new_root_id = INVALID_PAGE_ID;
    Page *root_page = bpm_->NewPage(&new_root_id);
    if (root_page == nullptr) {
      return false;
    }

    // Safe to write to the newly allocated page
    auto *root = reinterpret_cast<LeafPage *>(root_page->GetData());
    root->Init(new_root_id, INVALID_PAGE_ID, leaf_max_size_);
    root->Insert(key, value, comparator_);

    // Publish root_page_id_ while holding the lock
    root_page_id_ = new_root_id;

    bpm_->UnpinPage(root_page_id_, true);
    return true;
  }

  /**
   * TODO(student): Descend root to leaf to find target leaf page.
   * Returns fetched Page* with pin_count = 1.
   */
  Page *FindLeafPage(const KeyType &key,
                     OperationType op = OperationType::SEARCH,
                     Context *ctx = nullptr) {
    if (op == OperationType::INSERT) {
      root_latch_.lock();
      ctx->root_is_latched_ = true;

      if (IsEmpty()) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
        return nullptr;
      }
      Page *curr_page = bpm_->FetchPage(root_page_id_);
      if (curr_page == nullptr) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
        return nullptr;
      }
      curr_page->WLatch();
      ctx->write_set_.push_back(curr_page); // root
      auto *curr_node = reinterpret_cast<BPlusTreePage *>(curr_page->GetData());

      // If the root is already a leaf (height 1) and safe from splitting,
      // release root latch early
      if (curr_node->IsLeafPage() &&
          curr_node->GetSize() < curr_node->GetMaxSize() - 1) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
      }

      while (!curr_node->IsLeafPage()) {
        auto *internal_node = reinterpret_cast<InternalPage *>(curr_node);
        page_id_t child_page_id = internal_node->Lookup(key, comparator_);

        Page *child_page = bpm_->FetchPage(child_page_id);
        if (child_page == nullptr) {
          if (ctx->root_is_latched_) {
            root_latch_.unlock();
            ctx->root_is_latched_ = false;
          }
          ctx->Clear(bpm_, false);
          return nullptr;
        }
        child_page->WLatch();
        auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());

        if (child->GetSize() < child->GetMaxSize() - 1) {
          if (ctx->root_is_latched_) {
            root_latch_.unlock();
            ctx->root_is_latched_ = false;
          }
          ctx->Clear(bpm_, false);
        }
        curr_page = child_page;
        curr_node = reinterpret_cast<BPlusTreePage *>(curr_page->GetData());
        ctx->write_set_.push_back(curr_page);
      }
      return curr_page;
    } else if (op == OperationType::DELETE) {
      root_latch_.lock();
      ctx->root_is_latched_ = true;
      if (IsEmpty()) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
        return nullptr;
      }
      Page *curr_page = bpm_->FetchPage(root_page_id_);
      if (curr_page == nullptr) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
        return nullptr;
      }
      curr_page->WLatch();
      ctx->write_set_.push_back(curr_page); // root
      auto *curr_node = reinterpret_cast<BPlusTreePage *>(curr_page->GetData());

      // If the root is already a leaf (height 1) and safe from splitting,
      // release root latch early
      if (curr_node->IsLeafPage() &&
          curr_node->GetSize() > curr_node->GetMinSize()) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
      }
      while (!curr_node->IsLeafPage()) {
        auto *internal_node = reinterpret_cast<InternalPage *>(curr_node);
        page_id_t child_page_id = internal_node->Lookup(key, comparator_);

        Page *child_page = bpm_->FetchPage(child_page_id);
        if (child_page == nullptr) {
          if (ctx->root_is_latched_) {
            root_latch_.unlock();
            ctx->root_is_latched_ = false;
          }
          ctx->Clear(bpm_, false);
          return nullptr;
        }
        child_page->WLatch();
        auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());

        if (child->GetSize() > child->GetMinSize()) {
          if (ctx->root_is_latched_) {
            root_latch_.unlock();
            ctx->root_is_latched_ = false;
          }
          ctx->Clear(bpm_, false);
        }
        curr_page = child_page;
        curr_node = reinterpret_cast<BPlusTreePage *>(curr_page->GetData());
        ctx->write_set_.push_back(curr_page);
      }
      return curr_page;
    } else {
      root_latch_.lock_shared();
      if (IsEmpty()) {
        root_latch_.unlock_shared();
        return nullptr;
      }
      Page *curr_page = bpm_->FetchPage(root_page_id_);
      if (curr_page == nullptr) {
        root_latch_.unlock_shared();
        return nullptr;
      }

      bool is_optimistic = (op == OperationType::OPTIMISTIC_INSERT ||
                            op == OperationType::OPTIMISTIC_DELETE);
      auto *curr_node = reinterpret_cast<BPlusTreePage *>(curr_page->GetData());

      // If root is already a leaf and we are optimistic, acquire WLatch; otherwise RLatch
      if (curr_node->IsLeafPage() && is_optimistic) {
        curr_page->WLatch();
      } else {
        curr_page->RLatch();
      }
      root_latch_.unlock_shared();

      while (!curr_node->IsLeafPage()) {
        auto *internal_node = reinterpret_cast<InternalPage *>(curr_node);
        page_id_t child_page_id = internal_node->Lookup(key, comparator_);

        Page *child_page = bpm_->FetchPage(child_page_id);
        if (child_page == nullptr) {
          curr_page->RUnlatch();
          bpm_->UnpinPage(curr_page->GetPageId(), false);
          return nullptr;
        }

        auto *child_node = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
        // For optimistic write operations, acquire exclusive WLatch on the target leaf
        if (child_node->IsLeafPage() && is_optimistic) {
          child_page->WLatch();
        } else {
          child_page->RLatch();
        }

        curr_page->RUnlatch();
        bpm_->UnpinPage(curr_page->GetPageId(), false);
        curr_page = child_page;
        curr_node = child_node;
      }

      if (ctx != nullptr) {
        if (is_optimistic) {
          ctx->write_set_.push_back(curr_page);
        } else {
          ctx->read_set_.push_back(curr_page);
        }
      }
      return curr_page;
    }
  }

  /**
   * TODO(student): Insert into existing leaf page and handle potential split.
   *
   * Invariants & Steps:
   * 1. leaf_page = FindLeafPage(key).
   * 2. new_size = leaf->Insert(key, value, comparator_).
   * 3. If new_size == -1 (duplicate), unpin leaf (false) and return false.
   * 4. If new_size >= leaf->GetMaxSize():
   *    - Allocate new leaf page via bpm_->NewPage(&new_leaf_id).
   *    - leaf->Split(new_leaf_id, new_leaf).
   *    - Update next sibling's prev_page_id if new_leaf->next !=
   * INVALID_PAGE_ID.
   *    - Push separator key (new_leaf->KeyAt(0)) to parent:
   *      InsertIntoParent(leaf_page, split_key, new_leaf_page).
   *    - Unpin new_leaf (true).
   * 5. Unpin leaf_page (true).
   * 6. Return true.
   */
  bool InsertIntoLeaf(const KeyType &key, const ValueType &value,
                      Context *ctx) {
    Page *leaf_page = FindLeafPage(key, OperationType::INSERT, ctx);
    if (leaf_page == nullptr) {
      if (ctx->root_is_latched_) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
      }
      ctx->Clear(bpm_, false);
      return false;
    }

    auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());
    int new_size = leaf->Insert(key, value, comparator_);

    if (new_size == -1) {
      if (ctx->root_is_latched_) {
        root_latch_.unlock();
        ctx->root_is_latched_ = false;
      }
      ctx->Clear(bpm_, false);
      return false;
    }

    if (new_size >= leaf->GetMaxSize()) {
      page_id_t new_leaf_id;
      Page *new_leaf_page = bpm_->NewPage(&new_leaf_id);
      if (new_leaf_page == nullptr) {
        if (ctx->root_is_latched_) {
          root_latch_.unlock();
          ctx->root_is_latched_ = false;
        }
        ctx->Clear(bpm_, false);
        return false;
      }
      new_leaf_page->WLatch();

      auto *new_leaf = reinterpret_cast<LeafPage *>(new_leaf_page->GetData());
      leaf->Split(new_leaf_id, new_leaf);

      if (new_leaf->GetNextPageId() != INVALID_PAGE_ID) {
        Page *next_page = bpm_->FetchPage(new_leaf->GetNextPageId());
        if (next_page != nullptr) {
          // will need to latch and avoid deadlocks here
          next_page->WLatch();
          auto *next_leaf = reinterpret_cast<LeafPage *>(next_page->GetData());
          next_leaf->SetPrevPageId(new_leaf_id);
          next_page->WUnlatch();
          bpm_->UnpinPage(next_page->GetPageId(), true);
        }
      }

      KeyType split_key = new_leaf->KeyAt(0);
      InsertIntoParent(leaf_page, split_key, new_leaf_page);
      new_leaf_page->WUnlatch();
      bpm_->UnpinPage(new_leaf_id, true);
    }

    if (ctx->root_is_latched_) {
      root_latch_.unlock();
      ctx->root_is_latched_ = false;
    }
    ctx->Clear(bpm_, true);
    return true;
  }

  /**
   * TODO(student): Insert separator key and new child pointer into parent node.
   *
   * Invariants & Steps:
   * 1. If old_page was the root (old_node->IsRootPage()):
   *    - Root split! Tree height increases by 1.
   *    - Allocate new root page via bpm_->NewPage(&new_root_id).
   *    - Init as InternalPage, call new_root->Populate(old_page->GetPageId(),
   * key, new_page->GetPageId()).
   *    - Update parent pointers of old_page and new_page to new_root_id.
   *    - root_page_id_ = new_root_id. Unpin new_root (true). Return.
   * 2. Else:
   *    - Fetch parent page via bpm_->FetchPage(old_node->GetParentPageId()).
   *    - parent->InsertNodeAfter(old_page->GetPageId(), key,
   * new_page->GetPageId()).
   *    - Update new_node's parent_page_id = parent_id.
   *    - If parent overflowed (parent->GetSize() > parent->GetMaxSize()):
   *      - Allocate new internal page via bpm_->NewPage(&new_parent_id).
   *      - parent->Split(new_parent_id, new_parent, &parent_split_key, bpm_).
   *      - Recurse: InsertIntoParent(parent_page, parent_split_key,
   * new_parent_page).
   *      - Unpin new_parent (true).
   *    - Unpin parent_page (true).
   */
  void InsertIntoParent(Page *old_page, const KeyType &key, Page *new_page) {
    BPlusTreePage *new_node =
        reinterpret_cast<BPlusTreePage *>(new_page->GetData());
    auto *old_node = reinterpret_cast<BPlusTreePage *>(old_page->GetData());
    if (old_node->IsRootPage()) {
      page_id_t new_root_id;
      Page *new_root_page = bpm_->NewPage(&new_root_id);
      if (new_root_page == nullptr)
        return;
      InternalPage *internal_new_root_page =
          reinterpret_cast<InternalPage *>(new_root_page->GetData());
      internal_new_root_page->Init(new_root_id, INVALID_PAGE_ID,
                                   internal_max_size_);
      internal_new_root_page->Populate(old_page->GetPageId(), key,
                                       new_page->GetPageId());
      new_node->SetParentPageId(new_root_id);
      old_node->SetParentPageId(new_root_id);
      root_page_id_ = new_root_id;
      bpm_->UnpinPage(new_root_id, true);
    } else {
      // internal node
      Page *parent_page = bpm_->FetchPage(old_node->GetParentPageId());
      if (parent_page == nullptr)
        return;
      InternalPage *internal_parent_page =
          reinterpret_cast<InternalPage *>(parent_page->GetData());
      internal_parent_page->InsertNodeAfter(old_page->GetPageId(), key,
                                            new_page->GetPageId());
      new_node->SetParentPageId(internal_parent_page->GetPageId());
      if (internal_parent_page->GetSize() >
          internal_parent_page->GetMaxSize()) {
        // overflow recursivly call split and insert to parent
        page_id_t recipient_page_id;
        Page *recipient_page = bpm_->NewPage(&recipient_page_id);
        if (recipient_page == nullptr) {
          bpm_->UnpinPage(parent_page->GetPageId(), true);
          return;
        }
        InternalPage *recipient =
            reinterpret_cast<InternalPage *>(recipient_page->GetData());
        KeyType parent_split_key;
        internal_parent_page->Split(recipient_page_id, recipient,
                                    &parent_split_key);
        for (int i = 0; i < recipient->GetSize(); i++) {
          ReparentChild(recipient->ValueAt(i), recipient_page_id);
        }
        InsertIntoParent(parent_page, parent_split_key, recipient_page);
        bpm_->UnpinPage(parent_page->GetPageId(), true);
        bpm_->UnpinPage(recipient_page->GetPageId(), true);
        return;
      }
      bpm_->UnpinPage(parent_page->GetPageId(), true);
    }
    return;
  }

  /**
   * Helper to reparent a child node to a new parent page ID.
   * Fetches the child page from the buffer pool, updates its parent pointer,
   * and unpins it with dirty=true.
   */
  void ReparentChild(page_id_t child_page_id, page_id_t new_parent_page_id) {
    Page *child_page = bpm_->FetchPage(child_page_id);
    if (child_page != nullptr) {
      auto *child_node =
          reinterpret_cast<BPlusTreePage *>(child_page->GetData());
      child_node->SetParentPageId(new_parent_page_id);
      bpm_->UnpinPage(child_page_id, true);
    }
  }

  /**
   * TODO(student): Handle underflow when a node's size drops below
   * GetMinSize(). Dispatches to MergeNodes or BorrowFromLeft / BorrowFromRight.
   *
   * @param underflowed_page The page that dropped below 50% capacity.
   * @return true if underflowed_page was merged and deleted; false if it
   * borrowed entries.
   */
  bool HandleUnderflow(Page *underflowed_page) {
    if (underflowed_page == nullptr) {
      return false;
    }

    auto *node = reinterpret_cast<BPlusTreePage *>(underflowed_page->GetData());

    // 1. Root underflow is handled by collapsing the root
    if (node->IsRootPage() || node->GetPageId() == root_page_id_) {
      return TryCollapseRoot(underflowed_page);
    }

    // If node is not underflowed, nothing to do
    if (node->GetSize() >= node->GetMinSize()) {
      return false;
    }

    // 2. Fetch parent page
    page_id_t parent_id = node->GetParentPageId();
    Page *parent_page = bpm_->FetchPage(parent_id);
    if (parent_page == nullptr) {
      return false;
    }
    auto *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());

    // 3. Find our index in parent
    int index = parent->ValueIndex(node->GetPageId());
    if (index == -1) {
      bpm_->UnpinPage(parent_id, false);
      return false;
    }

    // 4. Try borrowing or merging with left sibling (if index > 0)
    if (index > 0) {
      page_id_t left_id = parent->ValueAt(index - 1);
      Page *left_page = bpm_->FetchPage(left_id);
      if (left_page != nullptr) {
        auto *left_node =
            reinterpret_cast<BPlusTreePage *>(left_page->GetData());

        // Case A: Borrow from left sibling
        if (left_node->GetSize() > left_node->GetMinSize()) {
          BorrowFromLeft(left_page, underflowed_page, parent, index);
          bpm_->UnpinPage(left_id, true);
          bpm_->UnpinPage(parent_id, true);
          return false;
        }

        // Case B: Merge into left sibling
        if (left_node->GetSize() + node->GetSize() <= left_node->GetMaxSize()) {
          bool parent_underflow =
              MergeNodes(left_page, underflowed_page, parent, index);
          bpm_->UnpinPage(left_id, true);

          if (parent_underflow) {
            bool parent_deleted = HandleUnderflow(parent_page);
            if (!parent_deleted) {
              bpm_->UnpinPage(parent_id, true);
            }
          } else {
            bpm_->UnpinPage(parent_id, true);
          }
          return true; // underflowed_page was merged into left and deleted
        }

        bpm_->UnpinPage(left_id, false);
      }
    }

    // 5. Try borrowing or merging with right sibling (if index <
    // parent->GetSize() - 1)
    if (index < parent->GetSize() - 1) {
      page_id_t right_id = parent->ValueAt(index + 1);
      Page *right_page = bpm_->FetchPage(right_id);
      if (right_page != nullptr) {
        auto *right_node =
            reinterpret_cast<BPlusTreePage *>(right_page->GetData());

        // Case A: Borrow from right sibling
        if (right_node->GetSize() > right_node->GetMinSize()) {
          BorrowFromRight(underflowed_page, right_page, parent, index + 1);
          bpm_->UnpinPage(right_id, true);
          bpm_->UnpinPage(parent_id, true);
          return false;
        }

        // Case B: Merge right sibling into underflowed_page
        if (node->GetSize() + right_node->GetSize() <= node->GetMaxSize()) {
          bool parent_underflow =
              MergeNodes(underflowed_page, right_page, parent, index + 1);

          if (parent_underflow) {
            bool parent_deleted = HandleUnderflow(parent_page);
            if (!parent_deleted) {
              bpm_->UnpinPage(parent_id, true);
            }
          } else {
            bpm_->UnpinPage(parent_id, true);
          }
          return false; // underflowed_page survived as the left recipient
        }

        bpm_->UnpinPage(right_id, false);
      }
    }

    bpm_->UnpinPage(parent_id, false);
    return false;
  }

  /**
   * TODO(student): Merges right_page into left_page and deletes right_page.
   *
   * Invariant: left_page and right_page MUST be consecutive siblings under
   * parent.
   *
   * @param left_page     The surviving recipient page (retains its page_id).
   * @param right_page    The donor page (emptied, unpinned, and deleted from
   * BPM).
   * @param parent        The common internal routing parent node.
   * @param separator_idx The index in parent corresponding to the key
   * separating left & right.
   * @return true if parent now underflows as a result of removing
   * separator_idx.
   */
  bool MergeNodes(Page *left_page, Page *right_page, InternalPage *parent,
                  int separator_idx) {
    if (left_page == nullptr || right_page == nullptr || parent == nullptr) {
      return false;
    }

    auto *left_node = reinterpret_cast<BPlusTreePage *>(left_page->GetData());
    auto *right_node = reinterpret_cast<BPlusTreePage *>(right_page->GetData());

    if (left_node->IsLeafPage()) {
      auto *left_leaf = reinterpret_cast<LeafPage *>(left_node);
      auto *right_leaf = reinterpret_cast<LeafPage *>(right_node);

      // Merge right_leaf into left_leaf
      right_leaf->MergeInto(left_leaf);

      // Fix next sibling's prev pointer
      if (left_leaf->GetNextPageId() != INVALID_PAGE_ID) {
        Page *next_page = bpm_->FetchPage(left_leaf->GetNextPageId());
        if (next_page != nullptr) {
          auto *next_leaf = reinterpret_cast<LeafPage *>(next_page->GetData());
          next_leaf->SetPrevPageId(left_leaf->GetPageId());
          bpm_->UnpinPage(left_leaf->GetNextPageId(), true);
        }
      }
    } else {
      auto *left_int = reinterpret_cast<InternalPage *>(left_node);
      auto *right_int = reinterpret_cast<InternalPage *>(right_node);

      KeyType parent_separator = parent->KeyAt(separator_idx);
      right_int->MergeInto(left_int, parent_separator);

      // Reparent all children in left_int
      for (int i = 0; i < left_int->GetSize(); ++i) {
        ReparentChild(left_int->ValueAt(i), left_page->GetPageId());
      }
    }

    // Remove separator key and right child pointer from parent
    parent->Remove(separator_idx);

    // Unpin and delete right_page
    page_id_t right_page_id = right_page->GetPageId();
    bpm_->UnpinPage(right_page_id, false);
    bpm_->DeletePage(right_page_id);

    // Return true if parent now underflows
    return parent->GetSize() < parent->GetMinSize();
  }

  /**
   * TODO(student): Borrows the largest/last item from left_page and prepends it
   * to right_page. (Used when right_page underflowed and left sibling has spare
   * capacity).
   *
   * If pages are internal nodes, use:
   *   page_id_t moved = left_internal->LendLastTo(right_internal, separator);
   *   ReparentChild(moved, right_page->GetPageId());
   */
  void BorrowFromLeft(Page *left_page, Page *right_page, InternalPage *parent,
                      int separator_idx) {
    if (left_page == nullptr || right_page == nullptr || parent == nullptr) {
      return;
    }
    auto left_node = reinterpret_cast<BPlusTreePage *>(left_page->GetData());
    // right page under flow
    if (left_node->IsLeafPage()) {
      auto left = reinterpret_cast<LeafPage *>(left_node);
      auto right_leaf = reinterpret_cast<LeafPage *>(right_page->GetData());
      left->LendLastTo(right_leaf);
      parent->SetKeyAt(separator_idx, right_leaf->KeyAt(0));
    } else {
      auto left_int = reinterpret_cast<InternalPage *>(left_page->GetData());
      auto right_int = reinterpret_cast<InternalPage *>(right_page->GetData());

      KeyType new_parent_seprator = left_int->KeyAt(left_int->GetSize() - 1);
      KeyType old_parent_separator = parent->KeyAt(separator_idx);
      page_id_t moved_child =
          left_int->LendLastTo(right_int, old_parent_separator);
      ReparentChild(moved_child, right_page->GetPageId());
      parent->SetKeyAt(separator_idx, new_parent_seprator);
    }
  }

  /**
   * TODO(student): Borrows the smallest/first item from right_page and
   * appends it to left_page. (Used when left_page underflowed and right
   * sibling has spare capacity).
   *
   * If pages are internal nodes, use:
   *   page_id_t moved = right_internal->LendFirstTo(left_internal, separator);
   *   ReparentChild(moved, left_page->GetPageId());
   */
  void BorrowFromRight(Page *left_page, Page *right_page, InternalPage *parent,
                       int separator_idx) {
    if (left_page == nullptr || right_page == nullptr || parent == nullptr) {
      return;
    }
    auto right_node = reinterpret_cast<BPlusTreePage *>(right_page->GetData());
    // right page under flow
    if (right_node->IsLeafPage()) {
      auto right_leaf = reinterpret_cast<LeafPage *>(right_node);
      auto left_leaf = reinterpret_cast<LeafPage *>(left_page->GetData());
      right_leaf->LendFirstTo(left_leaf);
      parent->SetKeyAt(separator_idx, right_leaf->KeyAt(0));
    } else {
      auto left_int = reinterpret_cast<InternalPage *>(left_page->GetData());
      auto right_int = reinterpret_cast<InternalPage *>(right_page->GetData());

      KeyType new_parent_separator = right_int->KeyAt(1);
      KeyType old_parent_separator = parent->KeyAt(separator_idx);
      page_id_t moved_child =
          right_int->LendFirstTo(left_int, old_parent_separator);
      ReparentChild(moved_child, left_page->GetPageId());
      parent->SetKeyAt(separator_idx, new_parent_separator);
    }
  }

  /**
   * TODO(student): Shrinks tree height if root node becomes degenerate:
   * - Internal root with only 1 child pointer -> child becomes new root.
   * - Leaf root with 0 keys -> tree becomes empty (INVALID_PAGE_ID).
   *
   * @return true if tree height was reduced.
   */
  bool TryCollapseRoot(Page *root_page) {
    if (root_page == nullptr) {
      return false;
    }
    auto *root_node = reinterpret_cast<BPlusTreePage *>(root_page->GetData());
    if (root_node->GetPageId() != root_page_id_) {
      return false;
    }

    if (root_node->IsLeafPage()) {
      if (root_node->GetSize() == 0) {
        page_id_t old_root_id = root_page_id_;
        root_page_id_ = INVALID_PAGE_ID;
        bpm_->DeletePage(old_root_id);
        return true;
      }
      return false;
    }

    // Root is internal node: if only 1 child pointer remaining (size == 1),
    // child becomes root
    auto *internal_root = reinterpret_cast<InternalPage *>(root_node);
    if (internal_root->GetSize() == 1) {
      page_id_t new_root_id = internal_root->ValueAt(0);
      page_id_t old_root_id = root_page_id_;
      root_page_id_ = new_root_id;

      Page *new_root_page = bpm_->FetchPage(new_root_id);
      if (new_root_page != nullptr) {
        auto *new_root_node =
            reinterpret_cast<BPlusTreePage *>(new_root_page->GetData());
        new_root_node->SetParentPageId(INVALID_PAGE_ID);
        bpm_->UnpinPage(new_root_id, true);
      }

      bpm_->DeletePage(old_root_id);
      return true;
    }

    return false;
  }
};
