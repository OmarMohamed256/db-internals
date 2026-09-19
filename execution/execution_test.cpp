#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <cstdio>
#include <unordered_set>

#include "abstract_executor.h"
#include "mock_scan_executor.h"
#include "seq_scan_executor.h"
#include "index_scan_executor.h"
#include "filter_executor.h"
#include "limit_executor.h"
#include "nested_loop_join_executor.h"
#include "hash_join_executor.h"
#include "order_row.h"

#include "../storage/rid.h"
#include "../storage/user_row.h"
#include "../storage/table_heap.h"
#include "../btree/b_plus_tree.h"
#include "../buffer_pool_manager_skeleton/async_disk_manager.h"
#include "../buffer_pool_manager_skeleton/lru_k_replacer.h"
#include "../buffer_pool_manager_skeleton/buffer_pool_manager.h"

static void CleanUpDb(const std::string &db_name) {
    std::remove(db_name.c_str());
}

// ============================================================================
// Milestone 1: Sequential Table Scan (SeqScanExecutor)
// ============================================================================
void TestSeqScan() {
    std::cout << "\n=== [Milestone 1] Volcano Sequential Scan (SeqScanExecutor) ===" << std::endl;
    const std::string db_name = "test_exec_seqscan.db";
    CleanUpDb(db_name);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_name);
    auto replacer = std::make_unique<LRUKReplacer>(30);
    auto bpm = std::make_unique<BufferPoolManager>(30, disk_manager.get(), replacer.get());
    auto table_heap = std::make_unique<TableHeap>(bpm.get());

    constexpr int TOTAL_ROWS = 300;
    std::vector<RID> inserted_rids;
    inserted_rids.reserve(TOTAL_ROWS);

    std::cout << "  • Inserting " << TOTAL_ROWS << " records into TableHeap..." << std::endl;
    for (int i = 1; i <= TOTAL_ROWS; ++i) {
        UserRow user(i, ("User_" + std::to_string(i)).c_str(), 25.0 * i, 1710000000 + i);
        RID rid;
        assert(table_heap->InsertTuple(user, &rid));
        inserted_rids.push_back(rid);
    }

    // Delete 30 records across various pages
    std::unordered_set<int64_t> deleted_ids;
    for (int i = 10; i <= 300; i += 10) {
        assert(table_heap->MarkDelete(inserted_rids[i - 1]));
        deleted_ids.insert(i);
    }
    std::cout << "  • Logically deleted " << deleted_ids.size() << " records with tombstones." << std::endl;

    // Execute SeqScan
    std::cout << "  • Executing SeqScanExecutor..." << std::endl;
    SeqScanExecutor seq_scan(table_heap.get());
    seq_scan.Init();

    UserRow row;
    RID rid;
    int live_count = 0;
    std::unordered_set<int64_t> observed_ids;

    while (seq_scan.Next(&row, &rid)) {
        assert(deleted_ids.find(row.id) == deleted_ids.end() && "Deleted row leaked into scan!");
        assert(row.balance == 25.0 * row.id);
        assert(rid.IsValid());
        observed_ids.insert(row.id);
        live_count++;
    }

    std::cout << "  • Live Count is " << live_count << std::endl;
    assert(live_count == TOTAL_ROWS - static_cast<int>(deleted_ids.size()));
    assert(observed_ids.size() == static_cast<size_t>(live_count));
    std::cout << "  • Correctly scanned " << live_count << " live records (skipped all "
              << deleted_ids.size() << " deleted rows)." << std::endl;

    // Test Re-initialization / Rewind
    std::cout << "  • Testing Init() rewind behavior..." << std::endl;
    seq_scan.Init();
    int rewind_count = 0;
    while (seq_scan.Next(&row, &rid)) {
        rewind_count++;
    }
    assert(rewind_count == live_count);
    std::cout << "  • Rewind verified: second scan yielded identical count (" << rewind_count << ")." << std::endl;

    CleanUpDb(db_name);
    std::cout << "  ✓ Milestone 1 (SeqScan) passed successfully!" << std::endl;
}

// ============================================================================
// Milestone 2: Index Scan, Predicate Filter & Limit
// ============================================================================
void TestIndexScanAndFilter() {
    std::cout << "\n=== [Milestone 2] IndexScan, Filter & Limit Pipeline ===" << std::endl;
    const std::string db_name = "test_exec_index_filter.db";
    CleanUpDb(db_name);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_name);
    auto replacer = std::make_unique<LRUKReplacer>(50);
    auto bpm = std::make_unique<BufferPoolManager>(50, disk_manager.get(), replacer.get());
    auto table_heap = std::make_unique<TableHeap>(bpm.get());
    BPlusTree<int64_t, RID> index("users_pk_idx", bpm.get());

    constexpr int NUM_USERS = 1000;
    std::cout << "  • Seeding " << NUM_USERS << " users into TableHeap and indexing via B+ Tree..." << std::endl;
    for (int i = 1; i <= NUM_USERS; ++i) {
        UserRow user(i, ("User_" + std::to_string(i)).c_str(), 10.0 * i, 1710000000 + i);
        RID rid;
        assert(table_heap->InsertTuple(user, &rid));
        assert(index.Insert(user.id, rid));
    }

    // Sub-test 2A: Point Lookup (low_key == high_key)
    std::cout << "  • Sub-test 2A: Point IndexScan for user_id = 450..." << std::endl;
    IndexScanExecutor point_scan(&index, table_heap.get(), 450);
    point_scan.Init();

    UserRow row;
    RID rid;
    assert(point_scan.Next(&row, &rid));
    assert(row.id == 450);
    assert(row.balance == 4500.0);
    assert(!point_scan.Next(&row, &rid) && "Point scan must return false on EOF");
    std::cout << "  • Sub-test 2A passed (exact single record yielded)." << std::endl;

    // Sub-test 2B: Range Scan [200, 260]
    std::cout << "  • Sub-test 2B: Range IndexScan [200 .. 260]..." << std::endl;
    IndexScanExecutor range_scan(&index, table_heap.get(), 200, 260);
    range_scan.Init();

    int expected_id = 200;
    int range_count = 0;
    while (range_scan.Next(&row, &rid)) {
        assert(row.id == expected_id);
        assert(row.balance == 10.0 * expected_id);
        expected_id++;
        range_count++;
    }
    assert(range_count == 61);
    assert(!range_scan.Next(&row, &rid));
    std::cout << "  • Sub-test 2B passed (scanned " << range_count << " rows in sorted order)." << std::endl;

    // Sub-test 2C: Pipelined Tree: Limit(Filter(IndexScan[1 .. 1000]))
    // Query: SELECT * FROM users WHERE balance > 7500.0 LIMIT 20
    std::cout << "  • Sub-test 2C: Composed Pipeline: Limit(20) -> Filter(balance > 7500.0) -> IndexScan..." << std::endl;
    IndexScanExecutor full_index_scan(&index, table_heap.get(), 1, NUM_USERS);
    FilterExecutor<UserRow> filter_exec(&full_index_scan, [](const UserRow &r) {
        return r.balance > 7500.0;
    });
    LimitExecutor<UserRow> limit_exec(&filter_exec, 20);

    limit_exec.Init();
    int limit_count = 0;
    int current_id = 751; // First user with balance > 7500.0 is 751 (balance 7510.0)
    while (limit_exec.Next(&row, &rid)) {
        assert(row.id == current_id);
        assert(row.balance > 7500.0);
        current_id++;
        limit_count++;
    }
    assert(limit_count == 20);
    assert(!limit_exec.Next(&row, &rid));
    std::cout << "  • Sub-test 2C passed (limit truncated stream at exactly 20 rows)." << std::endl;

    CleanUpDb(db_name);
    std::cout << "  ✓ Milestone 2 (IndexScan, Filter & Limit) passed successfully!" << std::endl;
}

// ============================================================================
// Milestone 3: Nested Loop Join (NestedLoopJoinExecutor)
// ============================================================================
void TestNestedLoopJoin() {
    std::cout << "\n=== [Milestone 3] Relational Joins: NestedLoopJoinExecutor ===" << std::endl;
    const std::string db_name = "test_exec_nlj.db";
    CleanUpDb(db_name);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_name);
    auto replacer = std::make_unique<LRUKReplacer>(30);
    auto bpm = std::make_unique<BufferPoolManager>(30, disk_manager.get(), replacer.get());
    auto table_heap = std::make_unique<TableHeap>(bpm.get());

    constexpr int NUM_USERS = 50;
    constexpr int NUM_ORDERS = 150;

    std::cout << "  • Populating " << NUM_USERS << " users in TableHeap..." << std::endl;
    for (int i = 1; i <= NUM_USERS; ++i) {
        UserRow user(i, ("User_" + std::to_string(i)).c_str(), 100.0 * i, 1710000000 + i);
        RID rid;
        assert(table_heap->InsertTuple(user, &rid));
    }

    std::cout << "  • Generating " << NUM_ORDERS << " orders in memory (3 orders per user)..." << std::endl;
    std::vector<OrderRow> orders;
    orders.reserve(NUM_ORDERS + 10);
    for (int i = 1; i <= NUM_ORDERS; ++i) {
        int64_t user_id = ((i - 1) % NUM_USERS) + 1;
        orders.emplace_back(i, user_id, 15.5 * i, 1710500000 + i);
    }
    // Add 10 orphan orders that reference non-existent users
    for (int i = 1; i <= 10; ++i) {
        orders.emplace_back(NUM_ORDERS + i, 99999 + i, 99.0, 1710900000);
    }

    SeqScanExecutor outer_users(table_heap.get());
    MockScanExecutor<OrderRow> inner_orders(orders);

    NestedLoopJoinExecutor nlj(&outer_users, &inner_orders);
    nlj.Init();

    JoinedUserOrderRow joined_row;
    RID rid;
    int join_count = 0;
    while (nlj.Next(&joined_row, &rid)) {
        assert(joined_row.user_id <= NUM_USERS);
        assert(joined_row.order_id <= NUM_ORDERS);
        assert(joined_row.user_id == ((joined_row.order_id - 1) % NUM_USERS) + 1);
        assert(joined_row.user_balance == 100.0 * joined_row.user_id);
        join_count++;
    }

    assert(join_count == NUM_ORDERS);
    std::cout << "  • Successfully produced " << join_count
              << " joined rows (orphan orders correctly filtered)." << std::endl;

    CleanUpDb(db_name);
    std::cout << "  ✓ Milestone 3 (NestedLoopJoin) passed successfully!" << std::endl;
}

// ============================================================================
// Milestone 4: Hash Join vs Nested Loop Join Benchmark
// ============================================================================
void TestHashJoinBenchmark() {
    std::cout << "\n=== [Milestone 4] HashJoin vs NestedLoopJoin Benchmark ===" << std::endl;
    const std::string db_name = "test_exec_hashjoin.db";
    CleanUpDb(db_name);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_name);
    auto replacer = std::make_unique<LRUKReplacer>(100);
    auto bpm = std::make_unique<BufferPoolManager>(100, disk_manager.get(), replacer.get());
    auto table_heap = std::make_unique<TableHeap>(bpm.get());

    constexpr int BENCH_USERS = 1000;
    constexpr int BENCH_ORDERS = 5000;

    std::cout << "  • Seeding " << BENCH_USERS << " users into TableHeap..." << std::endl;
    for (int i = 1; i <= BENCH_USERS; ++i) {
        UserRow user(i, ("Customer_" + std::to_string(i)).c_str(), 50.0 * i, 1710000000 + i);
        RID rid;
        assert(table_heap->InsertTuple(user, &rid));
    }

    std::cout << "  • Generating " << BENCH_ORDERS << " orders (5 orders per user)..." << std::endl;
    std::vector<OrderRow> orders;
    orders.reserve(BENCH_ORDERS);
    for (int i = 1; i <= BENCH_ORDERS; ++i) {
        int64_t uid = ((i - 1) % BENCH_USERS) + 1;
        orders.emplace_back(i, uid, 12.75 * i, 1710000000 + i);
    }

    // -------------------------------------------------------------
    // Part A: Hash Join Execution (O(M + N) Linear Time)
    // -------------------------------------------------------------
    std::cout << "  • Running HashJoinExecutor..." << std::endl;
    SeqScanExecutor hj_users(table_heap.get());
    MockScanExecutor<OrderRow> hj_orders(orders);
    HashJoinExecutor hash_join(&hj_users, &hj_orders);

    auto t0 = std::chrono::high_resolution_clock::now();
    hash_join.Init();

    JoinedUserOrderRow joined_row;
    RID rid;
    int hj_count = 0;
    while (hash_join.Next(&joined_row, &rid)) {
        assert(joined_row.user_id == ((joined_row.order_id - 1) % BENCH_USERS) + 1);
        hj_count++;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double hj_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    assert(hj_count == BENCH_ORDERS);
    std::cout << "  • Hash Join finished: " << hj_count << " rows in " << hj_ms << " ms." << std::endl;

    // -------------------------------------------------------------
    // Part B: Nested Loop Join Execution (O(M * N) Quadratic Time)
    // -------------------------------------------------------------
    std::cout << "  • Running NestedLoopJoinExecutor (Quadratic Comparisons: "
              << (BENCH_USERS * BENCH_ORDERS) << " iterations)..." << std::endl;
    SeqScanExecutor nlj_users(table_heap.get());
    MockScanExecutor<OrderRow> nlj_orders(orders);
    NestedLoopJoinExecutor nlj(&nlj_users, &nlj_orders);

    auto t2 = std::chrono::high_resolution_clock::now();
    nlj.Init();

    int nlj_count = 0;
    while (nlj.Next(&joined_row, &rid)) {
        assert(joined_row.user_id == ((joined_row.order_id - 1) % BENCH_USERS) + 1);
        nlj_count++;
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double nlj_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();

    assert(nlj_count == BENCH_ORDERS);
    std::cout << "  • Nested Loop Join finished: " << nlj_count << " rows in " << nlj_ms << " ms." << std::endl;

    double speedup = nlj_ms / (hj_ms > 0.001 ? hj_ms : 0.001);
    std::cout << "  >>> SPEEDUP SUMMARY: Hash Join is " << speedup << "x faster than Nested Loop! <<<" << std::endl;

    CleanUpDb(db_name);
    std::cout << "  ✓ Milestone 4 (HashJoin Benchmark) passed successfully!" << std::endl;
}

// ============================================================================
// Main Dispatcher
// ============================================================================
int main(int argc, char **argv) {
    int test_num = 0;
    if (argc > 1) {
        test_num = std::atoi(argv[1]);
    }

    std::cout << "=================================================================" << std::endl;
    std::cout << "DBMS EXECUTION SUITE: Volcano Processing & Relational Joins" << std::endl;
    if (test_num > 0) {
        std::cout << "Running Milestone Test #" << test_num << std::endl;
    }
    std::cout << "=================================================================" << std::endl;

    if (test_num == 0 || test_num == 1) TestSeqScan();
    if (test_num == 0 || test_num == 2) TestIndexScanAndFilter();
    if (test_num == 0 || test_num == 3) TestNestedLoopJoin();
    if (test_num == 0 || test_num == 4) TestHashJoinBenchmark();

    std::cout << "\n=================================================================" << std::endl;
    std::cout << "ALL EXECUTION TESTS PASSED WITH FLYING COLORS!" << std::endl;
    std::cout << "=================================================================" << std::endl;
    return 0;
}
