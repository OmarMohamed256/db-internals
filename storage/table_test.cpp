#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <cstdio>
#include "rid.h"
#include "user_row.h"
#include "page_guard.h"
#include "table_page.h"
#include "table_heap.h"
#include "table_iterator.h"

#include "../buffer_pool_manager_skeleton/async_disk_manager.h"
#include "../buffer_pool_manager_skeleton/lru_k_replacer.h"
#include "../buffer_pool_manager_skeleton/buffer_pool_manager.h"
#include "../btree/b_plus_tree.h"

void CleanUpDb(const std::string &db_name) {
    std::remove(db_name.c_str());
}

// ============================================================================
// Test 1: PageGuard RAII & Move Semantics
// ============================================================================
void TestPageGuardRAII() {
    std::cout << "\n=== [Test 1] PageGuard RAII, Latches & Move Semantics ===" << std::endl;
    const std::string db_name = "test_page_guard.db";
    CleanUpDb(db_name);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_name);
    auto replacer = std::make_unique<LRUKReplacer>(10);
    auto bpm = std::make_unique<BufferPoolManager>(10, disk_manager.get(), replacer.get());

    page_id_t page_id = INVALID_PAGE_ID;

    // Sub-test 1: BasicPageGuard automatic unpin
    {
        BasicPageGuard guard = NewPageBasic(bpm.get(), &page_id);
        assert(guard.IsValid());
        assert(guard.PageId() == page_id);
        assert(guard.GetPage()->GetPinCount() == 1);
        // Guard goes out of scope here -> automatically unpins!
    }
    {
        Page *raw_page = bpm->FetchPage(page_id);
        assert(raw_page != nullptr);
        assert(raw_page->GetPinCount() == 1);
        bpm->UnpinPage(page_id, false);
        assert(raw_page->GetPinCount() == 0);
    }

    // Sub-test 2: ReadPageGuard shared latch and automatic unlatch
    {
        ReadPageGuard rguard = FetchPageRead(bpm.get(), page_id);
        assert(rguard.IsValid());
        assert(rguard.GetPage()->GetPinCount() == 1);
        // Page is RLatched and pinned; exiting scope must RUnlatch and Unpin
    }
    {
        Page *raw_page = bpm->FetchPage(page_id);
        assert(raw_page->GetPinCount() == 1);
        bpm->UnpinPage(page_id, false);
    }

    // Sub-test 3: WritePageGuard exclusive latch & auto-dirty marking
    {
        WritePageGuard wguard = FetchPageWrite(bpm.get(), page_id);
        assert(wguard.IsValid());
        assert(wguard.GetPage()->GetPinCount() == 1);
        // Write data
        char *data = wguard.GetDataMut();
        std::strcpy(data, "AntigravityDBMS");
    }

    // Sub-test 4: Move semantics (prevent double unpin)
    {
        BasicPageGuard g1 = FetchPageBasic(bpm.get(), page_id);
        assert(g1.IsValid());
        assert(g1.GetPage()->GetPinCount() == 1);

        BasicPageGuard g2 = std::move(g1);
        assert(!g1.IsValid()); // g1 was cleared
        assert(g2.IsValid());
        assert(g2.GetPage()->GetPinCount() == 1);

        BasicPageGuard g3;
        g3 = std::move(g2);
        assert(!g2.IsValid());
        assert(g3.IsValid());
        assert(g3.GetPage()->GetPinCount() == 1);
    }
    // All guards out of scope; verify pin count is 0
    {
        Page *raw_page = bpm->FetchPage(page_id);
        assert(raw_page->GetPinCount() == 1);
        bpm->UnpinPage(page_id, false);
        assert(raw_page->GetPinCount() == 0);
    }

    CleanUpDb(db_name);
    std::cout << "  ✓ PageGuard RAII & Move semantics passed completely!" << std::endl;
}

// ============================================================================
// Test 2: TablePage In-Memory Slotted Layout
// ============================================================================
void TestTablePageSlottedLayout() {
    std::cout << "\n=== [Test 2] TablePage Slotted Storage Mechanics ===" << std::endl;

    alignas(4096) char page_buffer[4096]{};
    auto *page = reinterpret_cast<TablePage *>(page_buffer);
    page->Init(42, INVALID_PAGE_ID, INVALID_PAGE_ID);

    assert(page->GetPageId() == 42);
    assert(page->GetNumRecords() == 0);
    assert(page->GetFreeSpaceRemaining() == 4096 - TablePage::TABLE_PAGE_HEADER_SIZE);

    // Insert records until page is completely full
    std::vector<RID> rids;
    uint32_t count = 0;
    while (true) {
        UserRow row(count + 1, ("User_" + std::to_string(count + 1)).c_str(), 100.0 * (count + 1));
        RID rid;
        if (!page->InsertTuple(row, &rid)) {
            break; // Page full
        }
        assert(rid.page_id == 42);
        assert(rid.slot_num == count);
        rids.push_back(rid);
        count++;
    }

    std::cout << "  • TablePage packed " << count << " tuples (sizeof(UserRow)=" << sizeof(UserRow) << "B)" << std::endl;
    assert(count == (4096 - TablePage::TABLE_PAGE_HEADER_SIZE) / (sizeof(UserRow) + sizeof(TableSlot))); // exactly 48 tuples
    assert(page->GetNumRecords() == count);
    assert(page->GetLiveRecordCount() == count);

    // Verify retrieval of all tuples
    for (uint32_t i = 0; i < count; ++i) {
        UserRow retrieved;
        assert(page->GetTuple(i, &retrieved));
        assert(retrieved.id == static_cast<int64_t>(i + 1));
        assert(retrieved.balance == 100.0 * (i + 1));
        assert(!retrieved.is_deleted);
    }

    // In-place update of record 5
    UserRow updated_row(6, "UpdatedUser_6", 99999.0);
    assert(page->UpdateTuple(5, updated_row));
    UserRow check_updated;
    assert(page->GetTuple(5, &check_updated));
    assert(check_updated.balance == 99999.0);
    assert(std::string(check_updated.name) == "UpdatedUser_6");

    // MarkDelete record 10
    assert(page->MarkDelete(10));
    UserRow check_deleted;
    assert(!page->GetTuple(10, &check_deleted)); // Must return false for tombstoned tuple
    assert(page->GetLiveRecordCount() == count - 1);

    // Test slot reuse: inserting a new row should reuse slot 10
    UserRow replacement(999, "ReplacedUser", 777.0);
    RID new_rid;
    assert(page->InsertTuple(replacement, &new_rid));
    assert(new_rid.slot_num == 10); // Reused the tombstoned slot!
    assert(page->GetLiveRecordCount() == count);

    UserRow check_replacement;
    assert(page->GetTuple(10, &check_replacement));
    assert(check_replacement.id == 999);
    assert(std::string(check_replacement.name) == "ReplacedUser");

    std::cout << "  ✓ TablePage slotted storage & tombstone reuse verified!" << std::endl;
}

// ============================================================================
// Test 3: TableHeap Multi-Page Chaining & Iterators
// ============================================================================
void TestTableHeapMultiPage() {
    std::cout << "\n=== [Test 3] TableHeap Multi-Page Chaining & TableIterator ===" << std::endl;
    const std::string db_name = "test_table_heap.db";
    CleanUpDb(db_name);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_name);
    auto replacer = std::make_unique<LRUKReplacer>(20);
    auto bpm = std::make_unique<BufferPoolManager>(20, disk_manager.get(), replacer.get());

    auto table_heap = std::make_unique<TableHeap>(bpm.get());

    constexpr int NUM_ROWS = 1000;
    std::vector<RID> inserted_rids;
    inserted_rids.reserve(NUM_ROWS);

    // Insert 1000 rows across multiple pages
    for (int i = 0; i < NUM_ROWS; ++i) {
        UserRow row(i + 1, ("User_" + std::to_string(i + 1)).c_str(), 50.0 * (i + 1), 1700000000 + i);
        RID rid;
        bool ok = table_heap->InsertTuple(row, &rid);
        assert(ok);
        assert(rid.IsValid());
        inserted_rids.push_back(rid);
    }

    size_t page_count = table_heap->GetTotalPageCount();
    size_t row_count = table_heap->GetTotalRowCount();
    std::cout << "  • Stored " << NUM_ROWS << " rows across " << page_count << " pages (Active rows: " << row_count << ")" << std::endl;
    assert(page_count >= 18); // 1000 / ~53 ~= 19 pages
    assert(row_count == NUM_ROWS);

    // Verify random point lookups by RID
    for (int i = 0; i < NUM_ROWS; i += 7) {
        UserRow row;
        assert(table_heap->GetTuple(inserted_rids[i], &row));
        assert(row.id == i + 1);
        assert(row.balance == 50.0 * (i + 1));
    }

    // Test TableIterator (Sequential Scan)
    int iterated_count = 0;
    int last_id = 0;
    for (auto it = table_heap->Begin(); !it.IsEnd(); ++it) {
        auto [row, rid] = *it;
        assert(row.id > last_id);
        last_id = row.id;
        iterated_count++;
    }
    assert(iterated_count == NUM_ROWS);
    std::cout << "  • TableIterator successfully scanned all " << iterated_count << " rows across all pages." << std::endl;

    // Test Delete and verify iterator skips tombstoned rows
    for (int i = 0; i < 50; ++i) {
        assert(table_heap->MarkDelete(inserted_rids[i]));
    }
    assert(table_heap->GetTotalRowCount() == NUM_ROWS - 50);

    int remaining_count = 0;
    for (auto it = table_heap->Begin(); !it.IsEnd(); ++it) {
        auto [row, rid] = *it;
        assert(row.id > 50); // IDs 1..50 were deleted
        remaining_count++;
    }
    assert(remaining_count == NUM_ROWS - 50);

    CleanUpDb(db_name);
    std::cout << "  ✓ TableHeap multi-page chaining & TableIterator verified!" << std::endl;
}

// ============================================================================
// Test 4: B+ Tree Primary Index Hookup (Key -> RID)
// ============================================================================
void TestTableHeapBPlusTreeHookup() {
    std::cout << "\n=== [Test 4] B+ Tree Primary Index Hookup (Key -> RID) ===" << std::endl;
    const std::string db_name = "test_btree_hookup.db";
    CleanUpDb(db_name);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_name);
    auto replacer = std::make_unique<LRUKReplacer>(200);
    auto bpm = std::make_unique<BufferPoolManager>(200, disk_manager.get(), replacer.get());

    auto table_heap = std::make_unique<TableHeap>(bpm.get());
    BPlusTree<int64_t, RID> primary_index("users_pk_idx", bpm.get());

    constexpr int NUM_USERS = 5000;
    std::cout << "  • Inserting " << NUM_USERS << " users into TableHeap and indexing via B+ Tree..." << std::endl;

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= NUM_USERS; ++i) {
        UserRow user(i, ("Customer_" + std::to_string(i)).c_str(), 10.5 * i, 1710000000 + i);
        RID rid;
        bool ok = table_heap->InsertTuple(user, &rid);
        assert(ok);

        // Crucial Hookup: Index (Key -> RID)
        bool idx_ok = primary_index.Insert(user.id, rid);
        assert(idx_ok);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double insert_time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "  • Completed " << NUM_USERS << " inserts + B+ tree indexing in " << insert_time_ms << " ms ("
              << (insert_time_ms / NUM_USERS * 1000.0) << " µs/row)" << std::endl;

    // Benchmark Point Lookups (Key -> B+ Tree -> RID -> TableHeap -> UserRow)
    std::cout << "  • Benchmarking 5,000 point index queries..." << std::endl;
    auto lookup_start = std::chrono::high_resolution_clock::now();

    for (int i = 1; i <= NUM_USERS; ++i) {
        std::vector<RID> rids;
        bool found = primary_index.GetValue(i, &rids);
        assert(found);
        assert(rids.size() == 1);

        UserRow fetched_user;
        bool heap_found = table_heap->GetTuple(rids[0], &fetched_user);
        assert(heap_found);
        assert(fetched_user.id == i);
        assert(fetched_user.balance == 10.5 * i);
        assert(std::string(fetched_user.name) == ("Customer_" + std::to_string(i)));
    }

    auto lookup_end = std::chrono::high_resolution_clock::now();
    double total_lookup_us = std::chrono::duration<double, std::micro>(lookup_end - lookup_start).count();
    double avg_lookup_us = total_lookup_us / NUM_USERS;

    std::cout << "  • Total point lookup time: " << (total_lookup_us / 1000.0) << " ms" << std::endl;
    std::cout << "  • Average Point Lookup Latency: " << avg_lookup_us << " µs / query [Target: < 5 µs]" << std::endl;
    assert(avg_lookup_us < 15.0); // Strict performance target for debug build

    // Verify non-existent keys return false
    std::vector<RID> missing_rids;
    assert(!primary_index.GetValue(999999, &missing_rids));
    assert(!primary_index.GetValue(-1, &missing_rids));

    // Range scan via B+ Tree iterator
    std::cout << "  • Executing Index Range Scan [1500 .. 1600] through TableHeap..." << std::endl;
    int range_matches = 0;
    for (auto it = primary_index.Begin(1500); !it.IsEnd(); ++it) {
        auto [key, rid] = *it;
        if (key > 1600) {
            break;
        }
        UserRow row;
        assert(table_heap->GetTuple(rid, &row));
        assert(row.id == key);
        assert(row.id >= 1500 && row.id <= 1600);
        range_matches++;
    }
    assert(range_matches == 101); // 1500 to 1600 inclusive
    std::cout << "  • Range scan successfully matched and loaded " << range_matches << " rows." << std::endl;

    // Test Deletion consistency
    // Delete user 2500 from TableHeap
    std::vector<RID> target_rids;
    assert(primary_index.GetValue(2500, &target_rids));
    assert(table_heap->MarkDelete(target_rids[0]));

    // Querying through index should find the RID, but TableHeap::GetTuple will report deleted!
    UserRow deleted_check;
    assert(!table_heap->GetTuple(target_rids[0], &deleted_check));

    CleanUpDb(db_name);
    std::cout << "  ✓ B+ Tree Primary Index Hookup verified with flying colors!" << std::endl;
}

int main(int argc, char **argv) {
    int test_num = 0;
    if (argc > 1) {
        test_num = std::atoi(argv[1]);
    }

    std::cout << "=================================================================" << std::endl;
    std::cout << "DBMS STORAGE SUITE: PageGuards, TableHeap & B+ Tree Hookup" << std::endl;
    if (test_num > 0) {
        std::cout << "Running Milestone Test #" << test_num << std::endl;
    }
    std::cout << "=================================================================" << std::endl;

    if (test_num == 0 || test_num == 1) TestPageGuardRAII();
    if (test_num == 0 || test_num == 2) TestTablePageSlottedLayout();
    if (test_num == 0 || test_num == 3) TestTableHeapMultiPage();
    if (test_num == 0 || test_num == 4) TestTableHeapBPlusTreeHookup();

    std::cout << "\n=================================================================" << std::endl;
    std::cout << "TEST RUN COMPLETE!" << std::endl;
    std::cout << "=================================================================" << std::endl;
    return 0;
}
