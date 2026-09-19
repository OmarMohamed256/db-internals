#include "table_heap.h"
#include "page_guard.h"
#include "rid.h"
#include "table_iterator.h"
#include "table_page.h"
#include <cassert>

// ============================================================================
// TableHeap Implementation (Student Skeleton)
// ============================================================================

TableHeap::TableHeap(BufferPoolManager *bpm) : bpm_(bpm) {
  assert(bpm_ != nullptr);
  WritePageGuard guard = NewPageWrite(bpm, &first_page_id_);
  guard.AsMut<TablePage>()->Init(first_page_id_);
  last_page_id_ = first_page_id_;
}

TableHeap::TableHeap(BufferPoolManager *bpm, page_id_t first_page_id)
    : bpm_(bpm), first_page_id_(first_page_id), last_page_id_(first_page_id) {
  assert(bpm_ != nullptr);
  page_id_t curr = first_page_id;
  while (curr != INVALID_PAGE_ID) {
    ReadPageGuard guard = FetchPageRead(bpm_, curr);
    if (!guard.IsValid()) {
      break;
    }
    last_page_id_ = curr;
    curr = guard.As<TablePage>()->GetNextPageId();
  }
};

bool TableHeap::InsertTuple(const UserRow &row, RID *out_rid) {
  std::lock_guard<std::mutex> lock(latch_);
  WritePageGuard cur_guard = FetchPageWrite(bpm_, last_page_id_);
  if (!cur_guard.IsValid()) {
    return false;
  }
  auto *cur_page = cur_guard.AsMut<TablePage>();
  if (cur_page->InsertTuple(row, out_rid)) {
    return true;
  }
  page_id_t new_page_id = INVALID_PAGE_ID;
  WritePageGuard new_guard = NewPageWrite(bpm_, &new_page_id);
  if (!new_guard.IsValid()) {
    return false;
  }

  // 5. Get the TablePage* for the new page:
  auto *new_page = new_guard.AsMut<TablePage>();

  // 6. Initialize the new page with its prev_page pointer:
  new_page->Init(new_page_id, last_page_id_, INVALID_PAGE_ID);

  // 7. Chain the old tail to the new page:
  cur_page->SetNextPageId(new_page_id);

  // 8. Update TableHeap's tail tracker:
  last_page_id_ = new_page_id;

  // 9. Insert the row into the new page:
  return new_page->InsertTuple(row, out_rid);
};

bool TableHeap::GetTuple(const RID &rid, UserRow *out_row) {
  ReadPageGuard guard = FetchPageRead(bpm_, rid.page_id);
  if (!guard.IsValid())
    return false;
  if (guard.As<TablePage>()->GetTuple(rid.slot_num, out_row)) {
    return true;
  }
  return false;
};

bool TableHeap::UpdateTuple(const UserRow &row, const RID &rid) {
  WritePageGuard guard = FetchPageWrite(bpm_, rid.page_id);
  if (!guard.IsValid())
    return false;
  auto *cur_page = guard.AsMut<TablePage>();
  if (cur_page->UpdateTuple(rid.slot_num, row)) {
    return true;
  }
  return false;
};

bool TableHeap::MarkDelete(const RID &rid) {
  WritePageGuard guard = FetchPageWrite(bpm_, rid.page_id);
  if (!guard.IsValid())
    return false;
  auto *cur_page = guard.AsMut<TablePage>();
  if (cur_page->MarkDelete(rid.slot_num)) {
    return true;
  }
  return false;
};

TableIterator TableHeap::Begin() {
  if (first_page_id_ == INVALID_PAGE_ID) {
    return End();
  }
  return TableIterator(this, RID(first_page_id_, 0));
}

TableIterator TableHeap::End() {
  return TableIterator(this, RID(INVALID_PAGE_ID, 0));
}

size_t TableHeap::GetTotalPageCount() {
  size_t count = 0;
  page_id_t curr = first_page_id_;

  while (curr != INVALID_PAGE_ID) {
    ReadPageGuard guard = FetchPageRead(bpm_, curr);
    if (!guard.IsValid()) {
      break;
    }
    count++;
    // Hop directly to the next page!
    curr = guard.As<TablePage>()->GetNextPageId();
  }

  return count;
}

size_t TableHeap::GetTotalRowCount() {
  size_t total = 0;
  page_id_t curr = first_page_id_;

  while (curr != INVALID_PAGE_ID) {
    ReadPageGuard guard = FetchPageRead(bpm_, curr);
    if (!guard.IsValid()) {
      break;
    }
    // Ask the page for its count:
    total += guard.As<TablePage>()->GetLiveRecordCount();
    curr = guard.As<TablePage>()->GetNextPageId();
  }

  return total;
}

// ============================================================================
// TableIterator Implementation (Student Skeleton)
// ============================================================================

TableIterator::TableIterator(TableHeap *table_heap, RID rid)
    : table_heap_(table_heap), rid_(rid) {
  if (rid_.IsValid()) {
    AdvanceToNextLiveTuple();
  }
}

std::pair<UserRow, RID> TableIterator::operator*() {
  return {current_row_, rid_};
}

TableIterator &TableIterator::operator++() {
  if (rid_.IsValid()) {
    rid_.slot_num++;
    AdvanceToNextLiveTuple();
  }
  return *this;
}

void TableIterator::AdvanceToNextLiveTuple() {
  // TODO:
  // 1. While rid_.IsValid():
  //    a. Fetch page at rid_.page_id.
  //    b. While rid_.slot_num < page->GetNumRecords():
  //       - If page->GetTuple(rid_.slot_num, &current_row_) succeeds: return
  //       (found live tuple).
  //       - Else: rid_.slot_num++.
  //    c. If page exhausted, follow page->GetNextPageId().
  //    d. If next page is INVALID_PAGE_ID, set rid_ = RID(INVALID_PAGE_ID, 0)
  //    and break. e. Else set rid_.page_id = next_page_id, rid_.slot_num = 0,
  //    and continue.
  while (rid_.IsValid()) {
    ReadPageGuard guard = FetchPageRead(table_heap_->GetBPM(), rid_.page_id);
    auto page = guard.As<TablePage>();
    while (rid_.slot_num < page->GetNumRecords()) {
      if (page->GetTuple(rid_.slot_num, &current_row_)) {
        return;
      } else {
        rid_.slot_num++;
      }
    }
    // exhausted the current page
    page_id_t next_page_id = page->GetNextPageId();
    if (next_page_id == INVALID_PAGE_ID) {
      rid_ = RID(INVALID_PAGE_ID, 0);
      break;
    } else {
      rid_ = {next_page_id, 0};
    }
  }
}
