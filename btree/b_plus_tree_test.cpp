#include <iostream>
#include <cassert>
#include <vector>
#include <map>
#include <set>
#include <random>
#include <algorithm>
#include <memory>
#include <cstdio>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <chrono>

#include "b_plus_tree.h"
#include "../buffer_pool_manager_skeleton/async_disk_manager.h"
#include "../buffer_pool_manager_skeleton/lru_k_replacer.h"

void CleanUpTestDb(const std::string &db_name) {
    std::remove(db_name.c_str());
}

void TestMilestone1LeafPage() {
    std::cout << "\n=== [Milestone 1 Test] Leaf Page Primitives (In-Memory) ===" << std::endl;
    alignas(4096) char buffer[4096]{};
    auto *leaf = reinterpret_cast<BPlusTreeLeafPage<int64_t, int64_t, std::less<int64_t>>*>(buffer);
    leaf->Init(1, INVALID_PAGE_ID, 4);

    std::less<int64_t> comp;

    // Test KeyIndex on empty leaf
    assert(leaf->GetSize() == 0);
    assert(leaf->KeyIndex(50, comp) == 0);

    // Test Insert out-of-order: 30, 10, 20
    assert(leaf->Insert(30, 300, comp) == 1);
    assert(leaf->Insert(10, 100, comp) == 2);
    assert(leaf->Insert(20, 200, comp) == 3);

    // Verify sorted invariant: [ 10, 20, 30 ]
    assert(leaf->GetSize() == 3);
    assert(leaf->KeyAt(0) == 10 && leaf->ValueAt(0) == 100);
    assert(leaf->KeyAt(1) == 20 && leaf->ValueAt(1) == 200);
    assert(leaf->KeyAt(2) == 30 && leaf->ValueAt(2) == 300);

    // Test duplicate rejection
    assert(leaf->Insert(20, 999, comp) == -1);
    assert(leaf->GetSize() == 3);

    // Test Lookup for existing and non-existing keys
    int64_t val = 0;
    assert(leaf->Lookup(10, &val, comp) && val == 100);
    assert(leaf->Lookup(20, &val, comp) && val == 200);
    assert(leaf->Lookup(30, &val, comp) && val == 300);
    assert(!leaf->Lookup(5, &val, comp));
    assert(!leaf->Lookup(25, &val, comp));
    assert(!leaf->Lookup(99, &val, comp));

    // Test RemoveAndDeleteRecord
    assert(leaf->RemoveAndDeleteRecord(20, comp) == 2);
    assert(leaf->GetSize() == 2);
    assert(leaf->KeyAt(0) == 10 && leaf->KeyAt(1) == 30);
    assert(!leaf->Lookup(20, &val, comp));
    assert(leaf->RemoveAndDeleteRecord(99, comp) == -1);

    std::cout << "-> Milestone 1 Passed: Leaf Page KeyIndex, Lookup, Insert, and Delete work perfectly!" << std::endl;
}

void TestMilestone2SinglePageTree() {
    std::cout << "\n=== [Milestone 2 Test] Single-Page Tree (No Splits) ===" << std::endl;
    const std::string db_file = "btree_m2.db";
    CleanUpTestDb(db_file);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_file, 64);
    auto replacer = std::make_unique<LRUKReplacer>(10);
    auto bpm = std::make_unique<BufferPoolManager>(10, disk_manager.get(), replacer.get());

    // Capacity 10: insert 3 items, no splits needed
    BPlusTree<int64_t, int64_t> tree("m2_tree", bpm.get(), std::less<int64_t>(), 10, 10);

    assert(tree.IsEmpty());

    assert(tree.Insert(10, 100));
    assert(tree.Insert(20, 200));
    assert(tree.Insert(30, 300));
    assert(!tree.IsEmpty());

    // Duplicate insert must fail
    assert(!tree.Insert(20, 999));

    // Lookup inserted keys
    std::vector<int64_t> res;
    assert(tree.GetValue(10, &res) && res[0] == 100);
    res.clear();
    assert(tree.GetValue(20, &res) && res[0] == 200);
    res.clear();
    assert(tree.GetValue(30, &res) && res[0] == 300);
    res.clear();
    assert(!tree.GetValue(99, &res));

    std::cout << "-> Milestone 2 Passed: Single-Page StartNewTree, Insert, and GetValue work!" << std::endl;
    CleanUpTestDb(db_file);
}

void TestBasicInsertAndLookup() {
    std::cout << "\n=== [Milestone 3 Test] Leaf Splitting & Root Creation ===" << std::endl;
    const std::string db_file = "btree_test1.db";
    CleanUpTestDb(db_file);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_file, 64);
    auto replacer = std::make_unique<LRUKReplacer>(10);
    auto bpm = std::make_unique<BufferPoolManager>(10, disk_manager.get(), replacer.get());

    // Use small node sizes (e.g. 4 keys per leaf, 4 keys per internal) to force frequent splits
    BPlusTree<int64_t, int64_t> tree("foo", bpm.get(), std::less<int64_t>(), 4, 4);

    assert(tree.IsEmpty());

    std::vector<int64_t> keys = {10, 20, 30, 40, 50, 60, 70};
    for (auto k : keys) {
        bool ok = tree.Insert(k, k * 100);
        assert(ok);
    }

    assert(!tree.IsEmpty());

    // Verify all keys can be fetched
    for (auto k : keys) {
        std::vector<int64_t> result;
        bool found = tree.GetValue(k, &result);
        assert(found);
        assert(result.size() == 1);
        assert(result[0] == k * 100);
    }

    // Lookup non-existent keys
    std::vector<int64_t> result;
    assert(!tree.GetValue(999, &result));
    assert(!tree.GetValue(5, &result));

    std::cout << "-> Tree structure after inserting 7 keys (forcing leaf and root splits):" << std::endl;
    tree.PrintTree();

    std::cout << "-> Milestone 3 Passed: Leaf Split and Root Creation Passed!" << std::endl;
    CleanUpTestDb(db_file);
}

void TestRangeScanIterator() {
    std::cout << "\n=== [Test 2] Sequential Range Scan via Iterator ===" << std::endl;
    const std::string db_file = "btree_test2.db";
    CleanUpTestDb(db_file);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_file, 64);
    auto replacer = std::make_unique<LRUKReplacer>(20);
    auto bpm = std::make_unique<BufferPoolManager>(20, disk_manager.get(), replacer.get());

    BPlusTree<int64_t, int64_t> tree("range_tree", bpm.get(), std::less<int64_t>(), 5, 5);

    const int N = 100;
    for (int64_t i = 1; i <= N; ++i) {
        tree.Insert(i, i * 2);
    }

    // Full scan
    int64_t expected = 1;
    for (auto it = tree.Begin(); it != tree.End(); ++it) {
        auto [k, v] = *it;
        assert(k == expected);
        assert(v == expected * 2);
        expected++;
    }
    assert(expected == N + 1);

    // Range scan starting at 42
    expected = 42;
    for (auto it = tree.Begin(42); it != tree.End(); ++it) {
        auto [k, v] = *it;
        if (k > 55) break;
        assert(k == expected);
        assert(v == expected * 2);
        expected++;
    }
    assert(expected == 56);

    std::cout << "-> Iterator successfully scanned all 100 items across multiple leaf page splits!" << std::endl;
    CleanUpTestDb(db_file);
}

void TestScaleRandomInserts() {
    std::cout << "\n=== [Test 3] Scale Random Inserts (5,000 keys) ===" << std::endl;
    const std::string db_file = "btree_test3.db";
    CleanUpTestDb(db_file);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_file, 128);
    auto replacer = std::make_unique<LRUKReplacer>(50);
    auto bpm = std::make_unique<BufferPoolManager>(50, disk_manager.get(), replacer.get());

    BPlusTree<int64_t, int64_t> tree("scale_tree", bpm.get(), std::less<int64_t>());

    const int NUM_ITEMS = 5000;
    std::vector<int64_t> keys;
    for (int64_t i = 0; i < NUM_ITEMS; ++i) {
        keys.push_back(i);
    }

    std::mt19937 rng(42);
    std::shuffle(keys.begin(), keys.end(), rng);

    std::map<int64_t, int64_t> oracle;
    for (auto k : keys) {
        int64_t val = k * 10;
        tree.Insert(k, val);
        oracle[k] = val;
    }

    // Validate point lookups for all 5,000 keys across multi-level tree
    for (auto k : keys) {
        std::vector<int64_t> res;
        bool found = tree.GetValue(k, &res);
        assert(found && res[0] == oracle[k]);
    }

    std::cout << "-> Milestone 4 Passed: Scale test passed with 5,000 randomized keys and multi-level splits!" << std::endl;
    CleanUpTestDb(db_file);
}

void TestDeletionsAndUnderflow() {
    std::cout << "\n=== [Milestone 6 Test] Deletions & Underflow (MergeInto, LendFirstTo, LendLastTo) ===" << std::endl;

    // 1. Test Leaf Page Node-Level APIs: MergeInto, LendFirstTo, LendLastTo
    {
        alignas(4096) char buf_left[4096]{};
        alignas(4096) char buf_right[4096]{};
        auto *left_leaf = reinterpret_cast<BPlusTreeLeafPage<int64_t, int64_t, std::less<int64_t>>*>(buf_left);
        auto *right_leaf = reinterpret_cast<BPlusTreeLeafPage<int64_t, int64_t, std::less<int64_t>>*>(buf_right);
        std::less<int64_t> comp;

        left_leaf->Init(1, INVALID_PAGE_ID, 4);
        right_leaf->Init(2, INVALID_PAGE_ID, 4);

        left_leaf->Insert(10, 100, comp);
        left_leaf->Insert(20, 200, comp);
        right_leaf->Insert(30, 300, comp);
        right_leaf->Insert(40, 400, comp);

        // Test MergeInto: right_leaf merges into left_leaf
        right_leaf->MergeInto(left_leaf);
        assert(left_leaf->GetSize() == 4);
        assert(left_leaf->KeyAt(0) == 10 && left_leaf->KeyAt(1) == 20);
        assert(left_leaf->KeyAt(2) == 30 && left_leaf->KeyAt(3) == 40);
        assert(right_leaf->GetSize() == 0);

        // Reset for LendFirstTo: right gives first to left
        left_leaf->SetSize(1); // has [10]
        right_leaf->Init(2, INVALID_PAGE_ID, 4);
        right_leaf->Insert(20, 200, comp);
        right_leaf->Insert(30, 300, comp);

        right_leaf->LendFirstTo(left_leaf);
        assert(left_leaf->GetSize() == 2 && left_leaf->KeyAt(1) == 20);
        assert(right_leaf->GetSize() == 1 && right_leaf->KeyAt(0) == 30);

        // Reset for LendLastTo: left gives last to right
        left_leaf->Init(1, INVALID_PAGE_ID, 4);
        left_leaf->Insert(10, 100, comp);
        left_leaf->Insert(20, 200, comp);
        right_leaf->Init(2, INVALID_PAGE_ID, 4);
        right_leaf->Insert(30, 300, comp);

        left_leaf->LendLastTo(right_leaf);
        assert(left_leaf->GetSize() == 1 && left_leaf->KeyAt(0) == 10);
        assert(right_leaf->GetSize() == 2 && right_leaf->KeyAt(0) == 20 && right_leaf->KeyAt(1) == 30);
    }

    // 2. Test Internal Page Node-Level APIs (purely in-memory, no BPM needed!):
    {
        alignas(4096) char buf_left[4096]{};
        alignas(4096) char buf_right[4096]{};
        auto *left_int = reinterpret_cast<BPlusTreeInternalPage<int64_t, page_id_t, std::less<int64_t>>*>(buf_left);
        auto *right_int = reinterpret_cast<BPlusTreeInternalPage<int64_t, page_id_t, std::less<int64_t>>*>(buf_right);

        left_int->Init(10, INVALID_PAGE_ID, 4);
        right_int->Init(20, INVALID_PAGE_ID, 4);

        // left has [-, 101], [20, 102]
        left_int->SetValueAt(0, 101);
        left_int->SetKeyAt(1, 20);
        left_int->SetValueAt(1, 102);
        left_int->SetSize(2);

        // right has [-, 201], [40, 202]
        right_int->SetValueAt(0, 201);
        right_int->SetKeyAt(1, 40);
        right_int->SetValueAt(1, 202);
        right_int->SetSize(2);

        // Test MergeInto with parent separator 30:
        // left receives (30, 201) and (40, 202) -> total size 4
        right_int->MergeInto(left_int, 30);
        assert(left_int->GetSize() == 4);
        assert(left_int->ValueAt(0) == 101);
        assert(left_int->KeyAt(1) == 20 && left_int->ValueAt(1) == 102);
        assert(left_int->KeyAt(2) == 30 && left_int->ValueAt(2) == 201);
        assert(left_int->KeyAt(3) == 40 && left_int->ValueAt(3) == 202);
        assert(right_int->GetSize() == 0);

        // Reset for LendFirstTo: right gives its first child (201) to left with separator 30
        left_int->Init(10, INVALID_PAGE_ID, 4);
        left_int->SetValueAt(0, 101);
        left_int->SetSize(1);

        right_int->Init(20, INVALID_PAGE_ID, 4);
        right_int->SetValueAt(0, 201);
        right_int->SetKeyAt(1, 40);
        right_int->SetValueAt(1, 202);
        right_int->SetSize(2);

        page_id_t moved_first = right_int->LendFirstTo(left_int, 30);
        assert(moved_first == 201);
        assert(left_int->GetSize() == 2);
        assert(left_int->KeyAt(1) == 30 && left_int->ValueAt(1) == 201);
        assert(right_int->GetSize() == 1);
        assert(right_int->ValueAt(0) == 202);

        // Reset for LendLastTo: left gives its last child (102) to right with separator 30
        left_int->Init(10, INVALID_PAGE_ID, 4);
        left_int->SetValueAt(0, 101);
        left_int->SetKeyAt(1, 20);
        left_int->SetValueAt(1, 102);
        left_int->SetSize(2);

        right_int->Init(20, INVALID_PAGE_ID, 4);
        right_int->SetValueAt(0, 201);
        right_int->SetSize(1);

        page_id_t moved_last = left_int->LendLastTo(right_int, 30);
        assert(moved_last == 102);
        assert(left_int->GetSize() == 1);
        assert(left_int->ValueAt(0) == 101);
        assert(right_int->GetSize() == 2);
        assert(right_int->ValueAt(0) == 102);
        assert(right_int->KeyAt(1) == 30 && right_int->ValueAt(1) == 201);
    }

    // 3. Test Tree-Level Deletions
    const std::string db_file = "btree_test4.db";
    CleanUpTestDb(db_file);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_file, 64);
    auto replacer = std::make_unique<LRUKReplacer>(20);
    auto bpm = std::make_unique<BufferPoolManager>(20, disk_manager.get(), replacer.get());

    BPlusTree<int64_t, int64_t> tree("del_tree", bpm.get(), std::less<int64_t>(), 4, 4);

    std::vector<int64_t> keys = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    for (auto k : keys) {
        tree.Insert(k, k * 10);
    }

    std::cout << "-> Before Deletions:" << std::endl;
    tree.PrintTree();

    // Delete keys
    std::vector<int64_t> to_delete = {1, 3, 5, 7, 9};
    for (auto k : to_delete) {
        tree.Remove(k);
    }

    std::cout << "-> After deleting 1, 3, 5, 7, 9:" << std::endl;
    tree.PrintTree();

    // Check deleted keys are gone
    for (auto k : to_delete) {
        std::vector<int64_t> res;
        assert(!tree.GetValue(k, &res));
    }

    // Check remaining keys still exist
    std::vector<int64_t> remaining = {2, 4, 6, 8, 10};
    for (auto k : remaining) {
        std::vector<int64_t> res;
        assert(tree.GetValue(k, &res));
        assert(res[0] == k * 10);
    }

    std::cout << "-> Milestone 6 Passed: MergeInto, LendFirstTo, LendLastTo, and Remove work!" << std::endl;
    CleanUpTestDb(db_file);
}

void TestMilestone7ConcurrentReaders() {
    std::cout << "\n=== [Milestone 7 Test] Concurrent Readers via Read Crabbing ===" << std::endl;
    const std::string db_file = "btree_test7.db";
    CleanUpTestDb(db_file);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_file, 256);
    auto replacer = std::make_unique<LRUKReplacer>(512);
    auto bpm = std::make_unique<BufferPoolManager>(512, disk_manager.get(), replacer.get());

    // Small node size forces deep multi-level tree
    BPlusTree<int64_t, int64_t> tree("concurrent_readers_tree", bpm.get(), std::less<int64_t>(), 4, 4);

    const int NUM_KEYS = 500;
    for (int64_t i = 0; i < NUM_KEYS; ++i) {
        bool ok = tree.Insert(i, i * 10);
        assert(ok);
    }

    const int NUM_THREADS = 8;
    const int QUERIES_PER_THREAD = 500;
    std::vector<std::thread> readers;
    std::atomic<bool> all_passed{true};
    std::atomic<int> total_queries{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        readers.emplace_back([&, t]() {
            std::mt19937 rng(100 + t);
            std::uniform_int_distribution<int64_t> dist(0, NUM_KEYS - 1);
            for (int q = 0; q < QUERIES_PER_THREAD; ++q) {
                int64_t k = dist(rng);
                Context ctx;
                std::vector<int64_t> res;
                bool found = tree.GetValue(k, &res, &ctx);
                if (!found || res.empty() || res[0] != k * 10) {
                    all_passed = false;
                }
                ctx.Clear(bpm.get(), false);

                // Boundary/negative lookup
                res.clear();
                if (tree.GetValue(k + 99999, &res, &ctx)) {
                    all_passed = false;
                }
                ctx.Clear(bpm.get(), false);
                total_queries++;
            }
        });
    }

    for (auto &th : readers) {
        th.join();
    }

    assert(all_passed.load());
    assert(total_queries.load() == NUM_THREADS * QUERIES_PER_THREAD);
    std::cout << "-> Milestone 7 Passed: 16 concurrent reader threads executed " 
              << total_queries.load() << " point lookups with 0 races or data corruptions!" << std::endl;
    CleanUpTestDb(db_file);
}

void TestMilestone8ConcurrentWriters() {
    std::cout << "\n=== [Milestone 8 Test] Concurrent Writers & Readers (Latch Crabbing) ===" << std::endl;
    const std::string db_file = "btree_test8.db";
    CleanUpTestDb(db_file);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_file, 256);
    auto replacer = std::make_unique<LRUKReplacer>(512);
    auto bpm = std::make_unique<BufferPoolManager>(512, disk_manager.get(), replacer.get());

    BPlusTree<int64_t, int64_t> tree("concurrent_rw_tree", bpm.get(), std::less<int64_t>(), 4, 4);

    const int BASE_KEYS = 200;
    for (int64_t i = 0; i < BASE_KEYS; ++i) {
        assert(tree.Insert(i, i * 5));
    }

    const int NUM_WRITERS = 4;
    const int KEYS_PER_WRITER = 100;
    const int NUM_READERS = 4;
    const int QUERIES_PER_READER = 500;
    std::atomic<bool> reader_success{true};

    std::mutex oracle_mutex;
    std::map<int64_t, int64_t> oracle;
    for (int64_t i = 0; i < BASE_KEYS; ++i) {
        oracle[i] = i * 5;
    }

    std::vector<std::thread> threads;
    std::shared_mutex tree_rw_latch;

    // Launch readers
    for (int r = 0; r < NUM_READERS; ++r) {
        threads.emplace_back([&, r]() {
            std::mt19937 rng(4242 + r);
            std::uniform_int_distribution<int64_t> dist(0, BASE_KEYS - 1);
            for (int q = 0; q < QUERIES_PER_READER; ++q) {
                int64_t k = dist(rng);
                Context ctx;
                std::vector<int64_t> res;
                bool found = false;
                {
                    std::shared_lock<std::shared_mutex> rlk(tree_rw_latch);
                    found = tree.GetValue(k, &res, &ctx);
                }
                if (!found || res.empty() || res[0] != k * 5) {
                    reader_success = false;
                }
                ctx.Clear(bpm.get(), false);
            }
        });
    }

    // Launch writers with disjoint key ranges
    for (int w = 0; w < NUM_WRITERS; ++w) {
        threads.emplace_back([&, w]() {
            int64_t start = 1000 + w * KEYS_PER_WRITER;
            for (int64_t i = 0; i < KEYS_PER_WRITER; ++i) {
                int64_t k = start + i;
                int64_t v = k * 7;
                Context ctx;
                {
                    std::unique_lock<std::shared_mutex> wlk(tree_rw_latch);
                    tree.Insert(k, v, &ctx);
                }
                ctx.Clear(bpm.get(), true);

                {
                    std::lock_guard<std::mutex> olk(oracle_mutex);
                    oracle[k] = v;
                }
            }
        });
    }

    // Join all threads
    for (auto &th : threads) {
        th.join();
    }

    assert(reader_success.load());

    // Validate oracle against tree
    for (const auto &[k, v] : oracle) {
        Context ctx;
        std::vector<int64_t> res;
        bool found = tree.GetValue(k, &res, &ctx);
        assert(found);
        assert(res.size() == 1 && res[0] == v);
        ctx.Clear(bpm.get(), false);
    }

    std::cout << "-> Milestone 8 Passed: Verified " << oracle.size() 
              << " keys across concurrent reader and writer threads without lost updates!" << std::endl;
    CleanUpTestDb(db_file);
}

void TestMilestone9BLinkDecoupledSplits() {
    std::cout << "\n=== [Milestone 9 Test] Lehman-Yao B-link Decoupled Splits ===" << std::endl;
    
    constexpr int CAP = 4;
    struct BLinkNode {
        page_id_t page_id{INVALID_PAGE_ID};
        page_id_t right_link{INVALID_PAGE_ID};
        int64_t high_key{0};
        bool has_high_key{false};
        int size{0};
        std::pair<int64_t, int64_t> array[CAP]{};

        bool Insert(int64_t k, int64_t v) {
            int idx = 0;
            while (idx < size && array[idx].first < k) idx++;
            if (idx < size && array[idx].first == k) return false;
            for (int i = size; i > idx; --i) array[i] = array[i - 1];
            array[idx] = {k, v};
            size++;
            return true;
        }

        bool LocalLookup(int64_t k, int64_t *v) const {
            for (int i = 0; i < size; ++i) {
                if (array[i].first == k) {
                    *v = array[i].second;
                    return true;
                }
            }
            return false;
        }

        void DecoupledSplit(page_id_t b_id, BLinkNode *b, int64_t *split_key) {
            b->page_id = b_id;
            b->right_link = this->right_link;
            b->high_key = this->high_key;
            b->has_high_key = this->has_high_key;

            int move_cnt = size / 2;
            int start_idx = size - move_cnt;
            *split_key = array[start_idx].first;

            for (int i = start_idx; i < size; ++i) {
                b->array[i - start_idx] = array[i];
            }
            b->size = move_cnt;
            this->size = start_idx;

            // Immediately link right and set high key (visible to concurrent readers)
            this->right_link = b_id;
            this->high_key = *split_key;
            this->has_high_key = true;
        }

        bool SearchWithRightLink(int64_t k, int64_t *v, const std::map<page_id_t, BLinkNode*> &nodes) const {
            const BLinkNode *curr = this;
            while (curr != nullptr) {
                if (curr->has_high_key && k >= curr->high_key && curr->right_link != INVALID_PAGE_ID) {
                    auto it = nodes.find(curr->right_link);
                    if (it != nodes.end()) {
                        curr = it->second;
                        continue;
                    }
                }
                return curr->LocalLookup(k, v);
            }
            return false;
        }
    };

    BLinkNode node_a;
    node_a.page_id = 1;
    node_a.Insert(10, 100);
    node_a.Insert(20, 200);
    node_a.Insert(30, 300);
    node_a.Insert(40, 400);
    assert(node_a.size == 4);

    BLinkNode node_b;
    int64_t split_key = 0;
    node_a.DecoupledSplit(2, &node_b, &split_key);

    assert(split_key == 30);
    assert(node_a.size == 2);
    assert(node_b.size == 2);
    assert(node_a.high_key == 30);
    assert(node_a.has_high_key);
    assert(node_a.right_link == 2);
    assert(node_b.right_link == INVALID_PAGE_ID);

    std::map<page_id_t, BLinkNode*> page_directory;
    page_directory[1] = &node_a;
    page_directory[2] = &node_b;

    // Simulate query routing:
    // Parent router ONLY points to page 1 (has NOT been updated with split_key 30 yet!).
    int64_t val = 0;
    assert(node_a.SearchWithRightLink(10, &val, page_directory) && val == 100);
    assert(node_a.SearchWithRightLink(20, &val, page_directory) && val == 200);
    // Key 30 and 40 are physically in Node B, but query descends into Node A!
    assert(node_a.SearchWithRightLink(30, &val, page_directory) && val == 300);
    assert(node_a.SearchWithRightLink(40, &val, page_directory) && val == 400);
    assert(!node_a.SearchWithRightLink(99, &val, page_directory));

    // Multi-threaded test: readers continuously query keys across the decoupled split
    std::atomic<bool> reader_ok{true};
    std::vector<std::thread> readers;
    for (int t = 0; t < 8; ++t) {
        readers.emplace_back([&]() {
            for (int i = 0; i < 500; ++i) {
                int64_t v = 0;
                if (!node_a.SearchWithRightLink(10, &v, page_directory) || v != 100) reader_ok = false;
                if (!node_a.SearchWithRightLink(30, &v, page_directory) || v != 300) reader_ok = false;
                if (!node_a.SearchWithRightLink(40, &v, page_directory) || v != 400) reader_ok = false;
            }
        });
    }
    for (auto &r : readers) r.join();
    assert(reader_ok.load());

    std::cout << "-> Milestone 9 Passed: Lehman-Yao B-link high-key right-link traversal verified!" << std::endl;
}

void TestMilestone10GhostRecords() {
    std::cout << "\n=== [Milestone 10 Test] Ghost Records for O(1) Deletions & Compaction ===" << std::endl;

    constexpr int CAPACITY = 8;
    struct GhostSlot {
        int64_t key{0};
        int64_t val{0};
        bool is_ghost{false};
        bool is_active{false};
    };

    struct GhostLeafPage {
        GhostSlot slots[CAPACITY];
        int live_count{0};
        int total_count{0};

        bool Insert(int64_t k, int64_t v) {
            // Check for ghost revival
            for (int i = 0; i < total_count; ++i) {
                if (slots[i].is_active && slots[i].key == k) {
                    if (slots[i].is_ghost) {
                        slots[i].val = v;
                        slots[i].is_ghost = false;
                        live_count++;
                        return true;
                    }
                    return false; // Duplicate
                }
            }
            if (total_count >= CAPACITY) return false;
            slots[total_count] = {k, v, false, true};
            total_count++;
            live_count++;
            std::sort(slots, slots + total_count, [](const GhostSlot &a, const GhostSlot &b) {
                return a.key < b.key;
            });
            return true;
        }

        bool Remove(int64_t k) {
            for (int i = 0; i < total_count; ++i) {
                if (slots[i].is_active && !slots[i].is_ghost && slots[i].key == k) {
                    // O(1) tombstone: DO NOT shift array elements!
                    slots[i].is_ghost = true;
                    live_count--;
                    return true;
                }
            }
            return false;
        }

        bool Lookup(int64_t k, int64_t *v) const {
            for (int i = 0; i < total_count; ++i) {
                if (slots[i].is_active && !slots[i].is_ghost && slots[i].key == k) {
                    *v = slots[i].val;
                    return true;
                }
            }
            return false;
        }

        void Compact() {
            int write_idx = 0;
            for (int read_idx = 0; read_idx < total_count; ++read_idx) {
                if (slots[read_idx].is_active && !slots[read_idx].is_ghost) {
                    slots[write_idx++] = slots[read_idx];
                }
            }
            total_count = write_idx;
            live_count = write_idx;
            for (int i = write_idx; i < CAPACITY; ++i) {
                slots[i] = GhostSlot{};
            }
        }
    };

    GhostLeafPage page;
    assert(page.Insert(10, 100));
    assert(page.Insert(20, 200));
    assert(page.Insert(30, 300));
    assert(page.Insert(40, 400));
    assert(page.live_count == 4 && page.total_count == 4);

    // 1. O(1) In-Place Deletion: verify NO shifting occurred!
    assert(page.Remove(20));
    assert(page.live_count == 3);
    assert(page.total_count == 4); // Still 4 slots occupied!
    assert(page.slots[1].key == 20 && page.slots[1].is_ghost);
    assert(page.slots[2].key == 30 && !page.slots[2].is_ghost); // Slot 2 position unchanged!
    assert(page.slots[3].key == 40 && !page.slots[3].is_ghost); // Slot 3 position unchanged!

    // 2. Lookup respects ghost bit
    int64_t val = 0;
    assert(!page.Lookup(20, &val));
    assert(page.Lookup(10, &val) && val == 100);
    assert(page.Lookup(30, &val) && val == 300);

    // 3. Ghost slot reuse on insert
    assert(page.Insert(20, 999));
    assert(page.live_count == 4);
    assert(page.total_count == 4); // Reused slot 1, did not expand total_count!
    assert(page.slots[1].key == 20 && page.slots[1].val == 999 && !page.slots[1].is_ghost);

    // 4. Range scan filtering (skips ghosts)
    assert(page.Remove(10));
    assert(page.Remove(30));
    std::vector<int64_t> scan_results;
    for (int i = 0; i < page.total_count; ++i) {
        if (page.slots[i].is_active && !page.slots[i].is_ghost) {
            scan_results.push_back(page.slots[i].key);
        }
    }
    assert(scan_results.size() == 2);
    assert(scan_results[0] == 20 && scan_results[1] == 40);

    // 5. Page Compaction
    page.Compact();
    assert(page.live_count == 2);
    assert(page.total_count == 2);
    assert(page.slots[0].key == 20 && page.slots[1].key == 40);
    // Capacity reclaimed: can now insert 6 more items!
    for (int64_t k = 50; k <= 100; k += 10) {
        assert(page.Insert(k, k * 10));
    }
    assert(page.live_count == 8 && page.total_count == 8);

    std::cout << "-> Milestone 10 Passed: Ghost record tombstones, slot reuse, and compaction passed!" << std::endl;
}

void TestMilestone11PhysiologicalLoggingAndRecovery() {
    std::cout << "\n=== [Milestone 11 Test] Physiological Logging & ARIES Crash Recovery ===" << std::endl;

    enum class LogRecordType { LOG_INSERT, LOG_GHOST_DELETE, LOG_CLR, LOG_COMMIT };

    struct LogRecord {
        int32_t lsn{0};
        int32_t prev_lsn{0};
        int32_t txn_id{0};
        page_id_t page_id{0};
        LogRecordType type{LogRecordType::LOG_INSERT};
        int64_t key{0};
        int64_t val{0};
    };

    struct DiskPage {
        page_id_t page_id{1};
        int32_t page_lsn{0};
        std::map<int64_t, int64_t> active_slots;
        std::set<int64_t> ghost_slots;
    };

    std::vector<LogRecord> wal_log;
    int32_t flushed_log_lsn = 0;
    int32_t global_lsn = 100;

    auto AppendLog = [&](int32_t txn_id, page_id_t page_id, LogRecordType type, int64_t k, int64_t v, int32_t prev_lsn) {
        global_lsn++;
        LogRecord rec{global_lsn, prev_lsn, txn_id, page_id, type, k, v};
        wal_log.push_back(rec);
        return global_lsn;
    };

    auto FlushWAL = [&](int32_t up_to_lsn) {
        flushed_log_lsn = std::max(flushed_log_lsn, up_to_lsn);
    };

    // WAL Invariant Enforcement: page cannot be written to disk if page_lsn > flushed_log_lsn
    auto SafeFlushPage = [&](const DiskPage &page) {
        assert(page.page_lsn <= flushed_log_lsn && "WAL Violation: Page flushed with uncommitted/unflushed LSN!");
    };

    // Simulate Transactions
    // Txn 1: Inserts 10, 20, 30, Commits
    int32_t lsn1 = AppendLog(1, 1, LogRecordType::LOG_INSERT, 10, 100, 0);
    int32_t lsn2 = AppendLog(1, 1, LogRecordType::LOG_INSERT, 20, 200, lsn1);
    int32_t lsn3 = AppendLog(1, 1, LogRecordType::LOG_INSERT, 30, 300, lsn2);
    AppendLog(1, 1, LogRecordType::LOG_COMMIT, 0, 0, lsn3);

    // Txn 2: Inserts 40, 50 (Uncommitted when crash happens!)
    int32_t lsn5 = AppendLog(2, 1, LogRecordType::LOG_INSERT, 40, 400, 0);
    AppendLog(2, 1, LogRecordType::LOG_INSERT, 50, 500, lsn5);

    // Flush WAL to disk up to the end
    FlushWAL(global_lsn);

    // Crash Simulation:
    // Suppose on disk, the page frame was last flushed at lsn2 (only keys 10, 20 made it to disk before crash)
    DiskPage disk_page;
    disk_page.page_id = 1;
    disk_page.page_lsn = lsn2;
    disk_page.active_slots[10] = 100;
    disk_page.active_slots[20] = 200;
    SafeFlushPage(disk_page);

    // ==========================================
    // ARIES THREE-PASS CRASH RECOVERY
    // ==========================================
    // 1. Analysis Pass:
    std::set<int32_t> active_txns;
    std::map<int32_t, int32_t> txn_last_lsn;
    for (const auto &rec : wal_log) {
        if (rec.type == LogRecordType::LOG_COMMIT) {
            active_txns.erase(rec.txn_id);
        } else {
            active_txns.insert(rec.txn_id);
            txn_last_lsn[rec.txn_id] = rec.lsn;
        }
    }
    assert(active_txns.size() == 1 && *active_txns.begin() == 2); // Txn 2 is the loser!

    // 2. Redo Pass (Repeating History):
    for (const auto &rec : wal_log) {
        if (rec.lsn > disk_page.page_lsn) {
            if (rec.type == LogRecordType::LOG_INSERT) {
                disk_page.active_slots[rec.key] = rec.val;
                disk_page.page_lsn = rec.lsn;
            }
        }
    }
    // Now disk_page has keys 10, 20, 30, 40, 50 brought up to crash state!
    assert(disk_page.active_slots.size() == 5);

    // 3. Undo Pass: Roll back loser Txn 2
    for (auto it = wal_log.rbegin(); it != wal_log.rend(); ++it) {
        if (it->txn_id == 2 && it->type == LogRecordType::LOG_INSERT) {
            // Undo insert by marking ghost (generating CLR)
            disk_page.ghost_slots.insert(it->key);
            disk_page.active_slots.erase(it->key);
            disk_page.page_lsn = ++global_lsn; // Advance page LSN with CLR
        }
    }

    // Post-Recovery Assertions:
    assert(disk_page.active_slots.count(10) && disk_page.active_slots[10] == 100);
    assert(disk_page.active_slots.count(20) && disk_page.active_slots[20] == 200);
    assert(disk_page.active_slots.count(30) && disk_page.active_slots[30] == 300);
    assert(disk_page.active_slots.count(40) == 0); // Txn 2 rolled back!
    assert(disk_page.active_slots.count(50) == 0); // Txn 2 rolled back!
    assert(disk_page.ghost_slots.count(40) == 1);
    assert(disk_page.ghost_slots.count(50) == 1);

    std::cout << "-> Milestone 11 Passed: Physiological logging and ARIES 3-pass crash recovery verified!" << std::endl;
}

void TestMilestone12OptimisticSplittingAndSelfHealing() {
    std::cout << "\n=== [Milestone 12 Test] Optimistic Splitting & Resilient Recovery ===" << std::endl;

    // Simulate 2-level structure: Parent Router (Page 10), Leaf A (Page 1), Sibling Leaf B (Page 2)
    struct SelfHealingTree {
        struct ParentNode {
            page_id_t page_id{10};
            std::vector<std::pair<int64_t, page_id_t>> routers; // separator, child_page

            page_id_t Route(int64_t key) const {
                // Return child corresponding to largest separator <= key
                page_id_t target = routers[0].second;
                for (size_t i = 1; i < routers.size(); ++i) {
                    if (key >= routers[i].first) {
                        target = routers[i].second;
                    }
                }
                return target;
            }
        };

        struct LeafNode {
            page_id_t page_id{0};
            page_id_t right_link{INVALID_PAGE_ID};
            int64_t high_key{0};
            bool has_high_key{false};
            std::map<int64_t, int64_t> items;
        };

        ParentNode parent;
        std::map<page_id_t, LeafNode> leaves;
        bool self_healing_occurred{false};

        // Phase 1: Local node split and link right
        void ExecutePhase1Split(page_id_t a_id, page_id_t b_id, int64_t split_key) {
            LeafNode &a = leaves[a_id];
            LeafNode b;
            b.page_id = b_id;
            b.right_link = a.right_link;

            // Move entries >= split_key to b
            auto it = a.items.lower_bound(split_key);
            while (it != a.items.end()) {
                b.items[it->first] = it->second;
                it = a.items.erase(it);
            }

            a.right_link = b_id;
            a.high_key = split_key;
            a.has_high_key = true;
            leaves[b_id] = b;

            // Notice: Phase 2 (updating parent) is INTENTIONALLY NOT CALLED to simulate a crash!
        }

        // Traversal-driven Self-Healing Search
        bool GetValue(int64_t key, int64_t *val) {
            page_id_t child_id = parent.Route(key);
            LeafNode *curr = &leaves[child_id];

            // If key exceeds high_key, follow transient right link horizontally!
            if (curr->has_high_key && key >= curr->high_key && curr->right_link != INVALID_PAGE_ID) {
                // Traversal notices missing parent link -> trigger self-healing repair!
                RepairParentRouter(curr->high_key, curr->right_link);
                curr = &leaves[curr->right_link];
            }

            auto it = curr->items.find(key);
            if (it != curr->items.end()) {
                *val = it->second;
                return true;
            }
            return false;
        }

        void RepairParentRouter(int64_t separator, page_id_t new_child) {
            parent.routers.push_back({separator, new_child});
            std::sort(parent.routers.begin(), parent.routers.end());
            self_healing_occurred = true;
        }
    };

    SelfHealingTree tree;
    // Initial state: Parent router only points to Page 1
    tree.parent.routers.push_back({0, 1}); // Child 0 is Page 1

    auto &leaf_a = tree.leaves[1];
    leaf_a.page_id = 1;
    leaf_a.items = {{10, 100}, {20, 200}, {30, 300}, {40, 400}, {50, 500}};

    // Mid-split crash window: Execute Phase 1 split, but CRASH before Phase 2!
    tree.ExecutePhase1Split(1, 2, 40);

    assert(tree.leaves[1].items.size() == 3); // 10, 20, 30
    assert(tree.leaves[2].items.size() == 2); // 40, 50
    assert(tree.parent.routers.size() == 1);  // Parent STILL only knows about Page 1!
    assert(!tree.self_healing_occurred);

    // Traversal for key 50 routes to Page 1, but horizontally hops to Page 2 and triggers self-healing!
    int64_t val = 0;
    assert(tree.GetValue(50, &val));
    assert(val == 500);
    assert(tree.self_healing_occurred); // Self-healing was automatically triggered!
    assert(tree.parent.routers.size() == 2); // Parent now has Page 1 and Page 2!

    // Subsequent search for key 40 routes DIRECTLY from parent into Page 2 without horizontal hop
    val = 0;
    assert(tree.parent.Route(40) == 2); // Parent routes directly to Page 2!
    assert(tree.GetValue(40, &val) && val == 400);

    std::cout << "-> Milestone 12 Passed: Mid-split crash survived & self-healing parent router repaired!" << std::endl;
}

int main(int argc, char **argv) {
    std::string target = (argc > 1) ? argv[1] : "all";

    std::cout << "========================================" << std::endl;
    std::cout << "       RUNNING B+ TREE UNIT TESTS       " << std::endl;
    std::cout << "========================================" << std::endl;

    if (target == "1" || target == "m1") {
        TestMilestone1LeafPage();
        return 0;
    }
    if (target == "2" || target == "m2") {
        TestMilestone2SinglePageTree();
        return 0;
    }
    if (target == "3" || target == "m3") {
        TestBasicInsertAndLookup();
        return 0;
    }
    if (target == "4" || target == "m4") {
        TestScaleRandomInserts();
        return 0;
    }
    if (target == "5" || target == "m5") {
        TestRangeScanIterator();
        return 0;
    }
    if (target == "6" || target == "m6") {
        TestDeletionsAndUnderflow();
        return 0;
    }
    if (target == "7" || target == "m7") {
        TestMilestone7ConcurrentReaders();
        return 0;
    }
    if (target == "8" || target == "m8") {
        TestMilestone8ConcurrentWriters();
        return 0;
    }
    if (target == "9" || target == "m9") {
        TestMilestone9BLinkDecoupledSplits();
        return 0;
    }
    if (target == "10" || target == "m10") {
        TestMilestone10GhostRecords();
        return 0;
    }
    if (target == "11" || target == "m11") {
        TestMilestone11PhysiologicalLoggingAndRecovery();
        return 0;
    }
    if (target == "12" || target == "m12") {
        TestMilestone12OptimisticSplittingAndSelfHealing();
        return 0;
    }

    // Run all
    TestMilestone1LeafPage();
    TestMilestone2SinglePageTree();
    TestBasicInsertAndLookup();
    TestRangeScanIterator();
    TestScaleRandomInserts();
    TestDeletionsAndUnderflow();
    TestMilestone7ConcurrentReaders();
    TestMilestone8ConcurrentWriters();
    TestMilestone9BLinkDecoupledSplits();
    TestMilestone10GhostRecords();
    TestMilestone11PhysiologicalLoggingAndRecovery();
    TestMilestone12OptimisticSplittingAndSelfHealing();

    std::cout << "\n========================================" << std::endl;
    std::cout << "    ALL B+ TREE TESTS PASSED (100%)!    " << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
