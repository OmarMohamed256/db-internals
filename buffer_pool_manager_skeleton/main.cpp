#include <iostream>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <memory>
#include "buffer_pool_manager.h"
#include "lru_k_replacer.h"
#include "async_disk_manager.h"
#include "io_uring_manager.h"

void CleanUpTestDb(const std::string &db_name) {
    std::remove(db_name.c_str());
}

void TestBasicAllocationAndPersistence() {
    std::cout << "\n=== [Test 1] Basic Allocation & Disk Persistence ===" << std::endl;
    const std::string db_file = "test1.db";
    CleanUpTestDb(db_file);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_file, 16);
    auto replacer = std::make_unique<LRUKReplacer>(5);
    auto bpm = std::make_unique<BufferPoolManager>(5, disk_manager.get(), replacer.get());

    page_id_t p0_id = INVALID_PAGE_ID, p1_id = INVALID_PAGE_ID, p2_id = INVALID_PAGE_ID;
    Page *p0 = nullptr, *p1 = nullptr, *p2 = nullptr;

    bool p0_done = false, p1_done = false, p2_done = false;

    bpm->NewPageAsync([&p0, &p0_id, &p0_done](Page *p) {
        p0 = p;
        if (p) p0_id = p->GetPageId();
        p0_done = true;
    });

    bpm->NewPageAsync([&p1, &p1_id, &p1_done](Page *p) {
        p1 = p;
        if (p) p1_id = p->GetPageId();
        p1_done = true;
    });

    bpm->NewPageAsync([&p2, &p2_id, &p2_done](Page *p) {
        p2 = p;
        if (p) p2_id = p->GetPageId();
        p2_done = true;
    });

    // Poll until all allocations finish
    while (!p0_done || !p1_done || !p2_done) {
        bpm->PollCompletions();
    }

    assert(p0 != nullptr && p0_id == 0);
    assert(p1 != nullptr && p1_id == 1);
    assert(p2 != nullptr && p2_id == 2);

    // Write distinctive data to each page
    snprintf(p0->GetData(), PAGE_SIZE, "Data inside Page 0");
    snprintf(p1->GetData(), PAGE_SIZE, "Data inside Page 1");
    snprintf(p2->GetData(), PAGE_SIZE, "Data inside Page 2");

    // Unpin pages with dirty flag = true
    assert(bpm->UnpinPage(p0_id, true));
    assert(bpm->UnpinPage(p1_id, true));
    assert(bpm->UnpinPage(p2_id, true));

    // Flush all pages to disk
    bool flush_done = false;
    bpm->FlushAllPagesAsync([&flush_done](bool success) {
        assert(success);
        flush_done = true;
    });

    while (!flush_done) {
        bpm->PollCompletions();
    }
    std::cout << "-> Successfully created, wrote, unpinned, and flushed 3 pages." << std::endl;

    CleanUpTestDb(db_file);
}

void TestEvictionAndCacheMiss() {
    std::cout << "\n=== [Test 2] Eviction & Cache Hit/Miss ===" << std::endl;
    const std::string db_file = "test2.db";
    CleanUpTestDb(db_file);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_file, 16);
    auto replacer = std::make_unique<LRUKReplacer>(3);
    auto bpm = std::make_unique<BufferPoolManager>(3, disk_manager.get(), replacer.get()); // Pool size 3

    page_id_t p0_id = INVALID_PAGE_ID, p1_id = INVALID_PAGE_ID, p2_id = INVALID_PAGE_ID, p3_id = INVALID_PAGE_ID;
    Page *p0 = nullptr, *p1 = nullptr, *p2 = nullptr, *p3 = nullptr;
    bool done = false;

    // Allocate 3 pages
    bpm->NewPageAsync([&p0, &p0_id](Page *p) { p0 = p; p0_id = p->GetPageId(); });
    bpm->NewPageAsync([&p1, &p1_id](Page *p) { p1 = p; p1_id = p->GetPageId(); });
    bpm->NewPageAsync([&p2, &p2_id](Page *p) { p2 = p; p2_id = p->GetPageId(); });

    while (!p0 || !p1 || !p2) {
        bpm->PollCompletions();
    }

    snprintf(p0->GetData(), PAGE_SIZE, "Original Page 0");
    snprintf(p1->GetData(), PAGE_SIZE, "Original Page 1");
    snprintf(p2->GetData(), PAGE_SIZE, "Original Page 2");

    // Unpin all so they become evictable
    bpm->UnpinPage(p0_id, true);
    bpm->UnpinPage(p1_id, true);
    bpm->UnpinPage(p2_id, true);

    // Buffer pool is now full (3/3 frames used and unpinned).
    // Allocating Page 3 must trigger eviction of Page 0.
    done = false;
    bpm->NewPageAsync([&p3, &p3_id, &done](Page *p) {
        p3 = p;
        p3_id = p->GetPageId();
        done = true;
    });

    while (!done) {
        bpm->PollCompletions();
    }

    assert(p3 != nullptr && p3_id == 3);
    snprintf(p3->GetData(), PAGE_SIZE, "Original Page 3");
    bpm->UnpinPage(p3_id, true);
    std::cout << "-> Successfully evicted Page 0 and allocated Page 3 in its frame." << std::endl;

    // Now fetch Page 0 back from disk! (Will evict Page 1)
    done = false;
    Page *fetched_p0 = nullptr;
    bpm->FetchPageAsync(p0_id, [&fetched_p0, &done](Page *p) {
        fetched_p0 = p;
        done = true;
    });

    while (!done) {
        bpm->PollCompletions();
    }

    assert(fetched_p0 != nullptr);
    assert(strcmp(fetched_p0->GetData(), "Original Page 0") == 0);
    std::cout << "-> Successfully fetched evicted Page 0 back from disk: '" << fetched_p0->GetData() << "'" << std::endl;

    bpm->UnpinPage(p0_id, false);
    CleanUpTestDb(db_file);
}

void TestPinProtection() {
    std::cout << "\n=== [Test 3] Pin Protection (Out of Memory) ===" << std::endl;
    const std::string db_file = "test3.db";
    CleanUpTestDb(db_file);

    auto disk_manager = std::make_unique<AsyncDiskManager>(db_file, 16);
    auto replacer = std::make_unique<LRUKReplacer>(2);
    auto bpm = std::make_unique<BufferPoolManager>(2, disk_manager.get(), replacer.get()); // Pool size 2

    page_id_t p0_id = INVALID_PAGE_ID, p1_id = INVALID_PAGE_ID, p2_id = INVALID_PAGE_ID;
    Page *p0 = nullptr, *p1 = nullptr, *p2 = nullptr;
    bool done = false;

    bpm->NewPageAsync([&p0, &p0_id](Page *p) { p0 = p; p0_id = p->GetPageId(); });
    bpm->NewPageAsync([&p1, &p1_id](Page *p) { p1 = p; p1_id = p->GetPageId(); });

    while (!p0 || !p1) {
        bpm->PollCompletions();
    }

    assert(p0 != nullptr && p1 != nullptr);

    // Both p0 and p1 are pinned (pin_count = 1).
    // Attempting to allocate a 3rd page MUST fail and return nullptr!
    done = false;
    bpm->NewPageAsync([&p2, &done](Page *p) {
        p2 = p;
        done = true;
    });

    while (!done) {
        bpm->PollCompletions();
    }

    assert(p2 == nullptr);
    std::cout << "-> Successfully rejected NewPage when all frames are pinned (returned nullptr)." << std::endl;

    // Now unpin p0, and try again -> should succeed!
    bpm->UnpinPage(p0_id, false);

    done = false;
    bpm->NewPageAsync([&p2, &p2_id, &done](Page *p) {
        p2 = p;
        p2_id = p->GetPageId();
        done = true;
    });

    while (!done) {
        bpm->PollCompletions();
    }

    assert(p2 != nullptr && p2_id == 2);
    std::cout << "-> After unpinning Page 0, NewPage succeeded!" << std::endl;

    bpm->UnpinPage(p1_id, false);
    bpm->UnpinPage(p2_id, false);
    CleanUpTestDb(db_file);
}

void TestIOUringManagerAsyncOperations() {
    std::cout << "\n=== [Test 4] IOUringManager Async Operations ===" << std::endl;
    const std::string db_file = "test_io_uring.db";
    std::remove(db_file.c_str());

    {
        IOUringManager io_manager(db_file, 16);

        // Prepare some buffers
        alignas(4096) char write_buf0[PAGE_SIZE];
        alignas(4096) char write_buf1[PAGE_SIZE];
        snprintf(write_buf0, PAGE_SIZE, "Hello from Page 0 in io_uring!");
        snprintf(write_buf1, PAGE_SIZE, "Hello from Page 1 in io_uring!");

        bool write0_completed = false;
        bool write1_completed = false;

        // Perform async writes
        io_manager.AsyncWritePage(0, write_buf0, [&write0_completed](bool success) {
            assert(success);
            write0_completed = true;
        });

        io_manager.AsyncWritePage(1, write_buf1, [&write1_completed](bool success) {
            assert(success);
            write1_completed = true;
        });

        // Poll completions until both are finished
        while (!write0_completed || !write1_completed) {
            io_manager.PollCompletions();
        }
        std::cout << "-> Successfully completed async writes." << std::endl;

        // Prepare read buffers
        alignas(4096) char read_buf0[PAGE_SIZE]{};
        alignas(4096) char read_buf1[PAGE_SIZE]{};

        bool read0_completed = false;
        bool read1_completed = false;

        // Perform async reads
        io_manager.AsyncReadPage(0, read_buf0, [&read0_completed](bool success) {
            assert(success);
            read0_completed = true;
        });

        io_manager.AsyncReadPage(1, read_buf1, [&read1_completed](bool success) {
            assert(success);
            read1_completed = true;
        });

        // Poll completions until both are finished
        while (!read0_completed || !read1_completed) {
            io_manager.PollCompletions();
        }

        assert(strcmp(read_buf0, "Hello from Page 0 in io_uring!") == 0);
        assert(strcmp(read_buf1, "Hello from Page 1 in io_uring!") == 0);
        std::cout << "-> Successfully read and verified page contents asynchronously." << std::endl;
    }

    std::remove(db_file.c_str());
    std::cout << "-> IOUringManager test passed!" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "    RUNNING BUFFER POOL MANAGER TESTS   " << std::endl;
    std::cout << "========================================" << std::endl;

    TestBasicAllocationAndPersistence();
    TestEvictionAndCacheMiss();
    TestPinProtection();
    TestIOUringManagerAsyncOperations();

    std::cout << "\n========================================" << std::endl;
    std::cout << "       ALL TESTS PASSED WITH 100%!      " << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}
